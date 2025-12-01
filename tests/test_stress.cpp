#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <vector>
#include <random>
#include <chrono>

#include "common.hpp"
#include "order_book.hpp"
#include "spsc_queue.hpp"
#include "connection_manager.hpp"
#include "binary_protocol.hpp"

// =============================================================================
// Order Book High Load Tests
// =============================================================================

class OrderBookStressTest : public ::testing::Test {
protected:
  OrderBook book_;

  void SetUp() override {
    book_.clear();
  }
};

TEST_F(OrderBookStressTest, HighVolumeUpdates) {
  // Test order book under high volume of updates
  constexpr size_t NUM_UPDATES = 500000;

  uint64_t start = now_us();

  for (size_t i = 0; i < NUM_UPDATES; ++i) {
    float price = 100.0f + (i % 1000) * 0.01f;  // 1000 price levels
    int64_t qty = (i % 3 == 0) ? 0 : 1000 + (i % 100);  // 1/3 deletions
    uint8_t side = i % 2;
    book_.apply_update(side, price, qty);
  }

  uint64_t end = now_us();
  uint64_t duration_us = end - start;

  double updates_per_sec = (static_cast<double>(NUM_UPDATES) / duration_us) * 1000000.0;
  std::cout << "  High volume update throughput: " << static_cast<size_t>(updates_per_sec) << " updates/sec" << std::endl;

  // Expect at least 1M updates/sec
  EXPECT_GT(updates_per_sec, 1000000) << "High volume throughput below 1M/sec";

  // Verify book is still valid
  EXPECT_GT(book_.bid_depth() + book_.ask_depth(), 0);
}

TEST_F(OrderBookStressTest, ManyPriceLevels) {
  // Test order book with many price levels (stress memory/map operations)
  constexpr size_t NUM_LEVELS = 10000;

  uint64_t start = now_us();

  // Add many bid levels
  for (size_t i = 0; i < NUM_LEVELS; ++i) {
    float price = 100.0f - i * 0.001f;  // Very fine-grained prices
    book_.apply_update(0, price, 1000 + i);
  }

  // Add many ask levels
  for (size_t i = 0; i < NUM_LEVELS; ++i) {
    float price = 100.01f + i * 0.001f;
    book_.apply_update(1, price, 800 + i);
  }

  uint64_t end = now_us();
  uint64_t duration_us = end - start;

  EXPECT_EQ(book_.bid_depth(), NUM_LEVELS);
  EXPECT_EQ(book_.ask_depth(), NUM_LEVELS);

  // Verify best bid/ask still work correctly
  float bid_price, ask_price;
  uint64_t bid_qty, ask_qty;

  EXPECT_TRUE(book_.get_best_bid(bid_price, bid_qty));
  EXPECT_TRUE(book_.get_best_ask(ask_price, ask_qty));

  EXPECT_FLOAT_EQ(bid_price, 100.0f);
  EXPECT_FLOAT_EQ(ask_price, 100.01f);

  std::cout << "  Many levels test: " << NUM_LEVELS << " levels each side in "
            << duration_us << " us" << std::endl;
}

TEST_F(OrderBookStressTest, RapidPriceChanges) {
  // Simulate rapid price changes (typical during high volatility)
  constexpr size_t NUM_CHANGES = 100000;

  // Initialize with some levels
  for (int i = 0; i < 10; ++i) {
    book_.apply_update(0, 100.0f - i * 0.01f, 1000);
    book_.apply_update(1, 100.01f + i * 0.01f, 1000);
  }

  std::mt19937 rng(42);
  std::uniform_real_distribution<float> price_dist(99.0f, 101.0f);
  std::uniform_int_distribution<int64_t> qty_dist(0, 10000);
  std::uniform_int_distribution<int> side_dist(0, 1);

  uint64_t start = now_us();

  for (size_t i = 0; i < NUM_CHANGES; ++i) {
    float price = price_dist(rng);
    int64_t qty = qty_dist(rng);
    uint8_t side = side_dist(rng);
    book_.apply_update(side, price, qty);
  }

  uint64_t end = now_us();
  uint64_t duration_us = end - start;

  double changes_per_sec = (static_cast<double>(NUM_CHANGES) / duration_us) * 1000000.0;
  std::cout << "  Rapid price change throughput: " << static_cast<size_t>(changes_per_sec) << " changes/sec" << std::endl;

  // Book should still be valid
  EXPECT_GE(book_.bid_depth(), 0);
  EXPECT_GE(book_.ask_depth(), 0);
}

