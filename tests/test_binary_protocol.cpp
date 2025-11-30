#include <gtest/gtest.h>
#include <cstring>
#include <vector>

#include "binary_protocol.hpp"

// Test fixture for Binary Protocol tests
class BinaryProtocolTest : public ::testing::Test {
protected:
  void SetUp() override {}
  void TearDown() override {}
};

// Message type tests
TEST_F(BinaryProtocolTest, MessageTypeValues) {
  EXPECT_EQ(static_cast<uint8_t>(MessageType::TICK), 0x01);
  EXPECT_EQ(static_cast<uint8_t>(MessageType::HEARTBEAT), 0xFF);
  EXPECT_EQ(static_cast<uint8_t>(MessageType::SNAPSHOT_REQUEST), 0x10);
  EXPECT_EQ(static_cast<uint8_t>(MessageType::SNAPSHOT_RESPONSE), 0x11);
  EXPECT_EQ(static_cast<uint8_t>(MessageType::ORDER_BOOK_UPDATE), 0x02);
}

// Size constants tests
TEST_F(BinaryProtocolTest, SizeConstants) {
  EXPECT_EQ(MessageHeader::HEADER_SIZE, 13);
  EXPECT_EQ(TickPayload::PAYLOAD_SIZE, 20);
  EXPECT_EQ(HeartbeatPayload::PAYLOAD_SIZE, 8);
  EXPECT_EQ(SnapshotRequestPayload::PAYLOAD_SIZE, 4);
  EXPECT_EQ(SnapshotResponsePayload::HEADER_SIZE, 6);
  EXPECT_EQ(OrderBookLevel::SIZE, 12);
  EXPECT_EQ(OrderBookUpdatePayload::PAYLOAD_SIZE, 17);
}

// Tick serialization/deserialization tests
TEST_F(BinaryProtocolTest, SerializeDeserializeTick) {
  uint64_t sequence = 42;
  uint64_t timestamp = 1234567890123456789ULL;
  char symbol[4] = {'A', 'A', 'P', 'L'};
  float price = 150.25f;
  int32_t volume = 1000;

  std::string message = serialize_tick(sequence, timestamp, symbol, price, volume);

  // Check message size
  EXPECT_EQ(message.size(), MessageHeader::HEADER_SIZE + TickPayload::PAYLOAD_SIZE);

  // Deserialize header
  MessageHeader header = deserialize_header(message.data());
  EXPECT_EQ(header.length, TickPayload::PAYLOAD_SIZE);
  EXPECT_EQ(header.type, MessageType::TICK);
  EXPECT_EQ(header.sequence, sequence);

  // Deserialize payload
  TickPayload tick = deserialize_tick_payload(message.data() + MessageHeader::HEADER_SIZE);
  EXPECT_EQ(tick.timestamp, timestamp);
  EXPECT_EQ(memcmp(tick.symbol, symbol, 4), 0);
  EXPECT_FLOAT_EQ(tick.price, price);
  EXPECT_EQ(tick.volume, volume);
}

TEST_F(BinaryProtocolTest, SerializeTickWithBinaryTick) {
  BinaryTick tick;
  tick.timestamp = 9876543210ULL;
  memcpy(tick.symbol, "GOOG", 4);
  tick.price = 2750.50f;
  tick.volume = 500;

  std::string message = serialize_tick(tick);

  EXPECT_EQ(message.size(), MessageHeader::HEADER_SIZE + TickPayload::PAYLOAD_SIZE);

  TickPayload result = deserialize_tick_payload(message.data() + MessageHeader::HEADER_SIZE);
  EXPECT_EQ(result.timestamp, tick.timestamp);
  EXPECT_FLOAT_EQ(result.price, tick.price);
  EXPECT_EQ(result.volume, tick.volume);
}

// Heartbeat serialization/deserialization tests
TEST_F(BinaryProtocolTest, SerializeDeserializeHeartbeat) {
  uint64_t sequence = 100;
  uint64_t timestamp = 1234567890ULL;

  std::string message = serialize_heartbeat(sequence, timestamp);

  EXPECT_EQ(message.size(), MessageHeader::HEADER_SIZE + HeartbeatPayload::PAYLOAD_SIZE);

  MessageHeader header = deserialize_header(message.data());
  EXPECT_EQ(header.length, HeartbeatPayload::PAYLOAD_SIZE);
  EXPECT_EQ(header.type, MessageType::HEARTBEAT);
  EXPECT_EQ(header.sequence, sequence);

  HeartbeatPayload heartbeat = deserialize_heartbeat_payload(message.data() + MessageHeader::HEADER_SIZE);
  EXPECT_EQ(heartbeat.timestamp, timestamp);
}

