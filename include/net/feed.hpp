#ifndef NET_FEED_HPP
#define NET_FEED_HPP

/**
 * Consolidated Feed Handler Module
 *
 * This header provides a unified interface for building feed handlers that:
 * - Connect to market data servers (TCP)
 * - Parse both text and binary protocols
 * - Use lock-free queues for inter-thread communication
 * - Manage connection lifecycle with reconnection support
 * - Collect latency and throughput statistics
 *
 * Architecture:
 *   Socket → Reader Thread → SPSC Queue → Processor Thread → Callback
 *
 * Usage:
 *   net::FeedHandler handler(config);
 *   handler.set_tick_callback([](const net::Tick& tick) {
 *     // Process tick
 *   });
 *   handler.start();
 *   // ... wait for completion ...
 *   handler.stop();
 *   handler.print_stats();
 */

#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <numeric>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

// Include protocol parsers
#include "../binary_protocol.hpp"
#include "../text_protocol.hpp"
#include "../spsc_queue.hpp"
#include "../order_book.hpp"

namespace net {

//=============================================================================
// Utility Functions
//=============================================================================

inline uint64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::high_resolution_clock::now().time_since_epoch()
  ).count();
}

//=============================================================================
// Configuration
//=============================================================================

enum class Protocol {
  TEXT,
  BINARY
};

struct FeedConfig {
  std::string host = "127.0.0.1";
  uint16_t port = 0;
  Protocol protocol = Protocol::TEXT;
  size_t queue_size = 1024 * 1024;
  bool verbose = false;
  std::chrono::seconds reconnect_timeout{5};
  int max_reconnect_attempts = 3;

  bool is_valid() const { return port != 0; }
};

//=============================================================================
// Unified Tick Structure
//=============================================================================

struct Tick {
  uint64_t timestamp;
  char symbol[8];
  double price;
  int64_t volume;
  uint64_t recv_timestamp_ns;

  Tick() : timestamp(0), price(0.0), volume(0), recv_timestamp_ns(0) {
    symbol[0] = '\0';
  }

  explicit Tick(const TextTick& tt, uint64_t recv_ts) {
    timestamp = tt.timestamp;
    std::memcpy(symbol, tt.symbol, sizeof(symbol));
    price = tt.price;
    volume = tt.volume;
    recv_timestamp_ns = recv_ts;
  }

  explicit Tick(const TickPayload& tp, uint64_t recv_ts) {
    timestamp = tp.timestamp;
    std::memcpy(symbol, tp.symbol, 4);
    symbol[4] = '\0';
    price = static_cast<double>(tp.price);
    volume = tp.volume;
    recv_timestamp_ns = recv_ts;
  }
};

//=============================================================================
// Statistics Collector
//=============================================================================

class LatencyStats {
public:
  void reserve(size_t n) { latencies_.reserve(n); }
  void add(uint64_t latency_ns) { latencies_.push_back(latency_ns); }
  size_t count() const { return latencies_.size(); }

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

  double percentile(double p) const {
    if (latencies_.empty()) return 0.0;
    std::vector<uint64_t> sorted = latencies_;
    std::sort(sorted.begin(), sorted.end());
    size_t idx = static_cast<size_t>(sorted.size() * p / 100.0);
    return sorted[std::min(idx, sorted.size() - 1)] / 1000.0;
  }

private:
  std::vector<uint64_t> latencies_;
};

//=============================================================================
// Connection Manager
//=============================================================================

class Connection {
public:
  Connection(const std::string& host, uint16_t port, bool verbose = false)
      : host_(host), port_(port), verbose_(verbose), sockfd_(-1) {}

  ~Connection() { disconnect(); }

  bool connect() {
    sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_ < 0) {
      if (verbose_) perror("socket");
      return false;
    }

    // Enable TCP_NODELAY
    int flag = 1;
    setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    // Set receive buffer size
    int rcvbuf = 256 * 1024;
    setsockopt(sockfd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);

    if (inet_pton(AF_INET, host_.c_str(), &server_addr.sin_addr) <= 0) {
      server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }

    if (verbose_) {
      std::cout << "[Connection] Connecting to " << host_ << ":" << port_ << "...\n";
    }

    if (::connect(sockfd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
      if (verbose_) perror("connect");
      close(sockfd_);
      sockfd_ = -1;
      return false;
    }

    if (verbose_) {
      std::cout << "[Connection] Connected!\n";
    }
    return true;
  }

  void disconnect() {
    if (sockfd_ >= 0) {
      close(sockfd_);
      sockfd_ = -1;
    }
  }

  bool is_connected() const { return sockfd_ >= 0; }
  int fd() const { return sockfd_; }

private:
  std::string host_;
  uint16_t port_;
  bool verbose_;
  int sockfd_;
};

