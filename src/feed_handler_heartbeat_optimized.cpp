#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <thread>
#include <unistd.h>

#include "binary_protocol.hpp"
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
struct FeedStatsOptimized {
  uint64_t ticks_received = 0;
  uint64_t heartbeats_received = 0;
  uint64_t reconnections = 0;
  uint64_t gaps_detected = 0;

  void print() const {
    std::cout << "\n=== Feed Handler Statistics (OPTIMIZED) ===" << std::endl;
    std::cout << "Ticks received:      " << ticks_received << std::endl;
    std::cout << "Heartbeats received: " << heartbeats_received << std::endl;
    std::cout << "Reconnections:       " << reconnections << std::endl;
    std::cout << "Sequence gaps:       " << gaps_detected << std::endl;
  }
};

/**
 * Optimized Feed Handler
 *
 * Optimizations applied:
 * 1. Inline critical functions (process_tick, process_heartbeat)
 * 2. Branch prediction hints for hot/cold paths
 * 3. Reduced string operations (avoid allocation in hot path)
 * 4. Fast-path message type checking
 * 5. Optimized sequence tracker (sentinel values, no std::optional)
 * 6. Reduced modulo in print throttling (use bitwise AND)
 */
class FeedHandlerOptimized {
public:
  FeedHandlerOptimized(const std::string &host, int port)
      : conn_manager_(host, port), should_stop_(false), stats_() {}

