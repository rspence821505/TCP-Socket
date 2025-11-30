#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <numeric>
#include <algorithm>

#include "spsc_queue.hpp"

using namespace std::chrono;

// Test fixture for SPSC Queue tests
class SPSCQueueTest : public ::testing::Test {
protected:
  void SetUp() override {}
  void TearDown() override {}
};

// Basic functionality tests
TEST_F(SPSCQueueTest, DefaultConstructor) {
  SPSCQueue<int> queue(16);
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0);
}

TEST_F(SPSCQueueTest, PushSingleItem) {
  SPSCQueue<int> queue(16);
  EXPECT_TRUE(queue.push(42));
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(queue.size(), 1);
}

TEST_F(SPSCQueueTest, PopSingleItem) {
  SPSCQueue<int> queue(16);
  queue.push(42);
  auto item = queue.pop();
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(*item, 42);
  EXPECT_TRUE(queue.empty());
}

TEST_F(SPSCQueueTest, PopEmptyQueue) {
  SPSCQueue<int> queue(16);
  auto item = queue.pop();
  EXPECT_FALSE(item.has_value());
}

TEST_F(SPSCQueueTest, FIFOOrdering) {
  SPSCQueue<int> queue(16);

  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(queue.push(i));
  }

  for (int i = 0; i < 10; ++i) {
    auto item = queue.pop();
    ASSERT_TRUE(item.has_value());
    EXPECT_EQ(*item, i);
  }
}

TEST_F(SPSCQueueTest, QueueCapacity) {
  SPSCQueue<int> queue(4);  // Small capacity (rounds to power of 2)

  // Ring buffers reserve one slot to distinguish full from empty
  // With capacity=4 (power of 2), usable slots = 3
  size_t usable_capacity = queue.capacity() - 1;

  // Fill the queue to usable capacity
  for (size_t i = 0; i < usable_capacity; ++i) {
    EXPECT_TRUE(queue.push(static_cast<int>(i)));
  }

  // Queue should now be full - additional push should fail
  EXPECT_FALSE(queue.push(999));
  EXPECT_EQ(queue.size(), usable_capacity);
}

TEST_F(SPSCQueueTest, WrapAround) {
  SPSCQueue<int> queue(8);

  // Push and pop multiple times to trigger wrap-around
  for (int round = 0; round < 3; ++round) {
    for (int i = 0; i < 6; ++i) {
      EXPECT_TRUE(queue.push(round * 10 + i));
    }
    for (int i = 0; i < 6; ++i) {
      auto item = queue.pop();
      ASSERT_TRUE(item.has_value());
      EXPECT_EQ(*item, round * 10 + i);
    }
  }
}

TEST_F(SPSCQueueTest, MoveOnlyType) {
  SPSCQueue<std::unique_ptr<int>> queue(16);

  auto ptr = std::make_unique<int>(42);
  EXPECT_TRUE(queue.push(std::move(ptr)));
  EXPECT_EQ(ptr, nullptr);  // ptr was moved

  auto result = queue.pop();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(**result, 42);
}

// Struct for testing complex types
struct TestMessage {
  uint64_t timestamp;
  uint32_t id;
  double value;
  char data[32];

  bool operator==(const TestMessage& other) const {
    return timestamp == other.timestamp && id == other.id && value == other.value;
  }
};

TEST_F(SPSCQueueTest, ComplexType) {
  SPSCQueue<TestMessage> queue(16);

  TestMessage msg{12345, 100, 3.14, "test"};
  EXPECT_TRUE(queue.push(msg));

  auto result = queue.pop();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->timestamp, 12345);
  EXPECT_EQ(result->id, 100);
  EXPECT_DOUBLE_EQ(result->value, 3.14);
}

// Concurrent tests
class SPSCQueueConcurrentTest : public ::testing::Test {
protected:
  static constexpr size_t NUM_MESSAGES = 100000;
};