// Snapshot request serialization/deserialization tests
TEST_F(BinaryProtocolTest, SerializeDeserializeSnapshotRequest) {
  uint64_t sequence = 200;
  char symbol[4] = {'M', 'S', 'F', 'T'};

  std::string message = serialize_snapshot_request(sequence, symbol);

  EXPECT_EQ(message.size(), MessageHeader::HEADER_SIZE + SnapshotRequestPayload::PAYLOAD_SIZE);

  MessageHeader header = deserialize_header(message.data());
  EXPECT_EQ(header.length, SnapshotRequestPayload::PAYLOAD_SIZE);
  EXPECT_EQ(header.type, MessageType::SNAPSHOT_REQUEST);
  EXPECT_EQ(header.sequence, sequence);

  SnapshotRequestPayload request = deserialize_snapshot_request(message.data() + MessageHeader::HEADER_SIZE);
  EXPECT_EQ(memcmp(request.symbol, symbol, 4), 0);
}

// Snapshot response serialization/deserialization tests
TEST_F(BinaryProtocolTest, SerializeDeserializeSnapshotResponse) {
  uint64_t sequence = 300;
  char symbol[4] = {'T', 'S', 'L', 'A'};

  std::vector<OrderBookLevel> bids = {
    {100.50f, 1000},
    {100.25f, 2000},
    {100.00f, 1500}
  };

  std::vector<OrderBookLevel> asks = {
    {100.75f, 800},
    {101.00f, 1200}
  };

  std::string message = serialize_snapshot_response(sequence, symbol, bids, asks);

  // Check header
  MessageHeader header = deserialize_header(message.data());
  EXPECT_EQ(header.type, MessageType::SNAPSHOT_RESPONSE);
  EXPECT_EQ(header.sequence, sequence);

  // Deserialize
  char symbol_out[4];
  std::vector<OrderBookLevel> bids_out, asks_out;
  deserialize_snapshot_response(message.data() + MessageHeader::HEADER_SIZE,
                                 header.length, symbol_out, bids_out, asks_out);

  EXPECT_EQ(memcmp(symbol_out, symbol, 4), 0);
  ASSERT_EQ(bids_out.size(), bids.size());
  ASSERT_EQ(asks_out.size(), asks.size());

  for (size_t i = 0; i < bids.size(); ++i) {
    EXPECT_FLOAT_EQ(bids_out[i].price, bids[i].price);
    EXPECT_EQ(bids_out[i].quantity, bids[i].quantity);
  }

  for (size_t i = 0; i < asks.size(); ++i) {
    EXPECT_FLOAT_EQ(asks_out[i].price, asks[i].price);
    EXPECT_EQ(asks_out[i].quantity, asks[i].quantity);
  }
}

TEST_F(BinaryProtocolTest, SnapshotResponseEmptyBook) {
  char symbol[4] = {'E', 'M', 'P', 'T'};
  std::vector<OrderBookLevel> empty_bids, empty_asks;

  std::string message = serialize_snapshot_response(1, symbol, empty_bids, empty_asks);

  MessageHeader header = deserialize_header(message.data());
  EXPECT_EQ(header.type, MessageType::SNAPSHOT_RESPONSE);

  char symbol_out[4];
  std::vector<OrderBookLevel> bids_out, asks_out;
  deserialize_snapshot_response(message.data() + MessageHeader::HEADER_SIZE,
                                 header.length, symbol_out, bids_out, asks_out);

  EXPECT_TRUE(bids_out.empty());
  EXPECT_TRUE(asks_out.empty());
}

// Order book update serialization/deserialization tests
TEST_F(BinaryProtocolTest, SerializeDeserializeOrderBookUpdate) {
  uint64_t sequence = 400;
  char symbol[4] = {'A', 'M', 'Z', 'N'};
  uint8_t side = 0;  // bid
  float price = 3500.00f;
  int64_t quantity = 100;

  std::string message = serialize_order_book_update(sequence, symbol, side, price, quantity);

  EXPECT_EQ(message.size(), MessageHeader::HEADER_SIZE + OrderBookUpdatePayload::PAYLOAD_SIZE);

  MessageHeader header = deserialize_header(message.data());
  EXPECT_EQ(header.length, OrderBookUpdatePayload::PAYLOAD_SIZE);
  EXPECT_EQ(header.type, MessageType::ORDER_BOOK_UPDATE);
  EXPECT_EQ(header.sequence, sequence);

  OrderBookUpdatePayload update = deserialize_order_book_update(message.data() + MessageHeader::HEADER_SIZE);
  EXPECT_EQ(memcmp(update.symbol, symbol, 4), 0);
  EXPECT_EQ(update.side, side);
  EXPECT_FLOAT_EQ(update.price, price);
  EXPECT_EQ(update.quantity, quantity);
}

TEST_F(BinaryProtocolTest, OrderBookUpdateAskSide) {
  char symbol[4] = {'N', 'F', 'L', 'X'};
  uint8_t side = 1;  // ask
  float price = 650.75f;
  int64_t quantity = 250;

  std::string message = serialize_order_book_update(1, symbol, side, price, quantity);
  OrderBookUpdatePayload update = deserialize_order_book_update(message.data() + MessageHeader::HEADER_SIZE);

  EXPECT_EQ(update.side, 1);
}

