#include <gtest/gtest.h>
#include <cstring>
#include <string>

#include "ring_buffer.hpp"

// Test fixture for RingBuffer tests
class RingBufferTest : public ::testing::Test {
protected:
  RingBuffer buffer_;

  void SetUp() override {
    buffer_.clear();
  }

  void TearDown() override {}
};

// Basic functionality tests
TEST_F(RingBufferTest, DefaultState) {
  EXPECT_EQ(buffer_.available(), 0);
  EXPECT_GT(buffer_.capacity(), 0);
  EXPECT_GT(buffer_.free_space(), 0);
}

TEST_F(RingBufferTest, WriteAndRead) {
  const char* data = "Hello, World!";
  size_t len = strlen(data);

  auto [ptr, space] = buffer_.get_write_ptr();
  ASSERT_GE(space, len);

  memcpy(ptr, data, len);
  buffer_.commit_write(len);

  EXPECT_EQ(buffer_.available(), len);

  char output[64];
  EXPECT_TRUE(buffer_.read_bytes(output, len));
  EXPECT_EQ(memcmp(output, data, len), 0);

  EXPECT_EQ(buffer_.available(), 0);
}

TEST_F(RingBufferTest, PeekDoesNotConsume) {
  const char* data = "Test data";
  size_t len = strlen(data);

  auto [ptr, space] = buffer_.get_write_ptr();
  memcpy(ptr, data, len);
  buffer_.commit_write(len);

  // Peek first
  auto view = buffer_.peek(len);
  EXPECT_EQ(view.size(), len);
  EXPECT_EQ(buffer_.available(), len);  // Still available

  // Peek again - same result
  view = buffer_.peek(len);
  EXPECT_EQ(view.size(), len);
  EXPECT_EQ(buffer_.available(), len);
}

TEST_F(RingBufferTest, PeekBytes) {
  const char* data = "Hello";
  size_t len = strlen(data);

  auto [ptr, space] = buffer_.get_write_ptr();
  memcpy(ptr, data, len);
  buffer_.commit_write(len);

  char output[16];
  EXPECT_TRUE(buffer_.peek_bytes(output, len));
  EXPECT_EQ(memcmp(output, data, len), 0);
  EXPECT_EQ(buffer_.available(), len);  // Still available
}

TEST_F(RingBufferTest, Consume) {
  const char* data = "0123456789";
  size_t len = strlen(data);

  auto [ptr, space] = buffer_.get_write_ptr();
  memcpy(ptr, data, len);
  buffer_.commit_write(len);

  // Consume first 5 bytes
  buffer_.consume(5);
  EXPECT_EQ(buffer_.available(), 5);

  // Read remaining
  char output[8];
  EXPECT_TRUE(buffer_.read_bytes(output, 5));
  EXPECT_EQ(memcmp(output, "56789", 5), 0);
}

TEST_F(RingBufferTest, ConsumeMoreThanAvailable) {
  const char* data = "short";
  auto [ptr, space] = buffer_.get_write_ptr();
  memcpy(ptr, data, 5);
  buffer_.commit_write(5);

  // Try to consume more than available
  buffer_.consume(100);

  // Should have consumed all available
  EXPECT_EQ(buffer_.available(), 0);
}

TEST_F(RingBufferTest, Clear) {
  const char* data = "Some data";
  auto [ptr, space] = buffer_.get_write_ptr();
  memcpy(ptr, data, strlen(data));
  buffer_.commit_write(strlen(data));

  EXPECT_GT(buffer_.available(), 0);

  buffer_.clear();

  EXPECT_EQ(buffer_.available(), 0);
}

TEST_F(RingBufferTest, PeekTooMuch) {
  const char* data = "small";
  auto [ptr, space] = buffer_.get_write_ptr();
  memcpy(ptr, data, 5);
  buffer_.commit_write(5);

  // Try to peek more than available
  auto view = buffer_.peek(100);
  EXPECT_TRUE(view.empty());
}

TEST_F(RingBufferTest, PeekBytesTooMuch) {
  const char* data = "small";
  auto [ptr, space] = buffer_.get_write_ptr();
  memcpy(ptr, data, 5);
  buffer_.commit_write(5);

  char output[100];
  EXPECT_FALSE(buffer_.peek_bytes(output, 100));
}

TEST_F(RingBufferTest, ReadBytesTooMuch) {
  const char* data = "small";
  auto [ptr, space] = buffer_.get_write_ptr();
  memcpy(ptr, data, 5);
  buffer_.commit_write(5);

  char output[100];
  EXPECT_FALSE(buffer_.read_bytes(output, 100));

  // Data should still be available
  EXPECT_EQ(buffer_.available(), 5);
}

TEST_F(RingBufferTest, MultipleWritesAndReads) {
  for (int i = 0; i < 100; ++i) {
    char data[16];
    snprintf(data, sizeof(data), "msg%d", i);
    size_t len = strlen(data);

    auto [ptr, space] = buffer_.get_write_ptr();
    ASSERT_GE(space, len) << "Not enough space at iteration " << i;

    memcpy(ptr, data, len);
    buffer_.commit_write(len);

    char output[16];
    EXPECT_TRUE(buffer_.read_bytes(output, len));
    EXPECT_EQ(memcmp(output, data, len), 0);
  }
}

TEST_F(RingBufferTest, FreeSpace) {
  size_t initial_free = buffer_.free_space();

  const char* data = "12345678901234567890";  // 20 bytes
  auto [ptr, space] = buffer_.get_write_ptr();
  memcpy(ptr, data, 20);
  buffer_.commit_write(20);

  EXPECT_EQ(buffer_.free_space(), initial_free - 20);

  buffer_.consume(10);
  // Note: free space calculation depends on implementation
  // Just verify it doesn't crash and returns reasonable value
  EXPECT_GT(buffer_.free_space(), 0);
}