TEST_F(OrderBookStressTest, LargeSnapshotLoad) {
  // Test loading large snapshots
  constexpr size_t NUM_LEVELS = 500;
  constexpr size_t NUM_SNAPSHOTS = 100;

  std::vector<OrderBookLevel> bids, asks;
  bids.reserve(NUM_LEVELS);
  asks.reserve(NUM_LEVELS);

  for (size_t i = 0; i < NUM_LEVELS; ++i) {
    bids.push_back({100.0f - i * 0.01f, static_cast<uint64_t>(1000 + i)});
    asks.push_back({100.01f + i * 0.01f, static_cast<uint64_t>(800 + i)});
  }

  uint64_t start = now_us();

  for (size_t i = 0; i < NUM_SNAPSHOTS; ++i) {
    book_.load_snapshot(bids, asks);
  }

  uint64_t end = now_us();
  uint64_t duration_us = end - start;

  double snapshots_per_sec = (static_cast<double>(NUM_SNAPSHOTS) / duration_us) * 1000000.0;
  std::cout << "  Large snapshot load: " << static_cast<size_t>(snapshots_per_sec)
            << " snapshots/sec (" << NUM_LEVELS << " levels each)" << std::endl;

  EXPECT_EQ(book_.bid_depth(), NUM_LEVELS);
  EXPECT_EQ(book_.ask_depth(), NUM_LEVELS);
}

TEST_F(OrderBookStressTest, ClearAndReloadStress) {
  // Test rapid clear/reload cycles
  constexpr size_t NUM_CYCLES = 1000;

  std::vector<OrderBookLevel> bids = {{100.0f, 1000}, {99.99f, 2000}};
  std::vector<OrderBookLevel> asks = {{100.01f, 800}, {100.02f, 1500}};

  uint64_t start = now_us();

  for (size_t i = 0; i < NUM_CYCLES; ++i) {
    book_.load_snapshot(bids, asks);
    book_.clear();
  }

  uint64_t end = now_us();
  uint64_t duration_us = end - start;

  std::cout << "  Clear/reload cycles: " << NUM_CYCLES << " in " << duration_us << " us" << std::endl;

  EXPECT_TRUE(book_.empty());
}

// =============================================================================
// SPSC Queue Backpressure Tests
// =============================================================================

class QueueBackpressureTest : public ::testing::Test {
protected:
  void SetUp() override {}
};

TEST_F(QueueBackpressureTest, QueueFullReturnsFailure) {
  // Small queue to easily fill
  SPSCQueue<int> queue(4);  // Will round up to 4

  // Fill the queue (capacity - 1 elements due to ring buffer design)
  int successful_pushes = 0;
  for (int i = 0; i < 10; ++i) {
    if (queue.push(i)) {
      successful_pushes++;
    } else {
      break;
    }
  }

  // Queue should accept capacity-1 elements
  EXPECT_GE(successful_pushes, 1);
  EXPECT_LT(successful_pushes, 10);

  // Additional pushes should fail
  EXPECT_FALSE(queue.push(999)) << "Push to full queue should fail";
}