  void run() {
    std::cout << "=== Optimized Feed Handler ===" << std::endl;
    std::cout << "Optimizations:" << std::endl;
    std::cout << "  ✅ Inlined hot functions" << std::endl;
    std::cout << "  ✅ Branch prediction hints" << std::endl;
    std::cout << "  ✅ Reduced string allocations" << std::endl;
    std::cout << "  ✅ Optimized sequence tracking" << std::endl;
    std::cout << "  ✅ Bitwise throttling for prints" << std::endl;
    std::cout << std::endl;

    // Initial connection
    if (!conn_manager_.connect()) {
      std::cerr << "Failed to connect to exchange" << std::endl;
      return;
    }

    // Main loop
    while (!should_stop_) {
      // Check for heartbeat timeout
      if (__builtin_expect(conn_manager_.is_heartbeat_timeout(), 0)) {
        std::cout << "[FeedHandler] ⚠️  Heartbeat timeout detected ("
                  << conn_manager_.seconds_since_last_message()
                  << "s since last message)" << std::endl;

        // Attempt reconnection
        if (conn_manager_.reconnect()) {
          stats_.reconnections++;
          sequence_tracker_.reset();
        } else {
          std::cerr << "[FeedHandler] Reconnection failed, retrying..."
                    << std::endl;
          continue;
        }
      }

      // Read and process messages
      if (__builtin_expect(!read_and_process(), 0)) {
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
  inline bool read_and_process() {
    // Try to read from socket
    auto [write_ptr, write_space] = buffer_.get_write_ptr();

    if (__builtin_expect(write_space == 0, 0)) {
      std::cerr << "[FeedHandler] Ring buffer full!" << std::endl;
      return false;
    }

    ssize_t bytes_read =
        recv(conn_manager_.sockfd(), write_ptr, write_space, 0);

    if (__builtin_expect(bytes_read < 0, 0)) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // No data available
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return true;
      } else {
        perror("[FeedHandler] recv failed");
        return false;
      }
    } else if (__builtin_expect(bytes_read == 0, 0)) {
      return false; // Connection closed
    }

    buffer_.commit_write(bytes_read);

    // Process all complete messages
    process_messages();

    return true;
  }

  inline void process_messages() {
    while (true) {
      // Check if we have at least the header
      if (__builtin_expect(buffer_.available() < MessageHeader::HEADER_SIZE,
                           0)) {
        break; // Need more data
      }

      // Peek at header
      char header_bytes[MessageHeader::HEADER_SIZE];
      if (__builtin_expect(
              !buffer_.peek_bytes(header_bytes, MessageHeader::HEADER_SIZE),
              0)) {
        break;
      }

      MessageHeader header = deserialize_header(header_bytes);

      // Fast-path type checking with branch prediction
      // Most messages are TICK, so check that first
      const bool is_tick = (header.type == MessageType::TICK);
      const bool is_heartbeat = (header.type == MessageType::HEARTBEAT);

      if (__builtin_expect(!is_tick && !is_heartbeat, 0)) {
        std::cerr << "[FeedHandler] Unknown message type: "
                  << static_cast<int>(header.type) << std::endl;
        return;
      }

      // Check if complete message is available
      const size_t total_size = MessageHeader::HEADER_SIZE + header.length;
      if (__builtin_expect(buffer_.available() < total_size, 0)) {
        break; // Need more data
      }

      // Extract complete message (stack allocation for small messages)
      char message_bytes[64]; // Enough for TICK or HEARTBEAT
      if (__builtin_expect(!buffer_.read_bytes(message_bytes, total_size), 0)) {
        break;
      }

      // Update heartbeat timer (we received a message)
      conn_manager_.update_last_message_time();

      // Check sequence number (inlined fast path)
      const bool sequence_ok =
          sequence_tracker_.process_sequence(header.sequence);
      if (__builtin_expect(!sequence_ok, 0)) {
        stats_.gaps_detected++;
      }

      // Process based on type (branch predicted)
      const char *payload = message_bytes + MessageHeader::HEADER_SIZE;

      if (__builtin_expect(is_tick, 1)) {
        process_tick_inline(header, payload);
      } else {
        process_heartbeat_inline(header, payload);
      }
    }
  }

  /**
   * OPTIMIZED: Inlined tick processing
   * - Avoids function call overhead
   * - Uses bitwise AND for throttling (faster than modulo)
   * - Reduces string allocations
   */
  inline __attribute__((always_inline)) void
  process_tick_inline(const MessageHeader &header, const char *payload) {
    // Deserialize inline
    uint64_t timestamp_net;
    memcpy(&timestamp_net, payload, 8);
    const uint64_t timestamp = ntohll(timestamp_net);

    const char *symbol = payload + 8; // Point directly, avoid copy

    float price;
    uint32_t price_net;
    memcpy(&price_net, payload + 12, 4);
    const uint32_t price_bits = ntohl(price_net);
    memcpy(&price, &price_bits, 4);

    int32_t volume;
    int32_t volume_net;
    memcpy(&volume_net, payload + 16, 4);
    volume = ntohl(volume_net);

    stats_.ticks_received++;

    // Print periodically using bitwise AND (faster than modulo)
    // (stats_.ticks_received & 0x3FFF) == 0 means every 16384 messages
    if (__builtin_expect((stats_.ticks_received & 0x3FFF) == 0, 0)) {
      // Only compute symbol length when printing
      const int symbol_len = strnlen(symbol, 4);

      std::cout << "[Tick seq=" << header.sequence << "] [";
      std::cout.write(symbol, symbol_len);
      std::cout << "] $" << price << " @ " << volume << std::endl;
    }
  }

  /**
   * OPTIMIZED: Inlined heartbeat processing
   * - Avoids function call overhead
   * - Fast path for common heartbeat
   */
  inline __attribute__((always_inline)) void
  process_heartbeat_inline(const MessageHeader &header, const char *payload) {
    uint64_t timestamp_net;
    memcpy(&timestamp_net, payload, 8);
    const uint64_t timestamp = ntohll(timestamp_net);

    stats_.heartbeats_received++;

    std::cout << "[Heartbeat seq=" << header.sequence
              << "] timestamp=" << timestamp << std::endl;
  }

  ConnectionManager conn_manager_;
  SequenceTrackerOptimized sequence_tracker_;
  RingBuffer buffer_;
  std::atomic<bool> should_stop_;
  FeedStatsOptimized stats_;
};

int main(int argc, char *argv[]) {
  std::string host = "127.0.0.1";
  int port = 9999;

  if (argc > 1) {
    port = std::atoi(argv[1]);
  }

  FeedHandlerOptimized handler(host, port);

  // Set up signal handler for graceful shutdown
  std::thread handler_thread([&]() { handler.run(); });

  // Run for a while then stop (or wait for Ctrl+C)
  std::this_thread::sleep_for(std::chrono::seconds(60));
  handler.stop();

  handler_thread.join();

  return 0;
}
