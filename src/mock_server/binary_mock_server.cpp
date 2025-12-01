#include <arpa/inet.h>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <random>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "binary_protocol.hpp"

// Global flag for graceful shutdown
volatile sig_atomic_t keep_running = 1;

void signal_handler(int) { keep_running = 0; }

class BinaryMockExchangeServer {
private:
  int server_fd;
  int port;
  std::vector<std::string> symbols;
  std::mt19937 rng;

public:
  BinaryMockExchangeServer(int port) : port(port), rng(std::random_device{}()) {
    // Initialize some stock symbols (truncated/padded to 4 chars)
    symbols = {"AAPL", "MSFT", "GOOG", "AMZN", "TSLA", "META", "NVDA", "JPM "};
  }

  bool start() {
    // 1. Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
      perror("socket creation failed");
      return false;
    }

    // 2. Set socket options to reuse address
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) <
        0) {
      perror("setsockopt failed");
      close(server_fd);
      return false;
    }

    // 3. Set up server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // 4. Bind socket to address
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
        0) {
      perror("bind failed");
      close(server_fd);
      return false;
    }

    // 5. Listen for connections
    if (listen(server_fd, 5) < 0) {
      perror("listen failed");
      close(server_fd);
      return false;
    }

    std::cout << "Binary mock exchange server listening on port " << port
              << std::endl;
    return true;
  }

  void run() {
    while (keep_running) {
      std::cout << "Waiting for client connection..." << std::endl;

      // Accept a client connection
      struct sockaddr_in client_addr;
      socklen_t client_len = sizeof(client_addr);

      int client_fd =
          accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
      if (client_fd < 0) {
        if (keep_running) {
          perror("accept failed");
        }
        continue;
      }

      std::cout << "Client connected from " << inet_ntoa(client_addr.sin_addr)
                << ":" << ntohs(client_addr.sin_port) << std::endl;

      // Handle this client
      handle_client(client_fd);

      std::cout << "Client disconnected" << std::endl;
    }
  }

  void handle_client(int client_fd) {
    uint64_t message_count = 0;
    auto start_time = std::chrono::steady_clock::now();

    // Generate and send binary messages
    while (keep_running && message_count < 50000) {
      BinaryTick tick = generate_tick();
      std::string message = serialize_tick(tick);

      ssize_t bytes_sent = send(client_fd, message.data(), message.length(), 0);
      if (bytes_sent < 0) {
        perror("send failed");
        break;
      }

      message_count++;

      // Optional: Add small delay to simulate realistic feed rate
      // Uncomment to slow down the feed
      if (message_count % 10 == 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(1000));
      }
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    double seconds = duration.count() / 1000.0;

    std::cout << "Sent " << message_count << " messages in " << seconds << "s ("
              << static_cast<int>(message_count / seconds) << " msgs/sec)"
              << std::endl;

    close(client_fd);
  }

  BinaryTick generate_tick() {
    BinaryTick tick;

    // Generate timestamp
    tick.timestamp =
        std::chrono::system_clock::now().time_since_epoch().count();

    // Random symbol
    std::uniform_int_distribution<size_t> symbol_dist(0, symbols.size() - 1);
    const std::string &symbol_str = symbols[symbol_dist(rng)];

    // Copy symbol (ensure exactly 4 bytes)
    memcpy(tick.symbol, symbol_str.c_str(), 4);

    // Random price between 100.00 and 500.00
    std::uniform_real_distribution<float> price_dist(100.0f, 500.0f);
    tick.price = price_dist(rng);

    // Random volume between 100 and 10000
    std::uniform_int_distribution<int32_t> volume_dist(100, 10000);
    tick.volume = volume_dist(rng);

    return tick;
  }

  void stop() {
    keep_running = 0;
    if (server_fd >= 0) {
      close(server_fd);
    }
  }

  ~BinaryMockExchangeServer() { stop(); }
};

int main(int argc, char *argv[]) {
  // Set up signal handler for graceful shutdown (Ctrl+C)
  signal(SIGINT, signal_handler);

  int port = 9999;
  if (argc > 1) {
    port = std::atoi(argv[1]);
  }

  BinaryMockExchangeServer server(port);

  if (!server.start()) {
    return 1;
  }

  server.run();

  std::cout << "\nServer shutting down..." << std::endl;
  return 0;
}