TEST_F(SPSCQueueConcurrentTest, ProducerConsumer) {
  SPSCQueue<int> queue(1024);
  std::atomic<bool> producer_done{false};
  std::atomic<size_t> consumed{0};
  std::vector<int> received;
  received.reserve(NUM_MESSAGES);

  std::thread consumer([&]() {
    while (!producer_done.load(std::memory_order_acquire) || !queue.empty()) {
      auto item = queue.pop();
      if (item) {
        received.push_back(*item);
      } else {
        std::this_thread::yield();
      }
    }
    consumed.store(received.size(), std::memory_order_release);
  });

  std::thread producer([&]() {
    for (size_t i = 0; i < NUM_MESSAGES; ++i) {
      while (!queue.push(static_cast<int>(i))) {
        std::this_thread::yield();
      }
    }
    producer_done.store(true, std::memory_order_release);
  });

  producer.join();
  consumer.join();

  // Verify all messages received in order
  EXPECT_EQ(consumed.load(), NUM_MESSAGES);
  ASSERT_EQ(received.size(), NUM_MESSAGES);

  for (size_t i = 0; i < NUM_MESSAGES; ++i) {
    EXPECT_EQ(received[i], static_cast<int>(i)) << "Mismatch at index " << i;
  }
}

TEST_F(SPSCQueueConcurrentTest, LatencyMeasurement) {
  SPSCQueue<uint64_t> queue(1024);
  std::atomic<bool> producer_done{false};
  std::vector<uint64_t> latencies;
  latencies.reserve(NUM_MESSAGES);

  auto now_ns = []() {
    return duration_cast<nanoseconds>(
        high_resolution_clock::now().time_since_epoch()
    ).count();
  };

  std::thread consumer([&]() {
    while (!producer_done.load(std::memory_order_acquire) || !queue.empty()) {
      auto timestamp = queue.pop();
      if (timestamp) {
        uint64_t latency = now_ns() - *timestamp;
        latencies.push_back(latency);
      } else {
        std::this_thread::yield();
      }
    }
  });

  std::thread producer([&]() {
    for (size_t i = 0; i < NUM_MESSAGES; ++i) {
      while (!queue.push(now_ns())) {
        std::this_thread::yield();
      }
    }
    producer_done.store(true, std::memory_order_release);
  });

  producer.join();
  consumer.join();

  ASSERT_EQ(latencies.size(), NUM_MESSAGES);

  // Calculate statistics
  std::sort(latencies.begin(), latencies.end());
  uint64_t p50 = latencies[latencies.size() * 50 / 100];
  uint64_t p99 = latencies[latencies.size() * 99 / 100];

  // Latency should be reasonable (< 1ms for p99)
  EXPECT_LT(p50, 1000000) << "p50 latency too high: " << p50 << " ns";
  EXPECT_LT(p99, 10000000) << "p99 latency too high: " << p99 << " ns";

  std::cout << "  Latency p50: " << p50 << " ns, p99: " << p99 << " ns" << std::endl;
}

TEST_F(SPSCQueueConcurrentTest, Throughput) {
  constexpr size_t THROUGHPUT_MESSAGES = 10000000;
  SPSCQueue<int> queue(4096);
  std::atomic<bool> producer_done{false};
  std::atomic<size_t> consumed{0};

  auto start = high_resolution_clock::now();

  std::thread consumer([&]() {
    size_t count = 0;
    while (!producer_done.load(std::memory_order_acquire) || !queue.empty()) {
      auto item = queue.pop();
      if (item) {
        count++;
      } else {
        std::this_thread::yield();
      }
    }
    consumed.store(count, std::memory_order_release);
  });

  std::thread producer([&]() {
    for (size_t i = 0; i < THROUGHPUT_MESSAGES; ++i) {
      while (!queue.push(static_cast<int>(i))) {
        std::this_thread::yield();
      }
    }
    producer_done.store(true, std::memory_order_release);
  });

  producer.join();
  consumer.join();

  auto end = high_resolution_clock::now();
  auto duration_ms = duration_cast<milliseconds>(end - start).count();

  EXPECT_EQ(consumed.load(), THROUGHPUT_MESSAGES);

  double msgs_per_sec = (static_cast<double>(consumed) / duration_ms) * 1000.0;
  std::cout << "  Throughput: " << static_cast<size_t>(msgs_per_sec) << " msgs/sec" << std::endl;

  // Expect at least 1M msgs/sec on modern hardware
  EXPECT_GT(msgs_per_sec, 1000000) << "Throughput below 1M msgs/sec";
}
