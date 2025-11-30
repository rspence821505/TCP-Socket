#include <gtest/gtest.h>
#include <string>
#include <string_view>

#include "text_protocol.hpp"

// Test fixture for Text Protocol tests
class TextProtocolTest : public ::testing::Test {
protected:
  void SetUp() override {}
  void TearDown() override {}
};

// Basic parsing tests
TEST_F(TextProtocolTest, ParseValidTick) {
  auto tick = parse_text_tick("1234567890 AAPL 150.25 100");
  ASSERT_TRUE(tick.has_value());
  EXPECT_EQ(tick->timestamp, 1234567890);
  EXPECT_STREQ(tick->symbol, "AAPL");
  EXPECT_DOUBLE_EQ(tick->price, 150.25);
  EXPECT_EQ(tick->volume, 100);
}

TEST_F(TextProtocolTest, ParseTickWithTabs) {
  auto tick = parse_text_tick("1234567890\tGOOG\t2750.50\t50");
  ASSERT_TRUE(tick.has_value());
  EXPECT_EQ(tick->timestamp, 1234567890);
  EXPECT_STREQ(tick->symbol, "GOOG");
  EXPECT_DOUBLE_EQ(tick->price, 2750.50);
  EXPECT_EQ(tick->volume, 50);
}

TEST_F(TextProtocolTest, ParseTickWithMultipleSpaces) {
  auto tick = parse_text_tick("1234567890   MSFT   300.00   200");
  ASSERT_TRUE(tick.has_value());
  EXPECT_EQ(tick->timestamp, 1234567890);
  EXPECT_STREQ(tick->symbol, "MSFT");
  EXPECT_DOUBLE_EQ(tick->price, 300.00);
  EXPECT_EQ(tick->volume, 200);
}

TEST_F(TextProtocolTest, ParseTickWithLeadingWhitespace) {
  auto tick = parse_text_tick("  1234567890 TSLA 800.00 75");
  ASSERT_TRUE(tick.has_value());
  EXPECT_EQ(tick->timestamp, 1234567890);
  EXPECT_STREQ(tick->symbol, "TSLA");
}

TEST_F(TextProtocolTest, ParseTickLargeTimestamp) {
  auto tick = parse_text_tick("9999999999999 IBM 140.00 1000");
  ASSERT_TRUE(tick.has_value());
  EXPECT_EQ(tick->timestamp, 9999999999999ULL);
}

TEST_F(TextProtocolTest, ParseTickNegativeVolume) {
  auto tick = parse_text_tick("1234567890 AAPL 150.25 -500");
  ASSERT_TRUE(tick.has_value());
  EXPECT_EQ(tick->volume, -500);
}

TEST_F(TextProtocolTest, ParseTickDecimalPrice) {
  auto tick = parse_text_tick("1234567890 AAPL 0.0001 100");
  ASSERT_TRUE(tick.has_value());
  EXPECT_NEAR(tick->price, 0.0001, 0.00001);
}

// Invalid input tests
TEST_F(TextProtocolTest, ParseEmptyLine) {
  auto tick = parse_text_tick("");
  EXPECT_FALSE(tick.has_value());
}

TEST_F(TextProtocolTest, ParseWhitespaceOnly) {
  auto tick = parse_text_tick("   ");
  EXPECT_FALSE(tick.has_value());
}

TEST_F(TextProtocolTest, ParseMissingTimestamp) {
  auto tick = parse_text_tick("AAPL 150.25 100");
  EXPECT_FALSE(tick.has_value());  // "AAPL" is not a valid number
}

TEST_F(TextProtocolTest, ParseMissingVolume) {
  auto tick = parse_text_tick("1234567890 AAPL 150.25");
  EXPECT_FALSE(tick.has_value());
}

TEST_F(TextProtocolTest, ParseMissingPrice) {
  auto tick = parse_text_tick("1234567890 AAPL");
  EXPECT_FALSE(tick.has_value());
}

TEST_F(TextProtocolTest, ParseMissingSymbol) {
  auto tick = parse_text_tick("1234567890");
  EXPECT_FALSE(tick.has_value());
}

TEST_F(TextProtocolTest, ParseInvalidTimestamp) {
  auto tick = parse_text_tick("notanumber AAPL 150.25 100");
  EXPECT_FALSE(tick.has_value());
}

TEST_F(TextProtocolTest, ParseInvalidPrice) {
  auto tick = parse_text_tick("1234567890 AAPL notaprice 100");
  EXPECT_FALSE(tick.has_value());
}

TEST_F(TextProtocolTest, ParseInvalidVolume) {
  auto tick = parse_text_tick("1234567890 AAPL 150.25 notavolume");
  EXPECT_FALSE(tick.has_value());
}

TEST_F(TextProtocolTest, ParseSymbolTooLong) {
  // Symbol > 7 chars should fail
  auto tick = parse_text_tick("1234567890 VERYLONGSYMBOL 150.25 100");
  EXPECT_FALSE(tick.has_value());
}

// Serialization tests
TEST_F(TextProtocolTest, SerializeTextTick) {
  TextTick tick;
  tick.timestamp = 1234567890;
  strcpy(tick.symbol, "AAPL");
  tick.price = 150.25;
  tick.volume = 100;

  std::string serialized = serialize_text_tick(tick);
  EXPECT_EQ(serialized, "1234567890 AAPL 150.25 100\n");
}

