#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <algorithm>
#include <numeric>

#include "binary_protocol.hpp"
#include "ring_buffer.hpp"
#include "spsc_queue.hpp"

// Message wrapper with timing for latency measurement
struct TimedMessage {
  BinaryTick tick;
  uint64_t recv_timestamp_ns;  // When recv() completed
  uint64_t parse_timestamp_ns; // When parsing completed
  
  TimedMessage() = default;
  TimedMessage(const BinaryTick& t, uint64_t recv_ts, uint64_t parse_ts)
    : tick(t), recv_timestamp_ns(recv_ts), parse_timestamp_ns(parse_ts) {}
};

// Helper: Get current timestamp in nanoseconds
inline uint64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::high_resolution_clock::now().time_since_epoch()
  ).count();
}

// Statistics collector
struct LatencyStats {
  std::vector<uint64_t> recv_to_parse_ns;    // Network + parsing
  std::vector<uint64_t> parse_to_process_ns; // Queue + processing
  std::vector<uint64_t> total_latency_ns;    // End-to-end
  
  void reserve(size_t n) {
    recv_to_parse_ns.reserve(n);
    parse_to_process_ns.reserve(n);
    total_latency_ns.reserve(n);
  }
  
  void add_measurement(uint64_t recv_ts, uint64_t parse_ts, uint64_t process_ts) {
    recv_to_parse_ns.push_back(parse_ts - recv_ts);
    parse_to_process_ns.push_back(process_ts - parse_ts);
    total_latency_ns.push_back(process_ts - recv_ts);
  }
  
  void print_stats(const std::string& name, const std::vector<uint64_t>& latencies) const {
    if (latencies.empty()) return;
    
    std::vector<uint64_t> sorted = latencies;
    std::sort(sorted.begin(), sorted.end());
    
    uint64_t sum = std::accumulate(sorted.begin(), sorted.end(), 0ULL);
    double mean = static_cast<double>(sum) / sorted.size();
    
    uint64_t p50 = sorted[sorted.size() * 50 / 100];
    uint64_t p95 = sorted[sorted.size() * 95 / 100];
    uint64_t p99 = sorted[sorted.size() * 99 / 100];
    uint64_t max = sorted.back();
    
    std::cout << "  " << name << ":" << std::endl;
    std::cout << "    Mean: " << mean << " ns (" << mean/1000.0 << " µs)" << std::endl;
    std::cout << "    p50:  " << p50 << " ns (" << p50/1000.0 << " µs)" << std::endl;
    std::cout << "    p95:  " << p95 << " ns (" << p95/1000.0 << " µs)" << std::endl;
    std::cout << "    p99:  " << p99 << " ns (" << p99/1000.0 << " µs)" << std::endl;
    std::cout << "    Max:  " << max << " ns (" << max/1000.0 << " µs)" << std::endl;
  }
  
  void print_all() const {
    std::cout << "\n=== Latency Breakdown ===" << std::endl;
    print_stats("Recv → Parse", recv_to_parse_ns);
    print_stats("Parse → Process", parse_to_process_ns);
    print_stats("Total (Recv → Process)", total_latency_ns);
  }
};

// Reader thread: Network I/O → Parse → Enqueue
class ReaderThread {
private:
  int sockfd_;
  SPSCQueue<TimedMessage>& queue_;
  RingBuffer buffer_;
  std::atomic<bool>& should_stop_;
  uint64_t messages_parsed_;
  uint64_t bytes_received_;
  
public:
  ReaderThread(int sockfd, SPSCQueue<TimedMessage>& queue, std::atomic<bool>& stop_flag)
    : sockfd_(sockfd), queue_(queue), should_stop_(stop_flag)
    , messages_parsed_(0), bytes_received_(0) {}
  
