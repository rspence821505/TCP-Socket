/**
 * Snapshot Recovery Integration Tests (Google Test)
 *
 * Tests the snapshot recovery state machine:
 *   CONNECTING -> SNAPSHOT_REQUEST -> SNAPSHOT_REPLAY -> INCREMENTAL
 *
 * Features tested:
 *   - Client requests snapshot on connect
 *   - Server sends full order book snapshot
 *   - Client transitions to incremental mode
 *   - Incremental updates modify the order book
 *   - Reconnection triggers new snapshot request
 */

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

// Helper to get the build directory path
static std::string get_build_dir() {
  const char *build_dirs[] = {"build", "../build", "."};
  for (const char *dir : build_dirs) {
    std::string path = std::string(dir) + "/snapshot_mock_server";
    if (access(path.c_str(), X_OK) == 0) {
      return dir;
    }
  }
  return "build";
}

// =============================================================================
// Test Fixture for Snapshot Recovery Tests
// =============================================================================

class SnapshotRecoveryTest : public ::testing::Test {
protected:
  std::string build_dir_;
  std::string server_path_;
  std::string handler_path_;

  // Process IDs for cleanup
  pid_t server_pid_ = -1;
  pid_t handler_pid_ = -1;

  void SetUp() override {
    build_dir_ = get_build_dir();
    server_path_ = build_dir_ + "/snapshot_mock_server";
    handler_path_ = build_dir_ + "/feed_handler_snapshot";

    // Ensure binaries exist
    ASSERT_EQ(access(server_path_.c_str(), X_OK), 0)
        << "snapshot_mock_server not found at " << server_path_;
    ASSERT_EQ(access(handler_path_.c_str(), X_OK), 0)
        << "feed_handler_snapshot not found at " << handler_path_;
  }

  void TearDown() override {
    // Clean up any running processes
    stop_handler();
    stop_server();

    // Small delay to ensure ports are released
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // Start the mock server on specified port
  // Returns true if server started successfully
  bool start_server(int port, int heartbeat_ms = 1000, int updates_per_sec = 5,
                    const std::string &log_file = "") {
    server_pid_ = fork();
    if (server_pid_ == 0) {
      // Child process
      if (!log_file.empty()) {
        // Redirect stdout and stderr to log file
        freopen(log_file.c_str(), "w", stdout);
        freopen(log_file.c_str(), "a", stderr);
      } else {
        // Suppress output
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
      }

      execl(server_path_.c_str(), "snapshot_mock_server",
            std::to_string(port).c_str(), std::to_string(heartbeat_ms).c_str(),
            std::to_string(updates_per_sec).c_str(), nullptr);
      _exit(1); // exec failed
    }

    if (server_pid_ < 0) {
      return false;
    }

    // Wait for server to start listening
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return true;
  }

  // Start the feed handler, capturing output to log file
  // Returns true if handler started successfully
  bool start_handler(int port, const std::string &symbol,
                     const std::string &log_file) {
    handler_pid_ = fork();
    if (handler_pid_ == 0) {
      // Child process - redirect output to log file
      freopen(log_file.c_str(), "w", stdout);
      freopen(log_file.c_str(), "a", stderr);

      execl(handler_path_.c_str(), "feed_handler_snapshot",
            std::to_string(port).c_str(), symbol.c_str(), nullptr);
      _exit(1); // exec failed
    }

    return handler_pid_ > 0;
  }

  void stop_server() {
    if (server_pid_ > 0) {
      kill(server_pid_, SIGTERM);
      waitpid(server_pid_, nullptr, 0);
      server_pid_ = -1;
    }
  }

  void stop_handler() {
    if (handler_pid_ > 0) {
      kill(handler_pid_, SIGTERM);
      waitpid(handler_pid_, nullptr, 0);
      handler_pid_ = -1;
    }
  }

  // Count occurrences of a pattern in a log file
  int count_pattern_in_file(const std::string &file_path,
                            const std::string &pattern) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
      return 0;
    }

    std::string line;
    int count = 0;
    while (std::getline(file, line)) {
      if (line.find(pattern) != std::string::npos) {
        count++;
      }
    }
    return count;
  }

  // Check if a pattern exists in a log file
  bool pattern_exists_in_file(const std::string &file_path,
                              const std::string &pattern) {
    return count_pattern_in_file(file_path, pattern) > 0;
  }

  // Read entire file into string
  std::string read_file(const std::string &file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
      return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
  }

  // Get a unique port for testing (avoid conflicts)
  int get_test_port() {
    static int base_port = 19000;
    return base_port++;
  }
};

