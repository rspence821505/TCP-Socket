#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <random>
#include <signal.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "common.hpp"
#include "text_protocol.hpp"

/**
 * Text Mock Server
 *
 * Sends newline-delimited tick messages in the format:
 *   timestamp symbol price volume\n
 *
 * Usage: text_mock_server <port> [msgs_per_sec] [duration_sec]
 */

static volatile sig_atomic_t running = 1;

void signal_handler(int) {
  running = 0;
}

class TextMockServer {
public:
  TextMockServer(int port, int msgs_per_sec, int duration_sec)
      : port_(port)
      , msgs_per_sec_(msgs_per_sec)
      , duration_sec_(duration_sec)
      , server_fd_(-1)
      , rng_(42) {}

  ~TextMockServer() {
    if (server_fd_ >= 0) {
      close(server_fd_);
    }
  }

  Result<void> start() {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
      return Result<void>::error("socket creation failed: " + std::string(strerror(errno)));
    }

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      close(server_fd_);
      return Result<void>::error("bind failed: " + std::string(strerror(errno)));
    }

    if (listen(server_fd_, 5) < 0) {
      close(server_fd_);
      return Result<void>::error("listen failed: " + std::string(strerror(errno)));
    }

    LOG_INFO("Server", "Text Mock Server listening on port %d", port_);
    LOG_INFO("Server", "Sending %d msgs/sec for %d seconds", msgs_per_sec_, duration_sec_);
    LOG_INFO("Server", "Format: timestamp symbol price volume\\n");

    return Result<void>();
  }

  void run() {
    while (running) {
      LOG_INFO("Server", "Waiting for connection...");

      struct sockaddr_in client_addr;
      socklen_t client_len = sizeof(client_addr);
      int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);

      if (client_fd < 0) {
        if (running) {
          LOG_PERROR("Server", "accept");
        }
        continue;
      }

      char client_ip[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
      LOG_INFO("Server", "Client connected from %s", client_ip);

      handle_client(client_fd);

      close(client_fd);
      LOG_INFO("Server", "Client disconnected");
    }
  }

private:
  void handle_client(int client_fd) {
    const char* symbols[] = {"AAPL", "GOOG", "MSFT", "AMZN", "TSLA", "META", "NVDA", "AMD"};
    const int num_symbols = sizeof(symbols) / sizeof(symbols[0]);

    std::uniform_real_distribution<double> price_dist(100.0, 500.0);
    std::uniform_int_distribution<int64_t> volume_dist(100, 10000);
    std::uniform_int_distribution<int> symbol_dist(0, num_symbols - 1);

    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::seconds(duration_sec_);

    uint64_t messages_sent = 0;
    uint64_t timestamp = now_us();

    // Calculate delay between messages
    auto msg_interval = std::chrono::nanoseconds(1'000'000'000 / msgs_per_sec_);
    auto next_send_time = start_time;

    // Buffer for batching small writes
    std::string batch_buffer;
    batch_buffer.reserve(64 * 1024);
    const size_t batch_threshold = 32 * 1024;

    while (running && std::chrono::steady_clock::now() < end_time) {
      // Generate tick
      const char* symbol = symbols[symbol_dist(rng_)];
      double price = price_dist(rng_);
      int64_t volume = volume_dist(rng_);

      std::string tick_line = serialize_text_tick(timestamp, symbol, price, volume);

      batch_buffer += tick_line;
      messages_sent++;
      timestamp++;

      // Send batch when buffer is large enough or rate limiting kicks in
      if (batch_buffer.size() >= batch_threshold) {
        if (send(client_fd, batch_buffer.data(), batch_buffer.size(), 0) < 0) {
          LOG_PERROR("Server", "send");
          break;
        }
        batch_buffer.clear();
      }

      // Rate limiting
      next_send_time += msg_interval;
      auto now = std::chrono::steady_clock::now();
      if (next_send_time > now) {
        std::this_thread::sleep_until(next_send_time);
      }

      // Progress report every second
      if (messages_sent % msgs_per_sec_ == 0) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time
        ).count();
        LOG_INFO("Server", "Sent %lu messages (%lds elapsed)", messages_sent, elapsed);
      }
    }

    // Send remaining data
    if (!batch_buffer.empty()) {
      send(client_fd, batch_buffer.data(), batch_buffer.size(), 0);
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time
    ).count();

    LOG_INFO("Server", "=== Summary ===");
    LOG_INFO("Server", "Total messages sent: %lu", messages_sent);
    LOG_INFO("Server", "Duration: %ld ms", elapsed);
    LOG_INFO("Server", "Actual rate: %.1f msgs/sec", messages_sent * 1000.0 / elapsed);
  }

  int port_;
  int msgs_per_sec_;
  int duration_sec_;
  int server_fd_;
  std::mt19937_64 rng_;
};

int main(int argc, char* argv[]) {
  if (argc < 2) {
    LOG_ERROR("Main", "Usage: %s <port> [msgs_per_sec] [duration_sec]", argv[0]);
    LOG_ERROR("Main", "Example: %s 9999 10000 10", argv[0]);
    LOG_ERROR("Main", "  Sends 10,000 text ticks/sec for 10 seconds");
    LOG_ERROR("Main", "Message format: timestamp symbol price volume\\n");
    LOG_ERROR("Main", "Example tick:   1234567890 AAPL 150.25 100");
    return 1;
  }

  int port = std::atoi(argv[1]);
  int msgs_per_sec = (argc > 2) ? std::atoi(argv[2]) : 1000;
  int duration_sec = (argc > 3) ? std::atoi(argv[3]) : 10;

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGPIPE, SIG_IGN);

  TextMockServer server(port, msgs_per_sec, duration_sec);

  auto start_result = server.start();
  if (!start_result) {
    LOG_ERROR("Server", "%s", start_result.error().c_str());
    return 1;
  }

  server.run();

  return 0;
}
