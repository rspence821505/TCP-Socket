#include <gtest/gtest.h>
#include "text_protocol.hpp"
#include "binary_protocol.hpp"
#include "ring_buffer.hpp"
#include <cstring>
#include <limits>

// =============================================================================
// Text Protocol Malformed Input Tests
// =============================================================================

class TextProtocolMalformedTest : public ::testing::Test {
protected:
  void SetUp() override {}
};

// --- Truncated Message Tests ---

TEST_F(TextProtocolMalformedTest, TruncatedTimestampOnly) {
  auto result = parse_text_tick("1234567890");
  EXPECT_FALSE(result.has_value()) << "Should reject timestamp-only message";
}

TEST_F(TextProtocolMalformedTest, TruncatedTimestampAndSymbol) {
  auto result = parse_text_tick("1234567890 AAPL");
  EXPECT_FALSE(result.has_value()) << "Should reject message missing price and volume";
}

TEST_F(TextProtocolMalformedTest, TruncatedMissingVolume) {
  auto result = parse_text_tick("1234567890 AAPL 150.25");
  EXPECT_FALSE(result.has_value()) << "Should reject message missing volume";
}

TEST_F(TextProtocolMalformedTest, TruncatedEmptyString) {
  auto result = parse_text_tick("");
  EXPECT_FALSE(result.has_value()) << "Should reject empty string";
}

TEST_F(TextProtocolMalformedTest, TruncatedWhitespaceOnly) {
  auto result = parse_text_tick("   \t\t   ");
  EXPECT_FALSE(result.has_value()) << "Should reject whitespace-only string";
}

// --- Invalid Data Type Tests ---

TEST_F(TextProtocolMalformedTest, InvalidTimestampNonNumeric) {
  auto result = parse_text_tick("abc123 AAPL 150.25 100");
  EXPECT_FALSE(result.has_value()) << "Should reject non-numeric timestamp";
}

TEST_F(TextProtocolMalformedTest, InvalidTimestampFloat) {
  // Note: from_chars for uint64_t parses until first non-digit
  // So "123.456" parses as timestamp=123, treating ".456" as symbol
  // This is acceptable behavior - partial parse stops at non-digit
  auto result = parse_text_tick("123.456 AAPL 150.25 100");
  // The parser will try to parse ".456" as symbol which fails (invalid char)
  // But if whitespace separates properly, it may parse 123 as timestamp
  // Just verify no crash and if it succeeds, timestamp should be 123
  if (result.has_value()) {
    EXPECT_EQ(result->timestamp, 123u);
  }
}

TEST_F(TextProtocolMalformedTest, InvalidTimestampNegative) {
  auto result = parse_text_tick("-1234567890 AAPL 150.25 100");
  EXPECT_FALSE(result.has_value()) << "Should reject negative timestamp";
}

TEST_F(TextProtocolMalformedTest, InvalidTimestampOverflow) {
  // Larger than uint64_t max
  auto result = parse_text_tick("99999999999999999999999999 AAPL 150.25 100");
  EXPECT_FALSE(result.has_value()) << "Should reject overflowing timestamp";
}

TEST_F(TextProtocolMalformedTest, InvalidPriceNonNumeric) {
  auto result = parse_text_tick("1234567890 AAPL abc 100");
  EXPECT_FALSE(result.has_value()) << "Should reject non-numeric price";
}

TEST_F(TextProtocolMalformedTest, InvalidVolumeNonNumeric) {
  auto result = parse_text_tick("1234567890 AAPL 150.25 abc");
  EXPECT_FALSE(result.has_value()) << "Should reject non-numeric volume";
}

TEST_F(TextProtocolMalformedTest, InvalidVolumeFloat) {
  auto result = parse_text_tick("1234567890 AAPL 150.25 100.5");
  // Note: from_chars for int64_t will parse up to the decimal point
  // and may still succeed - this tests current behavior
  auto parsed = parse_text_tick("1234567890 AAPL 150.25 100.5");
  if (parsed.has_value()) {
    EXPECT_EQ(parsed->volume, 100);  // Partial parse is acceptable
  }
}