TEST_F(QueueBackpressureTest, BackpressureRecovery) {
  SPSCQueue<int> queue(8);

  // Fill the queue
  int count = 0;
  while (queue.push(count)) {
    count++;
    if (count > 100) break;  // Safety limit
  }

  size_t filled_count = queue.size();
  EXPECT_GT(filled_count, 0);

  // Now consumer drains some items
  for (int i = 0; i < 3; ++i) {
    auto item = queue.pop();
    EXPECT_TRUE(item.has_value());
  }

  // Producer should be able to push again
  EXPECT_TRUE(queue.push(1000)) << "Push after drain should succeed";
  EXPECT_TRUE(queue.push(1001)) << "Push after drain should succeed";
}

TEST_F(QueueBackpressureTest, ProducerConsumerRaceOnFullQueue) {
  // Test concurrent producer trying to push and consumer draining
  SPSCQueue<uint64_t> queue(64);

  std::atomic<bool> producer_done{false};
  std::atomic<uint64_t> pushed_count{0};
  std::atomic<uint64_t> popped_count{0};
  std::atomic<uint64_t> backpressure_events{0};

  // Producer: tries to push as fast as possible
  std::thread producer([&]() {
    for (uint64_t i = 0; i < 100000; ++i) {
      while (!queue.push(i)) {
        backpressure_events++;
        std::this_thread::yield();  // Wait for space
      }
      pushed_count++;
    }
    producer_done = true;
  });

  // Consumer: pops with some delay to create backpressure
  std::thread consumer([&]() {
    while (!producer_done || !queue.empty()) {
      auto item = queue.pop();
      if (item.has_value()) {
        popped_count++;
      } else {
        std::this_thread::yield();
      }
    }
  });

  producer.join();
  consumer.join();

  std::cout << "  Backpressure events: " << backpressure_events << std::endl;
  std::cout << "  Pushed: " << pushed_count << ", Popped: " << popped_count << std::endl;

  EXPECT_EQ(pushed_count, 100000);
  EXPECT_EQ(popped_count, 100000);
  EXPECT_GT(backpressure_events, 0) << "Should have experienced some backpressure";
}

TEST_F(QueueBackpressureTest, BackpressureLatencyMeasurement) {
  // Measure latency when queue is nearly full vs empty
  SPSCQueue<uint64_t> queue(1024);

  // Measure push latency on empty queue
  uint64_t start_empty = now_ns();
  for (int i = 0; i < 100; ++i) {
    queue.push(i);
    queue.pop();
  }
  uint64_t end_empty = now_ns();
  uint64_t empty_latency = (end_empty - start_empty) / 100;

  // Fill queue to 90%
  size_t target_fill = queue.capacity() * 9 / 10;
  for (size_t i = 0; i < target_fill; ++i) {
    queue.push(i);
  }

  // Measure push latency on nearly full queue
  uint64_t start_full = now_ns();
  for (int i = 0; i < 100; ++i) {
    queue.push(i);
    queue.pop();
  }
  uint64_t end_full = now_ns();
  uint64_t full_latency = (end_full - start_full) / 100;

  std::cout << "  Empty queue push/pop latency: " << empty_latency << " ns" << std::endl;
  std::cout << "  Near-full queue push/pop latency: " << full_latency << " ns" << std::endl;

  // Both should be fast (lock-free)
  EXPECT_LT(empty_latency, 10000) << "Empty queue latency too high";
  EXPECT_LT(full_latency, 10000) << "Near-full queue latency too high";
}

TEST_F(QueueBackpressureTest, QueueEmptyPopReturnsNullopt) {
  SPSCQueue<int> queue(16);

  // Pop from empty queue
  auto result = queue.pop();
  EXPECT_FALSE(result.has_value()) << "Pop from empty queue should return nullopt";

  // Push and pop all
  queue.push(1);
  queue.push(2);
  queue.pop();
  queue.pop();

  // Pop from now-empty queue
  result = queue.pop();
  EXPECT_FALSE(result.has_value()) << "Pop from drained queue should return nullopt";
}

