/**
 * Feed Handler SPSC Queue Tests
 *
 * Tests for the lock-free SPSC queue-based feed handler.
 * Covers:
 *   - TimedMessage and FeedLatencyStats
 *   - Reader/Consumer thread communication via SPSC queue
 *   - End-to-end integration with mock server
 */

#include <gtest/gtest.h>
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "binary_protocol.hpp"
#include "common.hpp"
#include "ring_buffer.hpp"
#include "spsc_queue.hpp"

using namespace std::chrono;

// =============================================================================
// Simple serialization for tick messages (length prefix + payload only)
// This matches the format expected by feed_handler_spsc.cpp
// =============================================================================

inline std::string serialize_tick_simple(const BinaryTick& tick) {
  std::string message;
  message.reserve(4 + BinaryTick::PAYLOAD_SIZE);

  // 4-byte length prefix (network byte order)
  uint32_t length_net = htonl(BinaryTick::PAYLOAD_SIZE);
  message.append(reinterpret_cast<const char*>(&length_net), 4);

  // Timestamp (8 bytes, network byte order)
  uint64_t timestamp_net = htonll(tick.timestamp);
  message.append(reinterpret_cast<const char*>(&timestamp_net), 8);

  // Symbol (4 bytes)
  message.append(tick.symbol, 4);

  // Price (4 bytes, network byte order)
  uint32_t price_bits;
  memcpy(&price_bits, &tick.price, 4);
  uint32_t price_net = htonl(price_bits);
  message.append(reinterpret_cast<const char*>(&price_net), 4);

  // Volume (4 bytes, network byte order)
  int32_t volume_net = htonl(tick.volume);
  message.append(reinterpret_cast<const char*>(&volume_net), 4);

  return message;
}

// Deserialize simple tick format
inline BinaryTick deserialize_tick_simple(const char* payload) {
  BinaryTick tick;

  uint64_t timestamp_net;
  memcpy(&timestamp_net, payload, 8);
  tick.timestamp = ntohll(timestamp_net);
  payload += 8;

  memcpy(tick.symbol, payload, 4);
  payload += 4;

  uint32_t price_net;
  memcpy(&price_net, payload, 4);
  uint32_t price_bits = ntohl(price_net);
  memcpy(&tick.price, &price_bits, 4);
  payload += 4;

  int32_t volume_net;
  memcpy(&volume_net, payload, 4);
  tick.volume = ntohl(volume_net);

  return tick;
}

// =============================================================================
// TimedMessage - Message wrapper with timing for latency measurement
// =============================================================================

struct TimedMessage {
  BinaryTick tick;
  uint64_t recv_timestamp_ns;
  uint64_t parse_timestamp_ns;

  TimedMessage() = default;
  TimedMessage(const BinaryTick &t, uint64_t recv_ts, uint64_t parse_ts)
      : tick(t), recv_timestamp_ns(recv_ts), parse_timestamp_ns(parse_ts) {}
};

// =============================================================================
// FeedLatencyStats - Latency breakdown using consolidated LatencyStats
// =============================================================================

struct FeedLatencyStats {
  LatencyStats recv_to_parse;
  LatencyStats parse_to_process;
  LatencyStats total_latency;

  void reserve(size_t n) {
    recv_to_parse.reserve(n);
    parse_to_process.reserve(n);
    total_latency.reserve(n);
  }

  void add_measurement(uint64_t recv_ts, uint64_t parse_ts,
                       uint64_t process_ts) {
    recv_to_parse.add(parse_ts - recv_ts);
    parse_to_process.add(process_ts - parse_ts);
    total_latency.add(process_ts - recv_ts);
  }

  size_t count() const { return total_latency.count(); }
};

// =============================================================================
// Test Fixtures
// =============================================================================

class FeedHandlerTest : public ::testing::Test {
protected:
  void SetUp() override {}
  void TearDown() override {}
};

class FeedHandlerIntegrationTest : public ::testing::Test {
protected:
  int server_fd_ = -1;
  int port_ = 0;
  std::atomic<bool> server_running_{true};