//=============================================================================
// Protocol Readers
//=============================================================================

class TextProtocolReader {
public:
  TextProtocolReader(int sockfd, SPSCQueue<Tick>& queue,
                     std::atomic<bool>& should_stop, bool verbose)
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
          Tick unified(*tick_opt, recv_ts);
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
  void enqueue_with_backpressure(const Tick& tick) {
    int retries = 0;
    while (!queue_.push(tick) && !should_stop_) {
      if (++retries > 1000) {
        std::this_thread::yield();
        retries = 0;
      }
    }
  }

  int sockfd_;
  SPSCQueue<Tick>& queue_;
  std::atomic<bool>& should_stop_;
  bool verbose_;
  TextLineBuffer line_buffer_;
  uint64_t messages_parsed_;
  uint64_t parse_errors_;
};

class BinaryProtocolReader {
public:
  BinaryProtocolReader(int sockfd, SPSCQueue<Tick>& queue,
                       std::atomic<bool>& should_stop, bool verbose)
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

      size_t consumed = 0;
      while (consumed + MessageHeader::HEADER_SIZE <= buffer_pos) {
        MessageHeader header = deserialize_header(recv_buffer + consumed);

        size_t total_msg_size = MessageHeader::HEADER_SIZE + header.length;
        if (consumed + total_msg_size > buffer_pos) {
          break;
        }

        if (header.type == MessageType::TICK) {
          TickPayload payload = deserialize_tick_payload(
              recv_buffer + consumed + MessageHeader::HEADER_SIZE);
          Tick unified(payload, recv_ts);
          enqueue_with_backpressure(unified);
          messages_parsed_++;
        }

        consumed += total_msg_size;
      }

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
  void enqueue_with_backpressure(const Tick& tick) {
    int retries = 0;
    while (!queue_.push(tick) && !should_stop_) {
      if (++retries > 1000) {
        std::this_thread::yield();
        retries = 0;
      }
    }
  }

  int sockfd_;
  SPSCQueue<Tick>& queue_;
  std::atomic<bool>& should_stop_;
  bool verbose_;
  uint64_t messages_parsed_;
  uint64_t parse_errors_;
};

//=============================================================================
// Tick Processor
//=============================================================================

using TickCallback = std::function<void(const Tick&)>;

class TickProcessor {
public:
  TickProcessor(SPSCQueue<Tick>& queue, std::atomic<bool>& should_stop,
                bool verbose, TickCallback callback = nullptr)
      : queue_(queue), should_stop_(should_stop), verbose_(verbose)
      , callback_(callback), messages_processed_(0) {
    e2e_latency_.reserve(1'000'000);
  }

  void run() {
    while (!should_stop_ || !queue_.empty()) {
      auto tick_opt = queue_.pop();
      if (tick_opt) {
        uint64_t process_ts = now_ns();
        const auto& tick = *tick_opt;

        e2e_latency_.add(process_ts - tick.recv_timestamp_ns);

        if (callback_) {
          callback_(tick);
        }

        messages_processed_++;

        if (verbose_ && messages_processed_ % 100000 == 0) {
          std::cout << "[Processor] Processed: " << messages_processed_
                    << " | Last: " << tick.symbol << " @ " << tick.price << std::endl;
        }
      } else {
        std::this_thread::yield();
      }
    }

    if (verbose_) {
      std::cout << "[Processor] Exiting. Processed: " << messages_processed_ << std::endl;
    }
  }

  uint64_t messages_processed() const { return messages_processed_; }
  const LatencyStats& latency_stats() const { return e2e_latency_; }

  void print_stats() const {
    std::cout << "\n=== End-to-End Latency ===" << std::endl;
    e2e_latency_.print("Recv → Process");
  }

private:
  SPSCQueue<Tick>& queue_;
  std::atomic<bool>& should_stop_;
  bool verbose_;
  TickCallback callback_;
  uint64_t messages_processed_;
  LatencyStats e2e_latency_;
};

//=============================================================================
// High-Level Feed Handler
//=============================================================================

class FeedHandler {
public:
  explicit FeedHandler(const FeedConfig& config)
      : config_(config)
      , queue_(config.queue_size)
      , should_stop_(false)
      , running_(false)
      , messages_parsed_(0)
      , parse_errors_(0) {}

  ~FeedHandler() {
    stop();
  }

  void set_tick_callback(TickCallback callback) {
    callback_ = callback;
  }