  void run() {
    std::cout << "[Reader] Thread started" << std::endl;
    
    while (!should_stop_.load(std::memory_order_acquire)) {
      // Step 1: Read from socket into ring buffer
      if (!read_from_socket()) {
        std::cout << "[Reader] Connection closed" << std::endl;
        break;
      }
      
      // Step 2: Parse all complete messages and enqueue them
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
        // No data available, yield to consumer
        std::this_thread::yield();
        return true;
      } else {
        perror("[Reader] recv failed");
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
        std::cerr << "[Reader] Invalid message length: " << length << std::endl;
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
      
      const char* payload = message_bytes + 4;
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
  SPSCQueue<TimedMessage>& queue_;
  std::atomic<bool>& reader_done_;
  LatencyStats stats_;
  uint64_t messages_processed_;
  
public:
  ConsumerThread(SPSCQueue<TimedMessage>& queue, std::atomic<bool>& reader_done)
    : queue_(queue), reader_done_(reader_done), messages_processed_(0) {
    stats_.reserve(100000); // Pre-allocate
  }
  
  void run() {
    std::cout << "[Consumer] Thread started" << std::endl;
    
    while (!reader_done_.load(std::memory_order_acquire) || !queue_.empty()) {
      auto msg = queue_.pop();
      
      if (msg) {
        uint64_t process_timestamp = now_ns();
        
        // Process the message (just print periodically to avoid spam)
        if (messages_processed_ % 10000 == 0) {
          std::string symbol(msg->tick.symbol, 4);
          size_t null_pos = symbol.find('\0');
          if (null_pos != std::string::npos) {
            symbol = symbol.substr(0, null_pos);
          }
          std::cout << "[Consumer] [" << symbol << "] $" << msg->tick.price 
                    << " @ " << msg->tick.volume << std::endl;
        }
        
        // Record latency
        stats_.add_measurement(
          msg->recv_timestamp_ns,
          msg->parse_timestamp_ns,
          process_timestamp
        );
        
        messages_processed_++;
      } else {
        // Queue empty, yield to producer
        std::this_thread::yield();
      }
    }
    
    std::cout << "[Consumer] Thread exiting. Processed " << messages_processed_ 
              << " messages" << std::endl;
  }
  
  const LatencyStats& get_stats() const { return stats_; }
  uint64_t get_message_count() const { return messages_processed_; }
};

int connect_to_exchange(const std::string& host, int port) {
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
  
  if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
    perror("connection failed");
    close(sockfd);
    return -1;
  }
  
  // Set non-blocking mode
  int flags = fcntl(sockfd, F_GETFL, 0);
  if (flags == -1 || fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("fcntl failed");
    close(sockfd);
    return -1;
  }
  
  std::cout << "Connected to " << host << ":" << port << std::endl;
  return sockfd;
}

int main(int argc, char* argv[]) {
  std::string host = "127.0.0.1";
  int port = 9999;
  size_t queue_size = 1024;
  
  if (argc > 1) {
    port = std::atoi(argv[1]);
  }
  if (argc > 2) {
    queue_size = std::atoi(argv[2]);
  }
  
  std::cout << "=== Exercise 5: Feed Handler with Lock-Free SPSC Queue ===" << std::endl;
  std::cout << "Queue capacity: " << queue_size << std::endl;
  
  // Connect to exchange
  int sockfd = connect_to_exchange(host, port);
  if (sockfd < 0) {
    return 1;
  }
  
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
    end_time - start_time
  );
  double seconds = duration.count() / 1000.0;
  
  // Print statistics
  std::cout << "\n=== Statistics ===" << std::endl;
  std::cout << "Messages parsed: " << reader.get_message_count() << std::endl;
  std::cout << "Messages processed: " << consumer.get_message_count() << std::endl;
  std::cout << "Time: " << seconds << "s" << std::endl;
  std::cout << "Throughput: " 
            << static_cast<int>(consumer.get_message_count() / seconds)
            << " msgs/sec" << std::endl;
  
  consumer.get_stats().print_all();
  
  close(sockfd);
  
  return 0;
}