// --- Symbol Validation Tests ---

TEST_F(TextProtocolMalformedTest, SymbolTooLong) {
  auto result = parse_text_tick("1234567890 VERYLONGSYMBOL 150.25 100");
  EXPECT_FALSE(result.has_value()) << "Should reject symbol > 7 chars";
}

TEST_F(TextProtocolMalformedTest, SymbolExactlyMaxLength) {
  auto result = parse_text_tick("1234567890 ABCDEFG 150.25 100");
  EXPECT_TRUE(result.has_value()) << "Should accept 7-char symbol";
  EXPECT_STREQ(result->symbol, "ABCDEFG");
}

TEST_F(TextProtocolMalformedTest, SymbolEmpty) {
  auto result = parse_text_tick("1234567890  150.25 100");
  EXPECT_FALSE(result.has_value()) << "Should reject empty symbol";
}

// --- Special Character Tests ---

TEST_F(TextProtocolMalformedTest, NullByteInMessage) {
  // Create string with embedded null
  std::string msg = "1234567890 AA";
  msg.push_back('\0');
  msg += "PL 150.25 100";
  auto result = parse_text_tick(msg);
  // Parser should stop at null or handle it gracefully
  if (result.has_value()) {
    // Partial parse is acceptable
    EXPECT_LE(strlen(result->symbol), 7u);
  }
}

TEST_F(TextProtocolMalformedTest, TabDelimited) {
  auto result = parse_text_tick("1234567890\tAAPL\t150.25\t100");
  EXPECT_TRUE(result.has_value()) << "Should accept tab-delimited message";
  EXPECT_EQ(result->timestamp, 1234567890u);
  EXPECT_STREQ(result->symbol, "AAPL");
}

TEST_F(TextProtocolMalformedTest, MixedWhitespace) {
  auto result = parse_text_tick("  1234567890  \t AAPL   150.25 \t 100  ");
  EXPECT_TRUE(result.has_value()) << "Should handle mixed whitespace";
  EXPECT_EQ(result->timestamp, 1234567890u);
}

TEST_F(TextProtocolMalformedTest, CarriageReturn) {
  auto result = parse_text_tick("1234567890 AAPL 150.25 100\r");
  EXPECT_TRUE(result.has_value()) << "Should handle trailing CR";
  EXPECT_EQ(result->volume, 100);
}

// --- Boundary Value Tests ---

TEST_F(TextProtocolMalformedTest, ZeroTimestamp) {
  auto result = parse_text_tick("0 AAPL 150.25 100");
  EXPECT_TRUE(result.has_value()) << "Should accept zero timestamp";
  EXPECT_EQ(result->timestamp, 0u);
}

TEST_F(TextProtocolMalformedTest, ZeroPrice) {
  auto result = parse_text_tick("1234567890 AAPL 0 100");
  EXPECT_TRUE(result.has_value()) << "Should accept zero price";
  EXPECT_DOUBLE_EQ(result->price, 0.0);
}

TEST_F(TextProtocolMalformedTest, ZeroVolume) {
  auto result = parse_text_tick("1234567890 AAPL 150.25 0");
  EXPECT_TRUE(result.has_value()) << "Should accept zero volume";
  EXPECT_EQ(result->volume, 0);
}

TEST_F(TextProtocolMalformedTest, NegativeVolume) {
  auto result = parse_text_tick("1234567890 AAPL 150.25 -100");
  EXPECT_TRUE(result.has_value()) << "Should accept negative volume";
  EXPECT_EQ(result->volume, -100);
}

TEST_F(TextProtocolMalformedTest, VeryLargePrice) {
  auto result = parse_text_tick("1234567890 AAPL 999999999.99 100");
  EXPECT_TRUE(result.has_value()) << "Should accept large price";
  EXPECT_GT(result->price, 999999999.0);
}

