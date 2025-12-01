#ifndef COMMON_HPP
#define COMMON_HPP

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

/**
 * Common Utilities Header
 *
 * Consolidates commonly used functionality across feed handlers:
 * - Timestamp utilities
 * - Latency statistics
 * - Logging framework
 * - Error handling (Result type)
 * - Socket utilities
 */

// =============================================================================
// Timestamp Utilities
// =============================================================================

/**
 * Get current timestamp in nanoseconds (high-resolution)
 */
inline uint64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

/**
 * Get current timestamp in microseconds
 */
inline uint64_t now_us() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

/**
 * Get current timestamp in milliseconds
 */
inline uint64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

// =============================================================================
// Logging Framework
// =============================================================================

enum class LogLevel { DEBUG, INFO, WARNING, ERROR };

/**
 * Simple thread-safe logger with consistent formatting
 *
 * Usage:
 *   Logger::info("Reader", "Received %d bytes", count);
 *   Logger::error("Socket", "Connection failed: %s", strerror(errno));
 */
class Logger {
public:
  static void set_level(LogLevel level) { min_level_ = level; }

  static void debug(const char *tag, const char *fmt, ...) {
    if (min_level_ > LogLevel::DEBUG)
      return;
    va_list args;
    va_start(args, fmt);
    log_impl("DEBUG", tag, fmt, args);
    va_end(args);
  }

  static void info(const char *tag, const char *fmt, ...) {
    if (min_level_ > LogLevel::INFO)
      return;
    va_list args;
    va_start(args, fmt);
    log_impl("INFO", tag, fmt, args);
    va_end(args);
  }

  static void warning(const char *tag, const char *fmt, ...) {
    if (min_level_ > LogLevel::WARNING)
      return;
    va_list args;
    va_start(args, fmt);
    log_impl("WARN", tag, fmt, args);
    va_end(args);
  }

  static void error(const char *tag, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_impl("ERROR", tag, fmt, args);
    va_end(args);
  }

  // Convenience: log with errno
  static void perror(const char *tag, const char *msg) {
    error(tag, "%s: %s", msg, strerror(errno));
  }

private:
  static inline LogLevel min_level_ = LogLevel::INFO;
  static inline std::mutex mutex_;

  static void log_impl(const char *level, const char *tag, const char *fmt,
                       va_list args) {
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "[" << tag << "] " << buffer << std::endl;
  }
};

// Convenience macros for logging
#define LOG_DEBUG(tag, ...) Logger::debug(tag, __VA_ARGS__)
#define LOG_INFO(tag, ...) Logger::info(tag, __VA_ARGS__)
#define LOG_WARN(tag, ...) Logger::warning(tag, __VA_ARGS__)
#define LOG_ERROR(tag, ...) Logger::error(tag, __VA_ARGS__)
#define LOG_PERROR(tag, msg) Logger::perror(tag, msg)

// =============================================================================
// Error Handling
// =============================================================================

/**
 * Result type for operations that can fail
 *
 * Usage:
 *   Result<int> connect_result = socket_connect("localhost", 9999);
 *   if (connect_result.ok()) {
 *     int sockfd = connect_result.value();
 *   } else {
 *     std::cerr << connect_result.error() << std::endl;
 *   }
 */
template <typename T> class Result {
public:
  // Success constructors
  Result(T value) : value_(std::move(value)), has_value_(true) {}

  // Error constructor
  static Result error(std::string msg) {
    Result r;
    r.error_ = std::move(msg);
    r.has_value_ = false;
    return r;
  }

  bool ok() const { return has_value_; }
  bool failed() const { return !has_value_; }

  T &value() { return value_; }
  const T &value() const { return value_; }

  const std::string &error() const { return error_; }

  // Conversion to bool for if statements
  explicit operator bool() const { return has_value_; }

private:
  Result() : has_value_(false) {}

  T value_;
  std::string error_;
  bool has_value_;
};