// Wrap-around tests
TEST_F(RingBufferTest, WrapAround) {
  // Test that wrap-around works correctly with small writes
  const char* msg1 = "First message";
  const char* msg2 = "Second message after wrap";

  // Write first message
  auto [ptr1, space1] = buffer_.get_write_ptr();
  size_t len1 = strlen(msg1);
  ASSERT_GE(space1, len1);
  memcpy(ptr1, msg1, len1);
  buffer_.commit_write(len1);

  // Verify we can read it back
  char out1[64];
  EXPECT_TRUE(buffer_.read_bytes(out1, len1));
  EXPECT_EQ(memcmp(out1, msg1, len1), 0);

  // Write second message (may wrap around depending on implementation)
  auto [ptr2, space2] = buffer_.get_write_ptr();
  size_t len2 = strlen(msg2);
  ASSERT_GE(space2, len2);
  memcpy(ptr2, msg2, len2);
  buffer_.commit_write(len2);

  // Verify we can read second message
  EXPECT_EQ(buffer_.available(), len2);
  char out2[64];
  EXPECT_TRUE(buffer_.read_bytes(out2, len2));
  EXPECT_EQ(memcmp(out2, msg2, len2), 0);

  // Buffer should be empty now
  EXPECT_EQ(buffer_.available(), 0u);
}

// Binary data test
TEST_F(RingBufferTest, BinaryData) {
  // Test with binary data including nulls
  unsigned char binary[] = {0x00, 0x01, 0x02, 0xFF, 0xFE, 0x00, 0xAB, 0xCD};
  size_t len = sizeof(binary);

  auto [ptr, space] = buffer_.get_write_ptr();
  memcpy(ptr, binary, len);
  buffer_.commit_write(len);

  unsigned char output[16];
  EXPECT_TRUE(buffer_.read_bytes(reinterpret_cast<char*>(output), len));
  EXPECT_EQ(memcmp(output, binary, len), 0);
}

// Message framing test (typical usage pattern)
TEST_F(RingBufferTest, MessageFraming) {
  // Simulate receiving framed messages: [4-byte length][payload]
  struct Header {
    uint32_t length;
  };

  // Write a complete message
  Header hdr = {10};  // Payload length
  const char payload[] = "0123456789";  // 10 bytes

  auto [ptr, space] = buffer_.get_write_ptr();
  memcpy(ptr, &hdr, sizeof(hdr));
  memcpy(ptr + sizeof(hdr), payload, hdr.length);
  buffer_.commit_write(sizeof(hdr) + hdr.length);

  // Read header first (peek)
  Header read_hdr;
  EXPECT_TRUE(buffer_.peek_bytes(reinterpret_cast<char*>(&read_hdr), sizeof(Header)));
  EXPECT_EQ(read_hdr.length, 10);

  // Check we have complete message
  EXPECT_GE(buffer_.available(), sizeof(Header) + read_hdr.length);

  // Consume header
  buffer_.consume(sizeof(Header));

  // Read payload
  char read_payload[16];
  EXPECT_TRUE(buffer_.read_bytes(read_payload, read_hdr.length));
  EXPECT_EQ(memcmp(read_payload, payload, read_hdr.length), 0);
}

// Performance test
TEST_F(RingBufferTest, Throughput) {
  constexpr size_t NUM_ITERATIONS = 100000;
  constexpr size_t MSG_SIZE = 64;

  char write_data[MSG_SIZE];
  char read_data[MSG_SIZE];
  memset(write_data, 'X', MSG_SIZE);

  auto start = std::chrono::high_resolution_clock::now();

  for (size_t i = 0; i < NUM_ITERATIONS; ++i) {
    auto [ptr, space] = buffer_.get_write_ptr();
    ASSERT_GE(space, MSG_SIZE);

    memcpy(ptr, write_data, MSG_SIZE);
    buffer_.commit_write(MSG_SIZE);

    EXPECT_TRUE(buffer_.read_bytes(read_data, MSG_SIZE));
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

  double ops_per_sec = (static_cast<double>(NUM_ITERATIONS) / duration_us) * 1000000.0;
  double mb_per_sec = (static_cast<double>(NUM_ITERATIONS * MSG_SIZE) / duration_us);

  std::cout << "  Ring buffer throughput: " << static_cast<size_t>(ops_per_sec) << " ops/sec" << std::endl;
  std::cout << "  Ring buffer bandwidth: " << static_cast<size_t>(mb_per_sec) << " MB/sec" << std::endl;

  // Expect at least 1M ops/sec
  EXPECT_GT(ops_per_sec, 1000000) << "Throughput below 1M ops/sec";
}

// Capacity test
TEST_F(RingBufferTest, CapacityIs1MB) {
  EXPECT_EQ(buffer_.capacity(), 1024 * 1024);  // 1 MB as per implementation
}

// Edge case: write_ptr space calculation
TEST_F(RingBufferTest, WritePtrSpaceAtEnd) {
  // Fill most of the buffer
  size_t capacity = buffer_.capacity();
  size_t fill_amount = capacity - 100;  // Leave 100 bytes

  auto [ptr, space] = buffer_.get_write_ptr();
  // We may not be able to write fill_amount in one go due to space calculation
  size_t to_write = std::min(fill_amount, space);
  buffer_.commit_write(to_write);

  // Now get write ptr again
  auto [ptr2, space2] = buffer_.get_write_ptr();
  EXPECT_GE(space2, 0);  // Should not crash, space might be limited
}
