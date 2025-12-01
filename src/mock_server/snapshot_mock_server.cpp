#include <arpa/inet.h>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <map>
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

// Simulated order book state
struct SimulatedOrderBook {
  std::map<float, uint64_t> bids;
  std::map<float, uint64_t> asks;
  
  void initialize(const std::string& /*symbol*/, std::mt19937& rng) {
    // Generate initial order book around a base price
    std::uniform_real_distribution<float> base_price_dist(100.0f, 200.0f);
    float mid_price = base_price_dist(rng);
    
    // Generate bid levels (below mid)
    for (int i = 0; i < 10; ++i) {
      float price = mid_price - (i + 1) * 0.01f;
      uint64_t quantity = 1000 + rng() % 9000;
      bids[price] = quantity;
    }
    
    // Generate ask levels (above mid)
    for (int i = 0; i < 10; ++i) {
      float price = mid_price + (i + 1) * 0.01f;
      uint64_t quantity = 1000 + rng() % 9000;
      asks[price] = quantity;
    }
  }
  
  std::vector<OrderBookLevel> get_top_bids(size_t n) const {
    std::vector<OrderBookLevel> result;
    auto it = bids.rbegin();
    for (size_t i = 0; i < n && it != bids.rend(); ++i, ++it) {
      result.push_back({it->first, it->second});
    }
    return result;
  }
  
  std::vector<OrderBookLevel> get_top_asks(size_t n) const {
    std::vector<OrderBookLevel> result;
    auto it = asks.begin();
    for (size_t i = 0; i < n && it != asks.end(); ++i, ++it) {
      result.push_back({it->first, it->second});
    }
    return result;
  }
  
  // Apply a random update and return the update message
  std::pair<uint8_t, float> apply_random_update(std::mt19937& rng, int64_t& quantity_out) {
    std::uniform_int_distribution<int> side_dist(0, 1);
    uint8_t side = side_dist(rng);
    
    auto& book_side = (side == 0) ? bids : asks;
    
    if (book_side.empty()) {
      quantity_out = 0;
      return {side, 0.0f};
    }
    
    std::uniform_int_distribution<int> action_dist(0, 2);
    int action = action_dist(rng);  // 0=delete, 1=update, 2=add
    
    if (action == 0 && book_side.size() > 3) {
      // Delete a level
      auto it = book_side.begin();
      std::advance(it, rng() % book_side.size());
      float price = it->first;
      book_side.erase(it);
      quantity_out = 0;
      return {side, price};
    } else if (action == 1 && !book_side.empty()) {
      // Update existing level
      auto it = book_side.begin();
      std::advance(it, rng() % book_side.size());
      float price = it->first;
      uint64_t new_qty = 1000 + rng() % 9000;
      it->second = new_qty;
      quantity_out = static_cast<int64_t>(new_qty);
      return {side, price};
    } else {
      // Add new level
      float mid = (bids.empty() || asks.empty()) ? 100.0f : 
                  (bids.rbegin()->first + asks.begin()->first) / 2.0f;
      
      float offset = (0.01f + (rng() % 10) * 0.01f) * ((rng() % 2) ? 1 : -1);
      float price = (side == 0) ? mid + offset - 0.05f : mid + offset + 0.05f;
      
      uint64_t quantity = 1000 + rng() % 9000;
      book_side[price] = quantity;
      quantity_out = static_cast<int64_t>(quantity);
      return {side, price};
    }
  }
};

class SnapshotMockServer {
private:
  int server_fd;
  int port;
  std::mt19937 rng;
  uint64_t sequence_number_;
  
  // Configuration
  int heartbeat_interval_ms_;
  int updates_per_second_;
  
  // Simulated order books
  std::map<std::string, SimulatedOrderBook> order_books_;

public:
  SnapshotMockServer(int port, int heartbeat_interval_ms = 1000, int updates_per_second = 10)
    : port(port), rng(std::random_device{}()), sequence_number_(0)
    , heartbeat_interval_ms_(heartbeat_interval_ms)
    , updates_per_second_(updates_per_second) {
    
    // Initialize order books for a few symbols
    initialize_symbol("AAPL");
    initialize_symbol("MSFT");
    initialize_symbol("GOOG");
  }
  
  void initialize_symbol(const std::string& symbol_str) {
    SimulatedOrderBook book;
    book.initialize(symbol_str, rng);
    order_books_[symbol_str] = book;
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

    LOG_INFO("Server", "Snapshot-enabled mock server listening on port %d", port);
    LOG_INFO("Server", "  Heartbeat interval: %dms", heartbeat_interval_ms_);
    LOG_INFO("Server", "  Updates per second: %d", updates_per_second_);
    std::cout << "[Server] Symbols: ";
    for (const auto& [symbol, book] : order_books_) {
      std::cout << symbol << " ";
    }
    std::cout << std::endl;
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
    // Set client socket to non-blocking for reading requests
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    
    auto last_heartbeat = std::chrono::steady_clock::now();
    auto last_update = std::chrono::steady_clock::now();
    
    uint64_t update_count = 0;
    uint64_t heartbeat_count = 0;
    bool snapshot_sent = false;
    
    while (keep_running) {
      auto now = std::chrono::steady_clock::now();
      
      // Check for snapshot requests from client
      check_for_snapshot_request(client_fd, snapshot_sent);
      
      // Send heartbeat if interval has passed
      auto elapsed_since_heartbeat = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_heartbeat
      );
      
      if (elapsed_since_heartbeat.count() >= heartbeat_interval_ms_) {
        if (!send_heartbeat(client_fd)) {
          break;
        }
        heartbeat_count++;
        last_heartbeat = now;
      }
      
      // Send incremental updates (only after snapshot has been sent)
      if (snapshot_sent) {
        auto elapsed_since_update = std::chrono::duration_cast<std::chrono::milliseconds>(
          now - last_update
        );
        
        int update_interval_ms = 1000 / updates_per_second_;
        
        if (elapsed_since_update.count() >= update_interval_ms) {
          if (!send_random_update(client_fd)) {
            break;
          }
          update_count++;
          last_update = now;
        }
      }
      
      // Small sleep to avoid busy loop
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      
      // Stop after sending enough data
      if (update_count >= 1000) {
        break;
      }
    }
    
