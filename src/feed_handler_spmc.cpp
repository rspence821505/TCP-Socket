#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
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
#include "ring_buffer.hpp"
#include "spmc_queue.hpp"

// Message wrapper with timing
struct TimedMessage {
  BinaryTick tick;
  uint64_t recv_timestamp_ns;
  uint64_t parse_timestamp_ns;

  TimedMessage() = default;
  TimedMessage(const BinaryTick &t, uint64_t recv_ts, uint64_t parse_ts)
      : tick(t), recv_timestamp_ns(recv_ts), parse_timestamp_ns(parse_ts) {}
};

// Helper: Get current timestamp in nanoseconds
inline uint64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

// CRITICAL: This structure demonstrates FALSE SHARING
// Without padding, each consumer's counters are adjacent in memory
// When consumer1 updates its counter, it invalidates consumer2's cache line
struct ConsumerStatsUnpadded {
  uint64_t messages_processed;
  uint64_t total_latency_ns;

  ConsumerStatsUnpadded() : messages_processed(0), total_latency_ns(0) {}
};

// FIXED: Proper cache-line padding prevents false sharing
struct ConsumerStatsPadded {
  alignas(64) uint64_t messages_processed; // Start of cache line
  uint64_t total_latency_ns;

  // Padding to fill rest of cache line (64 bytes total)
  char padding[64 - 2 * sizeof(uint64_t)];

  ConsumerStatsPadded() : messages_processed(0), total_latency_ns(0) {}
};

// Reader thread (same as SPSC version - single producer)
class ReaderThread {
private:
  int sockfd_;
  SPMCQueue<TimedMessage> &queue_;
  RingBuffer buffer_;
  std::atomic<bool> &should_stop_;
  uint64_t messages_parsed_;
  uint64_t bytes_received_;

public:
  ReaderThread(int sockfd, SPMCQueue<TimedMessage> &queue,
               std::atomic<bool> &stop_flag)
      : sockfd_(sockfd), queue_(queue), should_stop_(stop_flag),
        messages_parsed_(0), bytes_received_(0) {}

  void run() {
    std::cout << "[Reader] Thread started" << std::endl;

    while (!should_stop_.load(std::memory_order_acquire)) {
      if (!read_from_socket()) {
        std::cout << "[Reader] Connection closed" << std::endl;
        break;
      }

      parse_and_enqueue();
    }

    std::cout << "[Reader] Thread exiting. Parsed " << messages_parsed_
              << " messages (" << bytes_received_ / 1024.0 / 1024.0 << " MB)"
              << std::endl;
  }

  uint64_t get_message_count() const { return messages_parsed_; }

private:
  bool read_from_socket() {
    auto [write_ptr, write_space] = buffer_.get_write_ptr();

    if (write_space == 0) {
      std::cerr << "[Reader] Ring buffer full!" << std::endl;
      return false;
    }

    ssize_t bytes_read = recv(sockfd_, write_ptr, write_space, 0);

    if (bytes_read < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        std::this_thread::yield();
        return true;
      } else {
        perror("[Reader] recv failed");
        return false;
      }
    } else if (bytes_read == 0) {
      return false;
    }

    uint64_t recv_timestamp = now_ns();
    buffer_.commit_write(bytes_read);
    bytes_received_ += bytes_read;
    last_recv_timestamp_ = recv_timestamp;

    return true;
  }

  void parse_and_enqueue() {
    while (true) {
      if (buffer_.available() < 4) {
        break;
      }

      char length_bytes[4];
      if (!buffer_.peek_bytes(length_bytes, 4)) {
        break;
      }

      uint32_t length_net;
      memcpy(&length_net, length_bytes, 4);
      uint32_t length = ntohl(length_net);

      if (length != BinaryTick::PAYLOAD_SIZE) {
        std::cerr << "[Reader] Invalid message length: " << length << std::endl;
        return;
      }

      size_t total_size = 4 + length;
      if (buffer_.available() < total_size) {
        break;
      }

      char message_bytes[4 + BinaryTick::PAYLOAD_SIZE];
      if (!buffer_.read_bytes(message_bytes, total_size)) {
        break;
      }

      const char *payload = message_bytes + 4;
      BinaryTick tick = deserialize_tick(payload);

      uint64_t parse_timestamp = now_ns();
      TimedMessage msg(tick, last_recv_timestamp_, parse_timestamp);

      // Spin if full (backpressure)
      while (!queue_.push(std::move(msg))) {
        std::this_thread::yield();
      }

      messages_parsed_++;
    }
  }

  uint64_t last_recv_timestamp_;
};

