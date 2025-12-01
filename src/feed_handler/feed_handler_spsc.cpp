#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <numeric>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "binary_protocol.hpp"
#include "common.hpp"
#include "ring_buffer.hpp"
#include "spsc_queue.hpp"

// Message wrapper with timing for latency measurement
struct TimedMessage {
  BinaryTick tick;
  uint64_t recv_timestamp_ns;  // When recv() completed
  uint64_t parse_timestamp_ns; // When parsing completed

  TimedMessage() = default;
  TimedMessage(const BinaryTick &t, uint64_t recv_ts, uint64_t parse_ts)
      : tick(t), recv_timestamp_ns(recv_ts), parse_timestamp_ns(parse_ts) {}
};

// Latency breakdown using consolidated LatencyStats from common.hpp
struct FeedLatencyStats {
  LatencyStats recv_to_parse;    // Network + parsing
  LatencyStats parse_to_process; // Queue + processing
  LatencyStats total_latency;    // End-to-end

  void reserve(size_t n) {
    recv_to_parse.reserve(n);
    parse_to_process.reserve(n);
    total_latency.reserve(n);
  }

  void add_measurement(uint64_t recv_ts, uint64_t parse_ts,
                       uint64_t process_ts) {
    recv_to_parse.add(parse_ts - recv_ts);
    parse_to_process.add(process_ts - parse_ts);
    total_latency.add(process_ts - recv_ts);
  }

  void print_all() const {
    std::cout << "\n=== Latency Breakdown ===" << std::endl;
    recv_to_parse.print_indented("Recv → Parse");
    parse_to_process.print_indented("Parse → Process");
    total_latency.print_indented("Total (Recv → Process)");
  }
};

// Reader thread: Network I/O → Parse → Enqueue
class ReaderThread {
private:
  int sockfd_;
  SPSCQueue<TimedMessage> &queue_;
  RingBuffer buffer_;
  std::atomic<bool> &should_stop_;
  uint64_t messages_parsed_;
  uint64_t bytes_received_;

public:
  ReaderThread(int sockfd, SPSCQueue<TimedMessage> &queue,
               std::atomic<bool> &stop_flag)
      : sockfd_(sockfd), queue_(queue), should_stop_(stop_flag),
        messages_parsed_(0), bytes_received_(0) {}

  void run() {
    LOG_INFO("Reader", "Thread started");

    while (!should_stop_.load(std::memory_order_acquire)) {
      // Step 1: Read from socket into ring buffer
      if (!read_from_socket()) {
        LOG_INFO("Reader", "Connection closed");
        break;
      }

      // Step 2: Parse all complete messages and enqueue them
      parse_and_enqueue();
    }

    LOG_INFO("Reader", "Thread exiting. Parsed %lu messages (%s)",
             messages_parsed_, format_bytes(bytes_received_).c_str());
  }

  uint64_t get_message_count() const { return messages_parsed_; }

private:
  bool read_from_socket() {
    auto [write_ptr, write_space] = buffer_.get_write_ptr();

    if (write_space == 0) {
      LOG_ERROR("Reader", "Ring buffer full!");
      return false;
    }

    ssize_t bytes_read = recv(sockfd_, write_ptr, write_space, 0);

    if (bytes_read < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // No data available, yield to consumer
        std::this_thread::yield();
        return true;
      } else {
        LOG_PERROR("Reader", "recv failed");
        return false;
      }
    } else if (bytes_read == 0) {
      return false; // Connection closed
    }

    // Timestamp immediately after recv() completes
    uint64_t recv_timestamp = now_ns();

    buffer_.commit_write(bytes_read);
    bytes_received_ += bytes_read;

    // Store the recv timestamp for the next message(s) we parse
    // Note: This is a simplification - in reality, all bytes in this recv()
    // should share the same timestamp. For simplicity, we'll use this for
    // all messages parsed in the next step.
    last_recv_timestamp_ = recv_timestamp;