    LOG_INFO("Server", "Session stats: %lu heartbeats, %lu updates sent",
             heartbeat_count, update_count);

    close(client_fd);
  }
  
  void check_for_snapshot_request(int client_fd, bool& snapshot_sent) {
    char buffer[1024];
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);
    
    if (bytes_read <= 0) {
      return;  // No data or error (expected with non-blocking)
    }
    
    // Parse message
    if (bytes_read < static_cast<ssize_t>(MessageHeader::HEADER_SIZE)) {
      return;
    }
    
    MessageHeader header = deserialize_header(buffer);
    
    if (header.type == MessageType::SNAPSHOT_REQUEST) {
      SnapshotRequestPayload request = deserialize_snapshot_request(
        buffer + MessageHeader::HEADER_SIZE
      );
      
      std::string symbol_str(request.symbol, 4);
      size_t null_pos = symbol_str.find('\0');
      if (null_pos != std::string::npos) {
        symbol_str = symbol_str.substr(0, null_pos);
      }
      
      LOG_INFO("Server", "Received snapshot request for symbol: %s", symbol_str.c_str());

      // Send snapshot
      send_snapshot(client_fd, symbol_str);
      snapshot_sent = true;
    }
  }
  
  bool send_snapshot(int client_fd, const std::string& symbol_str) {
    auto it = order_books_.find(symbol_str);
    if (it == order_books_.end()) {
      LOG_ERROR("Server", "Unknown symbol: %s", symbol_str.c_str());
      return false;
    }

    const auto& book = it->second;

    // Get top 10 levels
    auto bids = book.get_top_bids(10);
    auto asks = book.get_top_asks(10);

    char symbol[4];
    memset(symbol, 0, 4);
    memcpy(symbol, symbol_str.c_str(), std::min(symbol_str.size(), size_t(4)));

    std::string message = serialize_snapshot_response(sequence_number_++, symbol, bids, asks);

    ssize_t bytes_sent = send(client_fd, message.data(), message.length(), 0);
    if (bytes_sent < 0) {
      LOG_PERROR("Server", "send snapshot failed");
      return false;
    }

    LOG_INFO("Server", "Sent snapshot (seq=%lu) for %s: %zu bids, %zu asks",
             sequence_number_ - 1, symbol_str.c_str(), bids.size(), asks.size());

    return true;
  }
  
  bool send_random_update(int client_fd) {
    // Pick a random symbol
    if (order_books_.empty()) return false;
    
    auto it = order_books_.begin();
    std::advance(it, rng() % order_books_.size());
    const std::string& symbol_str = it->first;
    auto& book = it->second;
    
    // Apply random update
    int64_t quantity;
    auto [side, price] = book.apply_random_update(rng, quantity);
    
    char symbol[4];
    memset(symbol, 0, 4);
    memcpy(symbol, symbol_str.c_str(), std::min(symbol_str.size(), size_t(4)));
    
    std::string message = serialize_order_book_update(
      sequence_number_++, symbol, side, price, quantity
    );
    
    ssize_t bytes_sent = send(client_fd, message.data(), message.length(), 0);
    if (bytes_sent < 0) {
      LOG_PERROR("Server", "send update failed");
      return false;
    }

    return true;
  }
  
  bool send_heartbeat(int client_fd) {
    uint64_t timestamp = now_ns();
    std::string message = serialize_heartbeat(sequence_number_++, timestamp);

    ssize_t bytes_sent = send(client_fd, message.data(), message.length(), 0);
    if (bytes_sent < 0) {
      LOG_PERROR("Server", "send heartbeat failed");
      return false;
    }

    LOG_INFO("Server", "Heartbeat (seq=%lu)", sequence_number_ - 1);

    return true;
  }

  void stop() {
    keep_running = 0;
    if (server_fd >= 0) {
      close(server_fd);
    }
  }

  ~SnapshotMockServer() { stop(); }
};

int main(int argc, char* argv[]) {
  signal(SIGINT, signal_handler);

  int port = 9999;
  int heartbeat_interval_ms = 1000;
  int updates_per_second = 10;
  
  if (argc > 1) {
    port = std::atoi(argv[1]);
  }
  if (argc > 2) {
    heartbeat_interval_ms = std::atoi(argv[2]);
  }
  if (argc > 3) {
    updates_per_second = std::atoi(argv[3]);
  }

  SnapshotMockServer server(port, heartbeat_interval_ms, updates_per_second);

  if (!server.start()) {
    return 1;
  }

  server.run();

  LOG_INFO("Server", "Server shutting down...");
  return 0;
}
