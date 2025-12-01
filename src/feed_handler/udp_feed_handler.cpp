#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <algorithm>
#include <numeric>
#include <vector>

#include "binary_protocol.hpp"
#include "common.hpp"
#include "udp_protocol.hpp"

// Statistics
struct UDPFeedStats {
  uint64_t messages_received = 0;
  uint64_t gaps_detected = 0;
  uint64_t retransmit_requests_sent = 0;
  uint64_t gaps_filled = 0;
  uint64_t duplicates = 0;
  
  std::vector<uint64_t> latencies_ns;
  
  void reserve_latencies(size_t n) {
    latencies_ns.reserve(n);
  }
  
  void add_latency(uint64_t latency_ns) {
    latencies_ns.push_back(latency_ns);
  }
  
  void print() const {
    std::cout << "\n=== UDP Feed Handler Statistics ===" << std::endl;
    std::cout << "Messages received:        " << messages_received << std::endl;
    std::cout << "Gaps detected:            " << gaps_detected << std::endl;
    std::cout << "Gaps filled (retransmit): " << gaps_filled << std::endl;
    std::cout << "Duplicate messages:       " << duplicates << std::endl;
    std::cout << "Retransmit requests sent: " << retransmit_requests_sent << std::endl;
    
    double loss_rate = 100.0 * gaps_detected / (messages_received + gaps_detected);
    std::cout << "Effective packet loss:    " << loss_rate << "%" << std::endl;
    
    if (!latencies_ns.empty()) {
      std::vector<uint64_t> sorted = latencies_ns;
      std::sort(sorted.begin(), sorted.end());
      
      uint64_t sum = std::accumulate(sorted.begin(), sorted.end(), 0ULL);
      double mean_ns = static_cast<double>(sum) / sorted.size();
      
      uint64_t p50 = sorted[sorted.size() * 50 / 100];
      uint64_t p95 = sorted[sorted.size() * 95 / 100];
      uint64_t p99 = sorted[sorted.size() * 99 / 100];
      
      std::cout << "\nLatency (recv → processed):" << std::endl;
      std::cout << "  Mean: " << mean_ns / 1000.0 << " µs" << std::endl;
      std::cout << "  p50:  " << p50 / 1000.0 << " µs" << std::endl;
      std::cout << "  p95:  " << p95 / 1000.0 << " µs" << std::endl;
      std::cout << "  p99:  " << p99 / 1000.0 << " µs" << std::endl;
    }
  }
};

class UDPFeedHandler {
public:
  UDPFeedHandler(const std::string& host, int udp_port, int tcp_port)
    : host_(host)
    , udp_port_(udp_port)
    , tcp_port_(tcp_port)
    , udp_fd_(-1)
    , tcp_fd_(-1)
    , should_stop_(false)
  {
    stats_.reserve_latencies(100000);
  }
  
  ~UDPFeedHandler() {
    stop();
  }
  
  bool start() {
    // Create UDP socket
    udp_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd_ < 0) {
      LOG_PERROR("UDPFeed", "UDP socket creation failed");
      return false;
    }

    // Bind UDP socket to receive on specific port
    struct sockaddr_in udp_addr;
    memset(&udp_addr, 0, sizeof(udp_addr));
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_addr.s_addr = INADDR_ANY;
    udp_addr.sin_port = htons(udp_port_);

    if (bind(udp_fd_, (struct sockaddr*)&udp_addr, sizeof(udp_addr)) < 0) {
      LOG_PERROR("UDPFeed", "UDP bind failed");
      close(udp_fd_);
      return false;
    }

    // Set UDP socket to non-blocking
    int flags = fcntl(udp_fd_, F_GETFL, 0);
    fcntl(udp_fd_, F_SETFL, flags | O_NONBLOCK);

    // Connect to TCP control channel
    SocketOptions tcp_opts;
    tcp_opts.non_blocking = true;