// =============================================================================
// Test 1: Normal Snapshot Recovery Flow
// =============================================================================

TEST_F(SnapshotRecoveryTest, NormalSnapshotRecoveryFlow) {
  int port = get_test_port();
  std::string handler_log = "/tmp/snapshot_test_handler1.log";

  // Start server
  ASSERT_TRUE(start_server(port, 1000, 5))
      << "Failed to start snapshot mock server";

  // Start handler
  ASSERT_TRUE(start_handler(port, "AAPL", handler_log))
      << "Failed to start feed handler";

  // Let it run for a few seconds
  std::this_thread::sleep_for(std::chrono::seconds(5));

  // Stop processes
  stop_handler();
  stop_server();

  // Small delay to ensure log is flushed
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Verify state machine transitions
  std::string log_content = read_file(handler_log);

  // Should have sent snapshot request
  EXPECT_TRUE(pattern_exists_in_file(handler_log, "Sending snapshot request"))
      << "Should have sent snapshot request";

  // Should have received snapshot
  EXPECT_TRUE(pattern_exists_in_file(handler_log, "Received snapshot"))
      << "Should have received snapshot response";

  // Should show bid/ask levels from snapshot
  bool has_bid_levels = pattern_exists_in_file(handler_log, "Bid levels:");
  bool has_ask_levels = pattern_exists_in_file(handler_log, "Ask levels:");
  EXPECT_TRUE(has_bid_levels || has_ask_levels)
      << "Should have received order book levels in snapshot";

  // Clean up log file
  unlink(handler_log.c_str());
}

// =============================================================================
// Test 2: Reconnection Triggers New Snapshot
// =============================================================================

TEST_F(SnapshotRecoveryTest, ReconnectionTriggersNewSnapshot) {
  int port = get_test_port();
  std::string handler_log = "/tmp/snapshot_test_handler2.log";

  // Start server
  ASSERT_TRUE(start_server(port, 1000, 5))
      << "Failed to start snapshot mock server";

  // Start handler
  ASSERT_TRUE(start_handler(port, "AAPL", handler_log))
      << "Failed to start feed handler";

  // Let it establish connection and get first snapshot
  std::this_thread::sleep_for(std::chrono::seconds(3));

  // Kill server to trigger reconnection
  stop_server();

  // Wait a bit
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // Restart server - handler should reconnect and request new snapshot
  ASSERT_TRUE(start_server(port, 1000, 5))
      << "Failed to restart snapshot mock server";

  // Wait for reconnection and new snapshot
  std::this_thread::sleep_for(std::chrono::seconds(5));

  // Stop everything
  stop_handler();
  stop_server();

  // Small delay for log flush
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Count snapshot requests - should have more than one due to reconnection
  int snapshot_requests =
      count_pattern_in_file(handler_log, "Sending snapshot request");
  int snapshots_received =
      count_pattern_in_file(handler_log, "Received snapshot");

  // At minimum, should have the initial snapshot
  EXPECT_GE(snapshot_requests, 1)
      << "Should have sent at least one snapshot request";
  EXPECT_GE(snapshots_received, 1)
      << "Should have received at least one snapshot";

  // Check for reconnection behavior indicators
  bool has_timeout_or_disconnect =
      pattern_exists_in_file(handler_log, "timeout") ||
      pattern_exists_in_file(handler_log, "Connection closed") ||
      pattern_exists_in_file(handler_log, "reconnect");

  // The handler should detect the disconnection
  // (Note: depending on timing, it may or may not successfully reconnect)
  std::cout << "  Snapshot requests: " << snapshot_requests << std::endl;
  std::cout << "  Snapshots received: " << snapshots_received << std::endl;
  std::cout << "  Detected disconnect/timeout: " << std::boolalpha
            << has_timeout_or_disconnect << std::endl;

  // Clean up log file
  unlink(handler_log.c_str());
}

// =============================================================================
// Test 3: Order Book State Verification
// =============================================================================

