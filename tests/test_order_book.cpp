#include <gtest/gtest.h>
#include <vector>

#include "common.hpp"
#include "order_book.hpp"

// Test fixture for OrderBook tests
class OrderBookTest : public ::testing::Test {
protected:
  OrderBook book_;

  void SetUp() override {
    book_.clear();
  }

  void TearDown() override {}
};

// Basic functionality tests
TEST_F(OrderBookTest, DefaultState) {
  EXPECT_TRUE(book_.empty());
  EXPECT_EQ(book_.bid_depth(), 0);
  EXPECT_EQ(book_.ask_depth(), 0);
}

TEST_F(OrderBookTest, LoadSnapshot) {
  std::vector<OrderBookLevel> bids = {
    {100.50f, 1000},
    {100.25f, 2000},
    {100.00f, 1500}
  };

  std::vector<OrderBookLevel> asks = {
    {100.75f, 800},
    {101.00f, 1200}
  };

  book_.load_snapshot(bids, asks);

  EXPECT_FALSE(book_.empty());
  EXPECT_EQ(book_.bid_depth(), 3);
  EXPECT_EQ(book_.ask_depth(), 2);
}

TEST_F(OrderBookTest, LoadSnapshotClearsExisting) {
  // First snapshot
  std::vector<OrderBookLevel> bids1 = {{100.00f, 1000}};
  std::vector<OrderBookLevel> asks1 = {{101.00f, 1000}};
  book_.load_snapshot(bids1, asks1);

  EXPECT_EQ(book_.bid_depth(), 1);

  // Second snapshot should clear first
  std::vector<OrderBookLevel> bids2 = {{200.00f, 500}, {199.00f, 600}};
  std::vector<OrderBookLevel> asks2 = {{201.00f, 700}};
  book_.load_snapshot(bids2, asks2);

  EXPECT_EQ(book_.bid_depth(), 2);
  EXPECT_EQ(book_.ask_depth(), 1);

  float price;
  uint64_t qty;
  book_.get_best_bid(price, qty);
  EXPECT_FLOAT_EQ(price, 200.00f);
}

TEST_F(OrderBookTest, LoadSnapshotIgnoresZeroQuantity) {
  std::vector<OrderBookLevel> bids = {
    {100.50f, 1000},
    {100.25f, 0},  // Should be ignored
    {100.00f, 1500}
  };

  std::vector<OrderBookLevel> asks = {};

  book_.load_snapshot(bids, asks);

  EXPECT_EQ(book_.bid_depth(), 2);  // Only non-zero levels
}

// Best bid/ask tests
TEST_F(OrderBookTest, GetBestBid) {
  std::vector<OrderBookLevel> bids = {
    {100.00f, 1000},
    {100.50f, 500},   // Best bid (highest price)
    {100.25f, 750}
  };

  book_.load_snapshot(bids, {});

  float price;
  uint64_t qty;
  EXPECT_TRUE(book_.get_best_bid(price, qty));
  EXPECT_FLOAT_EQ(price, 100.50f);
  EXPECT_EQ(qty, 500);
}

TEST_F(OrderBookTest, GetBestAsk) {
  std::vector<OrderBookLevel> asks = {
    {101.00f, 1000},
    {100.75f, 500},   // Best ask (lowest price)
    {101.50f, 750}
  };

  book_.load_snapshot({}, asks);

  float price;
  uint64_t qty;
  EXPECT_TRUE(book_.get_best_ask(price, qty));
  EXPECT_FLOAT_EQ(price, 100.75f);
  EXPECT_EQ(qty, 500);
}

TEST_F(OrderBookTest, GetBestBidEmpty) {
  float price;
  uint64_t qty;
  EXPECT_FALSE(book_.get_best_bid(price, qty));
}

TEST_F(OrderBookTest, GetBestAskEmpty) {
  float price;
  uint64_t qty;
  EXPECT_FALSE(book_.get_best_ask(price, qty));
}

// Incremental update tests
TEST_F(OrderBookTest, ApplyUpdateAddBidLevel) {
  book_.apply_update(0, 100.50f, 1000);  // side=0 is bid

  EXPECT_EQ(book_.bid_depth(), 1);

  float price;
  uint64_t qty;
  book_.get_best_bid(price, qty);
  EXPECT_FLOAT_EQ(price, 100.50f);
  EXPECT_EQ(qty, 1000);
}