  void SetUp() override {
    // Reset state
    server_running_ = true;

    // Create and bind server socket with ephemeral port
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(server_fd_, 0) << "Failed to create server socket";

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0;  // Let OS assign port

    ASSERT_EQ(bind(server_fd_, (struct sockaddr *)&addr, sizeof(addr)), 0)
        << "Failed to bind server socket";

    // Get assigned port
    socklen_t len = sizeof(addr);
    getsockname(server_fd_, (struct sockaddr *)&addr, &len);
    port_ = ntohs(addr.sin_port);

    ASSERT_EQ(listen(server_fd_, 1), 0) << "Failed to listen on server socket";
  }

  void TearDown() override {
    server_running_ = false;
    if (server_fd_ >= 0) {
      close(server_fd_);
      server_fd_ = -1;
    }
  }

  int get_port() const { return port_; }

  // Start a mock server that sends N messages
  void run_mock_server(size_t num_messages, std::atomic<size_t> &sent_count) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd_, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) {
      return;
    }

    for (size_t i = 0; i < num_messages && server_running_; ++i) {
      BinaryTick tick;
      tick.timestamp = now_ns();
      memcpy(tick.symbol, "TEST", 4);
      tick.price = 100.0f + (i % 100) * 0.01f;
      tick.volume = 1000 + static_cast<int32_t>(i % 1000);

      std::string message = serialize_tick_simple(tick);
      ssize_t bytes_sent = send(client_fd, message.data(), message.length(), 0);
      if (bytes_sent < 0) {
        break;
      }
      sent_count++;
    }

    close(client_fd);
  }
};

// =============================================================================
// TimedMessage Tests
// =============================================================================

TEST_F(FeedHandlerTest, TimedMessageDefaultConstruction) {
  TimedMessage msg;
  // Default constructed, values are uninitialized but should be accessible
  EXPECT_GE(sizeof(msg), sizeof(BinaryTick) + 2 * sizeof(uint64_t));
}

TEST_F(FeedHandlerTest, TimedMessageParameterizedConstruction) {
  BinaryTick tick;
  tick.timestamp = 123456789;
  memcpy(tick.symbol, "AAPL", 4);
  tick.price = 150.50f;
  tick.volume = 1000;

  uint64_t recv_ts = 100;
  uint64_t parse_ts = 200;

  TimedMessage msg(tick, recv_ts, parse_ts);

  EXPECT_EQ(msg.tick.timestamp, 123456789u);
  EXPECT_EQ(msg.tick.price, 150.50f);
  EXPECT_EQ(msg.tick.volume, 1000);
  EXPECT_EQ(msg.recv_timestamp_ns, 100u);
  EXPECT_EQ(msg.parse_timestamp_ns, 200u);
}

// =============================================================================
// FeedLatencyStats Tests
// =============================================================================

TEST_F(FeedHandlerTest, FeedLatencyStatsEmpty) {
  FeedLatencyStats stats;
  EXPECT_EQ(stats.count(), 0u);
}

TEST_F(FeedHandlerTest, FeedLatencyStatsAddMeasurement) {
  FeedLatencyStats stats;
  stats.reserve(100);

  // recv=100, parse=150, process=200
  // recv_to_parse = 50, parse_to_process = 50, total = 100
  stats.add_measurement(100, 150, 200);

  EXPECT_EQ(stats.count(), 1u);
  EXPECT_EQ(stats.recv_to_parse.count(), 1u);
  EXPECT_EQ(stats.parse_to_process.count(), 1u);
  EXPECT_EQ(stats.total_latency.count(), 1u);
}

TEST_F(FeedHandlerTest, FeedLatencyStatsMultipleMeasurements) {
  FeedLatencyStats stats;
  stats.reserve(100);

  // Add multiple measurements
  stats.add_measurement(100, 120, 150);  // 20, 30, 50
  stats.add_measurement(200, 230, 280);  // 30, 50, 80
  stats.add_measurement(300, 310, 340);  // 10, 30, 40

  EXPECT_EQ(stats.count(), 3u);

  // Verify latency calculations are consistent
  EXPECT_EQ(stats.recv_to_parse.count(), 3u);
  EXPECT_EQ(stats.parse_to_process.count(), 3u);
  EXPECT_EQ(stats.total_latency.count(), 3u);
}

// =============================================================================
// SPSC Queue with TimedMessage Tests
// =============================================================================

