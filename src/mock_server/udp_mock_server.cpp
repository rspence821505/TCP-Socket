#include <arpa/inet.h>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <random>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "binary_protocol.hpp"
#include "udp_protocol.hpp"

volatile sig_atomic_t keep_running = 1;
void signal_handler(int) { keep_running = 0; }

class UDPMockServer {
private:
  int udp_fd_;
  int tcp_control_fd_;
  int udp_port_;
  int tcp_port_;
  
  std::mt19937 rng_;
  std::vector<std::string> symbols_;
  
  // Message cache for retransmits (sequence -> serialized message)
  std::map<uint64_t, std::string> message_cache_;
  uint64_t sequence_number_;
  
  // Packet loss simulation
  PacketLossConfig loss_config_;
  uint32_t burst_counter_;
  
  // Statistics
  uint64_t messages_sent_;
  uint64_t packets_dropped_;
  uint64_t retransmits_sent_;
  
public:
  UDPMockServer(int udp_port, int tcp_port, const PacketLossConfig& loss_config = PacketLossConfig())
    : udp_port_(udp_port)
    , tcp_port_(tcp_port)
    , rng_(std::random_device{}())
    , sequence_number_(0)
    , loss_config_(loss_config)
    , burst_counter_(0)
    , messages_sent_(0)
    , packets_dropped_(0)
    , retransmits_sent_(0)
  {
    symbols_ = {"AAPL", "MSFT", "GOOG", "AMZN", "TSLA", "META", "NVDA", "JPM "};
  }
  
  bool start() {
    // Create UDP socket
    udp_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd_ < 0) {
      perror("UDP socket creation failed");
      return false;
    }
    
    // Create TCP control socket
    tcp_control_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_control_fd_ < 0) {
      perror("TCP socket creation failed");
      close(udp_fd_);
      return false;
    }
    
    // Set TCP socket to reuse address
    int opt = 1;
    setsockopt(tcp_control_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind TCP control socket
    struct sockaddr_in tcp_addr;
    memset(&tcp_addr, 0, sizeof(tcp_addr));
    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = INADDR_ANY;
    tcp_addr.sin_port = htons(tcp_port_);
    
    if (bind(tcp_control_fd_, (struct sockaddr*)&tcp_addr, sizeof(tcp_addr)) < 0) {
      perror("TCP bind failed");
      close(udp_fd_);
      close(tcp_control_fd_);
      return false;
    }
    
    if (listen(tcp_control_fd_, 5) < 0) {
      perror("TCP listen failed");
      close(udp_fd_);
      close(tcp_control_fd_);
      return false;
    }
    
    std::cout << "📡 UDP Mock Server started" << std::endl;
    std::cout << "  UDP feed port: " << udp_port_ << std::endl;
    std::cout << "  TCP control port: " << tcp_port_ << std::endl;
    std::cout << "  Packet loss rate: " << (loss_config_.loss_rate * 100.0) << "%" << std::endl;
    if (loss_config_.burst_size > 1) {
      std::cout << "  Burst loss: " << loss_config_.burst_size << " packets @ " 
                << (loss_config_.burst_probability * 100.0) << "% probability" << std::endl;
    }
    
    return true;
  }
  
  void run() {
    // Start TCP control thread
    std::thread control_thread([this]() { handle_control_connections(); });
    
    // Wait for client to connect to TCP control channel first
    std::cout << "Waiting for client connection on control channel..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Run UDP feed
    send_udp_feed();
    
    keep_running = 0;
    control_thread.join();
    
    print_statistics();
  }
  