// Specialization for void (operations with no return value)
template <> class Result<void> {
public:
  Result() : has_value_(true) {}

  static Result error(std::string msg) {
    Result r;
    r.error_ = std::move(msg);
    r.has_value_ = false;
    return r;
  }

  bool ok() const { return has_value_; }
  bool failed() const { return !has_value_; }
  const std::string &error() const { return error_; }
  explicit operator bool() const { return has_value_; }

private:
  std::string error_;
  bool has_value_;
};

// =============================================================================
// Latency Statistics
// =============================================================================

/**
 * Unified latency statistics collector
 *
 * Usage:
 *   LatencyStats stats;
 *   stats.reserve(100000);
 *   stats.add(latency_ns);
 *   stats.print("End-to-end");
 */
class LatencyStats {
public:
  void reserve(size_t n) { latencies_.reserve(n); }

  void add(uint64_t latency_ns) { latencies_.push_back(latency_ns); }

  void clear() { latencies_.clear(); }

  size_t count() const { return latencies_.size(); }

  bool empty() const { return latencies_.empty(); }

  // Get percentile value (0-100)
  uint64_t percentile(int p) const {
    if (latencies_.empty())
      return 0;

    std::vector<uint64_t> sorted = latencies_;
    std::sort(sorted.begin(), sorted.end());

    size_t idx = sorted.size() * p / 100;
    if (idx >= sorted.size())
      idx = sorted.size() - 1;
    return sorted[idx];
  }

  double mean() const {
    if (latencies_.empty())
      return 0.0;
    uint64_t sum = std::accumulate(latencies_.begin(), latencies_.end(), 0ULL);
    return static_cast<double>(sum) / latencies_.size();
  }

  uint64_t max() const {
    if (latencies_.empty())
      return 0;
    return *std::max_element(latencies_.begin(), latencies_.end());
  }

  uint64_t min() const {
    if (latencies_.empty())
      return 0;
    return *std::min_element(latencies_.begin(), latencies_.end());
  }

  void print(const std::string &name) const {
    if (latencies_.empty()) {
      std::cout << name << ": No data" << std::endl;
      return;
    }

    std::vector<uint64_t> sorted = latencies_;
    std::sort(sorted.begin(), sorted.end());

    uint64_t sum = std::accumulate(sorted.begin(), sorted.end(), 0ULL);
    double mean_val = static_cast<double>(sum) / sorted.size();
    uint64_t p50 = sorted[sorted.size() * 50 / 100];
    uint64_t p95 = sorted[sorted.size() * 95 / 100];
    uint64_t p99 = sorted[sorted.size() * 99 / 100];
    uint64_t max_val = sorted.back();

    std::cout << name << ":" << std::endl;
    std::cout << "  Count: " << sorted.size() << std::endl;
    std::cout << "  Mean:  " << mean_val / 1000.0 << " us" << std::endl;
    std::cout << "  p50:   " << p50 / 1000.0 << " us" << std::endl;
    std::cout << "  p95:   " << p95 / 1000.0 << " us" << std::endl;
    std::cout << "  p99:   " << p99 / 1000.0 << " us" << std::endl;
    std::cout << "  Max:   " << max_val / 1000.0 << " us" << std::endl;
  }

  // Print with indentation (for nested output)
  void print_indented(const std::string &name) const {
    if (latencies_.empty())
      return;

    std::vector<uint64_t> sorted = latencies_;
    std::sort(sorted.begin(), sorted.end());

    uint64_t sum = std::accumulate(sorted.begin(), sorted.end(), 0ULL);
    double mean_val = static_cast<double>(sum) / sorted.size();
    uint64_t p50 = sorted[sorted.size() * 50 / 100];
    uint64_t p95 = sorted[sorted.size() * 95 / 100];
    uint64_t p99 = sorted[sorted.size() * 99 / 100];
    uint64_t max_val = sorted.back();

    std::cout << "  " << name << ":" << std::endl;
    std::cout << "    Mean: " << mean_val << " ns (" << mean_val / 1000.0
              << " µs)" << std::endl;
    std::cout << "    p50:  " << p50 << " ns (" << p50 / 1000.0 << " µs)"
              << std::endl;
    std::cout << "    p95:  " << p95 << " ns (" << p95 / 1000.0 << " µs)"
              << std::endl;
    std::cout << "    p99:  " << p99 << " ns (" << p99 / 1000.0 << " µs)"
              << std::endl;
    std::cout << "    Max:  " << max_val << " ns (" << max_val / 1000.0
              << " µs)" << std::endl;
  }