TEST_F(OrderBookTest, ApplyUpdateAddAskLevel) {
  book_.apply_update(1, 101.25f, 500);  // side=1 is ask

  EXPECT_EQ(book_.ask_depth(), 1);

  float price;
  uint64_t qty;
  book_.get_best_ask(price, qty);
  EXPECT_FLOAT_EQ(price, 101.25f);
  EXPECT_EQ(qty, 500);
}

TEST_F(OrderBookTest, ApplyUpdateModifyLevel) {
  // Add initial level
  book_.apply_update(0, 100.00f, 1000);

  // Update same price level
  book_.apply_update(0, 100.00f, 2000);

  EXPECT_EQ(book_.bid_depth(), 1);

  float price;
  uint64_t qty;
  book_.get_best_bid(price, qty);
  EXPECT_EQ(qty, 2000);
}

TEST_F(OrderBookTest, ApplyUpdateDeleteLevel) {
  // Add levels
  book_.apply_update(0, 100.00f, 1000);
  book_.apply_update(0, 100.50f, 500);
  EXPECT_EQ(book_.bid_depth(), 2);

  // Delete one level
  book_.apply_update(0, 100.00f, 0);  // quantity=0 deletes

  EXPECT_EQ(book_.bid_depth(), 1);

  float price;
  uint64_t qty;
  book_.get_best_bid(price, qty);
  EXPECT_FLOAT_EQ(price, 100.50f);
}

TEST_F(OrderBookTest, ApplyUpdateDeleteNonexistent) {
  // Delete from empty book - should not crash
  book_.apply_update(0, 100.00f, 0);
  EXPECT_TRUE(book_.empty());
}

// Top N levels tests
TEST_F(OrderBookTest, GetTopBids) {
  std::vector<OrderBookLevel> bids = {
    {100.00f, 1000},
    {100.50f, 500},
    {100.25f, 750},
    {99.75f, 1200},
    {99.50f, 800}
  };

  book_.load_snapshot(bids, {});

  auto top3 = book_.get_top_bids(3);
  ASSERT_EQ(top3.size(), 3);

  // Should be sorted by price descending
  EXPECT_FLOAT_EQ(top3[0].price, 100.50f);
  EXPECT_FLOAT_EQ(top3[1].price, 100.25f);
  EXPECT_FLOAT_EQ(top3[2].price, 100.00f);
}

TEST_F(OrderBookTest, GetTopAsks) {
  std::vector<OrderBookLevel> asks = {
    {101.00f, 1000},
    {100.50f, 500},
    {100.75f, 750},
    {101.25f, 1200}
  };

  book_.load_snapshot({}, asks);

  auto top3 = book_.get_top_asks(3);
  ASSERT_EQ(top3.size(), 3);

  // Should be sorted by price ascending
  EXPECT_FLOAT_EQ(top3[0].price, 100.50f);
  EXPECT_FLOAT_EQ(top3[1].price, 100.75f);
  EXPECT_FLOAT_EQ(top3[2].price, 101.00f);
}

TEST_F(OrderBookTest, GetTopBidsMoreThanAvailable) {
  std::vector<OrderBookLevel> bids = {
    {100.00f, 1000},
    {100.50f, 500}
  };

  book_.load_snapshot(bids, {});

  auto top5 = book_.get_top_bids(5);
  EXPECT_EQ(top5.size(), 2);  // Only returns what's available
}

TEST_F(OrderBookTest, GetTopBidsEmpty) {
  auto top = book_.get_top_bids(5);
  EXPECT_TRUE(top.empty());
}

// Clear tests
TEST_F(OrderBookTest, Clear) {
  std::vector<OrderBookLevel> bids = {{100.00f, 1000}};
  std::vector<OrderBookLevel> asks = {{101.00f, 500}};
  book_.load_snapshot(bids, asks);

  EXPECT_FALSE(book_.empty());

  book_.clear();

  EXPECT_TRUE(book_.empty());
  EXPECT_EQ(book_.bid_depth(), 0);
  EXPECT_EQ(book_.ask_depth(), 0);
}

