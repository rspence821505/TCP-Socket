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

  bool start() {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
      perror("socket");
      return false;
    }

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      perror("bind");
      return false;
    }

    if (listen(server_fd_, 5) < 0) {
      perror("listen");
      return false;
    }

    std::cout << "Text Mock Server listening on port " << port_ << std::endl;
    std::cout << "Sending " << msgs_per_sec_ << " msgs/sec for "
              << duration_sec_ << " seconds" << std::endl;
    std::cout << "Format: timestamp symbol price volume\\n" << std::endl;

    return true;
  }

  void run() {
    while (running) {
      std::cout << "Waiting for connection..." << std::endl;

      struct sockaddr_in client_addr;
      socklen_t client_len = sizeof(client_addr);
      int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);

      if (client_fd < 0) {
        if (running) {
          perror("accept");
        }
        continue;
      }

      char client_ip[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
      std::cout << "Client connected from " << client_ip << std::endl;

      handle_client(client_fd);

      close(client_fd);
      std::cout << "Client disconnected" << std::endl;
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
    uint64_t timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );

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
          perror("send");
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
        std::cout << "Sent " << messages_sent << " messages ("
                  << elapsed << "s elapsed)" << std::endl;
      }
    }

    // Send remaining data
    if (!batch_buffer.empty()) {
      send(client_fd, batch_buffer.data(), batch_buffer.size(), 0);
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time
    ).count();

    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "Total messages sent: " << messages_sent << std::endl;
    std::cout << "Duration: " << elapsed << " ms" << std::endl;
    std::cout << "Actual rate: "
              << (messages_sent * 1000.0 / elapsed) << " msgs/sec" << std::endl;
  }

  int port_;
  int msgs_per_sec_;
  int duration_sec_;
  int server_fd_;
  std::mt19937_64 rng_;
};

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <port> [msgs_per_sec] [duration_sec]" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Example: " << argv[0] << " 9999 10000 10" << std::endl;
    std::cerr << "  Sends 10,000 text ticks/sec for 10 seconds" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Message format: timestamp symbol price volume\\n" << std::endl;
    std::cerr << "Example tick:   1234567890 AAPL 150.25 100" << std::endl;
    return 1;
  }

  int port = std::atoi(argv[1]);
  int msgs_per_sec = (argc > 2) ? std::atoi(argv[2]) : 1000;
  int duration_sec = (argc > 3) ? std::atoi(argv[3]) : 10;

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGPIPE, SIG_IGN);

  TextMockServer server(port, msgs_per_sec, duration_sec);

  if (!server.start()) {
    return 1;
  }

  server.run();

  return 0;
}
