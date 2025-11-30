#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <numeric>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "binary_protocol.hpp"
#include "socket_config.hpp"
#include "udp_protocol.hpp"

// Helper: Get current timestamp in nanoseconds
inline uint64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

// Latency statistics
struct LatencyStats {
  std::vector<uint64_t> latencies_ns;
  uint64_t messages_received = 0;
  uint64_t gaps_detected = 0;

  void add(uint64_t latency_ns) { latencies_ns.push_back(latency_ns); }

  void print_summary(const std::string &protocol) const {
    if (latencies_ns.empty()) {
      std::cout << protocol << ": No data" << std::endl;
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

    std::cout << protocol << ":" << std::endl;
    std::cout << "  Messages: " << messages_received;
    if (gaps_detected > 0) {
      std::cout << " (gaps: " << gaps_detected << ")";
    }
    std::cout << std::endl;
    std::cout << "  Mean: " << mean_ns / 1000.0 << " µs" << std::endl;
    std::cout << "  p50:  " << p50 / 1000.0 << " µs" << std::endl;
    std::cout << "  p95:  " << p95 / 1000.0 << " µs" << std::endl;
    std::cout << "  p99:  " << p99 / 1000.0 << " µs" << std::endl;
    std::cout << "  Max:  " << max / 1000.0 << " µs" << std::endl;
  }

  void to_csv_line(const std::string &protocol) const {
    if (latencies_ns.empty())
      return;

    std::vector<uint64_t> sorted = latencies_ns;
    std::sort(sorted.begin(), sorted.end());

    uint64_t sum = std::accumulate(sorted.begin(), sorted.end(), 0ULL);
    double mean_ns = static_cast<double>(sum) / sorted.size();
    uint64_t p50 = sorted[sorted.size() * 50 / 100];
    uint64_t p95 = sorted[sorted.size() * 95 / 100];
    uint64_t p99 = sorted[sorted.size() * 99 / 100];

    double loss_rate = 100.0 * gaps_detected / messages_received;

    std::cout << protocol << "," << mean_ns / 1000.0 << "," << p50 / 1000.0
              << "," << p95 / 1000.0 << "," << p99 / 1000.0 << ","
              << messages_received << "," << gaps_detected << "," << loss_rate
              << std::endl;
  }
};

// TCP benchmark
LatencyStats run_tcp_benchmark(const std::string &host, int port,
                               const SocketConfig &config, size_t num_messages) {
  std::cout << "\n=== TCP Benchmark ===" << std::endl;
  std::cout << "Config: " << config.to_string() << std::endl;

  LatencyStats stats;

  // Connect
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("socket creation failed");
    return stats;
  }

  apply_socket_config(sockfd, config, false);

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = inet_addr(host.c_str());

  if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("connection failed");
    close(sockfd);
    return stats;
  }

  // Set non-blocking
  int flags = fcntl(sockfd, F_GETFL, 0);
  fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

  std::cout << "Connected to TCP server" << std::endl;

  std::vector<char> buffer;
  buffer.reserve(64 * 1024);

  auto start_time = std::chrono::steady_clock::now();
  SequenceGapTracker gap_tracker;

  while (stats.messages_received < num_messages) {
    uint64_t recv_start = now_ns();

    char temp[4096];
    ssize_t bytes_read = recv(sockfd, temp, sizeof(temp), 0);

    if (bytes_read < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      } else {
        perror("recv failed");
        break;
      }
    } else if (bytes_read == 0) {
      std::cout << "Connection closed" << std::endl;
      break;
    }

    buffer.insert(buffer.end(), temp, temp + bytes_read);

    while (buffer.size() >= MessageHeader::HEADER_SIZE) {
      // Parse the full header first
      MessageHeader header = deserialize_header(buffer.data());

      if (header.length != TickPayload::PAYLOAD_SIZE) {
        std::cerr << "Invalid message length: " << header.length
                  << " (expected " << TickPayload::PAYLOAD_SIZE << ")" << std::endl;
        close(sockfd);
        return stats;
      }

      size_t total_size = MessageHeader::HEADER_SIZE + header.length;
      if (buffer.size() < total_size) {
        break;
      }

      // Check sequence
      gap_tracker.process_sequence(header.sequence);

      uint64_t process_end = now_ns();
      stats.add(process_end - recv_start);

      buffer.erase(buffer.begin(), buffer.begin() + total_size);
      stats.messages_received++;

      if (stats.messages_received % 10000 == 0) {
        std::cout << "  Received " << stats.messages_received << " messages\r"
                  << std::flush;
      }
    }
  }

  auto end_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  stats.gaps_detected = gap_tracker.total_gaps_detected();

  std::cout << "\nTCP test completed in " << duration.count() / 1000.0 << "s"
            << std::endl;

  close(sockfd);
  return stats;
}

