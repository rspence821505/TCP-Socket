#include <arpa/inet.h>
#include <chrono>
#include <csignal>
#include <cstring>
#include <netinet/in.h>
#include <random>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "binary_protocol.hpp"
#include "common.hpp"

// Global flag for graceful shutdown
volatile sig_atomic_t keep_running = 1;

void signal_handler(int) { keep_running = 0; }

class HeartbeatMockServer {
private:
  int server_fd;
  int port;
  std::vector<std::string> symbols;
  std::mt19937 rng;
  uint64_t sequence_number_;
  
  // Configuration
  int heartbeat_interval_ms_;  // Send heartbeat every N milliseconds
  int messages_per_heartbeat_;  // Send N tick messages between heartbeats

public:
  HeartbeatMockServer(int port, int heartbeat_interval_ms = 1000, int messages_per_heartbeat = 10)
    : port(port), rng(std::random_device{}()), sequence_number_(0)
    , heartbeat_interval_ms_(heartbeat_interval_ms)
    , messages_per_heartbeat_(messages_per_heartbeat) {
    // Initialize some stock symbols (4 chars)
    symbols = {"AAPL", "MSFT", "GOOG", "AMZN", "TSLA", "META", "NVDA", "JPM "};
  }

  bool start() {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
      LOG_PERROR("Server", "socket creation failed");
      return false;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
      LOG_PERROR("Server", "setsockopt failed");
      close(server_fd);
      return false;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
      LOG_PERROR("Server", "bind failed");
      close(server_fd);
      return false;
    }

    if (listen(server_fd, 5) < 0) {
      LOG_PERROR("Server", "listen failed");
      close(server_fd);
      return false;
    }

    LOG_INFO("Server", "Heartbeat mock server listening on port %d", port);
    LOG_INFO("Server", "  Heartbeat interval: %dms", heartbeat_interval_ms_);
    LOG_INFO("Server", "  Messages per heartbeat: %d", messages_per_heartbeat_);
    return true;
  }

  void run() {
    while (keep_running) {
      LOG_INFO("Server", "Waiting for client connection...");

      struct sockaddr_in client_addr;
      socklen_t client_len = sizeof(client_addr);

      int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
      if (client_fd < 0) {
        if (keep_running) {
          LOG_PERROR("Server", "accept failed");
        }
        continue;
      }

      LOG_INFO("Server", "Client connected from %s:%d", inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));

      // Reset sequence number for new connection
      sequence_number_ = 0;

      handle_client(client_fd);

      LOG_INFO("Server", "Client disconnected");
    }
  }

  void handle_client(int client_fd) {
    uint64_t tick_count = 0;
    uint64_t heartbeat_count = 0;
    auto start_time = std::chrono::steady_clock::now();
    auto last_heartbeat = start_time;

    while (keep_running) {
      auto now = std::chrono::steady_clock::now();
      auto elapsed_since_heartbeat = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_heartbeat
      );
      
      // Send heartbeat if interval has passed
      if (elapsed_since_heartbeat.count() >= heartbeat_interval_ms_) {
        if (!send_heartbeat(client_fd)) {
          break;
        }
        heartbeat_count++;
        last_heartbeat = now;
      }
      
      // Send tick messages
      for (int i = 0; i < messages_per_heartbeat_ && keep_running; ++i) {
        if (!send_tick(client_fd)) {
          break;
        }
        tick_count++;
        
        // Small delay between ticks
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      
      // Stop after a reasonable number of messages
      if (tick_count >= 50000) {
        break;
      }
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time
    );
    double seconds = duration.count() / 1000.0;

    LOG_INFO("Server", "Sent %lu ticks and %lu heartbeats in %.2fs",
             tick_count, heartbeat_count, seconds);
    LOG_INFO("Server", "Final sequence number: %lu", sequence_number_);

    close(client_fd);
  }

  bool send_tick(int client_fd) {
    uint64_t timestamp = now_ns();

    // Random symbol
    std::uniform_int_distribution<size_t> symbol_dist(0, symbols.size() - 1);
    const std::string& symbol_str = symbols[symbol_dist(rng)];

    // Random price and volume
    std::uniform_real_distribution<float> price_dist(100.0f, 500.0f);
    float price = price_dist(rng);

    std::uniform_int_distribution<int32_t> volume_dist(100, 10000);
    int32_t volume = volume_dist(rng);

    // Serialize
    std::string message = serialize_tick(
      sequence_number_++,
      timestamp,
      symbol_str.c_str(),
      price,
      volume
    );

    // Send
    ssize_t bytes_sent = send(client_fd, message.data(), message.length(), 0);
    if (bytes_sent < 0) {
      LOG_PERROR("Server", "send tick failed");
      return false;
    }

    return true;
  }
  
  bool send_heartbeat(int client_fd) {
    uint64_t timestamp = now_ns();

    // Serialize
    std::string message = serialize_heartbeat(sequence_number_++, timestamp);

    // Send
    ssize_t bytes_sent = send(client_fd, message.data(), message.length(), 0);
    if (bytes_sent < 0) {
      LOG_PERROR("Server", "send heartbeat failed");
      return false;
    }

    LOG_INFO("Server", "Sent heartbeat (seq=%lu)", sequence_number_ - 1);

    return true;
  }

  void stop() {
    keep_running = 0;
    if (server_fd >= 0) {
      close(server_fd);
    }
  }

  ~HeartbeatMockServer() { stop(); }
};

int main(int argc, char* argv[]) {
  signal(SIGINT, signal_handler);

  int port = 9999;
  int heartbeat_interval_ms = 1000;  // 1 second
  int messages_per_heartbeat = 10;
  
  if (argc > 1) {
    port = std::atoi(argv[1]);
  }
  if (argc > 2) {
    heartbeat_interval_ms = std::atoi(argv[2]);
  }
  if (argc > 3) {
    messages_per_heartbeat = std::atoi(argv[3]);
  }

  HeartbeatMockServer server(port, heartbeat_interval_ms, messages_per_heartbeat);

  if (!server.start()) {
    return 1;
  }

  server.run();

  LOG_INFO("Server", "Server shutting down...");
  return 0;
}