  bool start() {
    if (running_) return true;

    connection_ = std::make_unique<Connection>(config_.host, config_.port, config_.verbose);
    if (!connection_->connect()) {
      return false;
    }

    should_stop_ = false;
    running_ = true;
    start_time_ = std::chrono::steady_clock::now();

    // Start processor thread
    processor_ = std::make_unique<TickProcessor>(queue_, should_stop_, config_.verbose, callback_);
    processor_thread_ = std::thread([this]() { processor_->run(); });

    // Start reader thread
    if (config_.protocol == Protocol::TEXT) {
      text_reader_ = std::make_unique<TextProtocolReader>(
          connection_->fd(), queue_, should_stop_, config_.verbose);
      reader_thread_ = std::thread([this]() { text_reader_->run(); });
    } else {
      binary_reader_ = std::make_unique<BinaryProtocolReader>(
          connection_->fd(), queue_, should_stop_, config_.verbose);
      reader_thread_ = std::thread([this]() { binary_reader_->run(); });
    }

    return true;
  }

  void wait() {
    if (reader_thread_.joinable()) {
      reader_thread_.join();
    }
    should_stop_ = true;
    if (processor_thread_.joinable()) {
      processor_thread_.join();
    }
    end_time_ = std::chrono::steady_clock::now();
    update_stats();
    running_ = false;
  }

  void stop() {
    should_stop_ = true;
    if (connection_) {
      connection_->disconnect();
    }
    wait();
  }

  bool is_running() const { return running_; }

  // Statistics
  uint64_t messages_parsed() const { return messages_parsed_; }
  uint64_t messages_processed() const {
    return processor_ ? processor_->messages_processed() : 0;
  }
  uint64_t parse_errors() const { return parse_errors_; }

  double duration_ms() const {
    auto duration = end_time_ - start_time_;
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
  }

  double throughput() const {
    double ms = duration_ms();
    return ms > 0 ? messages_parsed_ * 1000.0 / ms : 0.0;
  }

  void print_stats() const {
    std::cout << "\n=== Feed Handler Summary ===" << std::endl;
    std::cout << "Duration: " << duration_ms() << " ms" << std::endl;
    std::cout << "Messages parsed: " << messages_parsed_ << std::endl;
    std::cout << "Messages processed: " << messages_processed() << std::endl;
    std::cout << "Parse errors: " << parse_errors_ << std::endl;
    std::cout << "Throughput: " << throughput() << " msgs/sec" << std::endl;

    if (processor_) {
      processor_->print_stats();
    }
  }

private:
  void update_stats() {
    if (text_reader_) {
      messages_parsed_ = text_reader_->messages_parsed();
      parse_errors_ = text_reader_->parse_errors();
    } else if (binary_reader_) {
      messages_parsed_ = binary_reader_->messages_parsed();
      parse_errors_ = binary_reader_->parse_errors();
    }
  }

  FeedConfig config_;
  SPSCQueue<Tick> queue_;
  std::atomic<bool> should_stop_;
  std::atomic<bool> running_;

  std::unique_ptr<Connection> connection_;
  std::unique_ptr<TextProtocolReader> text_reader_;
  std::unique_ptr<BinaryProtocolReader> binary_reader_;
  std::unique_ptr<TickProcessor> processor_;

  std::thread reader_thread_;
  std::thread processor_thread_;

  TickCallback callback_;

  std::chrono::steady_clock::time_point start_time_;
  std::chrono::steady_clock::time_point end_time_;

  uint64_t messages_parsed_;
  uint64_t parse_errors_;
};

//=============================================================================
// Book Updating Feed Handler (with Order Book integration)
//=============================================================================

class BookUpdatingFeedHandler {
public:
  explicit BookUpdatingFeedHandler(const FeedConfig& config)
      : config_(config), handler_(config) {
    handler_.set_tick_callback([this](const Tick& tick) {
      on_tick(tick);
    });
  }

  bool start() { return handler_.start(); }
  void wait() { handler_.wait(); }
  void stop() { handler_.stop(); }
  bool is_running() const { return handler_.is_running(); }

  void print_stats() const {
    handler_.print_stats();
    print_books();
  }

  void print_books() const {
    std::cout << "\n=== Order Books ===" << std::endl;
    for (const auto& [symbol, book] : books_) {
      book.print_top_of_book(symbol);
    }
  }

  const std::unordered_map<std::string, OrderBook>& books() const { return books_; }

private:
  void on_tick(const Tick& tick) {
    std::string symbol(tick.symbol);
    auto& book = get_or_create_book(symbol);
    book.apply_update(0, static_cast<float>(tick.price), tick.volume);
  }

  OrderBook& get_or_create_book(const std::string& symbol) {
    auto it = books_.find(symbol);
    if (it == books_.end()) {
      it = books_.emplace(symbol, OrderBook()).first;
    }
    return it->second;
  }

  FeedConfig config_;
  FeedHandler handler_;
  std::unordered_map<std::string, OrderBook> books_;
};

} // namespace net

#endif // NET_FEED_HPP