TEST_F(FeedHandlerTest, SPSCQueueWithTimedMessage) {
  SPSCQueue<TimedMessage> queue(16);

  BinaryTick tick;
  tick.timestamp = now_ns();
  memcpy(tick.symbol, "MSFT", 4);
  tick.price = 300.0f;
  tick.volume = 500;

  TimedMessage msg(tick, 1000, 2000);

  EXPECT_TRUE(queue.push(std::move(msg)));
  EXPECT_EQ(queue.size(), 1u);

  auto popped = queue.pop();
  ASSERT_TRUE(popped.has_value());
  EXPECT_EQ(popped->tick.price, 300.0f);
  EXPECT_EQ(popped->recv_timestamp_ns, 1000u);
}

TEST_F(FeedHandlerTest, SPSCQueueProducerConsumer) {
  constexpr size_t NUM_MESSAGES = 10000;
  SPSCQueue<TimedMessage> queue(1024);
  std::atomic<bool> producer_done{false};
  std::atomic<size_t> consumed_count{0};

  // Producer thread
  std::thread producer([&]() {
    for (size_t i = 0; i < NUM_MESSAGES; ++i) {
      BinaryTick tick;
      tick.timestamp = i;
      memcpy(tick.symbol, "PROD", 4);
      tick.price = static_cast<float>(i);
      tick.volume = static_cast<int32_t>(i);

      TimedMessage msg(tick, now_ns(), now_ns());

      while (!queue.push(std::move(msg))) {
        std::this_thread::yield();
      }
    }
    producer_done = true;
  });

  // Consumer thread
  std::thread consumer([&]() {
    size_t expected_seq = 0;
    while (!producer_done || !queue.empty()) {
      auto msg = queue.pop();
      if (msg) {
        EXPECT_EQ(msg->tick.timestamp, expected_seq);
        expected_seq++;
        consumed_count++;
      } else {
        std::this_thread::yield();
      }
    }
  });

  producer.join();
  consumer.join();

  EXPECT_EQ(consumed_count.load(), NUM_MESSAGES);
}

// =============================================================================
// RingBuffer Tests (used by ReaderThread)
// =============================================================================

TEST_F(FeedHandlerTest, RingBufferBasic) {
  RingBuffer buffer;

  EXPECT_EQ(buffer.available(), 0u);

  auto [write_ptr, space] = buffer.get_write_ptr();
  EXPECT_GT(space, 0u);

  // Write some data
  const char *test_data = "Hello, World!";
  size_t len = strlen(test_data);
  memcpy(write_ptr, test_data, len);
  buffer.commit_write(len);

  EXPECT_EQ(buffer.available(), len);

  // Read it back
  char output[64];
  EXPECT_TRUE(buffer.read_bytes(output, len));
  EXPECT_EQ(memcmp(output, test_data, len), 0);
  EXPECT_EQ(buffer.available(), 0u);
}

TEST_F(FeedHandlerTest, RingBufferBinaryMessage) {
  RingBuffer buffer;

  // Create a binary tick message
  BinaryTick tick;
  tick.timestamp = now_ns();
  memcpy(tick.symbol, "GOOG", 4);
  tick.price = 140.25f;
  tick.volume = 2500;

  std::string message = serialize_tick_simple(tick);

  // Write to ring buffer
  auto [write_ptr, space] = buffer.get_write_ptr();
  ASSERT_GE(space, message.length());
  memcpy(write_ptr, message.data(), message.length());
  buffer.commit_write(message.length());

  EXPECT_EQ(buffer.available(), message.length());

  // Parse length prefix
  char length_bytes[4];
  EXPECT_TRUE(buffer.peek_bytes(length_bytes, 4));

  uint32_t length_net;
  memcpy(&length_net, length_bytes, 4);
  uint32_t length = ntohl(length_net);

  // Verify length matches expected payload size
  EXPECT_EQ(length, TickPayload::PAYLOAD_SIZE);
}

// =============================================================================
// Integration Tests with Mock Server
// =============================================================================

