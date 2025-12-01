#include <gtest/gtest.h>
#include <thread>
#include <chrono>

#include "common.hpp"

// =============================================================================
// Timestamp Utilities Tests
// =============================================================================

TEST(TimestampTest, NowNsReturnsPositive) {
  uint64_t ts = now_ns();
  EXPECT_GT(ts, 0);
}

TEST(TimestampTest, NowNsIncreases) {
  uint64_t ts1 = now_ns();
  std::this_thread::sleep_for(std::chrono::microseconds(100));
  uint64_t ts2 = now_ns();
  EXPECT_GT(ts2, ts1);
}

TEST(TimestampTest, NowUsReturnsPositive) {
  uint64_t ts = now_us();
  EXPECT_GT(ts, 0);
}

TEST(TimestampTest, NowMsReturnsPositive) {
  uint64_t ts = now_ms();
  EXPECT_GT(ts, 0);
}

TEST(TimestampTest, TimestampConsistency) {
  uint64_t ns = now_ns();
  uint64_t us = now_us();
  uint64_t ms = now_ms();

  // ns should be roughly us * 1000 and ms * 1000000
  // Allow for some timing variation
  EXPECT_LT(ns / 1000 - us, 1000);  // Within 1ms
  EXPECT_LT(us / 1000 - ms, 10);     // Within 10ms
}

// =============================================================================
// Result Type Tests
// =============================================================================

TEST(ResultTest, SuccessValue) {
  Result<int> r(42);
  EXPECT_TRUE(r.ok());
  EXPECT_FALSE(r.failed());
  EXPECT_EQ(r.value(), 42);
}

TEST(ResultTest, ErrorValue) {
  auto r = Result<int>::error("Something went wrong");
  EXPECT_FALSE(r.ok());
  EXPECT_TRUE(r.failed());
  EXPECT_EQ(r.error(), "Something went wrong");
}

TEST(ResultTest, BoolConversion) {
  Result<int> success(42);
  Result<int> failure = Result<int>::error("error");

  if (success) {
    EXPECT_TRUE(true);
  } else {
    FAIL() << "Success should be truthy";
  }

  if (failure) {
    FAIL() << "Failure should be falsy";
  } else {
    EXPECT_TRUE(true);
  }
}

TEST(ResultTest, VoidSuccess) {
  Result<void> r;
  EXPECT_TRUE(r.ok());
  EXPECT_FALSE(r.failed());
}

TEST(ResultTest, VoidError) {
  auto r = Result<void>::error("Operation failed");
  EXPECT_FALSE(r.ok());
  EXPECT_TRUE(r.failed());
  EXPECT_EQ(r.error(), "Operation failed");
}

TEST(ResultTest, StringResult) {
  Result<std::string> r("hello world");
  EXPECT_TRUE(r.ok());
  EXPECT_EQ(r.value(), "hello world");
}

// =============================================================================
// LatencyStats Tests
// =============================================================================

TEST(LatencyStatsTest, EmptyStats) {
  LatencyStats stats;
  EXPECT_TRUE(stats.empty());
  EXPECT_EQ(stats.count(), 0);
  EXPECT_EQ(stats.mean(), 0.0);
  EXPECT_EQ(stats.max(), 0);
  EXPECT_EQ(stats.min(), 0);
}

TEST(LatencyStatsTest, SingleValue) {
  LatencyStats stats;
  stats.add(1000);

  EXPECT_FALSE(stats.empty());
  EXPECT_EQ(stats.count(), 1);
  EXPECT_EQ(stats.mean(), 1000.0);
  EXPECT_EQ(stats.max(), 1000);
  EXPECT_EQ(stats.min(), 1000);
}

TEST(LatencyStatsTest, MultipleValues) {
  LatencyStats stats;
  stats.add(100);
  stats.add(200);
  stats.add(300);
  stats.add(400);
  stats.add(500);

  EXPECT_EQ(stats.count(), 5);
  EXPECT_EQ(stats.mean(), 300.0);
  EXPECT_EQ(stats.max(), 500);
  EXPECT_EQ(stats.min(), 100);
}