// =============================================================================
// Connection Manager Failure Mode Tests
// =============================================================================

class ConnectionFailureTest : public ::testing::Test {
protected:
  void SetUp() override {}
};

TEST_F(ConnectionFailureTest, ConnectionToInvalidPort) {
  // Test connection to a port that's not listening
  ConnectionManager cm("127.0.0.1", 59999);  // Unlikely to be in use

  EXPECT_FALSE(cm.connect()) << "Connection to invalid port should fail";
  EXPECT_EQ(cm.state(), ConnectionManager::State::DISCONNECTED);
}

TEST_F(ConnectionFailureTest, ConnectionToInvalidHost) {
  // Test connection to invalid IP
  ConnectionManager cm("192.0.2.1", 9999);  // TEST-NET-1, should not route

  EXPECT_FALSE(cm.connect()) << "Connection to invalid host should fail";
  EXPECT_EQ(cm.state(), ConnectionManager::State::DISCONNECTED);
}

TEST_F(ConnectionFailureTest, DisconnectIdempotent) {
  ConnectionManager cm("127.0.0.1", 9999);

  // Multiple disconnects should be safe
  cm.disconnect();
  cm.disconnect();
  cm.disconnect();

  EXPECT_EQ(cm.state(), ConnectionManager::State::DISCONNECTED);
}

TEST_F(ConnectionFailureTest, StateTransitions) {
  ConnectionManager cm("127.0.0.1", 59999);

  // Initial state
  EXPECT_EQ(cm.state(), ConnectionManager::State::DISCONNECTED);
  EXPECT_FALSE(cm.is_connected());

  // Failed connection should return to DISCONNECTED
  cm.connect();
  EXPECT_EQ(cm.state(), ConnectionManager::State::DISCONNECTED);

  // State name function should work
  EXPECT_STREQ(cm.state_name(), "DISCONNECTED");
}

TEST_F(ConnectionFailureTest, HeartbeatTimeoutDetection) {
  ConnectionManager cm("127.0.0.1", 9999, std::chrono::seconds(1));

  // Initially no timeout
  cm.update_last_message_time();
  EXPECT_FALSE(cm.is_heartbeat_timeout());

  // Wait for timeout
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));

  EXPECT_TRUE(cm.is_heartbeat_timeout()) << "Should detect heartbeat timeout after 1s";
  EXPECT_GT(cm.seconds_since_last_message(), 1.0);
}

TEST_F(ConnectionFailureTest, ReconnectAttemptsTracking) {
  ConnectionManager cm("127.0.0.1", 59999, std::chrono::seconds(1), std::chrono::seconds(1));

  EXPECT_EQ(cm.reconnect_attempts(), 0);

  // First reconnect attempt
  cm.reconnect();  // Will fail but should increment counter
  EXPECT_EQ(cm.reconnect_attempts(), 1);

  // Second reconnect attempt
  cm.reconnect();
  EXPECT_EQ(cm.reconnect_attempts(), 2);
}

TEST_F(ConnectionFailureTest, SnapshotStateTransitions) {
  ConnectionManager cm("127.0.0.1", 9999);

  // Can't transition without being connected
  cm.transition_to_snapshot_request();
  EXPECT_NE(cm.state(), ConnectionManager::State::SNAPSHOT_REQUEST);

  // Similarly for other transitions
  cm.transition_to_snapshot_replay();
  EXPECT_NE(cm.state(), ConnectionManager::State::SNAPSHOT_REPLAY);

  cm.transition_to_incremental();
  EXPECT_NE(cm.state(), ConnectionManager::State::INCREMENTAL);
}

TEST_F(ConnectionFailureTest, NeedsSnapshotRequest) {
  ConnectionManager cm("127.0.0.1", 9999);

  // Not connected, shouldn't need snapshot
  EXPECT_FALSE(cm.needs_snapshot_request());

  // mark_snapshot_requested should be safe to call
  cm.mark_snapshot_requested();
  EXPECT_FALSE(cm.needs_snapshot_request());
}