    auto tcp_result = socket_connect(host_, tcp_port_, tcp_opts);
    if (!tcp_result) {
      LOG_ERROR("UDPFeed", "TCP connection failed: %s", tcp_result.error().c_str());
      close(udp_fd_);
      return false;
    }
    tcp_fd_ = tcp_result.value();

    LOG_INFO("UDPFeed", "UDP Feed Handler started");
    LOG_INFO("UDPFeed", "  UDP feed: %s:%d", host_.c_str(), udp_port_);
    LOG_INFO("UDPFeed", "  TCP control: %s:%d", host_.c_str(), tcp_port_);

    return true;
  }
  
  void run(std::chrono::seconds duration) {
    LOG_INFO("UDPFeed", "Receiving UDP feed for %ld seconds...", duration.count());
    
    auto start_time = std::chrono::steady_clock::now();
    auto last_gap_check = start_time;
    
    while (!should_stop_) {
      auto now = std::chrono::steady_clock::now();
      
      // Check if we've run long enough
      if (now - start_time > duration) {
        break;
      }
      
      // Receive UDP packets
      receive_udp_packets();
      
      // Periodically check for gaps and request retransmits
      if (now - last_gap_check > std::chrono::seconds(1)) {
        request_retransmits();
        last_gap_check = now;
      }
      
      // Receive retransmitted packets on TCP
      receive_tcp_retransmits();
      
      // Small sleep to avoid busy loop
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    // Final gap check
    request_retransmits();
    
    // Wait a bit for final retransmits
    auto final_wait_start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - final_wait_start < std::chrono::seconds(2)) {
      receive_tcp_retransmits();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    stats_.print();
  }
  
  void stop() {
    should_stop_ = true;
    if (udp_fd_ >= 0) {
      close(udp_fd_);
      udp_fd_ = -1;
    }
    if (tcp_fd_ >= 0) {
      close(tcp_fd_);
      tcp_fd_ = -1;
    }
  }
  
  const UDPFeedStats& get_stats() const { return stats_; }
  
private:
  void receive_udp_packets() {
    char buffer[2048];
    
    while (true) {
      uint64_t recv_timestamp = now_ns();
      
      ssize_t bytes_read = recvfrom(udp_fd_, buffer, sizeof(buffer), 0, nullptr, nullptr);
      
      if (bytes_read < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;  // No more data
        } else {
          LOG_PERROR("UDPFeed", "UDP recvfrom failed");
          break;
        }
      }

      // Parse message
      if (bytes_read < static_cast<ssize_t>(MessageHeader::HEADER_SIZE)) {
        LOG_ERROR("UDPFeed", "Incomplete message received");
        continue;
      }
      
      MessageHeader header = deserialize_header(buffer);
      
      if (header.type == MessageType::TICK) {
        // Process sequence number
        bool expected = gap_tracker_.process_sequence(header.sequence);
        
        if (!expected) {
          // This could be a gap or a late arrival
          if (header.sequence < gap_tracker_.last_sequence()) {
            // Late arrival or retransmit - might fill a gap
            stats_.gaps_filled++;
          } else {
            // New gap detected
            stats_.gaps_detected += (header.sequence - gap_tracker_.last_sequence() - 1);
          }
        }
        
        // Deserialize tick
        const char* payload = buffer + MessageHeader::HEADER_SIZE;
        TickPayload tick = deserialize_tick_payload(payload);
        
        // Record latency
        uint64_t process_timestamp = now_ns();
        stats_.add_latency(process_timestamp - recv_timestamp);
        
        stats_.messages_received++;
        
        // Print periodically
        if (stats_.messages_received % 10000 == 0) {
          std::string symbol(tick.symbol, 4);
          size_t null_pos = symbol.find('\0');
          if (null_pos != std::string::npos) {
            symbol = symbol.substr(0, null_pos);
          }

          LOG_INFO("UDP", "seq=%lu [%s] $%.2f @ %d | Active gaps: %zu",
                   header.sequence, symbol.c_str(), tick.price, tick.volume, gap_tracker_.active_gaps());
        }
      }
    }
  }
  
  void receive_tcp_retransmits() {
    char buffer[2048];
    
    while (true) {
      ssize_t bytes_read = recv(tcp_fd_, buffer, sizeof(buffer), 0);
      
      if (bytes_read < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;  // No more data
        } else {
          LOG_PERROR("UDPFeed", "TCP recv failed");
          break;
        }
      } else if (bytes_read == 0) {
        LOG_ERROR("UDPFeed", "TCP control channel closed");
        should_stop_ = true;
        break;
      }
      
      // Append to TCP buffer
      tcp_buffer_.insert(tcp_buffer_.end(), buffer, buffer + bytes_read);
      
      // Process complete messages
      while (tcp_buffer_.size() >= MessageHeader::HEADER_SIZE) {
        MessageHeader header = deserialize_header(tcp_buffer_.data());
        
        size_t total_size = MessageHeader::HEADER_SIZE + header.length;
        if (tcp_buffer_.size() < total_size) {
          break;  // Need more data
        }
        
        if (header.type == MessageType::TICK) {
          // This is a retransmitted tick
          const char* payload = tcp_buffer_.data() + MessageHeader::HEADER_SIZE;
          TickPayload tick = deserialize_tick_payload(payload);
          
          // Process sequence (should fill a gap)
          bool filled_gap = gap_tracker_.process_sequence(header.sequence);
          
          if (filled_gap) {
            stats_.gaps_filled++;

            if (stats_.gaps_filled % 100 == 0) {
              LOG_INFO("TCP", "Retransmit seq=%lu Gap filled! Remaining gaps: %zu",
                       header.sequence, gap_tracker_.active_gaps());
            }
          } else {
            stats_.duplicates++;
          }
          
          stats_.messages_received++;
        }
        
        tcp_buffer_.erase(tcp_buffer_.begin(), tcp_buffer_.begin() + total_size);
      }
    }
  }
  
  void request_retransmits() {
    auto gap_ranges = gap_tracker_.get_gap_ranges();
    
    if (gap_ranges.empty()) {
      return;
    }
    
    // Limit retransmit requests (don't spam)
    size_t max_requests = 10;
    size_t requests_sent = 0;
    
    for (const auto& [start, end] : gap_ranges) {
      if (requests_sent >= max_requests) {
        break;
      }

      LOG_INFO("Retransmit", "Requesting seq %lu to %lu", start, end);

      std::string request = serialize_retransmit_request(start, end);

      ssize_t sent = send(tcp_fd_, request.data(), request.length(), 0);
      if (sent < 0) {
        LOG_PERROR("Retransmit", "Failed to send retransmit request");
        break;
      }

      stats_.retransmit_requests_sent++;
      requests_sent++;
    }
  }
  
  std::string host_;
  int udp_port_;
  int tcp_port_;
  int udp_fd_;
  int tcp_fd_;
  
  SequenceGapTracker gap_tracker_;
  UDPFeedStats stats_;
  
  std::vector<char> tcp_buffer_;
  std::atomic<bool> should_stop_;
};

int main(int argc, char* argv[]) {
  std::string host = "127.0.0.1";
  int udp_port = 9998;
  int tcp_port = 9999;
  int duration_seconds = 30;
  
  if (argc > 1) {
    udp_port = std::atoi(argv[1]);
  }
  if (argc > 2) {
    tcp_port = std::atoi(argv[2]);
  }
  if (argc > 3) {
    duration_seconds = std::atoi(argv[3]);
  }
  
  UDPFeedHandler handler(host, udp_port, tcp_port);
  
  if (!handler.start()) {
    return 1;
  }
  
  handler.run(std::chrono::seconds(duration_seconds));
  
  return 0;
}
