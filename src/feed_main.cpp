#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <numeric>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "binary_protocol.hpp"
#include "cli_parser.hpp"
#include "order_book.hpp"
#include "spsc_queue.hpp"
#include "text_protocol.hpp"

/**
 * Unified Feed Handler
 *
 * Supports both text and binary protocols with configurable threading:
 *   Reader Thread(s) → Parser Queue → Parser Thread(s) → Book Queue → Book Updater Thread(s)
 *
 * Usage:
 *   ./feed_handler --host localhost --port 9999 --threads=1,2,1
 */

// Get current timestamp in nanoseconds
inline uint64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::high_resolution_clock::now().time_since_epoch()
  ).count();
}

// Unified tick structure that works with both protocols
struct UnifiedTick {
  uint64_t timestamp;
  char symbol[8];
  double price;
  int64_t volume;
  uint64_t recv_timestamp_ns;

  UnifiedTick() : timestamp(0), price(0.0), volume(0), recv_timestamp_ns(0) {
    symbol[0] = '\0';
  }

  // Construct from TextTick
  explicit UnifiedTick(const TextTick& tt, uint64_t recv_ts) {
    timestamp = tt.timestamp;
    std::memcpy(symbol, tt.symbol, sizeof(symbol));
    price = tt.price;
    volume = tt.volume;
    recv_timestamp_ns = recv_ts;
  }

  // Construct from binary TickPayload
  explicit UnifiedTick(const TickPayload& tp, uint64_t recv_ts) {
    timestamp = tp.timestamp;
    std::memcpy(symbol, tp.symbol, sizeof(symbol));
    symbol[sizeof(symbol) - 1] = '\0';
    price = static_cast<double>(tp.price);
    volume = tp.volume;
    recv_timestamp_ns = recv_ts;
  }
};

// Latency statistics collector
class LatencyStats {
public:
  void reserve(size_t n) { latencies_.reserve(n); }
  void add(uint64_t latency_ns) { latencies_.push_back(latency_ns); }

  void print(const std::string& name) const {
    if (latencies_.empty()) {
      std::cout << name << ": No data" << std::endl;
      return;
    }

    std::vector<uint64_t> sorted = latencies_;
    std::sort(sorted.begin(), sorted.end());

    uint64_t sum = std::accumulate(sorted.begin(), sorted.end(), 0ULL);
    double mean = static_cast<double>(sum) / sorted.size();
    uint64_t p50 = sorted[sorted.size() * 50 / 100];
    uint64_t p95 = sorted[sorted.size() * 95 / 100];
    uint64_t p99 = sorted[sorted.size() * 99 / 100];
    uint64_t max_val = sorted.back();

    std::cout << name << ":\n"
              << "  Count: " << sorted.size() << "\n"
              << "  Mean:  " << mean / 1000.0 << " us\n"
              << "  p50:   " << p50 / 1000.0 << " us\n"
              << "  p95:   " << p95 / 1000.0 << " us\n"
              << "  p99:   " << p99 / 1000.0 << " us\n"
              << "  Max:   " << max_val / 1000.0 << " us" << std::endl;
  }

private:
  std::vector<uint64_t> latencies_;
};

// Text protocol reader
class TextReader {
public:
  TextReader(int sockfd, SPSCQueue<UnifiedTick>& queue, std::atomic<bool>& should_stop, bool verbose)
      : sockfd_(sockfd), queue_(queue), should_stop_(should_stop)
      , verbose_(verbose), messages_parsed_(0), parse_errors_(0) {}

  void run() {
    char recv_buffer[16 * 1024];

    while (!should_stop_) {
      ssize_t bytes_read = recv(sockfd_, recv_buffer, sizeof(recv_buffer), 0);
      uint64_t recv_ts = now_ns();

      if (bytes_read <= 0) {
        if (bytes_read == 0) {
          if (verbose_) std::cout << "[Reader] Server closed connection\n";
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
          if (verbose_) perror("[Reader] recv");
        }
        should_stop_ = true;
        break;
      }

      if (!line_buffer_.append(recv_buffer, bytes_read)) {
        std::cerr << "[Reader] Buffer overflow!\n";
        line_buffer_.reset();
        continue;
      }

      std::string_view line;
      while (line_buffer_.get_line(line)) {
        auto tick_opt = parse_text_tick(line);
        if (tick_opt) {
          UnifiedTick unified(*tick_opt, recv_ts);
          enqueue_with_backpressure(unified);
          messages_parsed_++;
        } else {
          parse_errors_++;
        }
      }
    }

    if (verbose_) {
      std::cout << "[Reader] Exiting. Parsed: " << messages_parsed_
                << ", Errors: " << parse_errors_ << std::endl;
    }
  }