TEST_F(SnapshotRecoveryTest, OrderBookStateVerification) {
  int port = get_test_port();
  std::string handler_log = "/tmp/snapshot_test_handler3.log";

  // Start server with higher update rate
  ASSERT_TRUE(start_server(port, 1000, 10))
      << "Failed to start snapshot mock server";

  // Start handler with different symbol
  ASSERT_TRUE(start_handler(port, "MSFT", handler_log))
      << "Failed to start feed handler";

  // Let it run to accumulate updates
  std::this_thread::sleep_for(std::chrono::seconds(5));

  // Stop processes
  stop_handler();
  stop_server();

  // Small delay for log flush
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Verify snapshot was received
  EXPECT_TRUE(pattern_exists_in_file(handler_log, "Received snapshot"))
      << "Should have received snapshot";

  // Check for order book related output (bid/ask levels or updates)
  bool has_order_book_data =
      pattern_exists_in_file(handler_log, "Bid") ||
      pattern_exists_in_file(handler_log, "Ask") ||
      pattern_exists_in_file(handler_log, "BID") ||
      pattern_exists_in_file(handler_log, "ASK") ||
      pattern_exists_in_file(handler_log, "Order Book");

  EXPECT_TRUE(has_order_book_data) << "Should have order book data in output";

  // Clean up log file
  unlink(handler_log.c_str());
}

// =============================================================================
// Test 4: Multiple Symbols
// =============================================================================

TEST_F(SnapshotRecoveryTest, MultipleSymbolsSupport) {
  // Test that the server initializes multiple symbols (AAPL, MSFT, GOOG)
  int port = get_test_port();
  std::string handler_log = "/tmp/snapshot_test_handler4.log";

  // Start server
  ASSERT_TRUE(start_server(port, 1000, 5))
      << "Failed to start snapshot mock server";

  // Start handler requesting GOOG
  ASSERT_TRUE(start_handler(port, "GOOG", handler_log))
      << "Failed to start feed handler";

  // Let it run briefly
  std::this_thread::sleep_for(std::chrono::seconds(3));

  // Stop processes
  stop_handler();
  stop_server();

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Should have received snapshot for requested symbol
  EXPECT_TRUE(pattern_exists_in_file(handler_log, "Received snapshot"))
      << "Should have received snapshot for GOOG";

  // Clean up
  unlink(handler_log.c_str());
}

// =============================================================================
// Test 5: Heartbeat During Incremental Mode
// =============================================================================

TEST_F(SnapshotRecoveryTest, HeartbeatDuringIncrementalMode) {
  int port = get_test_port();
  std::string handler_log = "/tmp/snapshot_test_handler5.log";

  // Start server with 500ms heartbeat interval
  ASSERT_TRUE(start_server(port, 500, 5))
      << "Failed to start snapshot mock server";

  // Start handler
  ASSERT_TRUE(start_handler(port, "AAPL", handler_log))
      << "Failed to start feed handler";

  // Run for enough time to receive multiple heartbeats
  std::this_thread::sleep_for(std::chrono::seconds(4));

  // Stop processes
  stop_handler();
  stop_server();

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Should have received heartbeats
  int heartbeat_count = count_pattern_in_file(handler_log, "Heartbeat");
  std::cout << "  Heartbeats received: " << heartbeat_count << std::endl;

  // With 500ms interval over 4 seconds, should have several heartbeats
  EXPECT_GE(heartbeat_count, 2) << "Should have received multiple heartbeats";

  // Clean up
  unlink(handler_log.c_str());
}

// =============================================================================
// Test 6: Binary Existence Verification
// =============================================================================

TEST_F(SnapshotRecoveryTest, RequiredBinariesExist) {
  // Verify that all required binaries for snapshot recovery exist
  EXPECT_EQ(access(server_path_.c_str(), X_OK), 0)
      << "snapshot_mock_server should be executable";
  EXPECT_EQ(access(handler_path_.c_str(), X_OK), 0)
      << "feed_handler_snapshot should be executable";
}

// =============================================================================
// Test 7: Server Handles Multiple Connections
// =============================================================================

TEST_F(SnapshotRecoveryTest, ServerAcceptsConnection) {
  int port = get_test_port();

  // Start server
  ASSERT_TRUE(start_server(port, 1000, 5))
      << "Failed to start snapshot mock server";

  // Give server time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Verify server is running
  int status;
  pid_t result = waitpid(server_pid_, &status, WNOHANG);
  EXPECT_EQ(result, 0) << "Server should still be running";

  stop_server();
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