    return true;
  }

  void parse_and_enqueue() {
    while (true) {
      // Check for complete message
      if (buffer_.available() < 4) {
        break; // Need length prefix
      }

      // Read length prefix
      char length_bytes[4];
      if (!buffer_.peek_bytes(length_bytes, 4)) {
        break;
      }

      uint32_t length_net;
      memcpy(&length_net, length_bytes, 4);
      uint32_t length = ntohl(length_net);

      // Validate length
      if (length != BinaryTick::PAYLOAD_SIZE) {
        LOG_ERROR("Reader", "Invalid message length: %u", length);
        return;
      }

      // Check if complete message is available
      size_t total_size = 4 + length;
      if (buffer_.available() < total_size) {
        break; // Need more data
      }

      // Extract and parse message
      char message_bytes[4 + BinaryTick::PAYLOAD_SIZE];
      if (!buffer_.read_bytes(message_bytes, total_size)) {
        break;
      }

      const char *payload = message_bytes + 4;
      BinaryTick tick = deserialize_tick(payload);

      uint64_t parse_timestamp = now_ns();

      // Create timed message
      TimedMessage msg(tick, last_recv_timestamp_, parse_timestamp);

      // Enqueue (spin if full, as requested: "block and wait")
      while (!queue_.push(std::move(msg))) {
        std::this_thread::yield();
      }

      messages_parsed_++;
    }
  }

  uint64_t last_recv_timestamp_;
};

// Consumer thread: Dequeue → Process → Record latency
class ConsumerThread {
private:
  SPSCQueue<TimedMessage> &queue_;
  std::atomic<bool> &reader_done_;
  FeedLatencyStats stats_;
  uint64_t messages_processed_;

public:
  ConsumerThread(SPSCQueue<TimedMessage> &queue, std::atomic<bool> &reader_done)
      : queue_(queue), reader_done_(reader_done), messages_processed_(0) {
    stats_.reserve(100000); // Pre-allocate
  }

  void run() {
    LOG_INFO("Consumer", "Thread started");

    while (!reader_done_.load(std::memory_order_acquire) || !queue_.empty()) {
      auto msg = queue_.pop();

      if (msg) {
        uint64_t process_timestamp = now_ns();

        // Process the message (just print periodically to avoid spam)
        if (messages_processed_ % 10000 == 0) {
          std::string symbol = trim_symbol(msg->tick.symbol, 4);
          LOG_INFO("Consumer", "[%s] $%.2f @ %d", symbol.c_str(), msg->tick.price, msg->tick.volume);
        }

        // Record latency
        stats_.add_measurement(msg->recv_timestamp_ns, msg->parse_timestamp_ns,
                               process_timestamp);

        messages_processed_++;
      } else {
        // Queue empty, yield to producer
        std::this_thread::yield();
      }
    }

    LOG_INFO("Consumer", "Thread exiting. Processed %lu messages", messages_processed_);
  }

  const FeedLatencyStats &get_stats() const { return stats_; }
  uint64_t get_message_count() const { return messages_processed_; }
};

Result<int> connect_to_exchange(const std::string &host, int port) {
  SocketOptions opts;
  opts.non_blocking = true;

  auto result = socket_connect(host, port, opts);
  if (!result) {
    return Result<int>::error(result.error());
  }

  LOG_INFO("Connect", "Connected to %s:%d", host.c_str(), port);
  return result;
}

int main(int argc, char *argv[]) {
  std::string host = "127.0.0.1";
  int port = 9999;
  size_t queue_size = 1024;

  if (argc > 1) {
    port = std::atoi(argv[1]);
  }
  if (argc > 2) {
    queue_size = std::atoi(argv[2]);
  }

  std::cout << "=== Feed Handler with Lock-Free SPSC Queue ===" << std::endl;
  std::cout << "Queue capacity: " << queue_size << std::endl;

  // Connect to exchange
  auto connect_result = connect_to_exchange(host, port);
  if (!connect_result) {
    LOG_ERROR("Main", "%s", connect_result.error().c_str());
    return 1;
  }
  int sockfd = connect_result.value();

  // Create shared state
  SPSCQueue<TimedMessage> queue(queue_size);
  std::atomic<bool> should_stop{false};
  std::atomic<bool> reader_done{false};

  auto start_time = std::chrono::steady_clock::now();

  // Start consumer thread first
  ConsumerThread consumer(queue, reader_done);
  std::thread consumer_thread([&]() { consumer.run(); });

  // Start reader thread
  ReaderThread reader(sockfd, queue, should_stop);
  std::thread reader_thread([&]() {
    reader.run();
    reader_done.store(true, std::memory_order_release);
  });

  // Wait for threads to complete
  reader_thread.join();
  consumer_thread.join();

  auto end_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);
  double seconds = duration.count() / 1000.0;

  // Print statistics
  std::cout << "\n=== Statistics ===" << std::endl;
  std::cout << "Messages parsed: " << reader.get_message_count() << std::endl;
  std::cout << "Messages processed: " << consumer.get_message_count()
            << std::endl;
  std::cout << "Time: " << seconds << "s" << std::endl;
  std::cout << "Throughput: "
            << static_cast<int>(consumer.get_message_count() / seconds)
            << " msgs/sec" << std::endl;

  consumer.get_stats().print_all();

  close(sockfd);

  return 0;
}