  // CSV output: name,mean,p50,p95,p99,max (all in µs)
  void print_csv(const std::string &name) const {
    if (latencies_.empty())
      return;

    std::vector<uint64_t> sorted = latencies_;
    std::sort(sorted.begin(), sorted.end());

    uint64_t sum = std::accumulate(sorted.begin(), sorted.end(), 0ULL);
    double mean_val = static_cast<double>(sum) / sorted.size();
    uint64_t p50 = sorted[sorted.size() * 50 / 100];
    uint64_t p95 = sorted[sorted.size() * 95 / 100];
    uint64_t p99 = sorted[sorted.size() * 99 / 100];
    uint64_t max_val = sorted.back();

    std::cout << name << "," << mean_val / 1000.0 << "," << p50 / 1000.0 << ","
              << p95 / 1000.0 << "," << p99 / 1000.0 << "," << max_val / 1000.0
              << std::endl;
  }

  // Access raw data for custom processing
  const std::vector<uint64_t> &data() const { return latencies_; }

private:
  std::vector<uint64_t> latencies_;
};

// =============================================================================
// Socket Utilities
// =============================================================================

/**
 * Socket configuration options
 */
struct SocketOptions {
  bool tcp_nodelay = true;    // Disable Nagle's algorithm
  bool non_blocking = false;  // Non-blocking mode
  int recv_buffer_size = 0;   // SO_RCVBUF (0 = system default)
  int send_buffer_size = 0;   // SO_SNDBUF (0 = system default)
  int connect_timeout_ms = 0; // Connection timeout (0 = system default)
};

/**
 * Create a TCP connection to the specified host and port
 *
 * @param host Hostname or IP address
 * @param port Port number
 * @param opts Socket configuration options
 * @return Result containing socket fd on success, error message on failure
 */