TEST_F(FeedHandlerIntegrationTest, ConnectAndReceiveMessages) {
  constexpr size_t NUM_MESSAGES = 1000;
  std::atomic<size_t> sent_count{0};
  std::atomic<size_t> received_count{0};

  // Start mock server in background thread
  std::thread server_thread([&]() {
    run_mock_server(NUM_MESSAGES, sent_count);
  });

  // Give server time to start accepting
  std::this_thread::sleep_for(milliseconds(50));

  // Connect client
  int client_fd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(client_fd, 0);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

  ASSERT_EQ(connect(client_fd, (struct sockaddr *)&addr, sizeof(addr)), 0);

  // Receive messages - use heap-allocated buffer to avoid stack overflow
  auto buffer = std::make_unique<RingBuffer>();
  auto start_time = steady_clock::now();
  auto timeout = seconds(5);

  while (received_count < NUM_MESSAGES) {
    if (steady_clock::now() - start_time > timeout) {
      break;
    }

    auto [write_ptr, space] = buffer->get_write_ptr();
    if (space > 0) {
      ssize_t bytes_read = recv(client_fd, write_ptr, space, MSG_DONTWAIT);
      if (bytes_read > 0) {
        buffer->commit_write(bytes_read);
      }
    }

    // Parse complete messages
    while (buffer->available() >= 4) {
      char length_bytes[4];
      if (!buffer->peek_bytes(length_bytes, 4)) break;

      uint32_t length_net;
      memcpy(&length_net, length_bytes, 4);
      uint32_t length = ntohl(length_net);

      size_t total_size = 4 + length;
      if (buffer->available() < total_size) break;

      char message_bytes[256];
      if (!buffer->read_bytes(message_bytes, total_size)) break;

      received_count++;
    }
  }

  close(client_fd);
  server_running_ = false;
  server_thread.join();

  EXPECT_EQ(received_count.load(), NUM_MESSAGES);
  EXPECT_EQ(sent_count.load(), NUM_MESSAGES);
}

TEST_F(FeedHandlerIntegrationTest, LatencyMeasurement) {
  constexpr size_t NUM_MESSAGES = 100;
  std::atomic<size_t> sent_count{0};

  // Start mock server
  std::thread server_thread([&]() {
    run_mock_server(NUM_MESSAGES, sent_count);
  });

  std::this_thread::sleep_for(milliseconds(50));

  // Connect client
  int client_fd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(client_fd, 0);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

  ASSERT_EQ(connect(client_fd, (struct sockaddr *)&addr, sizeof(addr)), 0);

  // Set up SPSC queue and latency tracking
  SPSCQueue<TimedMessage> queue(512);
  FeedLatencyStats stats;
  stats.reserve(NUM_MESSAGES);

  std::atomic<bool> reader_done{false};
  std::atomic<size_t> processed_count{0};

  // Reader thread - use heap-allocated buffer to avoid stack overflow
  std::thread reader([&]() {
    auto buffer = std::make_unique<RingBuffer>();
    size_t parsed = 0;
    auto timeout_start = steady_clock::now();
    constexpr auto TIMEOUT = seconds(10);

    while (parsed < NUM_MESSAGES) {
      if (steady_clock::now() - timeout_start > TIMEOUT) {
        break;  // Timeout
      }

      auto [write_ptr, space] = buffer->get_write_ptr();
      if (space > 0) {
        ssize_t bytes_read = recv(client_fd, write_ptr, space, MSG_DONTWAIT);
        if (bytes_read > 0) {
          uint64_t recv_ts = now_ns();
          buffer->commit_write(bytes_read);

          // Parse messages
          while (buffer->available() >= 4) {
            char length_bytes[4];
            if (!buffer->peek_bytes(length_bytes, 4)) break;

            uint32_t length_net;
            memcpy(&length_net, length_bytes, 4);
            uint32_t length = ntohl(length_net);

            size_t total_size = 4 + length;
            if (buffer->available() < total_size) break;

            char message_bytes[256];
            if (!buffer->read_bytes(message_bytes, total_size)) break;

            const char *payload = message_bytes + 4;
            BinaryTick tick = deserialize_tick_simple(payload);
            uint64_t parse_ts = now_ns();

            TimedMessage msg(tick, recv_ts, parse_ts);
            while (!queue.push(std::move(msg))) {
              std::this_thread::yield();
            }
            parsed++;
          }
        } else if (bytes_read == 0) {
          break;  // Connection closed
        }
      }
      std::this_thread::yield();
    }
    reader_done = true;
  });

  // Consumer thread
  std::thread consumer([&]() {
    auto timeout_start = steady_clock::now();
    constexpr auto TIMEOUT = seconds(10);

    while (!reader_done || !queue.empty()) {
      if (steady_clock::now() - timeout_start > TIMEOUT) {
        break;
      }
      auto msg = queue.pop();
      if (msg) {
        uint64_t process_ts = now_ns();
        stats.add_measurement(msg->recv_timestamp_ns, msg->parse_timestamp_ns,
                              process_ts);
        processed_count++;
      } else {
        std::this_thread::yield();
      }
    }
  });

  reader.join();
  consumer.join();
  close(client_fd);
  server_running_ = false;
  server_thread.join();

  EXPECT_EQ(processed_count.load(), NUM_MESSAGES);
  EXPECT_EQ(stats.count(), NUM_MESSAGES);

  // Verify latency stats are reasonable (all measurements should be positive)
  EXPECT_GT(stats.recv_to_parse.count(), 0u);
  EXPECT_GT(stats.parse_to_process.count(), 0u);
  EXPECT_GT(stats.total_latency.count(), 0u);
}