// Consumer thread template - can use either padded or unpadded stats
template <typename StatsType> class ConsumerThread {
private:
  int consumer_id_;
  SPMCQueue<TimedMessage> &queue_;
  std::atomic<bool> &reader_done_;
  StatsType &stats_; // Reference to shared stats (may have false sharing!)

public:
  ConsumerThread(int id, SPMCQueue<TimedMessage> &queue,
                 std::atomic<bool> &reader_done, StatsType &stats)
      : consumer_id_(id), queue_(queue), reader_done_(reader_done),
        stats_(stats) {}

  void run() {
    std::cout << "[Consumer " << consumer_id_ << "] Thread started"
              << std::endl;

    uint64_t local_messages = 0;
    uint64_t local_latency = 0;

    while (!reader_done_.load(std::memory_order_acquire) || !queue_.empty()) {
      auto msg = queue_.pop();

      if (msg) {
        uint64_t process_timestamp = now_ns();

        // Print periodically
        if (local_messages % 20000 == 0 && local_messages > 0) {
          std::string symbol(msg->tick.symbol, 4);
          size_t null_pos = symbol.find('\0');
          if (null_pos != std::string::npos) {
            symbol = symbol.substr(0, null_pos);
          }
          std::cout << "[Consumer " << consumer_id_ << "] [" << symbol << "] $"
                    << msg->tick.price << " @ " << msg->tick.volume
                    << std::endl;
        }

        // Accumulate stats locally (reduce atomic contention)
        local_messages++;
        local_latency += (process_timestamp - msg->recv_timestamp_ns);

        // Flush to shared stats every 1000 messages
        // THIS IS WHERE FALSE SHARING OCCURS if stats are unpadded!
        if (local_messages % 1000 == 0) {
          stats_.messages_processed += local_messages;
          stats_.total_latency_ns += local_latency;
          local_messages = 0;
          local_latency = 0;
        }
      } else {
        std::this_thread::yield();
      }
    }

    // Final flush
    stats_.messages_processed += local_messages;
    stats_.total_latency_ns += local_latency;

    std::cout << "[Consumer " << consumer_id_ << "] Thread exiting. Processed "
              << stats_.messages_processed << " messages" << std::endl;
  }
};

int connect_to_exchange(const std::string &host, int port) {
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("socket creation failed");
    return -1;
  }

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = inet_addr(host.c_str());

  if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("connection failed");
    close(sockfd);
    return -1;
  }

  int flags = fcntl(sockfd, F_GETFL, 0);
  if (flags == -1 || fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("fcntl failed");
    close(sockfd);
    return -1;
  }

  std::cout << "Connected to " << host << ":" << port << std::endl;
  return sockfd;
}