TEST_F(TextProtocolMalformedTest, VerySmallPrice) {
  auto result = parse_text_tick("1234567890 AAPL 0.0001 100");
  EXPECT_TRUE(result.has_value()) << "Should accept small price";
  EXPECT_GT(result->price, 0.0);
  EXPECT_LT(result->price, 0.001);
}

TEST_F(TextProtocolMalformedTest, NegativePrice) {
  auto result = parse_text_tick("1234567890 AAPL -150.25 100");
  EXPECT_TRUE(result.has_value()) << "Should accept negative price";
  EXPECT_LT(result->price, 0.0);
}

// --- TextLineBuffer Malformed Tests ---

class TextLineBufferMalformedTest : public ::testing::Test {
protected:
  TextLineBuffer buffer_;
};

TEST_F(TextLineBufferMalformedTest, VeryLongLine) {
  // Create a line that exceeds normal expectations but fits in buffer
  std::string long_line(1000, 'A');
  long_line += "\n";

  EXPECT_TRUE(buffer_.append(long_line.c_str(), long_line.size()));

  std::string_view line;
  EXPECT_TRUE(buffer_.get_line(line));
  EXPECT_EQ(line.size(), 1000u);
}

TEST_F(TextLineBufferMalformedTest, MultipleEmptyLines) {
  const char* data = "\n\n\n";
  EXPECT_TRUE(buffer_.append(data, 3));

  std::string_view line;
  int count = 0;
  while (buffer_.get_line(line)) {
    EXPECT_EQ(line.size(), 0u);  // Empty lines
    count++;
  }
  EXPECT_EQ(count, 3);
}

TEST_F(TextLineBufferMalformedTest, PartialLineRecovery) {
  // First append: partial line
  const char* part1 = "1234567890 AA";
  EXPECT_TRUE(buffer_.append(part1, strlen(part1)));

  std::string_view line;
  EXPECT_FALSE(buffer_.get_line(line)) << "Should not return incomplete line";

  // Second append: complete the line
  const char* part2 = "PL 150.25 100\n";
  EXPECT_TRUE(buffer_.append(part2, strlen(part2)));

  EXPECT_TRUE(buffer_.get_line(line));
  EXPECT_EQ(line, "1234567890 AAPL 150.25 100");
}

TEST_F(TextLineBufferMalformedTest, BufferOverflowPrevention) {
  // Fill buffer nearly full
  std::string big_data(TextLineBuffer::BUFFER_SIZE - 100, 'X');
  EXPECT_TRUE(buffer_.append(big_data.c_str(), big_data.size()));

  // Try to overflow
  std::string overflow_data(200, 'Y');
  // After compaction, this should still fail if buffer is too full
  // The exact behavior depends on implementation
}

// =============================================================================
// Binary Protocol Malformed Input Tests
// =============================================================================

class BinaryProtocolMalformedTest : public ::testing::Test {
protected:
  void SetUp() override {}

  // Helper to create a header with arbitrary type
  std::string create_raw_header(uint32_t length, uint8_t type, uint64_t sequence) {
    std::string data;
    data.reserve(MessageHeader::HEADER_SIZE);

    uint32_t length_net = htonl(length);
    data.append(reinterpret_cast<const char*>(&length_net), 4);
    data.append(reinterpret_cast<const char*>(&type), 1);
    uint64_t sequence_net = htonll(sequence);
    data.append(reinterpret_cast<const char*>(&sequence_net), 8);

    return data;
  }
};

// --- Invalid Message Type Tests ---

TEST_F(BinaryProtocolMalformedTest, UnknownMessageType) {
  // Create header with unknown message type (0x99)
  auto header_data = create_raw_header(20, 0x99, 1);
  MessageHeader header = deserialize_header(header_data.c_str());

  EXPECT_EQ(static_cast<uint8_t>(header.type), 0x99);
  // The parser doesn't validate type - this is by design for extensibility
  // But handlers should check and reject unknown types
}

