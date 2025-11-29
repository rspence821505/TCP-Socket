#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <thread>
#include <unistd.h>

#include "binary_protocol_v2.hpp"
#include "connection_manager.hpp"
#include "ring_buffer.hpp"
#include "sequence_tracker.hpp"

// Helper: Get current timestamp in nanoseconds
inline uint64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

// Statistics
struct FeedStats {
  uint64_t ticks_received = 0;
  uint64_t heartbeats_received = 0;
  uint64_t reconnections = 0;
  uint64_t gaps_detected = 0;

  void print() const {
    std::cout << "\n=== Feed Handler Statistics ===" << std::endl;
    std::cout << "Ticks received:      " << ticks_received << std::endl;
    std::cout << "Heartbeats received: " << heartbeats_received << std::endl;
    std::cout << "Reconnections:       " << reconnections << std::endl;
    std::cout << "Sequence gaps:       " << gaps_detected << std::endl;
  }
};

class FeedHandler {
public:
  FeedHandler(const std::string &host, int port)
      : conn_manager_(host, port), should_stop_(false), stats_() {}

  void run() {
    std::cout << "=== Feed Handler with Heartbeat & Reconnection ==="
              << std::endl;
    std::cout << "Features:" << std::endl;
    std::cout << "  - Heartbeat detection (2s timeout)" << std::endl;
    std::cout << "  - Automatic reconnection with exponential backoff"
              << std::endl;
    std::cout << "  - Sequence number gap detection" << std::endl;
    std::cout << std::endl;

    // Initial connection
    if (!conn_manager_.connect()) {
      std::cerr << "Failed to connect to exchange" << std::endl;
      return;
    }

    // Main loop
    while (!should_stop_) {
      // Check for heartbeat timeout
      if (conn_manager_.is_heartbeat_timeout()) {
        std::cout << "[FeedHandler] ⚠️  Heartbeat timeout detected ("
                  << conn_manager_.seconds_since_last_message()
                  << "s since last message)" << std::endl;

        // Attempt reconnection
        if (conn_manager_.reconnect()) {
          stats_.reconnections++;
          sequence_tracker_.reset(); // Reset sequence tracking after reconnect
        } else {
          std::cerr << "[FeedHandler] Reconnection failed, retrying..."
                    << std::endl;
          continue;
        }
      }

      // Read and process messages
      if (!read_and_process()) {
        // Connection closed by server
        std::cout << "[FeedHandler] Connection closed by server" << std::endl;

        if (!should_stop_) {
          // Try to reconnect
          if (conn_manager_.reconnect()) {
            stats_.reconnections++;
            sequence_tracker_.reset();
          }
        }
      }
    }

    stats_.print();
  }

  void stop() { should_stop_ = true; }

private:
  bool read_and_process() {
    // Try to read from socket
    auto [write_ptr, write_space] = buffer_.get_write_ptr();

    if (write_space == 0) {
      std::cerr << "[FeedHandler] Ring buffer full!" << std::endl;
      return false;
    }

    ssize_t bytes_read =
        recv(conn_manager_.sockfd(), write_ptr, write_space, 0);

    if (bytes_read < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // No data available
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return true;
      } else {
        perror("[FeedHandler] recv failed");
        return false;
      }
    } else if (bytes_read == 0) {
      return false; // Connection closed
    }

    buffer_.commit_write(bytes_read);

    // Process all complete messages
    process_messages();

    return true;
  }

  void process_messages() {
    while (true) {
      // Check if we have at least the header
      if (buffer_.available() < MessageHeader::HEADER_SIZE) {
        break; // Need more data
      }

      // Peek at header
      char header_bytes[MessageHeader::HEADER_SIZE];
      if (!buffer_.peek_bytes(header_bytes, MessageHeader::HEADER_SIZE)) {
        break;
      }

      MessageHeader header = deserialize_header(header_bytes);

      // Validate header
      if (header.type != MessageType::TICK &&
          header.type != MessageType::HEARTBEAT) {
        std::cerr << "[FeedHandler] Unknown message type: "
                  << static_cast<int>(header.type) << std::endl;
        return;
      }

      // Check if complete message is available
      size_t total_size = MessageHeader::HEADER_SIZE + header.length;
      if (buffer_.available() < total_size) {
        break; // Need more data
      }

      // Extract complete message
      std::vector<char> message_bytes(total_size);
      if (!buffer_.read_bytes(message_bytes.data(), total_size)) {
        break;
      }

      // Update heartbeat timer (we received a message)
      conn_manager_.update_last_message_time();

      // Check sequence number
      bool sequence_ok = sequence_tracker_.process_sequence(header.sequence);
      if (!sequence_ok) {
        stats_.gaps_detected++;
      }

      // Process based on type
      const char *payload = message_bytes.data() + MessageHeader::HEADER_SIZE;

      if (header.type == MessageType::TICK) {
        process_tick(header, payload);
      } else if (header.type == MessageType::HEARTBEAT) {
        process_heartbeat(header, payload);
      }
    }
  }

  void process_tick(const MessageHeader &header, const char *payload) {
    TickPayload tick = deserialize_tick_payload(payload);

    stats_.ticks_received++;

    // Print periodically to avoid spam
    if (stats_.ticks_received % 10000 == 0) {
      std::string symbol(tick.symbol, 4);
      size_t null_pos = symbol.find('\0');
      if (null_pos != std::string::npos) {
        symbol = symbol.substr(0, null_pos);
      }

      std::cout << "[Tick seq=" << header.sequence << "] [" << symbol << "] $"
                << tick.price << " @ " << tick.volume << std::endl;
    }
  }

  void process_heartbeat(const MessageHeader &header, const char *payload) {
    HeartbeatPayload heartbeat = deserialize_heartbeat_payload(payload);

    stats_.heartbeats_received++;

    std::cout << "[Heartbeat seq=" << header.sequence
              << "] timestamp=" << heartbeat.timestamp << std::endl;
  }

  ConnectionManager conn_manager_;
  SequenceTracker sequence_tracker_;
  RingBuffer buffer_;
  std::atomic<bool> should_stop_;
  FeedStats stats_;
};

int main(int argc, char *argv[]) {
  std::string host = "127.0.0.1";
  int port = 9999;

  if (argc > 1) {
    port = std::atoi(argv[1]);
  }

  FeedHandler handler(host, port);

  // Set up signal handler for graceful shutdown
  std::thread handler_thread([&]() { handler.run(); });

  // Run for a while then stop (or wait for Ctrl+C)
  std::this_thread::sleep_for(std::chrono::seconds(60));
  handler.stop();

  handler_thread.join();

  return 0;
}