// Multiple updates test
TEST_F(OrderBookTest, MultipleUpdates) {
  // Simulate receiving a stream of updates
  book_.apply_update(0, 100.00f, 1000);  // Add bid
  book_.apply_update(1, 100.25f, 800);   // Add ask
  book_.apply_update(0, 99.75f, 500);    // Add another bid
  book_.apply_update(0, 100.00f, 1200);  // Modify bid
  book_.apply_update(1, 100.50f, 600);   // Add another ask
  book_.apply_update(0, 99.75f, 0);      // Delete bid

  EXPECT_EQ(book_.bid_depth(), 1);
  EXPECT_EQ(book_.ask_depth(), 2);

  float bid_price, ask_price;
  uint64_t bid_qty, ask_qty;

  book_.get_best_bid(bid_price, bid_qty);
  book_.get_best_ask(ask_price, ask_qty);

  EXPECT_FLOAT_EQ(bid_price, 100.00f);
  EXPECT_EQ(bid_qty, 1200);
  EXPECT_FLOAT_EQ(ask_price, 100.25f);
}

// Spread calculation test (implicit from print_top_of_book logic)
TEST_F(OrderBookTest, BidAskSpread) {
  book_.apply_update(0, 100.00f, 1000);  // Best bid
  book_.apply_update(1, 100.10f, 800);   // Best ask

  float bid_price, ask_price;
  uint64_t bid_qty, ask_qty;

  book_.get_best_bid(bid_price, bid_qty);
  book_.get_best_ask(ask_price, ask_qty);

  float spread = ask_price - bid_price;
  float mid = (bid_price + ask_price) / 2.0f;

  // Use EXPECT_NEAR for floating point arithmetic results
  EXPECT_NEAR(spread, 0.10f, 0.0001f);
  EXPECT_NEAR(mid, 100.05f, 0.0001f);
}

// Performance test
TEST_F(OrderBookTest, UpdateThroughput) {
  constexpr size_t NUM_UPDATES = 100000;

  uint64_t start = now_us();

  for (size_t i = 0; i < NUM_UPDATES; ++i) {
    float price = 100.0f + (i % 100) * 0.01f;
    int64_t qty = (i % 2 == 0) ? 1000 : 0;  // Alternate add/delete
    uint8_t side = i % 2;
    book_.apply_update(side, price, qty);
  }

  uint64_t end = now_us();
  uint64_t duration_us = end - start;

  double updates_per_sec = (static_cast<double>(NUM_UPDATES) / duration_us) * 1000000.0;
  std::cout << "  Order book update throughput: " << static_cast<size_t>(updates_per_sec) << " updates/sec" << std::endl;

  // Expect at least 500k updates/sec
  EXPECT_GT(updates_per_sec, 500000) << "Update throughput below 500k/sec";
}

TEST_F(OrderBookTest, SnapshotLoadThroughput) {
  constexpr size_t NUM_SNAPSHOTS = 1000;
  constexpr size_t LEVELS_PER_SIDE = 20;

  std::vector<OrderBookLevel> bids, asks;
  bids.reserve(LEVELS_PER_SIDE);
  asks.reserve(LEVELS_PER_SIDE);

  for (size_t i = 0; i < LEVELS_PER_SIDE; ++i) {
    bids.push_back({100.0f - i * 0.01f, static_cast<uint64_t>(1000 + i * 100)});
    asks.push_back({100.01f + i * 0.01f, static_cast<uint64_t>(800 + i * 100)});
  }

  uint64_t start = now_us();

  for (size_t i = 0; i < NUM_SNAPSHOTS; ++i) {
    book_.load_snapshot(bids, asks);
  }

  uint64_t end = now_us();
  uint64_t duration_us = end - start;

  double snapshots_per_sec = (static_cast<double>(NUM_SNAPSHOTS) / duration_us) * 1000000.0;
  std::cout << "  Snapshot load throughput: " << static_cast<size_t>(snapshots_per_sec) << " snapshots/sec" << std::endl;

  // Expect at least 10k snapshots/sec
  EXPECT_GT(snapshots_per_sec, 10000) << "Snapshot load throughput below 10k/sec";
}
