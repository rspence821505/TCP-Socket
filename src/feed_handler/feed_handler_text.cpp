#include <arpa/inet.h>
#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "common.hpp"
#include "spsc_queue.hpp"
#include "text_protocol.hpp"

/**
 * Text Protocol Feed Handler
 *
 * Receives newline-delimited tick messages:
 *   timestamp symbol price volume\n
 *
 * Architecture:
 *   Reader Thread → SPSC Queue → Processor Thread
 *
 * Usage: feed_handler_text <port> [host]
 */

// Timed message for latency measurement
struct TimedTextTick {
  TextTick tick;
  uint64_t recv_timestamp_ns;
  uint64_t parse_timestamp_ns;
};

// Reader thread: receives data, parses lines, enqueues ticks
class ReaderThread {
public:
  ReaderThread(int sockfd, SPSCQueue<TimedTextTick>& queue, std::atomic<bool>& should_stop)
      : sockfd_(sockfd)
      , queue_(queue)
      , should_stop_(should_stop)
      , messages_parsed_(0)
      , parse_errors_(0) {}

  void run() {
    char recv_buffer[16 * 1024];

    while (!should_stop_) {
      ssize_t bytes_read = recv(sockfd_, recv_buffer, sizeof(recv_buffer), 0);
      uint64_t recv_ts = now_ns();

      if (bytes_read <= 0) {
        if (bytes_read == 0) {
          LOG_INFO("Reader", "Server closed connection");
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
          LOG_PERROR("Reader", "recv");
        }
        should_stop_ = true;
        break;
      }

      // Add received data to line buffer
      if (!line_buffer_.append(recv_buffer, bytes_read)) {
        LOG_ERROR("Reader", "Buffer overflow!");
        line_buffer_.reset();
        continue;
      }

      // Parse complete lines
      std::string_view line;
      while (line_buffer_.get_line(line)) {
        uint64_t parse_ts = now_ns();

        auto tick_opt = parse_text_tick(line);
        if (tick_opt) {
          TimedTextTick timed_tick;
          timed_tick.tick = *tick_opt;
          timed_tick.recv_timestamp_ns = recv_ts;
          timed_tick.parse_timestamp_ns = parse_ts;

          // Try to enqueue (with backpressure)
          int retries = 0;
          while (!queue_.push(timed_tick) && !should_stop_) {
            if (++retries > 1000) {
              std::this_thread::yield();
              retries = 0;
            }
          }

          messages_parsed_++;
        } else {
          parse_errors_++;
          // Uncomment to debug parse errors:
          // std::cerr << "[Reader] Parse error: " << line << std::endl;
        }
      }
    }

    LOG_INFO("Reader", "Exiting. Parsed: %lu, Errors: %lu", messages_parsed_, parse_errors_);
  }

  uint64_t messages_parsed() const { return messages_parsed_; }
  uint64_t parse_errors() const { return parse_errors_; }

private:
  int sockfd_;
  SPSCQueue<TimedTextTick>& queue_;
  std::atomic<bool>& should_stop_;
  TextLineBuffer line_buffer_;
  uint64_t messages_parsed_;
  uint64_t parse_errors_;
};

// Processor thread: dequeues ticks, updates stats
class ProcessorThread {
public:
  ProcessorThread(SPSCQueue<TimedTextTick>& queue, std::atomic<bool>& should_stop)
      : queue_(queue)
      , should_stop_(should_stop)
      , messages_processed_(0) {
    parse_latency_.reserve(1'000'000);
    total_latency_.reserve(1'000'000);
  }

  void run() {
    while (!should_stop_ || !queue_.empty()) {
      auto tick_opt = queue_.pop();
      if (tick_opt) {
        uint64_t process_ts = now_ns();
        const auto& tick = *tick_opt;

        // Record latencies
        parse_latency_.add(tick.parse_timestamp_ns - tick.recv_timestamp_ns);
        total_latency_.add(process_ts - tick.recv_timestamp_ns);

        messages_processed_++;

        // Print progress every 100k messages
        if (messages_processed_ % 100000 == 0) {
          LOG_INFO("Processor", "Processed: %lu | Last: %s @ %.2f",
                   messages_processed_, tick.tick.symbol, tick.tick.price);
        }
      } else {
        std::this_thread::yield();
      }
    }

    LOG_INFO("Processor", "Exiting. Processed: %lu", messages_processed_);
  }

  void print_stats() const {
    std::cout << "\n=== Latency Statistics ===" << std::endl;
    parse_latency_.print("Recv → Parse");
    total_latency_.print("Recv → Process (Total)");
  }

  uint64_t messages_processed() const { return messages_processed_; }

private:
  SPSCQueue<TimedTextTick>& queue_;
  std::atomic<bool>& should_stop_;
  uint64_t messages_processed_;
  LatencyStats parse_latency_;
  LatencyStats total_latency_;
};

Result<int> connect_to_server(const char* host, int port) {
  SocketOptions opts;
  opts.tcp_nodelay = true;
  opts.recv_buffer_size = 256 * 1024;

  LOG_INFO("Connect", "Connecting to %s:%d...", host, port);

  auto result = socket_connect(host, port, opts);
  if (!result) {
    return Result<int>::error(result.error());
  }

  LOG_INFO("Connect", "Connected!");
  return result;
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    LOG_ERROR("Main", "Usage: %s <port> [host]", argv[0]);
    LOG_ERROR("Main", "Example: %s 9999 127.0.0.1", argv[0]);
    LOG_ERROR("Main", "Receives text ticks in format: timestamp symbol price volume\\n");
    return 1;
  }

  int port = std::atoi(argv[1]);
  const char* host = (argc > 2) ? argv[2] : "127.0.0.1";

  // Connect to server
  auto connect_result = connect_to_server(host, port);
  if (!connect_result) {
    LOG_ERROR("Main", "%s", connect_result.error().c_str());
    return 1;
  }
  int sockfd = connect_result.value();

  // Create queue and control flag
  SPSCQueue<TimedTextTick> queue(1024 * 1024);  // 1M capacity
  std::atomic<bool> should_stop{false};

  // Create threads
  ReaderThread reader(sockfd, queue, should_stop);
  ProcessorThread processor(queue, should_stop);

  auto start_time = std::chrono::steady_clock::now();

  // Start threads
  std::thread reader_thread([&reader]() { reader.run(); });
  std::thread processor_thread([&processor]() { processor.run(); });

  // Wait for reader to finish (server disconnect or error)
  reader_thread.join();

  // Signal processor to stop and wait
  should_stop = true;
  processor_thread.join();

  auto end_time = std::chrono::steady_clock::now();
  auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time
  ).count();

  // Print statistics
  std::cout << "\n=== Summary ===" << std::endl;
  std::cout << "Duration: " << duration_ms << " ms" << std::endl;
  std::cout << "Messages parsed: " << reader.messages_parsed() << std::endl;
  std::cout << "Messages processed: " << processor.messages_processed() << std::endl;
  std::cout << "Parse errors: " << reader.parse_errors() << std::endl;

  if (duration_ms > 0) {
    double rate = reader.messages_parsed() * 1000.0 / duration_ms;
    std::cout << "Throughput: " << rate << " msgs/sec" << std::endl;
  }

  processor.print_stats();

  close(sockfd);
  return 0;
}