int main(int argc, char *argv[]) {
  std::string host = "127.0.0.1";
  int port = 9999;
  size_t queue_size = 2048;
  bool use_padding = false;

  if (argc > 1) {
    port = std::atoi(argv[1]);
  }
  if (argc > 2) {
    queue_size = std::atoi(argv[2]);
  }
  if (argc > 3) {
    use_padding = (std::string(argv[3]) == "padded");
  }

  std::cout << "=== Extension: SPMC Feed Handler ===" << std::endl;
  std::cout << "Configuration:" << std::endl;
  std::cout << "  Queue capacity: " << queue_size << std::endl;
  std::cout << "  Number of consumers: 2" << std::endl;
  std::cout << "  Cache-line padding: "
            << (use_padding ? "ENABLED" : "DISABLED") << std::endl;
  std::cout << std::endl;

  if (!use_padding) {
    std::cout << "⚠️  WARNING: Running WITHOUT padding - expect false sharing!"
              << std::endl;
    std::cout << "   Run with: " << argv[0] << " 9999 2048 padded" << std::endl;
    std::cout << std::endl;
  }

  int sockfd = connect_to_exchange(host, port);
  if (sockfd < 0) {
    return 1;
  }

  SPMCQueue<TimedMessage> queue(queue_size);
  std::atomic<bool> should_stop{false};
  std::atomic<bool> reader_done{false};

  auto start_time = std::chrono::steady_clock::now();

  if (use_padding) {
    // FIXED VERSION: Padded stats prevent false sharing
    std::array<ConsumerStatsPadded, 2> stats;

    std::thread consumer1([&]() {
      ConsumerThread<ConsumerStatsPadded> consumer(0, queue, reader_done,
                                                   stats[0]);
      consumer.run();
    });

    std::thread consumer2([&]() {
      ConsumerThread<ConsumerStatsPadded> consumer(1, queue, reader_done,
                                                   stats[1]);
      consumer.run();
    });

    ReaderThread reader(sockfd, queue, should_stop);
    std::thread reader_thread([&]() {
      reader.run();
      reader_done.store(true, std::memory_order_release);
    });

    reader_thread.join();
    consumer1.join();
    consumer2.join();

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    double seconds = duration.count() / 1000.0;

    uint64_t total_messages =
        stats[0].messages_processed + stats[1].messages_processed;
    uint64_t total_latency =
        stats[0].total_latency_ns + stats[1].total_latency_ns;

    std::cout << "\n=== Statistics ===" << std::endl;
    std::cout << "Consumer 0: " << stats[0].messages_processed << " messages"
              << std::endl;
    std::cout << "Consumer 1: " << stats[1].messages_processed << " messages"
              << std::endl;
    std::cout << "Total: " << total_messages << " messages in " << seconds
              << "s (" << static_cast<int>(total_messages / seconds)
              << " msgs/sec)" << std::endl;
    std::cout << "Average latency: "
              << (total_latency / total_messages / 1000.0) << " µs"
              << std::endl;

  } else {
    // BROKEN VERSION: Unpadded stats cause false sharing
    std::array<ConsumerStatsUnpadded, 2> stats;

    std::thread consumer1([&]() {
      ConsumerThread<ConsumerStatsUnpadded> consumer(0, queue, reader_done,
                                                     stats[0]);
      consumer.run();
    });

    std::thread consumer2([&]() {
      ConsumerThread<ConsumerStatsUnpadded> consumer(1, queue, reader_done,
                                                     stats[1]);
      consumer.run();
    });

    ReaderThread reader(sockfd, queue, should_stop);
    std::thread reader_thread([&]() {
      reader.run();
      reader_done.store(true, std::memory_order_release);
    });

    reader_thread.join();
    consumer1.join();
    consumer2.join();

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    double seconds = duration.count() / 1000.0;

    uint64_t total_messages =
        stats[0].messages_processed + stats[1].messages_processed;
    uint64_t total_latency =
        stats[0].total_latency_ns + stats[1].total_latency_ns;

    std::cout << "\n=== Statistics ===" << std::endl;
    std::cout << "Consumer 0: " << stats[0].messages_processed << " messages"
              << std::endl;
    std::cout << "Consumer 1: " << stats[1].messages_processed << " messages"
              << std::endl;
    std::cout << "Total: " << total_messages << " messages in " << seconds
              << "s (" << static_cast<int>(total_messages / seconds)
              << " msgs/sec)" << std::endl;
    std::cout << "Average latency: "
              << (total_latency / total_messages / 1000.0) << " µs"
              << std::endl;
  }

  close(sockfd);

  return 0;
}
