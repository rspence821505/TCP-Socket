/**
 * Integration Tests for TCP Feed Handler
 *
 * Build verification tests check that all required binaries exist.
 *
 * For full server/client communication tests, run the shell scripts directly:
 *   scripts/test_snapshot_recovery.sh
 *   scripts/run_heartbeat_test.sh
 *   scripts/test_feed_handler.sh
 *   scripts/benchmark_throughput.sh
 *   scripts/benchmark_zerocopy.sh
 */

#include <gtest/gtest.h>
#include <string>
#include <unistd.h>

// Helper to get the build directory path
static std::string get_build_dir() {
  const char* build_dirs[] = {"build", "../build", "."};
  for (const char* dir : build_dirs) {
    std::string path = std::string(dir) + "/binary_mock_server";
    if (access(path.c_str(), X_OK) == 0) {
      return dir;
    }
  }
  return "build";
}

//=============================================================================
// Test Fixture for Integration Tests
//=============================================================================

class IntegrationTest : public ::testing::Test {
protected:
  std::string build_dir_;

  void SetUp() override {
    build_dir_ = get_build_dir();
  }

  bool binary_exists(const std::string& name) {
    std::string path = build_dir_ + "/" + name;
    return access(path.c_str(), X_OK) == 0;
  }
};

//=============================================================================
// Build Verification Tests
// Verify all required binaries are built correctly
//=============================================================================

class BuildVerificationTest : public IntegrationTest {};

TEST_F(BuildVerificationTest, CoreBinariesExist) {
  EXPECT_TRUE(binary_exists("binary_mock_server")) << "binary_mock_server should exist";
  EXPECT_TRUE(binary_exists("mock_server")) << "mock_server should exist";
  EXPECT_TRUE(binary_exists("binary_client")) << "binary_client should exist";
  EXPECT_TRUE(binary_exists("blocking_client")) << "blocking_client should exist";
}

TEST_F(BuildVerificationTest, FeedHandlerBinariesExist) {
  EXPECT_TRUE(binary_exists("feed_handler_spsc")) << "feed_handler_spsc should exist";
  EXPECT_TRUE(binary_exists("feed_handler_spmc")) << "feed_handler_spmc should exist";
}

TEST_F(BuildVerificationTest, TextProtocolBinaries) {
  EXPECT_TRUE(binary_exists("text_mock_server")) << "text_mock_server should exist";
  EXPECT_TRUE(binary_exists("feed_handler_text")) << "feed_handler_text should exist";
}

TEST_F(BuildVerificationTest, HeartbeatBinaries) {
  EXPECT_TRUE(binary_exists("heartbeat_mock_server")) << "heartbeat_mock_server should exist";
  EXPECT_TRUE(binary_exists("feed_handler_heartbeat")) << "feed_handler_heartbeat should exist";
}

TEST_F(BuildVerificationTest, SnapshotBinaries) {
  EXPECT_TRUE(binary_exists("snapshot_mock_server")) << "snapshot_mock_server should exist";
  EXPECT_TRUE(binary_exists("feed_handler_snapshot")) << "feed_handler_snapshot should exist";
}

TEST_F(BuildVerificationTest, UDPBinaries) {
  EXPECT_TRUE(binary_exists("udp_mock_server")) << "udp_mock_server should exist";
  EXPECT_TRUE(binary_exists("udp_feed_handler")) << "udp_feed_handler should exist";
}

TEST_F(BuildVerificationTest, BenchmarkBinaries) {
  EXPECT_TRUE(binary_exists("socket_tuning_benchmark")) << "socket_tuning_benchmark should exist";
  EXPECT_TRUE(binary_exists("tcp_vs_udp_benchmark")) << "tcp_vs_udp_benchmark should exist";
  EXPECT_TRUE(binary_exists("benchmark_pool_vs_malloc")) << "benchmark_pool_vs_malloc should exist";
  EXPECT_TRUE(binary_exists("benchmark_parsing_hotpath")) << "benchmark_parsing_hotpath should exist";
}

TEST_F(BuildVerificationTest, ZeroCopyBinaries) {
  EXPECT_TRUE(binary_exists("binary_client_zerocopy")) << "binary_client_zerocopy should exist";
}

//=============================================================================
// Note about server/client integration tests
//=============================================================================

// The original shell script tests (test_snapshot_recovery.sh, run_heartbeat_test.sh, etc.)
// require spawning server and client processes with proper timeout handling.
//
// Due to cross-platform timeout command differences (Linux 'timeout' vs macOS),
// these tests are best run directly via the shell scripts:
//
//   make test-text-protocol     # Text protocol communication test
//   scripts/test_snapshot_recovery.sh  # Snapshot recovery state machine
//   scripts/run_heartbeat_test.sh      # Heartbeat/connection management
//   scripts/benchmark_throughput.sh    # Throughput benchmarks
//
// Or run them manually:
//   ./build/text_mock_server 9999 5000 2 &
//   ./build/feed_handler_text 9999 127.0.0.1