// UDP benchmark with gap tracking
LatencyStats run_udp_benchmark(const std::string &host, int udp_port,
                               int tcp_port, size_t num_messages,
                               std::chrono::seconds timeout) {
  std::cout << "\n=== UDP Benchmark ===" << std::endl;

  LatencyStats stats;

  // Create UDP socket
  int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (udp_fd < 0) {
    perror("UDP socket creation failed");
    return stats;
  }

  // Bind UDP socket
  struct sockaddr_in udp_addr;
  memset(&udp_addr, 0, sizeof(udp_addr));
  udp_addr.sin_family = AF_INET;
  udp_addr.sin_addr.s_addr = INADDR_ANY;
  udp_addr.sin_port = htons(udp_port);

  if (bind(udp_fd, (struct sockaddr *)&udp_addr, sizeof(udp_addr)) < 0) {
    perror("UDP bind failed");
    close(udp_fd);
    return stats;
  }

  // Set non-blocking
  int flags = fcntl(udp_fd, F_GETFL, 0);
  fcntl(udp_fd, F_SETFL, flags | O_NONBLOCK);

  // Connect to TCP control channel
  int tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (tcp_fd < 0) {
    perror("TCP socket creation failed");
    close(udp_fd);
    return stats;
  }

  struct sockaddr_in tcp_addr;
  memset(&tcp_addr, 0, sizeof(tcp_addr));
  tcp_addr.sin_family = AF_INET;
  tcp_addr.sin_addr.s_addr = inet_addr(host.c_str());
  tcp_addr.sin_port = htons(tcp_port);

  if (connect(tcp_fd, (struct sockaddr *)&tcp_addr, sizeof(tcp_addr)) < 0) {
    perror("TCP connection failed");
    close(udp_fd);
    close(tcp_fd);
    return stats;
  }

  flags = fcntl(tcp_fd, F_GETFL, 0);
  fcntl(tcp_fd, F_SETFL, flags | O_NONBLOCK);

  std::cout << "Listening on UDP port " << udp_port << std::endl;
  std::cout << "Control channel connected to TCP port " << tcp_port << std::endl;

  SequenceGapTracker gap_tracker;
  std::vector<char> tcp_buffer;
  auto start_time = std::chrono::steady_clock::now();
  auto last_retransmit_request = start_time;

  while (stats.messages_received < num_messages) {
    auto now = std::chrono::steady_clock::now();

    if (now - start_time > timeout) {
      std::cout << "\nTimeout reached" << std::endl;
      break;
    }

    // Receive UDP packets
    char buffer[2048];
    while (true) {
      uint64_t recv_timestamp = now_ns();

      ssize_t bytes_read =
          recvfrom(udp_fd, buffer, sizeof(buffer), 0, nullptr, nullptr);

      if (bytes_read < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        } else {
          perror("UDP recvfrom failed");
          break;
        }
      }

      if (bytes_read >= static_cast<ssize_t>(MessageHeader::HEADER_SIZE)) {
        MessageHeader header = deserialize_header(buffer);

        if (header.type == MessageType::TICK) {
          gap_tracker.process_sequence(header.sequence);

          uint64_t process_timestamp = now_ns();
          stats.add(process_timestamp - recv_timestamp);
          stats.messages_received++;

          if (stats.messages_received % 10000 == 0) {
            std::cout << "  Received " << stats.messages_received
                      << " messages (gaps: " << gap_tracker.active_gaps()
                      << ")\r" << std::flush;
          }
        }
      }
    }

    // Request retransmits periodically
    if (now - last_retransmit_request > std::chrono::seconds(1)) {
      auto gap_ranges = gap_tracker.get_gap_ranges();

      for (size_t i = 0; i < std::min(gap_ranges.size(), size_t(5)); ++i) {
        auto [start, end] = gap_ranges[i];
        std::string request = serialize_retransmit_request(start, end);
        send(tcp_fd, request.data(), request.length(), 0);
      }

      last_retransmit_request = now;
    }

    // Receive retransmits on TCP
    char tcp_temp[2048];
    while (true) {
      ssize_t bytes_read = recv(tcp_fd, tcp_temp, sizeof(tcp_temp), 0);

      if (bytes_read <= 0) {
        break;
      }

      tcp_buffer.insert(tcp_buffer.end(), tcp_temp, tcp_temp + bytes_read);

      while (tcp_buffer.size() >= MessageHeader::HEADER_SIZE) {
        MessageHeader header = deserialize_header(tcp_buffer.data());

        size_t total_size = MessageHeader::HEADER_SIZE + header.length;
        if (tcp_buffer.size() < total_size) {
          break;
        }

        if (header.type == MessageType::TICK) {
          gap_tracker.process_sequence(header.sequence);
          stats.messages_received++;
        }

        tcp_buffer.erase(tcp_buffer.begin(), tcp_buffer.begin() + total_size);
      }
    }

    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  std::cout << "\nUDP test completed" << std::endl;

  stats.gaps_detected = gap_tracker.total_gaps_detected();

  close(udp_fd);
  close(tcp_fd);
  return stats;
}