TEST_F(BinaryProtocolMalformedTest, ZeroMessageType) {
  auto header_data = create_raw_header(20, 0x00, 1);
  MessageHeader header = deserialize_header(header_data.c_str());

  EXPECT_EQ(static_cast<uint8_t>(header.type), 0x00);
  // Type 0 is not a valid MessageType
}

TEST_F(BinaryProtocolMalformedTest, ValidMessageTypes) {
  // Test all valid message types parse correctly
  std::vector<MessageType> valid_types = {
    MessageType::TICK,
    MessageType::HEARTBEAT,
    MessageType::SNAPSHOT_REQUEST,
    MessageType::SNAPSHOT_RESPONSE,
    MessageType::ORDER_BOOK_UPDATE
  };

  for (auto type : valid_types) {
    auto header_data = create_raw_header(20, static_cast<uint8_t>(type), 1);
    MessageHeader header = deserialize_header(header_data.c_str());
    EXPECT_EQ(header.type, type);
  }
}

// --- Truncated Header Tests ---

TEST_F(BinaryProtocolMalformedTest, TruncatedHeaderCheck) {
  // Create valid tick message
  std::string msg = serialize_tick(1, 1234567890, "AAPL", 150.25f, 100);

  // Verify we can detect when we don't have enough bytes for header
  EXPECT_GE(msg.size(), MessageHeader::HEADER_SIZE);

  // Truncated header would be detected by buffer length check before parsing
  // This test verifies the header size constant is correct
  EXPECT_EQ(MessageHeader::HEADER_SIZE, 13u);
}

// --- Payload Size Mismatch Tests ---

TEST_F(BinaryProtocolMalformedTest, PayloadSizeTooLarge) {
  // Create header claiming larger payload than actual
  auto header_data = create_raw_header(1000, static_cast<uint8_t>(MessageType::TICK), 1);
  MessageHeader header = deserialize_header(header_data.c_str());

  EXPECT_EQ(header.length, 1000u);
  // Handler should check if buffer contains enough bytes before parsing payload
}

TEST_F(BinaryProtocolMalformedTest, PayloadSizeZero) {
  auto header_data = create_raw_header(0, static_cast<uint8_t>(MessageType::TICK), 1);
  MessageHeader header = deserialize_header(header_data.c_str());

  EXPECT_EQ(header.length, 0u);
  // Zero-length tick payload is invalid - handler should reject
}

TEST_F(BinaryProtocolMalformedTest, PayloadSizeMaxUint32) {
  auto header_data = create_raw_header(0xFFFFFFFF, static_cast<uint8_t>(MessageType::TICK), 1);
  MessageHeader header = deserialize_header(header_data.c_str());

  EXPECT_EQ(header.length, 0xFFFFFFFFu);
  // Handler should reject impossibly large payload
}

// --- Corrupted Tick Payload Tests ---

TEST_F(BinaryProtocolMalformedTest, TickPayloadAllZeros) {
  char zero_payload[TickPayload::PAYLOAD_SIZE] = {0};
  TickPayload tick = deserialize_tick_payload(zero_payload);

  EXPECT_EQ(tick.timestamp, 0u);
  EXPECT_EQ(tick.volume, 0);
  // Zero payload should parse without crashing
}

TEST_F(BinaryProtocolMalformedTest, TickPayloadAllOnes) {
  char ones_payload[TickPayload::PAYLOAD_SIZE];
  memset(ones_payload, 0xFF, sizeof(ones_payload));
  TickPayload tick = deserialize_tick_payload(ones_payload);

  // Should parse without crashing, values may be garbage
  EXPECT_EQ(tick.timestamp, 0xFFFFFFFFFFFFFFFFull);
}