private:
  void send_udp_feed() {
    std::cout << "Starting UDP feed..." << std::endl;
    
    // Client address (will be set when client connects to TCP)
    // For simplicity, we'll broadcast to localhost:udp_port
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    client_addr.sin_port = htons(udp_port_);
    
    auto start_time = std::chrono::steady_clock::now();
    
    while (keep_running && messages_sent_ < 50000) {
      // Generate tick
      BinaryTick tick = generate_tick();
      std::string message = serialize_tick(sequence_number_, tick.timestamp, 
                                          tick.symbol, tick.price, tick.volume);
      
      // Cache for retransmits (keep last 10k messages)
      message_cache_[sequence_number_] = message;
      if (message_cache_.size() > 10000) {
        message_cache_.erase(message_cache_.begin());
      }
      
      // Simulate packet loss
      if (should_drop_packet()) {
        packets_dropped_++;
        
        if (messages_sent_ % 1000 == 0) {
          std::cout << "[UDP] Dropped packet seq=" << sequence_number_ << std::endl;
        }
      } else {
        // Send UDP packet
        ssize_t sent = sendto(udp_fd_, message.data(), message.length(), 0,
                             (struct sockaddr*)&client_addr, sizeof(client_addr));
        
        if (sent < 0) {
          perror("sendto failed");
          break;
        }
      }
      
      sequence_number_++;
      messages_sent_++;
      
      // Throttle to ~10k msgs/sec
      if (messages_sent_ % 100 == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time
    );
    double seconds = duration.count() / 1000.0;
    
    std::cout << "[UDP] Sent " << messages_sent_ << " messages in " << seconds << "s ("
              << static_cast<int>(messages_sent_ / seconds) << " msgs/sec)" << std::endl;
  }
  
  void handle_control_connections() {
    while (keep_running) {
      struct sockaddr_in client_addr;
      socklen_t client_len = sizeof(client_addr);
      
      int client_fd = accept(tcp_control_fd_, (struct sockaddr*)&client_addr, &client_len);
      if (client_fd < 0) {
        if (keep_running) {
          perror("accept failed");
        }
        continue;
      }
      
      std::cout << "[TCP Control] Client connected from " 
                << inet_ntoa(client_addr.sin_addr) << std::endl;
      
      // Handle retransmit requests
      handle_retransmit_requests(client_fd);
      
      close(client_fd);
    }
  }
  
  void handle_retransmit_requests(int client_fd) {
    char buffer[1024];
    
    while (keep_running) {
      ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);
      
      if (bytes_read <= 0) {
        break;  // Client disconnected
      }
      
      // Parse header
      if (bytes_read < static_cast<ssize_t>(MessageHeader::HEADER_SIZE)) {
        continue;
      }
      
      MessageHeader header = deserialize_header(buffer);
      
      if (static_cast<UDPMessageType>(header.type) == UDPMessageType::RETRANSMIT_REQUEST) {
        RetransmitRequest request = deserialize_retransmit_request(
          buffer + MessageHeader::HEADER_SIZE
        );
        
        std::cout << "[TCP Control] Retransmit request: seq " << request.start_sequence
                  << " to " << request.end_sequence << std::endl;
        
        // Send requested messages
        for (uint64_t seq = request.start_sequence; seq <= request.end_sequence; ++seq) {
          auto it = message_cache_.find(seq);
          if (it != message_cache_.end()) {
            ssize_t sent = send(client_fd, it->second.data(), it->second.length(), 0);
            if (sent < 0) {
              perror("retransmit send failed");
              return;
            }
            retransmits_sent_++;
          }
        }
      }
    }
  }
  
  bool should_drop_packet() {
    // Burst loss logic
    if (burst_counter_ > 0) {
      burst_counter_--;
      return true;
    }
    
    // Check if we should start a burst
    if (loss_config_.burst_size > 1) {
      std::uniform_real_distribution<double> burst_dist(0.0, 1.0);
      if (burst_dist(rng_) < loss_config_.burst_probability) {
        burst_counter_ = loss_config_.burst_size - 1;  // -1 because we're dropping this one
        return true;
      }
    }
    
    // Random loss
    std::uniform_real_distribution<double> loss_dist(0.0, 1.0);
    return loss_dist(rng_) < loss_config_.loss_rate;
  }
  
  BinaryTick generate_tick() {
    BinaryTick tick;
    
    tick.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    
    std::uniform_int_distribution<size_t> symbol_dist(0, symbols_.size() - 1);
    const std::string& symbol_str = symbols_[symbol_dist(rng_)];
    memcpy(tick.symbol, symbol_str.c_str(), 4);
    
    std::uniform_real_distribution<float> price_dist(100.0f, 500.0f);
    tick.price = price_dist(rng_);
    
    std::uniform_int_distribution<int32_t> volume_dist(100, 10000);
    tick.volume = volume_dist(rng_);
    
    return tick;
  }
  
  void print_statistics() {
    std::cout << "\n=== UDP Server Statistics ===" << std::endl;
    std::cout << "Messages sent: " << messages_sent_ << std::endl;
    std::cout << "Packets dropped: " << packets_dropped_ 
              << " (" << (100.0 * packets_dropped_ / messages_sent_) << "%)" << std::endl;
    std::cout << "Retransmits sent: " << retransmits_sent_ << std::endl;
  }
};

int main(int argc, char* argv[]) {
  signal(SIGINT, signal_handler);
  
  int udp_port = 9998;
  int tcp_port = 9999;
  double loss_rate = 0.01;  // 1% packet loss
  
  if (argc > 1) {
    udp_port = std::atoi(argv[1]);
  }
  if (argc > 2) {
    tcp_port = std::atoi(argv[2]);
  }
  if (argc > 3) {
    loss_rate = std::atof(argv[3]);
  }
  
  PacketLossConfig loss_config(loss_rate, 3, 0.002);  // 1% random + occasional 3-packet bursts
  
  UDPMockServer server(udp_port, tcp_port, loss_config);
  
  if (!server.start()) {
    return 1;
  }
  
  server.run();
  
  std::cout << "\nServer shutting down..." << std::endl;
  return 0;
}