  uint64_t messages_parsed() const { return messages_parsed_; }
  uint64_t parse_errors() const { return parse_errors_; }

private:
  void enqueue_with_backpressure(const UnifiedTick& tick) {
    int retries = 0;
    while (!queue_.push(tick) && !should_stop_) {
      if (++retries > 1000) {
        std::this_thread::yield();
        retries = 0;
      }
    }
  }

  int sockfd_;
  SPSCQueue<UnifiedTick>& queue_;
  std::atomic<bool>& should_stop_;
  bool verbose_;
  TextLineBuffer line_buffer_;
  uint64_t messages_parsed_;
  uint64_t parse_errors_;
};

// Binary protocol reader
class BinaryReader {
public:
  BinaryReader(int sockfd, SPSCQueue<UnifiedTick>& queue, std::atomic<bool>& should_stop, bool verbose)
      : sockfd_(sockfd), queue_(queue), should_stop_(should_stop)
      , verbose_(verbose), messages_parsed_(0), parse_errors_(0) {}

  void run() {
    char recv_buffer[64 * 1024];
    size_t buffer_pos = 0;

    while (!should_stop_) {
      ssize_t bytes_read = recv(sockfd_, recv_buffer + buffer_pos,
                                 sizeof(recv_buffer) - buffer_pos, 0);
      uint64_t recv_ts = now_ns();

      if (bytes_read <= 0) {
        if (bytes_read == 0) {
          if (verbose_) std::cout << "[Reader] Server closed connection\n";
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
          if (verbose_) perror("[Reader] recv");
        }
        should_stop_ = true;
        break;
      }

      buffer_pos += bytes_read;

      // Parse complete messages
      size_t consumed = 0;
      while (consumed + MessageHeader::HEADER_SIZE <= buffer_pos) {
        MessageHeader header = deserialize_header(recv_buffer + consumed);

        size_t total_msg_size = MessageHeader::HEADER_SIZE + header.length;
        if (consumed + total_msg_size > buffer_pos) {
          break;  // Incomplete message
        }

        if (header.type == MessageType::TICK) {
          TickPayload payload = deserialize_tick_payload(
              recv_buffer + consumed + MessageHeader::HEADER_SIZE);
          UnifiedTick unified(payload, recv_ts);
          enqueue_with_backpressure(unified);
          messages_parsed_++;
        }

        consumed += total_msg_size;
      }

      // Compact buffer
      if (consumed > 0) {
        memmove(recv_buffer, recv_buffer + consumed, buffer_pos - consumed);
        buffer_pos -= consumed;
      }
    }

    if (verbose_) {
      std::cout << "[Reader] Exiting. Parsed: " << messages_parsed_
                << ", Errors: " << parse_errors_ << std::endl;
    }
  }

  uint64_t messages_parsed() const { return messages_parsed_; }
  uint64_t parse_errors() const { return parse_errors_; }

private:
  void enqueue_with_backpressure(const UnifiedTick& tick) {
    int retries = 0;
    while (!queue_.push(tick) && !should_stop_) {
      if (++retries > 1000) {
        std::this_thread::yield();
        retries = 0;
      }
    }
  }

  int sockfd_;
  SPSCQueue<UnifiedTick>& queue_;
  std::atomic<bool>& should_stop_;
  bool verbose_;
  uint64_t messages_parsed_;
  uint64_t parse_errors_;
};

// Book updater thread
class BookUpdater {
public:
  BookUpdater(SPSCQueue<UnifiedTick>& queue, std::atomic<bool>& should_stop, bool verbose)
      : queue_(queue), should_stop_(should_stop), verbose_(verbose), messages_processed_(0) {
    e2e_latency_.reserve(1'000'000);
  }

  void run() {
    while (!should_stop_ || !queue_.empty()) {
      auto tick_opt = queue_.pop();
      if (tick_opt) {
        uint64_t process_ts = now_ns();
        const auto& tick = *tick_opt;

        // Record end-to-end latency
        e2e_latency_.add(process_ts - tick.recv_timestamp_ns);

        // Update order book (simplified - treat as bid update)
        std::string symbol(tick.symbol);
        auto& book = get_or_create_book(symbol);
        book.apply_update(0, static_cast<float>(tick.price), tick.volume);

        messages_processed_++;

        // Print progress
        if (verbose_ && messages_processed_ % 100000 == 0) {
          std::cout << "[BookUpdater] Processed: " << messages_processed_
                    << " | Last: " << tick.symbol << " @ " << tick.price << std::endl;
        }
      } else {
        std::this_thread::yield();
      }
    }

    if (verbose_) {
      std::cout << "[BookUpdater] Exiting. Processed: " << messages_processed_ << std::endl;
    }
  }