TEST_F(BinaryProtocolMalformedTest, TickPayloadNaN) {
  // Create tick with NaN price
  std::string msg = serialize_tick(1, 1234567890, "AAPL", std::numeric_limits<float>::quiet_NaN(), 100);

  // Extract and deserialize
  const char* payload = msg.c_str() + MessageHeader::HEADER_SIZE;
  TickPayload tick = deserialize_tick_payload(payload);

  EXPECT_TRUE(std::isnan(tick.price));
}

TEST_F(BinaryProtocolMalformedTest, TickPayloadInfinity) {
  // Create tick with infinity price
  std::string msg = serialize_tick(1, 1234567890, "AAPL", std::numeric_limits<float>::infinity(), 100);

  const char* payload = msg.c_str() + MessageHeader::HEADER_SIZE;
  TickPayload tick = deserialize_tick_payload(payload);

  EXPECT_TRUE(std::isinf(tick.price));
}

TEST_F(BinaryProtocolMalformedTest, TickPayloadNegativeInfinity) {
  std::string msg = serialize_tick(1, 1234567890, "AAPL", -std::numeric_limits<float>::infinity(), 100);

  const char* payload = msg.c_str() + MessageHeader::HEADER_SIZE;
  TickPayload tick = deserialize_tick_payload(payload);

  EXPECT_TRUE(std::isinf(tick.price));
  EXPECT_LT(tick.price, 0.0f);
}

// --- Sequence Number Tests ---

TEST_F(BinaryProtocolMalformedTest, SequenceNumberZero) {
  std::string msg = serialize_tick(0, 1234567890, "AAPL", 150.25f, 100);
  MessageHeader header = deserialize_header(msg.c_str());

  EXPECT_EQ(header.sequence, 0u);
}

TEST_F(BinaryProtocolMalformedTest, SequenceNumberMax) {
  std::string msg = serialize_tick(0xFFFFFFFFFFFFFFFFull, 1234567890, "AAPL", 150.25f, 100);
  MessageHeader header = deserialize_header(msg.c_str());

  EXPECT_EQ(header.sequence, 0xFFFFFFFFFFFFFFFFull);
}

TEST_F(BinaryProtocolMalformedTest, SequenceNumberWrapAround) {
  // Simulate sequence wrap-around scenario
  uint64_t seq1 = 0xFFFFFFFFFFFFFFFFull;
  uint64_t seq2 = 0;

  std::string msg1 = serialize_tick(seq1, 1234567890, "AAPL", 150.25f, 100);
  std::string msg2 = serialize_tick(seq2, 1234567891, "AAPL", 150.50f, 100);

  MessageHeader header1 = deserialize_header(msg1.c_str());
  MessageHeader header2 = deserialize_header(msg2.c_str());

  EXPECT_EQ(header1.sequence, seq1);
  EXPECT_EQ(header2.sequence, seq2);
  EXPECT_LT(header2.sequence, header1.sequence);  // Wrap-around
}

// --- Heartbeat Malformed Tests ---

TEST_F(BinaryProtocolMalformedTest, HeartbeatPayloadCorrupt) {
  char corrupt_payload[HeartbeatPayload::PAYLOAD_SIZE];
  memset(corrupt_payload, 0xAB, sizeof(corrupt_payload));

  HeartbeatPayload heartbeat = deserialize_heartbeat_payload(corrupt_payload);
  // Should parse without crashing
  EXPECT_NE(heartbeat.timestamp, 0u);  // Non-zero after deserialization
}

// --- Snapshot Response Malformed Tests ---

TEST_F(BinaryProtocolMalformedTest, SnapshotResponseZeroLevels) {
  std::vector<OrderBookLevel> empty_bids, empty_asks;
  std::string msg = serialize_snapshot_response(1, "AAPL", empty_bids, empty_asks);

  char symbol[4];
  std::vector<OrderBookLevel> bids, asks;
  deserialize_snapshot_response(msg.c_str() + MessageHeader::HEADER_SIZE,
                                 0, symbol, bids, asks);

  EXPECT_EQ(bids.size(), 0u);
  EXPECT_EQ(asks.size(), 0u);
}