inline Result<int> socket_connect(const std::string &host, int port,
                                  const SocketOptions &opts = {}) {
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    return Result<int>::error(
        std::string("socket creation failed: ") + strerror(errno));
  }

  // Apply socket options before connect
  if (opts.tcp_nodelay) {
    int flag = 1;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
      close(sockfd);
      return Result<int>::error(
          std::string("TCP_NODELAY failed: ") + strerror(errno));
    }
  }

  if (opts.recv_buffer_size > 0) {
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &opts.recv_buffer_size,
                   sizeof(opts.recv_buffer_size)) < 0) {
      close(sockfd);
      return Result<int>::error(
          std::string("SO_RCVBUF failed: ") + strerror(errno));
    }
  }

  if (opts.send_buffer_size > 0) {
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &opts.send_buffer_size,
                   sizeof(opts.send_buffer_size)) < 0) {
      close(sockfd);
      return Result<int>::error(
          std::string("SO_SNDBUF failed: ") + strerror(errno));
    }
  }

  // Prepare address
  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);

  if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
    // Try as hostname (fallback to localhost)
    if (host == "localhost") {
      server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else {
      close(sockfd);
      return Result<int>::error("Invalid address: " + host);
    }
  }

  // Connect with optional timeout
  // Default timeout of 5 seconds for non-blocking sockets to avoid indefinite hangs
  int effective_timeout_ms = opts.connect_timeout_ms;
  if (effective_timeout_ms == 0 && opts.non_blocking) {
    effective_timeout_ms = 5000;  // 5 second default for non-blocking
  }

  if (effective_timeout_ms > 0) {
    // Set socket-level send timeout which affects connect() on most platforms
    struct timeval tv;
    tv.tv_sec = effective_timeout_ms / 1000;
    tv.tv_usec = (effective_timeout_ms % 1000) * 1000;
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    // Set non-blocking for connect with timeout
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1 || fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
      close(sockfd);
      return Result<int>::error(
          std::string("fcntl non-blocking failed: ") + strerror(errno));
    }

    int connect_result =
        connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));

    if (connect_result < 0 && errno == EINPROGRESS) {
      // Wait for connection with timeout using select
      fd_set write_fds, error_fds;
      FD_ZERO(&write_fds);
      FD_ZERO(&error_fds);
      FD_SET(sockfd, &write_fds);
      FD_SET(sockfd, &error_fds);

      struct timeval select_tv;
      select_tv.tv_sec = effective_timeout_ms / 1000;
      select_tv.tv_usec = (effective_timeout_ms % 1000) * 1000;

      int select_result =
          select(sockfd + 1, nullptr, &write_fds, &error_fds, &select_tv);

      if (select_result <= 0) {
        close(sockfd);
        if (select_result == 0) {
          return Result<int>::error("connection timeout to " + host + ":" +
                                    std::to_string(port));
        }
        return Result<int>::error(std::string("select failed: ") +
                                  strerror(errno));
      }

      // Check if connection was successful
      int error = 0;
      socklen_t len = sizeof(error);
      if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 ||
          error != 0) {
        close(sockfd);
        return Result<int>::error("connection failed to " + host + ":" +
                                  std::to_string(port) + ": " +
                                  strerror(error ? error : errno));
      }
    } else if (connect_result < 0) {
      std::string err = "connection failed to " + host + ":" +
                        std::to_string(port) + ": " + strerror(errno);
      close(sockfd);
      return Result<int>::error(err);
    }

    // Set back to blocking mode unless non_blocking was requested
    if (!opts.non_blocking) {
      fcntl(sockfd, F_SETFL, flags);
    }
  } else {
    // Simple blocking connect
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
        0) {
      std::string err = "connection failed to " + host + ":" +
                        std::to_string(port) + ": " + strerror(errno);
      close(sockfd);
      return Result<int>::error(err);
    }

    // Set non-blocking after connect if requested
    if (opts.non_blocking) {
      int flags = fcntl(sockfd, F_GETFL, 0);
      if (flags == -1 || fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        close(sockfd);
        return Result<int>::error(
            std::string("fcntl non-blocking failed: ") + strerror(errno));
      }
    }
  }

  return Result<int>(sockfd);
}

/**
 * Set socket to non-blocking mode
 */
inline Result<void> socket_set_nonblocking(int sockfd) {
  int flags = fcntl(sockfd, F_GETFL, 0);
  if (flags == -1) {
    return Result<void>::error(
        std::string("fcntl F_GETFL failed: ") + strerror(errno));
  }

  if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
    return Result<void>::error(
        std::string("fcntl F_SETFL failed: ") + strerror(errno));
  }

  return Result<void>();
}

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * Format bytes as human-readable string
 */
inline std::string format_bytes(uint64_t bytes) {
  const char *units[] = {"B", "KB", "MB", "GB", "TB"};
  int unit = 0;
  double size = static_cast<double>(bytes);

  while (size >= 1024 && unit < 4) {
    size /= 1024;
    unit++;
  }

  char buf[32];
  snprintf(buf, sizeof(buf), "%.2f %s", size, units[unit]);
  return std::string(buf);
}

/**
 * Format duration in nanoseconds as human-readable string
 */
inline std::string format_duration_ns(uint64_t ns) {
  if (ns < 1000) {
    return std::to_string(ns) + " ns";
  } else if (ns < 1000000) {
    return std::to_string(ns / 1000) + " us";
  } else if (ns < 1000000000) {
    return std::to_string(ns / 1000000) + " ms";
  } else {
    return std::to_string(ns / 1000000000) + " s";
  }
}

/**
 * Trim null bytes from end of char array to create string
 */
inline std::string trim_symbol(const char *symbol, size_t max_len) {
  std::string s(symbol, max_len);
  size_t null_pos = s.find('\0');
  if (null_pos != std::string::npos) {
    s = s.substr(0, null_pos);
  }
  return s;
}

#endif // COMMON_HPP