// =============================================================================
// Concurrent Order Book + Queue Integration Tests
// =============================================================================

class IntegrationStressTest : public ::testing::Test {
protected:
  void SetUp() override {}
};

TEST_F(IntegrationStressTest, ProducerConsumerWithOrderBook) {
  // Simulate realistic feed handler scenario:
  // - Producer parses messages and pushes to queue
  // - Consumer pops and updates order book

  struct TickData {
    uint8_t side;
    float price;
    int64_t qty;
  };

  SPSCQueue<TickData> queue(4096);
  OrderBook book;

  std::atomic<bool> producer_done{false};
  std::atomic<uint64_t> updates_processed{0};

  constexpr size_t NUM_UPDATES = 100000;

  // Producer thread
  std::thread producer([&]() {
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> price_dist(99.0f, 101.0f);
    std::uniform_int_distribution<int64_t> qty_dist(0, 5000);
    std::uniform_int_distribution<int> side_dist(0, 1);

    for (size_t i = 0; i < NUM_UPDATES; ++i) {
      TickData tick{
        static_cast<uint8_t>(side_dist(rng)),
        price_dist(rng),
        qty_dist(rng)
      };

      while (!queue.push(tick)) {
        std::this_thread::yield();
      }
    }
    producer_done = true;
  });

  // Consumer thread
  std::thread consumer([&]() {
    while (!producer_done || !queue.empty()) {
      auto tick = queue.pop();
      if (tick.has_value()) {
        book.apply_update(tick->side, tick->price, tick->qty);
        updates_processed++;
      } else {
        std::this_thread::yield();
      }
    }
  });

  uint64_t start = now_us();

  producer.join();
  consumer.join();

  uint64_t end = now_us();
  uint64_t duration_us = end - start;

  double throughput = (static_cast<double>(NUM_UPDATES) / duration_us) * 1000000.0;

  std::cout << "  End-to-end throughput: " << static_cast<size_t>(throughput) << " updates/sec" << std::endl;
  std::cout << "  Updates processed: " << updates_processed << std::endl;

  EXPECT_EQ(updates_processed, NUM_UPDATES);
  EXPECT_GT(throughput, 100000) << "End-to-end throughput below 100k/sec";
}

TEST_F(IntegrationStressTest, MultipleSnapshotsWithUpdates) {
  // Test interleaved snapshots and incremental updates
  OrderBook book;

  std::vector<OrderBookLevel> initial_bids = {
    {100.50f, 1000}, {100.25f, 2000}, {100.00f, 1500}
  };
  std::vector<OrderBookLevel> initial_asks = {
    {100.75f, 800}, {101.00f, 1200}
  };

  constexpr size_t NUM_CYCLES = 100;
  constexpr size_t UPDATES_PER_CYCLE = 1000;

  uint64_t start = now_ms();

  for (size_t cycle = 0; cycle < NUM_CYCLES; ++cycle) {
    // Load snapshot
    book.load_snapshot(initial_bids, initial_asks);

    // Apply updates
    for (size_t i = 0; i < UPDATES_PER_CYCLE; ++i) {
      float price = 100.0f + (i % 100) * 0.01f;
      int64_t qty = (i % 5 == 0) ? 0 : 500 + i % 1000;
      uint8_t side = i % 2;
      book.apply_update(side, price, qty);
    }

    // Verify book state
    EXPECT_GT(book.bid_depth() + book.ask_depth(), 0);
  }

  uint64_t end = now_ms();
  uint64_t duration_ms = end - start;

  std::cout << "  Snapshot+update cycles: " << NUM_CYCLES << " in " << duration_ms << " ms" << std::endl;

  // Should complete in reasonable time
  EXPECT_LT(duration_ms, 5000) << "Snapshot+update cycles took too long";
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