  void print_stats() const {
    std::cout << "\n=== End-to-End Latency ===" << std::endl;
    e2e_latency_.print("Recv → Book Update");
  }

  void print_books() const {
    std::cout << "\n=== Order Books ===" << std::endl;
    for (const auto& [symbol, book] : books_) {
      book.print_top_of_book(symbol);
    }
  }

  uint64_t messages_processed() const { return messages_processed_; }

private:
  OrderBook& get_or_create_book(const std::string& symbol) {
    auto it = books_.find(symbol);
    if (it == books_.end()) {
      it = books_.emplace(symbol, OrderBook()).first;
    }
    return it->second;
  }

  SPSCQueue<UnifiedTick>& queue_;
  std::atomic<bool>& should_stop_;
  bool verbose_;
  uint64_t messages_processed_;
  LatencyStats e2e_latency_;
  std::unordered_map<std::string, OrderBook> books_;
};

int connect_to_server(const std::string& host, uint16_t port, bool verbose) {
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("socket");
    return -1;
  }

  // Enable TCP_NODELAY
  int flag = 1;
  setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

  // Set receive buffer size
  int rcvbuf = 256 * 1024;
  setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);

  if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  }

  if (verbose) {
    std::cout << "Connecting to " << host << ":" << port << "..." << std::endl;
  }

  if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
    perror("connect");
    close(sockfd);
    return -1;
  }

  if (verbose) {
    std::cout << "Connected!" << std::endl;
  }

  return sockfd;
}

int main(int argc, char* argv[]) {
  auto config_opt = CLIParser::parse(argc, argv);
  if (!config_opt) {
    std::cerr << "\nRun '" << argv[0] << " --help' for usage information.\n";
    return 1;
  }

  FeedConfig config = *config_opt;

  if (config.help_requested) {
    CLIParser::print_usage(argv[0]);
    return 0;
  }

  if (!config.is_valid()) {
    std::cerr << "Error: --port is required\n\n";
    CLIParser::print_usage(argv[0]);
    return 1;
  }

  if (config.verbose) {
    CLIParser::print_config(config);
  }

  // Connect to server
  int sockfd = connect_to_server(config.host, config.port, config.verbose);
  if (sockfd < 0) {
    return 1;
  }

  // Create queues
  SPSCQueue<UnifiedTick> reader_to_book(config.queue_size);

  // Control flag
  std::atomic<bool> should_stop{false};

  auto start_time = std::chrono::steady_clock::now();

  // Create and start threads based on protocol
  std::vector<std::thread> threads;
  std::unique_ptr<TextReader> text_reader;
  std::unique_ptr<BinaryReader> binary_reader;
  std::unique_ptr<BookUpdater> book_updater;

  // Create book updater (runs in its own thread)
  book_updater = std::make_unique<BookUpdater>(reader_to_book, should_stop, config.verbose);
  threads.emplace_back([&book_updater]() { book_updater->run(); });

  // Create reader based on protocol
  if (config.protocol == Protocol::TEXT) {
    text_reader = std::make_unique<TextReader>(sockfd, reader_to_book, should_stop, config.verbose);
    threads.emplace_back([&text_reader]() { text_reader->run(); });
  } else {
    binary_reader = std::make_unique<BinaryReader>(sockfd, reader_to_book, should_stop, config.verbose);
    threads.emplace_back([&binary_reader]() { binary_reader->run(); });
  }

  // Wait for reader thread to finish
  threads.back().join();
  threads.pop_back();

  // Signal other threads to stop
  should_stop = true;

  // Wait for remaining threads
  for (auto& t : threads) {
    if (t.joinable()) {
      t.join();
    }
  }

  auto end_time = std::chrono::steady_clock::now();
  auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time
  ).count();

  // Print statistics
  std::cout << "\n=== Summary ===" << std::endl;
  std::cout << "Duration: " << duration_ms << " ms" << std::endl;

  uint64_t messages_parsed = 0;
  uint64_t parse_errors = 0;

  if (text_reader) {
    messages_parsed = text_reader->messages_parsed();
    parse_errors = text_reader->parse_errors();
  } else if (binary_reader) {
    messages_parsed = binary_reader->messages_parsed();
    parse_errors = binary_reader->parse_errors();
  }

  std::cout << "Messages parsed: " << messages_parsed << std::endl;
  std::cout << "Messages processed: " << book_updater->messages_processed() << std::endl;
  std::cout << "Parse errors: " << parse_errors << std::endl;

  if (duration_ms > 0) {
    double rate = messages_parsed * 1000.0 / duration_ms;
    std::cout << "Throughput: " << rate << " msgs/sec" << std::endl;
  }

  book_updater->print_stats();
  book_updater->print_books();

  close(sockfd);
  return 0;
}