TEST_F(FeedHandlerIntegrationTest, DifferentQueueSizes) {
  // Test with various queue sizes to verify backpressure handling
  std::vector<size_t> queue_sizes = {16, 64, 256, 1024};

  for (size_t queue_size : queue_sizes) {
    constexpr size_t NUM_MESSAGES = 200;
    std::atomic<size_t> sent_count{0};

    // Reset server running flag for this iteration
    server_running_ = true;

    // Recreate server socket for each test
    if (server_fd_ >= 0) {
      close(server_fd_);
    }
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0;
    bind(server_fd_, (struct sockaddr *)&addr, sizeof(addr));

    socklen_t len = sizeof(addr);
    getsockname(server_fd_, (struct sockaddr *)&addr, &len);
    port_ = ntohs(addr.sin_port);
    listen(server_fd_, 1);

    std::thread server_thread([&]() {
      run_mock_server(NUM_MESSAGES, sent_count);
    });

    std::this_thread::sleep_for(milliseconds(50));

    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(port_);
    inet_pton(AF_INET, "127.0.0.1", &client_addr.sin_addr);
    connect(client_fd, (struct sockaddr *)&client_addr, sizeof(client_addr));

    SPSCQueue<TimedMessage> queue(queue_size);
    std::atomic<bool> reader_done{false};
    std::atomic<size_t> processed_count{0};

    std::thread reader([&]() {
      auto buffer = std::make_unique<RingBuffer>();
      size_t parsed = 0;
      auto timeout_start = steady_clock::now();
      constexpr auto TIMEOUT = seconds(5);

      while (parsed < NUM_MESSAGES) {
        if (steady_clock::now() - timeout_start > TIMEOUT) break;

        auto [write_ptr, space] = buffer->get_write_ptr();
        if (space > 0) {
          ssize_t bytes_read = recv(client_fd, write_ptr, space, MSG_DONTWAIT);
          if (bytes_read > 0) {
            uint64_t recv_ts = now_ns();
            buffer->commit_write(bytes_read);

            while (buffer->available() >= 4) {
              char length_bytes[4];
              if (!buffer->peek_bytes(length_bytes, 4)) break;

              uint32_t length_net;
              memcpy(&length_net, length_bytes, 4);
              uint32_t length = ntohl(length_net);

              size_t total_size = 4 + length;
              if (buffer->available() < total_size) break;

              char message_bytes[256];
              if (!buffer->read_bytes(message_bytes, total_size)) break;

              const char *payload = message_bytes + 4;
              BinaryTick tick = deserialize_tick_simple(payload);

              TimedMessage msg(tick, recv_ts, now_ns());
              while (!queue.push(std::move(msg))) {
                std::this_thread::yield();
              }
              parsed++;
            }
          } else if (bytes_read == 0) {
            break;  // Connection closed
          }
        }
        std::this_thread::yield();
      }
      reader_done = true;
    });

    std::thread consumer([&]() {
      auto timeout_start = steady_clock::now();
      constexpr auto TIMEOUT = seconds(5);

      while (!reader_done || !queue.empty()) {
        if (steady_clock::now() - timeout_start > TIMEOUT) break;
        auto msg = queue.pop();
        if (msg) {
          processed_count++;
        } else {
          std::this_thread::yield();
        }
      }
    });

    reader.join();
    consumer.join();
    close(client_fd);
    server_running_ = false;
    server_thread.join();

    EXPECT_EQ(processed_count.load(), NUM_MESSAGES)
        << "Failed with queue_size=" << queue_size;
  }
}

