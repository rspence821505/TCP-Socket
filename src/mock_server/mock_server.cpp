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

#include "common.hpp"

// Global flag for graceful shutdown
volatile sig_atomic_t keep_running = 1;

void signal_handler(int signum) { keep_running = 0; }

class MockExchangeServer {
private:
  int server_fd;
  int port;
  std::vector<std::string> symbols;
  std::mt19937 rng;

public:
  MockExchangeServer(int port) : port(port), rng(std::random_device{}()) {
    // Initialize some stock symbols
    symbols = {"AAPL", "MSFT", "GOOGL", "AMZN", "TSLA", "META", "NVDA", "JPM"};
  }

  bool start() {
    // 1. Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
      LOG_PERROR("Server", "socket creation failed");
      return false;
    }

    // 2. Set socket options to reuse address (avoid "Address already in use"
    // errors)
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) <
        0) {
      LOG_PERROR("Server", "setsockopt failed");
      close(server_fd);
      return false;
    }

    // 3. Set up server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr =
        INADDR_ANY; // Accept connections on any interface
    server_addr.sin_port = htons(port);

    // 4. Bind socket to address
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
        0) {
      LOG_PERROR("Server", "bind failed");
      close(server_fd);
      return false;
    }

    // 5. Listen for connections (backlog of 5)
    if (listen(server_fd, 5) < 0) {
      LOG_PERROR("Server", "listen failed");
      close(server_fd);
      return false;
    }

    LOG_INFO("Server", "Mock exchange server listening on port %d", port);
    return true;
  }

  void run() {
    while (keep_running) {
      LOG_INFO("Server", "Waiting for client connection...");

      // Accept a client connection
      struct sockaddr_in client_addr;
      socklen_t client_len = sizeof(client_addr);

      int client_fd =
          accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
      if (client_fd < 0) {
        if (keep_running) {
          LOG_PERROR("Server", "accept failed");
        }
        continue;
      }

      LOG_INFO("Server", "Client connected from %s:%d", inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));

      // Handle this client
      handle_client(client_fd);

      LOG_INFO("Server", "Client disconnected");
    }
  }

  void handle_client(int client_fd) {
    uint64_t message_count = 0;
    auto start_time = std::chrono::steady_clock::now();

    // Generate and send messages
    while (keep_running &&
           message_count < 50000) { // Send 50k messages by default
      std::string message = generate_tick();

      ssize_t bytes_sent =
          send(client_fd, message.c_str(), message.length(), 0);
      if (bytes_sent < 0) {
        LOG_PERROR("Server", "send failed");
        break;
      }

      message_count++;

      // Optional: Add small delay to simulate realistic feed rate
      // Uncomment to slow down the feed (e.g., 10k msgs/sec)
      if (message_count % 10 == 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(5000));
      }
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    double seconds = duration.count() / 1000.0;

    LOG_INFO("Server", "Sent %lu messages in %.2fs (%d msgs/sec)",
             message_count, seconds, static_cast<int>(message_count / seconds));

    close(client_fd);
  }

  std::string generate_tick() {
    // Generate random market data
    uint64_t timestamp = now_ns();

    // Random symbol
    std::uniform_int_distribution<size_t> symbol_dist(0, symbols.size() - 1);
    const std::string &symbol = symbols[symbol_dist(rng)];

    // Random price between 100.00 and 500.00
    std::uniform_real_distribution<double> price_dist(100.0, 500.0);
    double price = price_dist(rng);

    // Random volume between 100 and 10000
    std::uniform_int_distribution<int> volume_dist(100, 10000);
    int volume = volume_dist(rng);

    // Format: timestamp,symbol,price,volume\n
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%lld,%s,%.2f,%d\n", // Changed %ld to %lld
             static_cast<long long>(timestamp),           // Added explicit cast
             symbol.c_str(), price, volume);

    return std::string(buffer);
  }

  void stop() {
    keep_running = 0;
    if (server_fd >= 0) {
      close(server_fd);
    }
  }

  ~MockExchangeServer() { stop(); }
};

int main(int argc, char *argv[]) {
  // Set up signal handler for graceful shutdown (Ctrl+C)
  signal(SIGINT, signal_handler);

  int port = 9999;
  if (argc > 1) {
    port = std::atoi(argv[1]);
  }

  MockExchangeServer server(port);

  if (!server.start()) {
    return 1;
  }

  server.run();

  LOG_INFO("Server", "Server shutting down...");
  return 0;
}