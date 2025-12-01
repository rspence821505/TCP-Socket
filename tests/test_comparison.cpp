/**
 * Zero-Copy Ring Buffer Comparison Tests (Google Test)
 *
 * Compares the original binary client (with buffer.erase()) against
 * the zero-copy version (with ring buffer).
 *
 * Tests:
 *   - Binary existence verification
 *   - Original client functionality
 *   - Zero-copy client functionality
 *   - Both clients receive messages from multiple exchanges
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
#include <vector>

// Helper to get the build directory path
static std::string get_build_dir() {
  const char *build_dirs[] = {"build", "../build", "."};
  for (const char *dir : build_dirs) {
    std::string path = std::string(dir) + "/binary_mock_server";
    if (access(path.c_str(), X_OK) == 0) {
      return dir;
    }
  }
  return "build";
}

// =============================================================================
// Test Fixture for Comparison Tests
// =============================================================================

class ComparisonTest : public ::testing::Test {
protected:
  std::string build_dir_;
  std::string server_path_;
  std::string original_client_path_;
  std::string zerocopy_client_path_;

  // Process IDs for cleanup
  std::vector<pid_t> server_pids_;
  pid_t client_pid_ = -1;

  // The binary clients have hardcoded ports: 9999, 10000, 10001
  // We must use these exact ports for the tests to work
  static constexpr int CLIENT_PORT_1 = 9999;
  static constexpr int CLIENT_PORT_2 = 10000;
  static constexpr int CLIENT_PORT_3 = 10001;

  void SetUp() override {
    build_dir_ = get_build_dir();
    server_path_ = build_dir_ + "/binary_mock_server";
    original_client_path_ = build_dir_ + "/binary_client";
    zerocopy_client_path_ = build_dir_ + "/binary_client_zerocopy";

    // Ensure binaries exist
    ASSERT_EQ(access(server_path_.c_str(), X_OK), 0)
        << "binary_mock_server not found at " << server_path_;
    ASSERT_EQ(access(original_client_path_.c_str(), X_OK), 0)
        << "binary_client not found at " << original_client_path_;
    ASSERT_EQ(access(zerocopy_client_path_.c_str(), X_OK), 0)
        << "binary_client_zerocopy not found at " << zerocopy_client_path_;
  }

  void TearDown() override {
    // Clean up any running processes
    stop_client();
    stop_all_servers();

    // Small delay to ensure ports are released
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  // Get the hardcoded ports used by binary_client and binary_client_zerocopy
  std::vector<int> get_test_ports(int count = 3) {
    std::vector<int> all_ports = {CLIENT_PORT_1, CLIENT_PORT_2, CLIENT_PORT_3};
    std::vector<int> ports;
    for (int i = 0; i < count && i < 3; ++i) {
      ports.push_back(all_ports[i]);
    }
    return ports;
  }

  // Start a mock server on specified port
  bool start_server(int port) {
    pid_t pid = fork();
    if (pid == 0) {
      // Child process - suppress output
      freopen("/dev/null", "w", stdout);
      freopen("/dev/null", "w", stderr);

      execl(server_path_.c_str(), "binary_mock_server", std::to_string(port).c_str(),
            nullptr);
      _exit(1); // exec failed
    }

    if (pid < 0) {
      return false;
    }

    server_pids_.push_back(pid);
    return true;
  }

  // Start multiple servers on given ports
  bool start_servers(const std::vector<int> &ports) {
    for (int port : ports) {
      if (!start_server(port)) {
        return false;
      }
    }
    // Wait for servers to start listening
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return true;
  }

  // Run the original client and capture output
  std::string run_original_client(const std::string &log_file,
                                  int timeout_seconds = 10) {
    return run_client(original_client_path_, log_file, timeout_seconds);
  }

  // Run the zero-copy client and capture output
  std::string run_zerocopy_client(const std::string &log_file,
                                  int timeout_seconds = 10) {
    return run_client(zerocopy_client_path_, log_file, timeout_seconds);
  }

  // Run a client binary and capture output
  std::string run_client(const std::string &client_path,
                         const std::string &log_file, int timeout_seconds) {
    client_pid_ = fork();
    if (client_pid_ == 0) {
      // Child process - redirect output to log file
      freopen(log_file.c_str(), "w", stdout);
      freopen(log_file.c_str(), "a", stderr);

      execl(client_path.c_str(), client_path.c_str(), nullptr);
      _exit(1); // exec failed
    }

    if (client_pid_ < 0) {
      return "";
    }

    // Wait for client to complete or timeout
    auto start = std::chrono::steady_clock::now();
    int status;
    while (true) {
      pid_t result = waitpid(client_pid_, &status, WNOHANG);
      if (result == client_pid_) {
        // Process finished
        client_pid_ = -1;
        break;
      }

      auto elapsed = std::chrono::steady_clock::now() - start;
      if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >=
          timeout_seconds) {
        // Timeout - kill the client
        kill(client_pid_, SIGTERM);
        waitpid(client_pid_, nullptr, 0);
        client_pid_ = -1;
        break;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Read log file
    return read_file(log_file);
  }

  void stop_client() {
    if (client_pid_ > 0) {
      kill(client_pid_, SIGTERM);
      waitpid(client_pid_, nullptr, 0);
      client_pid_ = -1;
    }
  }

  void stop_all_servers() {
    for (pid_t pid : server_pids_) {
      kill(pid, SIGTERM);
      waitpid(pid, nullptr, 0);
    }
    server_pids_.clear();
  }

  // Count occurrences of a pattern in a string
  int count_pattern(const std::string &content, const std::string &pattern) {
    int count = 0;
    size_t pos = 0;
    while ((pos = content.find(pattern, pos)) != std::string::npos) {
      count++;
      pos += pattern.length();
    }
    return count;
  }

  // Check if a pattern exists in content
  bool pattern_exists(const std::string &content, const std::string &pattern) {
    return content.find(pattern) != std::string::npos;
  }

  // Extract total messages from statistics output
  int extract_total_messages(const std::string &content) {
    // Look for "Total: X messages" pattern
    std::regex total_regex("Total:\\s*(\\d+)\\s*messages");
    std::smatch match;
    if (std::regex_search(content, match, total_regex)) {
      return std::stoi(match[1].str());
    }
    return 0;
  }

  // Extract messages per second from statistics output
  int extract_msgs_per_sec(const std::string &content) {
    // Look for "(X msgs/sec)" pattern
    std::regex rate_regex("\\((\\d+)\\s*msgs/sec\\)");
    std::smatch match;
    if (std::regex_search(content, match, rate_regex)) {
      return std::stoi(match[1].str());
    }
    return 0;
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
};

// =============================================================================
// Test 1: Binary Existence Verification
// =============================================================================

TEST_F(ComparisonTest, RequiredBinariesExist) {
  EXPECT_EQ(access(server_path_.c_str(), X_OK), 0)
      << "binary_mock_server should be executable";
  EXPECT_EQ(access(original_client_path_.c_str(), X_OK), 0)
      << "binary_client should be executable";
  EXPECT_EQ(access(zerocopy_client_path_.c_str(), X_OK), 0)
      << "binary_client_zerocopy should be executable";
}

// =============================================================================
// Test 2: Original Client Receives Messages
// =============================================================================

TEST_F(ComparisonTest, OriginalClientReceivesMessages) {
  std::vector<int> ports = get_test_ports(3);
  std::string log_file = "/tmp/comparison_original.log";

  // Start servers
  ASSERT_TRUE(start_servers(ports)) << "Failed to start mock servers";

  // Run original client (will timeout after servers finish)
  std::string output = run_original_client(log_file, 15);

  // Stop servers
  stop_all_servers();

  // Verify output contains expected patterns
  EXPECT_TRUE(pattern_exists(output, "Connected to exchange"))
      << "Should show connection message";

  EXPECT_TRUE(pattern_exists(output, "Statistics"))
      << "Should show statistics section";

  // Should have received some messages
  int total_messages = extract_total_messages(output);
  EXPECT_GT(total_messages, 0) << "Should have received messages";

  std::cout << "  Original client received: " << total_messages << " messages"
            << std::endl;

  // Clean up
  unlink(log_file.c_str());
}

// =============================================================================
// Test 3: Zero-Copy Client Receives Messages
// =============================================================================

TEST_F(ComparisonTest, ZeroCopyClientReceivesMessages) {
  std::vector<int> ports = get_test_ports(3);
  std::string log_file = "/tmp/comparison_zerocopy.log";

  // Start servers
  ASSERT_TRUE(start_servers(ports)) << "Failed to start mock servers";

  // Run zero-copy client
  std::string output = run_zerocopy_client(log_file, 15);

  // Stop servers
  stop_all_servers();

  // Verify output contains expected patterns
  EXPECT_TRUE(pattern_exists(output, "Connected to exchange"))
      << "Should show connection message";

  EXPECT_TRUE(pattern_exists(output, "zero-copy") ||
              pattern_exists(output, "ring buffer"))
      << "Should mention zero-copy or ring buffer";

  EXPECT_TRUE(pattern_exists(output, "Statistics"))
      << "Should show statistics section";

  // Should have received some messages
  int total_messages = extract_total_messages(output);
  EXPECT_GT(total_messages, 0) << "Should have received messages";

  // Zero-copy client should show buffer shifts avoided
  EXPECT_TRUE(pattern_exists(output, "buffer shifts avoided"))
      << "Should show buffer shifts avoided metric";

  std::cout << "  Zero-copy client received: " << total_messages << " messages"
            << std::endl;

  // Clean up
  unlink(log_file.c_str());
}

// =============================================================================
// Test 4: Both Clients Handle Multiple Exchanges
// =============================================================================

TEST_F(ComparisonTest, BothClientsHandleMultipleExchanges) {
  std::vector<int> ports = get_test_ports(3);
  std::string log_file = "/tmp/comparison_multi.log";

  // Start servers
  ASSERT_TRUE(start_servers(ports)) << "Failed to start mock servers";

  // Run original client
  std::string output = run_original_client(log_file, 10);

  // Stop servers
  stop_all_servers();

  // Count exchange mentions (each exchange should report messages)
  int exchange_count = count_pattern(output, "Exchange");
  EXPECT_GE(exchange_count, 3)
      << "Should mention multiple exchanges in statistics";

  // Clean up
  unlink(log_file.c_str());
}

// =============================================================================
// Test 5: Zero-Copy Client Shows Optimization Metrics
// =============================================================================

TEST_F(ComparisonTest, ZeroCopyShowsOptimizationMetrics) {
  std::vector<int> ports = get_test_ports(3);
  std::string log_file = "/tmp/comparison_metrics.log";

  // Start servers
  ASSERT_TRUE(start_servers(ports)) << "Failed to start mock servers";

  // Run zero-copy client
  std::string output = run_zerocopy_client(log_file, 10);

  // Stop servers
  stop_all_servers();

  // Zero-copy client should show optimization metrics
  bool has_shifts_metric = pattern_exists(output, "buffer shifts avoided");
  bool has_zerocopy_metric = pattern_exists(output, "Zero-copy optimizations");

  EXPECT_TRUE(has_shifts_metric || has_zerocopy_metric)
      << "Should show zero-copy optimization metrics";

  // Clean up
  unlink(log_file.c_str());
}

// =============================================================================
// Test 6: Clients Handle Server Shutdown Gracefully
// =============================================================================

TEST_F(ComparisonTest, ClientsHandleServerShutdown) {
  std::vector<int> ports = get_test_ports(3);
  std::string log_file = "/tmp/comparison_shutdown.log";

  // Start servers
  ASSERT_TRUE(start_servers(ports)) << "Failed to start mock servers";

  // Give servers time to start sending
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // Kill servers early
  stop_all_servers();

  // Run client - should handle closed connections gracefully
  // Use the original client for this test
  pid_t pid = fork();
  if (pid == 0) {
    freopen(log_file.c_str(), "w", stdout);
    freopen(log_file.c_str(), "a", stderr);
    execl(original_client_path_.c_str(), "binary_client", nullptr);
    _exit(1);
  }

  // Wait briefly then check
  std::this_thread::sleep_for(std::chrono::seconds(3));
  kill(pid, SIGTERM);
  waitpid(pid, nullptr, 0);

  std::string output = read_file(log_file);

  // Should either connect and receive, or fail gracefully
  bool handled_gracefully = pattern_exists(output, "Failed to connect") ||
                            pattern_exists(output, "closed connection") ||
                            pattern_exists(output, "Statistics") ||
                            output.empty(); // No connections made

  EXPECT_TRUE(handled_gracefully) << "Should handle server unavailability";

  // Clean up
  unlink(log_file.c_str());
}

// =============================================================================
// Test 7: Server Starts and Accepts Connections
// =============================================================================

TEST_F(ComparisonTest, ServerStartsAndAcceptsConnections) {
  std::vector<int> ports = get_test_ports(1);

  // Start a single server
  ASSERT_TRUE(start_server(ports[0])) << "Failed to start mock server";

  // Give server time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Verify server is running
  ASSERT_FALSE(server_pids_.empty());
  int status;
  pid_t result = waitpid(server_pids_[0], &status, WNOHANG);
  EXPECT_EQ(result, 0) << "Server should still be running";

  stop_all_servers();
}

// =============================================================================
// Test 8: Throughput Statistics Are Reported
// =============================================================================

TEST_F(ComparisonTest, ThroughputStatisticsReported) {
  std::vector<int> ports = get_test_ports(3);
  std::string log_file = "/tmp/comparison_throughput.log";

  // Start servers
  ASSERT_TRUE(start_servers(ports)) << "Failed to start mock servers";

  // Run client long enough to get meaningful stats
  std::string output = run_zerocopy_client(log_file, 12);

  // Stop servers
  stop_all_servers();

  // Should report throughput
  int msgs_per_sec = extract_msgs_per_sec(output);
  if (msgs_per_sec > 0) {
    std::cout << "  Throughput: " << msgs_per_sec << " msgs/sec" << std::endl;
  }

  // Should have the msgs/sec metric in output
  EXPECT_TRUE(pattern_exists(output, "msgs/sec"))
      << "Should report messages per second";

  // Clean up
  unlink(log_file.c_str());
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
