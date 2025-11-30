#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <numeric>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#include "binary_protocol.hpp"
#include "socket_config.hpp"

// Helper: Get current timestamp in nanoseconds
inline uint64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

// Simple latency statistics
struct LatencyStats {
  std::vector<uint64_t> latencies_ns;

  void reserve(size_t n) { latencies_ns.reserve(n); }

  void add(uint64_t latency_ns) { latencies_ns.push_back(latency_ns); }

  void compute_and_print(const std::string &label) const {
    if (latencies_ns.empty()) {
      std::cout << label << ": No data" << std::endl;
      return;
    }

    std::vector<uint64_t> sorted = latencies_ns;
    std::sort(sorted.begin(), sorted.end());

    uint64_t sum = std::accumulate(sorted.begin(), sorted.end(), 0ULL);
    double mean_ns = static_cast<double>(sum) / sorted.size();

    uint64_t p50 = sorted[sorted.size() * 50 / 100];
    uint64_t p95 = sorted[sorted.size() * 95 / 100];
    uint64_t p99 = sorted[sorted.size() * 99 / 100];
    uint64_t max = sorted.back();

    std::cout << label << ":" << std::endl;
    std::cout << "  Mean: " << mean_ns / 1000.0 << " µs" << std::endl;
    std::cout << "  p50:  " << p50 / 1000.0 << " µs" << std::endl;
    std::cout << "  p95:  " << p95 / 1000.0 << " µs" << std::endl;
    std::cout << "  p99:  " << p99 / 1000.0 << " µs" << std::endl;
    std::cout << "  Max:  " << max / 1000.0 << " µs" << std::endl;
  }

  // CSV output for easy comparison
  void print_csv_line(const std::string &config_name) const {
    if (latencies_ns.empty())
      return;

    std::vector<uint64_t> sorted = latencies_ns;
    std::sort(sorted.begin(), sorted.end());

    uint64_t sum = std::accumulate(sorted.begin(), sorted.end(), 0ULL);
    double mean_ns = static_cast<double>(sum) / sorted.size();
    uint64_t p50 = sorted[sorted.size() * 50 / 100];
    uint64_t p95 = sorted[sorted.size() * 95 / 100];
    uint64_t p99 = sorted[sorted.size() * 99 / 100];
    uint64_t max = sorted.back();

    std::cout << config_name << "," << mean_ns / 1000.0 << "," << p50 / 1000.0
              << "," << p95 / 1000.0 << "," << p99 / 1000.0 << ","
              << max / 1000.0 << std::endl;
  }
};

// Connect to server with specific socket configuration
int connect_with_config(const std::string &host, int port,
                        const SocketConfig &config, bool verbose = true) {
  // 1. Create socket
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("socket creation failed");
    return -1;
  }

  // 2. Apply socket configuration BEFORE connect
  // This is important for buffer sizes - set before connection established
  if (!apply_socket_config(sockfd, config, verbose)) {
    close(sockfd);
    return -1;
  }

  // 3. Set up server address
  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = inet_addr(host.c_str());

  // 4. Connect to server
  if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("connection failed");
    close(sockfd);
    return -1;
  }

  // 5. Set non-blocking mode
  int flags = fcntl(sockfd, F_GETFL, 0);
  if (flags == -1 || fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("fcntl failed");
    close(sockfd);
    return -1;
  }

  if (verbose) {
    std::cout << "  ✓ Connected to " << host << ":" << port << std::endl;
  }

  return sockfd;
}

// Run benchmark with specific socket configuration
LatencyStats run_benchmark(const std::string &host, int port,
                           const SocketConfig &config, size_t num_messages,
                           bool verbose = true) {
  if (verbose) {
    std::cout << "\n=== Testing: " << config.to_string() << " ===" << std::endl;
  }

  LatencyStats stats;
  stats.reserve(num_messages);

  // Connect to server
  int sockfd = connect_with_config(host, port, config, verbose);
  if (sockfd < 0) {
    return stats;
  }

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
        perror("recv failed");
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
    while (buffer.size() >= 4) {
      // Read length prefix
      uint32_t length_net;
      memcpy(&length_net, buffer.data(), 4);
      uint32_t length = ntohl(length_net);

      // Validate length
      if (length != BinaryTick::PAYLOAD_SIZE) {
        std::cerr << "Invalid message length: " << length << std::endl;
        close(sockfd);
        return stats;
      }

      // Check if complete message is available
      size_t total_size = 4 + length;
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
      std::cerr << "Unknown option: " << arg << std::endl;
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
  LatencyStats stats = run_benchmark(host, port, config, num_messages, verbose);

  // Print results
  if (csv_output) {
    // CSV header if this is the first line
    // Format: config,mean,p50,p95,p99,max (all in µs)
    stats.print_csv_line(config.short_name());
  } else {
    stats.compute_and_print("Latency (recv → processed)");
  }

  return 0;
}