TEST_F(TextProtocolTest, SerializeTextTickDirect) {
  std::string serialized = serialize_text_tick(1234567890, "GOOG", 2750.50, 50);
  EXPECT_EQ(serialized, "1234567890 GOOG 2750.50 50\n");
}

TEST_F(TextProtocolTest, RoundTripSerialization) {
  uint64_t ts = 9876543210;
  const char* sym = "MSFT";
  double price = 299.99;
  int64_t vol = 1000;

  std::string serialized = serialize_text_tick(ts, sym, price, vol);
  // Remove trailing newline for parsing
  serialized.pop_back();

  auto tick = parse_text_tick(serialized);
  ASSERT_TRUE(tick.has_value());
  EXPECT_EQ(tick->timestamp, ts);
  EXPECT_STREQ(tick->symbol, sym);
  EXPECT_NEAR(tick->price, price, 0.01);
  EXPECT_EQ(tick->volume, vol);
}

// find_line tests
TEST_F(TextProtocolTest, FindLineWithNewline) {
  const char* buffer = "line1\nline2\n";
  size_t line_end;
  EXPECT_TRUE(find_line(buffer, strlen(buffer), line_end));
  EXPECT_EQ(line_end, 5);  // Position of first '\n'
}

TEST_F(TextProtocolTest, FindLineNoNewline) {
  const char* buffer = "incomplete line";
  size_t line_end;
  EXPECT_FALSE(find_line(buffer, strlen(buffer), line_end));
}

TEST_F(TextProtocolTest, FindLineEmptyBuffer) {
  size_t line_end;
  EXPECT_FALSE(find_line("", 0, line_end));
}

// TextLineBuffer tests
class TextLineBufferTest : public ::testing::Test {
protected:
  TextLineBuffer buffer_;
};

TEST_F(TextLineBufferTest, AppendAndGetLine) {
  const char* data = "1234567890 AAPL 150.25 100\n";
  EXPECT_TRUE(buffer_.append(data, strlen(data)));
  EXPECT_TRUE(buffer_.has_data());

  std::string_view line;
  EXPECT_TRUE(buffer_.get_line(line));
  EXPECT_EQ(line, "1234567890 AAPL 150.25 100");
}

TEST_F(TextLineBufferTest, MultipleLines) {
  const char* data = "line1\nline2\nline3\n";
  EXPECT_TRUE(buffer_.append(data, strlen(data)));

  std::string_view line;

  EXPECT_TRUE(buffer_.get_line(line));
  EXPECT_EQ(line, "line1");

  EXPECT_TRUE(buffer_.get_line(line));
  EXPECT_EQ(line, "line2");

  EXPECT_TRUE(buffer_.get_line(line));
  EXPECT_EQ(line, "line3");

  EXPECT_FALSE(buffer_.get_line(line));  // No more lines
}

TEST_F(TextLineBufferTest, PartialLine) {
  // Append partial line
  const char* part1 = "1234567890 AAPL ";
  EXPECT_TRUE(buffer_.append(part1, strlen(part1)));

  std::string_view line;
  EXPECT_FALSE(buffer_.get_line(line));  // No complete line yet

  // Append rest
  const char* part2 = "150.25 100\n";
  EXPECT_TRUE(buffer_.append(part2, strlen(part2)));

  EXPECT_TRUE(buffer_.get_line(line));
  EXPECT_EQ(line, "1234567890 AAPL 150.25 100");
}

TEST_F(TextLineBufferTest, HandleCRLF) {
  const char* data = "line1\r\nline2\r\n";
  EXPECT_TRUE(buffer_.append(data, strlen(data)));

  std::string_view line;

  EXPECT_TRUE(buffer_.get_line(line));
  EXPECT_EQ(line, "line1");  // Should strip \r

  EXPECT_TRUE(buffer_.get_line(line));
  EXPECT_EQ(line, "line2");
}

TEST_F(TextLineBufferTest, Reset) {
  const char* data = "some data\n";
  buffer_.append(data, strlen(data));
  EXPECT_TRUE(buffer_.has_data());

  buffer_.reset();
  EXPECT_FALSE(buffer_.has_data());
}

TEST_F(TextLineBufferTest, AvailableSpace) {
  EXPECT_EQ(buffer_.available_space(), TextLineBuffer::BUFFER_SIZE);

  const char* data = "test data";
  buffer_.append(data, strlen(data));
  EXPECT_EQ(buffer_.available_space(), TextLineBuffer::BUFFER_SIZE - strlen(data));
}

// Performance test
TEST_F(TextProtocolTest, ParsingThroughput) {
  const std::string tick_line = "1234567890123 AAPL 150.25 100";
  constexpr size_t NUM_PARSES = 100000;

  auto start = std::chrono::high_resolution_clock::now();

  size_t success_count = 0;
  for (size_t i = 0; i < NUM_PARSES; ++i) {
    auto tick = parse_text_tick(tick_line);
    if (tick.has_value()) {
      success_count++;
    }
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

  EXPECT_EQ(success_count, NUM_PARSES);

  double parses_per_sec = (static_cast<double>(NUM_PARSES) / duration_us) * 1000000.0;
  std::cout << "  Parsing throughput: " << static_cast<size_t>(parses_per_sec) << " parses/sec" << std::endl;

  // Expect at least 1M parses/sec
  EXPECT_GT(parses_per_sec, 1000000) << "Parsing throughput below 1M/sec";
}