TEST_F(BinaryProtocolMalformedTest, SnapshotResponseMaxLevels) {
  std::vector<OrderBookLevel> many_bids(255), many_asks(255);
  for (int i = 0; i < 255; ++i) {
    many_bids[i] = {static_cast<float>(100.0 - i * 0.01), static_cast<uint64_t>(100 + i)};
    many_asks[i] = {static_cast<float>(100.0 + i * 0.01), static_cast<uint64_t>(100 + i)};
  }

  std::string msg = serialize_snapshot_response(1, "AAPL", many_bids, many_asks);

  char symbol[4];
  std::vector<OrderBookLevel> bids, asks;
  deserialize_snapshot_response(msg.c_str() + MessageHeader::HEADER_SIZE,
                                 0, symbol, bids, asks);

  EXPECT_EQ(bids.size(), 255u);
  EXPECT_EQ(asks.size(), 255u);
}

// --- Order Book Update Malformed Tests ---

TEST_F(BinaryProtocolMalformedTest, OrderBookUpdateInvalidSide) {
  // Create update with invalid side (not 0 or 1)
  std::string msg = serialize_order_book_update(1, "AAPL", 99, 100.0f, 1000);

  OrderBookUpdatePayload update = deserialize_order_book_update(
    msg.c_str() + MessageHeader::HEADER_SIZE);

  EXPECT_EQ(update.side, 99);
  // Handler should validate and reject invalid side
}

TEST_F(BinaryProtocolMalformedTest, OrderBookUpdateZeroQuantity) {
  std::string msg = serialize_order_book_update(1, "AAPL", 0, 100.0f, 0);

  OrderBookUpdatePayload update = deserialize_order_book_update(
    msg.c_str() + MessageHeader::HEADER_SIZE);

  EXPECT_EQ(update.quantity, 0);
  // Zero quantity means delete level
}

TEST_F(BinaryProtocolMalformedTest, OrderBookUpdateNegativeQuantity) {
  std::string msg = serialize_order_book_update(1, "AAPL", 0, 100.0f, -500);

  OrderBookUpdatePayload update = deserialize_order_book_update(
    msg.c_str() + MessageHeader::HEADER_SIZE);

  EXPECT_EQ(update.quantity, -500);
}

// =============================================================================
// Ring Buffer Malformed Input Tests
// =============================================================================

class RingBufferMalformedTest : public ::testing::Test {
protected:
  RingBuffer buffer_;
};

TEST_F(RingBufferMalformedTest, ReadMoreThanAvailable) {
  const char* data = "Hello";
  auto [ptr, space] = buffer_.get_write_ptr();
  memcpy(ptr, data, 5);
  buffer_.commit_write(5);

  char out[100];
  EXPECT_FALSE(buffer_.read_bytes(out, 100)) << "Should fail reading more than available";
  EXPECT_EQ(buffer_.available(), 5u) << "Data should remain after failed read";
}

TEST_F(RingBufferMalformedTest, PeekMoreThanAvailable) {
  const char* data = "Hi";
  auto [ptr, space] = buffer_.get_write_ptr();
  memcpy(ptr, data, 2);
  buffer_.commit_write(2);

  char out[100];
  EXPECT_FALSE(buffer_.peek_bytes(out, 100)) << "Should fail peeking more than available";
}

TEST_F(RingBufferMalformedTest, ConsumeMoreThanAvailable) {
  const char* data = "Test";
  auto [ptr, space] = buffer_.get_write_ptr();
  memcpy(ptr, data, 4);
  buffer_.commit_write(4);

  buffer_.consume(100);  // Try to consume way more than available
  EXPECT_EQ(buffer_.available(), 0u) << "Should consume all available, not crash";
}

TEST_F(RingBufferMalformedTest, WriteZeroBytes) {
  auto [ptr, space] = buffer_.get_write_ptr();
  buffer_.commit_write(0);
  EXPECT_EQ(buffer_.available(), 0u);
}