// =============================================================================
// Throughput Test
// =============================================================================

TEST_F(FeedHandlerIntegrationTest, ThroughputMeasurement) {
  constexpr size_t NUM_MESSAGES = 5000;
  std::atomic<size_t> sent_count{0};

  std::thread server_thread([&]() {
    server_running_ = true;

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd_, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) return;

    // Send messages as fast as possible (no delay)
    for (size_t i = 0; i < NUM_MESSAGES && server_running_; ++i) {
      BinaryTick tick;
      tick.timestamp = now_ns();
      memcpy(tick.symbol, "FAST", 4);
      tick.price = 100.0f;
      tick.volume = 1000;

      std::string message = serialize_tick_simple(tick);
      if (send(client_fd, message.data(), message.length(), 0) < 0) break;
      sent_count++;
    }
    close(client_fd);
  });

  std::this_thread::sleep_for(milliseconds(50));

  int client_fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  connect(client_fd, (struct sockaddr *)&addr, sizeof(addr));

  SPSCQueue<TimedMessage> queue(2048);
  std::atomic<bool> reader_done{false};
  std::atomic<size_t> processed_count{0};

  auto start_time = steady_clock::now();

  std::thread reader([&]() {
    auto buffer = std::make_unique<RingBuffer>();
    size_t parsed = 0;
    auto timeout_start = steady_clock::now();
    constexpr auto TIMEOUT = seconds(10);

    while (parsed < NUM_MESSAGES) {
      if (steady_clock::now() - timeout_start > TIMEOUT) break;

      auto [write_ptr, space] = buffer->get_write_ptr();
      if (space > 0) {
        ssize_t bytes_read = recv(client_fd, write_ptr, space, MSG_DONTWAIT);
        if (bytes_read > 0) {
          uint64_t recv_ts = now_ns();
          buffer->commit_write(bytes_read);

          while (buffer->available() >= 4) {
            char length_bytes[4];
            if (!buffer->peek_bytes(length_bytes, 4)) break;

            uint32_t length_net;
            memcpy(&length_net, length_bytes, 4);
            uint32_t length = ntohl(length_net);

            size_t total_size = 4 + length;
            if (buffer->available() < total_size) break;

            char message_bytes[256];
            if (!buffer->read_bytes(message_bytes, total_size)) break;

            const char *payload = message_bytes + 4;
            BinaryTick tick = deserialize_tick_simple(payload);

            TimedMessage msg(tick, recv_ts, now_ns());
            while (!queue.push(std::move(msg))) {
              std::this_thread::yield();
            }
            parsed++;
          }
        } else if (bytes_read == 0) {
          break;  // Connection closed
        }
      }
      std::this_thread::yield();
    }
    reader_done = true;
  });

  std::thread consumer([&]() {
    auto timeout_start = steady_clock::now();
    constexpr auto TIMEOUT = seconds(10);

    while (!reader_done || !queue.empty()) {
      if (steady_clock::now() - timeout_start > TIMEOUT) break;
      auto msg = queue.pop();
      if (msg) {
        processed_count++;
      } else {
        std::this_thread::yield();
      }
    }
  });

  reader.join();
  consumer.join();

  auto end_time = steady_clock::now();
  auto duration_ms = duration_cast<milliseconds>(end_time - start_time).count();
  double seconds = duration_ms / 1000.0;
  double throughput = processed_count.load() / seconds;

  close(client_fd);
  server_running_ = false;
  server_thread.join();

  EXPECT_EQ(processed_count.load(), NUM_MESSAGES);

  std::cout << "  Throughput: " << static_cast<size_t>(throughput)
            << " msgs/sec (" << processed_count.load() << " messages in "
            << duration_ms << "ms)" << std::endl;

  // Expect reasonable throughput (at least 10k msgs/sec for local loopback)
  EXPECT_GT(throughput, 10000.0) << "Throughput unexpectedly low";
}