TEST_F(BinaryProtocolTest, OrderBookUpdateDeleteLevel) {
  char symbol[4] = {'M', 'E', 'T', 'A'};
  int64_t quantity = 0;  // Delete level

  std::string message = serialize_order_book_update(1, symbol, 0, 300.00f, quantity);
  OrderBookUpdatePayload update = deserialize_order_book_update(message.data() + MessageHeader::HEADER_SIZE);

  EXPECT_EQ(update.quantity, 0);
}

// Network byte order tests
TEST_F(BinaryProtocolTest, NetworkByteOrderConversion) {
  uint64_t original = 0x123456789ABCDEF0ULL;
  uint64_t converted = htonll(original);
  uint64_t back = ntohll(converted);
  EXPECT_EQ(original, back);
}

TEST_F(BinaryProtocolTest, SequencePreservation) {
  // Test that sequence numbers are properly preserved through serialization
  for (uint64_t seq : {0ULL, 1ULL, 255ULL, 65535ULL, 0xFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL}) {
    std::string message = serialize_heartbeat(seq, 0);
    MessageHeader header = deserialize_header(message.data());
    EXPECT_EQ(header.sequence, seq) << "Failed for sequence " << seq;
  }
}

// Edge cases
TEST_F(BinaryProtocolTest, ZeroValues) {
  char symbol[4] = {0, 0, 0, 0};
  std::string message = serialize_tick(0, 0, symbol, 0.0f, 0);

  TickPayload tick = deserialize_tick_payload(message.data() + MessageHeader::HEADER_SIZE);
  EXPECT_EQ(tick.timestamp, 0);
  EXPECT_FLOAT_EQ(tick.price, 0.0f);
  EXPECT_EQ(tick.volume, 0);
}

TEST_F(BinaryProtocolTest, MaxValues) {
  char symbol[4] = {127, 127, 127, 127};
  uint64_t max_timestamp = UINT64_MAX;
  int32_t max_volume = INT32_MAX;

  std::string message = serialize_tick(UINT64_MAX, max_timestamp, symbol,
                                        std::numeric_limits<float>::max(), max_volume);

  MessageHeader header = deserialize_header(message.data());
  EXPECT_EQ(header.sequence, UINT64_MAX);

  TickPayload tick = deserialize_tick_payload(message.data() + MessageHeader::HEADER_SIZE);
  EXPECT_EQ(tick.timestamp, max_timestamp);
  EXPECT_EQ(tick.volume, max_volume);
}

TEST_F(BinaryProtocolTest, NegativeVolume) {
  char symbol[4] = {'T', 'E', 'S', 'T'};
  int32_t negative_volume = -500;

  std::string message = serialize_tick(1, 1000, symbol, 100.0f, negative_volume);
  TickPayload tick = deserialize_tick_payload(message.data() + MessageHeader::HEADER_SIZE);

  EXPECT_EQ(tick.volume, negative_volume);
}

// Throughput test
TEST_F(BinaryProtocolTest, SerializationThroughput) {
  constexpr size_t NUM_MESSAGES = 100000;
  char symbol[4] = {'T', 'E', 'S', 'T'};

  auto start = std::chrono::high_resolution_clock::now();

  for (size_t i = 0; i < NUM_MESSAGES; ++i) {
    std::string msg = serialize_tick(i, 1234567890 + i, symbol, 100.0f + i, 100);
    // Prevent compiler optimization
    ASSERT_FALSE(msg.empty());
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

  double msgs_per_sec = (static_cast<double>(NUM_MESSAGES) / duration_us) * 1000000.0;
  std::cout << "  Serialization throughput: " << static_cast<size_t>(msgs_per_sec) << " msgs/sec" << std::endl;

  EXPECT_GT(msgs_per_sec, 1000000) << "Serialization throughput below 1M/sec";
}

TEST_F(BinaryProtocolTest, DeserializationThroughput) {
  constexpr size_t NUM_MESSAGES = 100000;
  char symbol[4] = {'T', 'E', 'S', 'T'};

  // Pre-generate messages
  std::vector<std::string> messages;
  messages.reserve(NUM_MESSAGES);
  for (size_t i = 0; i < NUM_MESSAGES; ++i) {
    messages.push_back(serialize_tick(i, 1234567890 + i, symbol, 100.0f + i, 100));
  }

  auto start = std::chrono::high_resolution_clock::now();

  uint64_t checksum = 0;
  for (const auto& msg : messages) {
    MessageHeader header = deserialize_header(msg.data());
    TickPayload tick = deserialize_tick_payload(msg.data() + MessageHeader::HEADER_SIZE);
    checksum += tick.volume;  // Prevent optimization
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

  double msgs_per_sec = (static_cast<double>(NUM_MESSAGES) / duration_us) * 1000000.0;
  std::cout << "  Deserialization throughput: " << static_cast<size_t>(msgs_per_sec) << " msgs/sec" << std::endl;

  EXPECT_GT(msgs_per_sec, 1000000) << "Deserialization throughput below 1M/sec";
  EXPECT_EQ(checksum, NUM_MESSAGES * 100);  // All volumes were 100
}