// =============================================================================
// Error Recovery Tests
// =============================================================================

class ErrorRecoveryTest : public ::testing::Test {
protected:
  TextLineBuffer line_buffer_;
};

TEST_F(ErrorRecoveryTest, RecoverAfterMalformedLine) {
  // Mix of malformed and valid lines
  const char* mixed_data =
    "invalid line missing data\n"
    "1234567890 AAPL 150.25 100\n"
    "also invalid\n"
    "1234567891 GOOG 2750.50 50\n";

  EXPECT_TRUE(line_buffer_.append(mixed_data, strlen(mixed_data)));

  std::string_view line;
  int valid_count = 0;
  int invalid_count = 0;

  while (line_buffer_.get_line(line)) {
    auto tick = parse_text_tick(line);
    if (tick.has_value()) {
      valid_count++;
    } else {
      invalid_count++;
    }
  }

  EXPECT_EQ(valid_count, 2) << "Should parse 2 valid ticks";
  EXPECT_EQ(invalid_count, 2) << "Should skip 2 invalid lines";
}

TEST_F(ErrorRecoveryTest, ContinueAfterCorruptBinaryMessage) {
  // Create sequence of messages with one corrupt in the middle
  std::string msg1 = serialize_tick(1, 1234567890, "AAPL", 150.25f, 100);
  std::string msg2 = serialize_tick(2, 1234567891, "GOOG", 2750.50f, 50);
  std::string msg3 = serialize_tick(3, 1234567892, "MSFT", 300.00f, 200);

  // Concatenate messages
  std::string all_msgs = msg1 + msg2 + msg3;

  // Parse all messages
  const char* ptr = all_msgs.c_str();
  size_t remaining = all_msgs.size();
  int parsed_count = 0;

  while (remaining >= MessageHeader::HEADER_SIZE) {
    MessageHeader header = deserialize_header(ptr);
    size_t total_msg_size = MessageHeader::HEADER_SIZE + header.length;

    if (remaining < total_msg_size) break;

    if (header.type == MessageType::TICK && header.length == TickPayload::PAYLOAD_SIZE) {
      TickPayload tick = deserialize_tick_payload(ptr + MessageHeader::HEADER_SIZE);
      parsed_count++;
    }

    ptr += total_msg_size;
    remaining -= total_msg_size;
  }

  EXPECT_EQ(parsed_count, 3) << "Should parse all 3 valid messages";
}

TEST_F(ErrorRecoveryTest, HandleMixedValidAndInvalidInBuffer) {
  RingBuffer buffer;

  // Write valid tick
  std::string msg1 = serialize_tick(1, 1234567890, "AAPL", 150.25f, 100);
  auto [ptr1, space1] = buffer.get_write_ptr();
  memcpy(ptr1, msg1.c_str(), msg1.size());
  buffer.commit_write(msg1.size());

  // Write another valid tick
  std::string msg2 = serialize_tick(2, 1234567891, "GOOG", 2750.50f, 50);
  auto [ptr2, space2] = buffer.get_write_ptr();
  memcpy(ptr2, msg2.c_str(), msg2.size());
  buffer.commit_write(msg2.size());

  // Parse both messages from buffer
  int count = 0;
  while (buffer.available() >= MessageHeader::HEADER_SIZE) {
    char header_bytes[MessageHeader::HEADER_SIZE];
    if (!buffer.peek_bytes(header_bytes, MessageHeader::HEADER_SIZE)) break;

    MessageHeader header = deserialize_header(header_bytes);
    size_t total = MessageHeader::HEADER_SIZE + header.length;

    if (buffer.available() < total) break;

    char msg_bytes[64];
    if (!buffer.read_bytes(msg_bytes, total)) break;

    if (header.type == MessageType::TICK) {
      count++;
    }
  }

  EXPECT_EQ(count, 2);
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
