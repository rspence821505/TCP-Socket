#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <numeric>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#include "binary_protocol.hpp"
#include "common.hpp"
#include "socket_config.hpp"

// Use consolidated LatencyStats from common.hpp
// Alias for backward compatibility
using BenchmarkLatencyStats = LatencyStats;

// Connect to server with specific socket configuration
Result<int> connect_with_config(const std::string &host, int port,
                                const SocketConfig &config, bool verbose = true) {
  // Map SocketConfig to SocketOptions
  SocketOptions opts;
  opts.tcp_nodelay = config.tcp_nodelay;
  opts.recv_buffer_size = config.rcvbuf_size;
  opts.send_buffer_size = config.sndbuf_size;
  opts.non_blocking = true;

  auto result = socket_connect(host, port, opts);
  if (!result) {
    return Result<int>::error(result.error());
  }
  int sockfd = result.value();

  // Apply TCP_QUICKACK if configured (Linux-only, handled separately)
#ifdef __linux__
  if (config.tcp_quickack) {
    int flag = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_QUICKACK, &flag, sizeof(flag));
  }
#endif

  if (verbose) {
    if (opts.tcp_nodelay) {
      std::cout << "  TCP_NODELAY enabled" << std::endl;
    }
    if (opts.recv_buffer_size > 0) {
      std::cout << "  SO_RCVBUF set" << std::endl;
    }
    if (opts.send_buffer_size > 0) {
      std::cout << "  SO_SNDBUF set" << std::endl;
    }
    std::cout << "  Connected to " << host << ":" << port << std::endl;
  }

  return Result<int>(sockfd);
}

// Run benchmark with specific socket configuration
BenchmarkLatencyStats run_benchmark(const std::string &host, int port,
                           const SocketConfig &config, size_t num_messages,
                           bool verbose = true) {
  if (verbose) {
    std::cout << "\n=== Testing: " << config.to_string() << " ===" << std::endl;
  }

  BenchmarkLatencyStats stats;
  stats.reserve(num_messages);

  // Connect to server
  auto connect_result = connect_with_config(host, port, config, verbose);
  if (!connect_result) {
    LOG_ERROR("Benchmark", "%s", connect_result.error().c_str());
    return stats;
  }
  int sockfd = connect_result.value();

  // Receive buffer for message reassembly
  std::vector<char> buffer;
  buffer.reserve(64 * 1024);

  uint64_t messages_received = 0;
  auto start_time = std::chrono::steady_clock::now();

  // Receive loop
  while (messages_received < num_messages) {
    // Timestamp before recv()
    uint64_t recv_start = now_ns();

    // Try to receive data
    char temp[4096];
    ssize_t bytes_read = recv(sockfd, temp, sizeof(temp), 0);

    if (bytes_read < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // No data available, try again
        continue;
      } else {
        LOG_PERROR("Benchmark", "recv failed");
        break;
      }
    } else if (bytes_read == 0) {
      if (verbose) {
        std::cout << "  Connection closed by server" << std::endl;
      }
      break;
    }

    // Append to buffer
    buffer.insert(buffer.end(), temp, temp + bytes_read);

    // Process all complete messages
    // New message format: [4-byte length][1-byte type][8-byte sequence][payload...]
    while (buffer.size() >= MessageHeader::HEADER_SIZE) {
      // Read header
      MessageHeader header = deserialize_header(buffer.data());

      // Validate length (should be reasonable)
      if (header.length > 1024) {
        LOG_ERROR("Benchmark", "Invalid message length: %u", header.length);
        close(sockfd);
        return stats;
      }

      // Check if complete message is available
      size_t total_size = MessageHeader::HEADER_SIZE + header.length;
      if (buffer.size() < total_size) {
        break; // Need more data
      }

      // Note: We don't need to deserialize for timing - just validate length
      // In a real system, you'd process the tick here
      // For benchmarking, we only care about the timing

      // Record latency (from start of recv() to now)
      uint64_t process_end = now_ns();
      stats.add(process_end - recv_start);

      // Remove processed message from buffer
      buffer.erase(buffer.begin(), buffer.begin() + total_size);

      messages_received++;
    }
  }

  auto end_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);
  double seconds = duration.count() / 1000.0;

  if (verbose) {
    std::cout << "  Received " << messages_received << " messages in "
              << seconds << "s ("
              << static_cast<int>(messages_received / seconds) << " msgs/sec)"
              << std::endl;
  }

  close(sockfd);
  return stats;
}

void print_usage(const char *prog_name) {
  std::cout << "Usage: " << prog_name << " [options]" << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  --host <host>       Server host (default: 127.0.0.1)"
            << std::endl;
  std::cout << "  --port <port>       Server port (default: 9999)" << std::endl;
  std::cout << "  --messages <n>      Number of messages to receive (default: "
               "10000)"
            << std::endl;
  std::cout << "  --nodelay <0|1>     Enable TCP_NODELAY (default: 0)"
            << std::endl;
  std::cout << "  --rcvbuf <bytes>    Receive buffer size (default: 0 = "
               "system default)"
            << std::endl;
  std::cout << "  --sndbuf <bytes>    Send buffer size (default: 0 = system "
               "default)"
            << std::endl;
  std::cout << "  --quickack <0|1>    Enable TCP_QUICKACK (default: 0, "
               "Linux-only)"
            << std::endl;
  std::cout << "  --csv               Output results in CSV format"
            << std::endl;
  std::cout << "  --quiet             Minimal output" << std::endl;
  std::cout << std::endl;
  std::cout << "Example:" << std::endl;
  std::cout << "  " << prog_name << " --nodelay 1 --rcvbuf 65536 --sndbuf 65536"
            << std::endl;
}

int main(int argc, char *argv[]) {
  // Default parameters
  std::string host = "127.0.0.1";
  int port = 9999;
  size_t num_messages = 10000;
  bool csv_output = false;
  bool verbose = true;

  SocketConfig config;

  // Parse command-line arguments
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      return 0;
    } else if (arg == "--host" && i + 1 < argc) {
      host = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      port = std::atoi(argv[++i]);
    } else if (arg == "--messages" && i + 1 < argc) {
      num_messages = std::atoi(argv[++i]);
    } else if (arg == "--nodelay" && i + 1 < argc) {
      config.tcp_nodelay = (std::atoi(argv[++i]) != 0);
    } else if (arg == "--rcvbuf" && i + 1 < argc) {
      config.rcvbuf_size = std::atoi(argv[++i]);
    } else if (arg == "--sndbuf" && i + 1 < argc) {
      config.sndbuf_size = std::atoi(argv[++i]);
    } else if (arg == "--quickack" && i + 1 < argc) {
      config.tcp_quickack = (std::atoi(argv[++i]) != 0);
    } else if (arg == "--csv") {
      csv_output = true;
      verbose = false;
    } else if (arg == "--quiet") {
      verbose = false;
    } else {
      LOG_ERROR("Benchmark", "Unknown option: %s", arg.c_str());
      print_usage(argv[0]);
      return 1;
    }
  }

  if (!csv_output && verbose) {
    std::cout << "=== Socket Tuning Benchmark ===" << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Host: " << host << ":" << port << std::endl;
    std::cout << "  Messages: " << num_messages << std::endl;
  }

  // Run benchmark
  BenchmarkLatencyStats stats = run_benchmark(host, port, config, num_messages, verbose);

  // Print results
  if (csv_output) {
    // CSV header if this is the first line
    // Format: config,mean,p50,p95,p99,max (all in µs)
    stats.print_csv(config.short_name());
  } else {
    stats.print("Latency (recv → processed)");
  }

  return 0;
}