TEST(LatencyStatsTest, Percentiles) {
  LatencyStats stats;
  stats.reserve(100);

  // Add values 1 to 100
  for (int i = 1; i <= 100; ++i) {
    stats.add(i);
  }

  // Using nearest-rank percentile method: index = ceil(p * n / 100)
  // For 100 values: p50 → index 50 → value 51
  //                 p95 → index 95 → value 96
  //                 p99 → index 99 → value 100
  EXPECT_EQ(stats.percentile(50), 51);
  EXPECT_EQ(stats.percentile(95), 96);
  EXPECT_EQ(stats.percentile(99), 100);
}

TEST(LatencyStatsTest, Clear) {
  LatencyStats stats;
  stats.add(100);
  stats.add(200);
  EXPECT_EQ(stats.count(), 2);

  stats.clear();
  EXPECT_TRUE(stats.empty());
  EXPECT_EQ(stats.count(), 0);
}

TEST(LatencyStatsTest, Reserve) {
  LatencyStats stats;
  stats.reserve(10000);

  // Should be able to add many values efficiently
  for (int i = 0; i < 10000; ++i) {
    stats.add(i);
  }

  EXPECT_EQ(stats.count(), 10000);
}

// =============================================================================
// Socket Utilities Tests
// =============================================================================

TEST(SocketTest, ConnectToInvalidPort) {
  auto result = socket_connect("127.0.0.1", 59999);  // Unlikely to be listening
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(result.error().find("connection failed") != std::string::npos ||
              result.error().find("refused") != std::string::npos);
}

TEST(SocketTest, ConnectToInvalidAddress) {
  auto result = socket_connect("invalid.address.that.does.not.exist", 9999);
  EXPECT_FALSE(result.ok());
}

TEST(SocketTest, SocketOptionsDefault) {
  SocketOptions opts;
  EXPECT_TRUE(opts.tcp_nodelay);
  EXPECT_FALSE(opts.non_blocking);
  EXPECT_EQ(opts.recv_buffer_size, 0);
  EXPECT_EQ(opts.send_buffer_size, 0);
}

// =============================================================================
// Utility Function Tests
// =============================================================================

TEST(UtilityTest, FormatBytes) {
  EXPECT_EQ(format_bytes(0), "0.00 B");
  EXPECT_EQ(format_bytes(512), "512.00 B");
  EXPECT_EQ(format_bytes(1024), "1.00 KB");
  EXPECT_EQ(format_bytes(1536), "1.50 KB");
  EXPECT_EQ(format_bytes(1024 * 1024), "1.00 MB");
  EXPECT_EQ(format_bytes(1024 * 1024 * 1024), "1.00 GB");
}

TEST(UtilityTest, FormatDurationNs) {
  EXPECT_EQ(format_duration_ns(500), "500 ns");
  EXPECT_EQ(format_duration_ns(5000), "5 us");
  EXPECT_EQ(format_duration_ns(5000000), "5 ms");
  EXPECT_EQ(format_duration_ns(5000000000), "5 s");
}

TEST(UtilityTest, TrimSymbol) {
  char symbol1[] = {'A', 'A', 'P', 'L'};
  EXPECT_EQ(trim_symbol(symbol1, 4), "AAPL");

  char symbol2[] = {'G', 'O', '\0', '\0'};
  EXPECT_EQ(trim_symbol(symbol2, 4), "GO");

  char symbol3[] = {'\0', '\0', '\0', '\0'};
  EXPECT_EQ(trim_symbol(symbol3, 4), "");
}

// =============================================================================
// Logger Tests (basic functionality)
// =============================================================================

TEST(LoggerTest, LogLevelDefault) {
  // Just verify logging doesn't crash
  Logger::info("Test", "Info message");
  Logger::warning("Test", "Warning message");
  Logger::error("Test", "Error message");
}

TEST(LoggerTest, LogWithFormatting) {
  Logger::info("Test", "Value: %d", 42);
  Logger::info("Test", "String: %s", "hello");
  Logger::info("Test", "Multiple: %d %s %.2f", 1, "two", 3.0);
}

TEST(LoggerTest, SetLogLevel) {
  Logger::set_level(LogLevel::WARNING);
  // INFO should be suppressed (we can't easily verify, just ensure no crash)
  Logger::info("Test", "This should be suppressed");
  Logger::warning("Test", "This should appear");

  // Reset to default
  Logger::set_level(LogLevel::INFO);
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