void print_comparison(const LatencyStats &tcp_stats,
                      const LatencyStats &udp_stats) {
  std::cout << "\n==================================================================="
            << std::endl;
  std::cout << "TCP vs UDP COMPARISON" << std::endl;
  std::cout << "==================================================================="
            << std::endl;

  tcp_stats.print_summary("TCP");
  std::cout << std::endl;
  udp_stats.print_summary("UDP");

  // Calculate improvements
  if (!tcp_stats.latencies_ns.empty() && !udp_stats.latencies_ns.empty()) {
    auto tcp_sorted = tcp_stats.latencies_ns;
    auto udp_sorted = udp_stats.latencies_ns;
    std::sort(tcp_sorted.begin(), tcp_sorted.end());
    std::sort(udp_sorted.begin(), udp_sorted.end());

    uint64_t tcp_p99 = tcp_sorted[tcp_sorted.size() * 99 / 100];
    uint64_t udp_p99 = udp_sorted[udp_sorted.size() * 99 / 100];

    double improvement = 100.0 * (tcp_p99 - udp_p99) / tcp_p99;

    std::cout << "\n=== Analysis ===" << std::endl;
    std::cout << "UDP p99 latency improvement: " << improvement << "%"
              << std::endl;

    if (udp_stats.gaps_detected > 0) {
      double loss_rate =
          100.0 * udp_stats.gaps_detected / udp_stats.messages_received;
      std::cout << "UDP packet loss rate: " << loss_rate << "%" << std::endl;
      std::cout << "Trade-off: " << improvement << "% lower latency at cost of "
                << loss_rate << "% packet loss + retransmit complexity"
                << std::endl;
    }
  }
}

int main(int argc, char *argv[]) {
  std::string host = "127.0.0.1";
  int tcp_port = 9999;
  int udp_port = 9998;
  int tcp_control_port = 9999;
  size_t num_messages = 10000;
  bool csv_output = false;
  bool tcp_only = false;

  // Parse arguments
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--tcp-port" && i + 1 < argc) {
      tcp_port = std::atoi(argv[++i]);
    } else if (arg == "--udp-port" && i + 1 < argc) {
      udp_port = std::atoi(argv[++i]);
    } else if (arg == "--messages" && i + 1 < argc) {
      num_messages = std::atoi(argv[++i]);
    } else if (arg == "--csv") {
      csv_output = true;
    } else if (arg == "--tcp-only") {
      tcp_only = true;
    }
  }

  std::cout << "=== TCP vs UDP Benchmark (Exercise 6 Extension) ===" << std::endl;
  std::cout << "Messages per test: " << num_messages << std::endl;

  // TCP benchmark with TCP_NODELAY
  SocketConfig tcp_config(true, 65536, 65536, false);
  LatencyStats tcp_stats =
      run_tcp_benchmark(host, tcp_port, tcp_config, num_messages);

  if (tcp_only) {
    // Print TCP-only results
    tcp_stats.print_summary("TCP");
    return 0;
  }

  std::cout << "\nWaiting 5 seconds before UDP test..." << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(5));

  // UDP benchmark
  LatencyStats udp_stats = run_udp_benchmark(
      host, udp_port, tcp_control_port, num_messages, std::chrono::seconds(60));

  if (csv_output) {
    std::cout << "\nprotocol,mean_us,p50_us,p95_us,p99_us,messages,gaps,loss_"
                 "rate"
              << std::endl;
    tcp_stats.to_csv_line("TCP");
    udp_stats.to_csv_line("UDP");
  } else {
    print_comparison(tcp_stats, udp_stats);
  }

  return 0;
}
