#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <thread>
#include <unistd.h>

#include "binary_protocol.hpp"
#include "connection_manager.hpp"
#include "order_book.hpp"
#include "ring_buffer.hpp"
#include "sequence_tracker.hpp"

// Statistics
struct FeedStatsV2 {
  uint64_t ticks_received = 0;
  uint64_t heartbeats_received = 0;
  uint64_t snapshots_received = 0;
  uint64_t incremental_updates = 0;
  uint64_t reconnections = 0;
  uint64_t gaps_detected = 0;

  void print() const {
    std::cout << "\n=== Feed Handler Statistics ===" << std::endl;
    std::cout << "Ticks received:         " << ticks_received << std::endl;
    std::cout << "Heartbeats received:    " << heartbeats_received << std::endl;
    std::cout << "Snapshots received:     " << snapshots_received << std::endl;
    std::cout << "Incremental updates:    " << incremental_updates << std::endl;
    std::cout << "Reconnections:          " << reconnections << std::endl;
    std::cout << "Sequence gaps:          " << gaps_detected << std::endl;
  }
};

class SnapshotFeedHandler {
public:
  SnapshotFeedHandler(const std::string &host, int port,
                      const std::string &symbol)
      : conn_manager_(host, port), should_stop_(false), stats_(),
        symbol_(symbol), client_sequence_(0) {
    // Pad symbol to 4 chars
    while (symbol_.size() < 4)
      symbol_.push_back('\0');
    symbol_.resize(4);
  }

  void run() {
    std::cout << "=== Snapshot Recovery Feed Handler ===" << std::endl;
    std::cout << "Symbol: " << symbol_.substr(0, 4) << std::endl;
    std::cout << "State Machine: CONNECTING → SNAPSHOT_REQUEST → "
                 "SNAPSHOT_REPLAY → INCREMENTAL"
              << std::endl;
    std::cout << std::endl;

    // Initial connection
    if (!conn_manager_.connect()) {
      std::cerr << "Failed to connect to exchange" << std::endl;
      return;
    }

    // Transition to snapshot request state
    conn_manager_.transition_to_snapshot_request();

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
          sequence_tracker_.reset();
          conn_manager_.transition_to_snapshot_request(); // Need new snapshot
        } else {
          std::cerr << "[FeedHandler] Reconnection failed, retrying..."
                    << std::endl;
          continue;
        }
      }

      // Request snapshot if needed
      if (conn_manager_.needs_snapshot_request()) {
        send_snapshot_request();
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
            conn_manager_.transition_to_snapshot_request(); // Need new snapshot
          }
        }
      }
    }

    stats_.print();

    // Print final order book
    if (!order_book_.empty()) {
      order_book_.print_depth(symbol_.substr(0, 4), 10);
    }
  }

  void stop() { should_stop_ = true; }

private:
  void send_snapshot_request() {
    std::cout << "[FeedHandler] 📤 Sending snapshot request for symbol: "
              << symbol_.substr(0, 4) << std::endl;

    std::string request =
        serialize_snapshot_request(client_sequence_++, symbol_.data());

    ssize_t bytes_sent =
        send(conn_manager_.sockfd(), request.data(), request.length(), 0);
    if (bytes_sent < 0) {
      perror("[FeedHandler] Failed to send snapshot request");
      return;
    }

    conn_manager_.mark_snapshot_requested();
    std::cout << "[FeedHandler] Waiting for snapshot response..." << std::endl;
  }

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

      // Check sequence number (only for non-snapshot messages)
      if (header.type != MessageType::SNAPSHOT_RESPONSE) {
        bool sequence_ok = sequence_tracker_.process_sequence(header.sequence);
        if (!sequence_ok) {
          stats_.gaps_detected++;
        }
      }

      // Process based on type
      const char *payload = message_bytes.data() + MessageHeader::HEADER_SIZE;

      switch (header.type) {
      case MessageType::TICK:
        process_tick(header, payload);
        break;
      case MessageType::HEARTBEAT:
        process_heartbeat(header, payload);
        break;
      case MessageType::SNAPSHOT_RESPONSE:
        process_snapshot_response(header, payload);
        break;
      case MessageType::ORDER_BOOK_UPDATE:
        process_order_book_update(header, payload);
        break;
      default:
        std::cerr << "[FeedHandler] Unknown message type: "
                  << static_cast<int>(header.type) << std::endl;
      }
    }
  }

  void process_tick(const MessageHeader &header, const char *payload) {
    TickPayload tick = deserialize_tick_payload(payload);

    stats_.ticks_received++;

    // Print periodically
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
              << "] timestamp=" << heartbeat.timestamp
              << " | State: " << conn_manager_.state_name() << std::endl;
  }

  void process_snapshot_response(const MessageHeader &header,
                                 const char *payload) {
    char symbol[4];
    std::vector<OrderBookLevel> bids, asks;

    deserialize_snapshot_response(payload, header.length, symbol, bids, asks);

    stats_.snapshots_received++;

    std::string symbol_str(symbol, 4);
    size_t null_pos = symbol_str.find('\0');
    if (null_pos != std::string::npos) {
      symbol_str = symbol_str.substr(0, null_pos);
    }

    std::cout << "[Snapshot seq=" << header.sequence
              << "] Received snapshot for " << symbol_str << std::endl;
    std::cout << "  Bid levels: " << bids.size() << std::endl;
    std::cout << "  Ask levels: " << asks.size() << std::endl;

    // Load snapshot into order book
    order_book_.load_snapshot(bids, asks);

    // Print the snapshot
    order_book_.print_depth(symbol_str, 5);

    // Transition to snapshot replay
    conn_manager_.transition_to_snapshot_replay();

    // Immediately transition to incremental (snapshot is complete)
    conn_manager_.transition_to_incremental();
  }

  void process_order_book_update(const MessageHeader &header,
                                 const char *payload) {
    // Only process incremental updates if we're in INCREMENTAL mode
    if (!conn_manager_.is_incremental_mode()) {
      // Buffer updates during snapshot replay (in production, you'd queue
      // these)
      std::cout << "[FeedHandler] ⚠️  Ignoring incremental update (not in "
                   "INCREMENTAL mode yet)"
                << std::endl;
      return;
    }

    OrderBookUpdatePayload update = deserialize_order_book_update(payload);

    stats_.incremental_updates++;

    std::string symbol_str(update.symbol, 4);
    size_t null_pos = symbol_str.find('\0');
    if (null_pos != std::string::npos) {
      symbol_str = symbol_str.substr(0, null_pos);
    }

    // Apply update to order book
    order_book_.apply_update(update.side, update.price, update.quantity);

    // Print update and current top of book
    const char *side_str = (update.side == 0) ? "BID" : "ASK";
    const char *action_str = (update.quantity == 0)  ? "DELETE"
                             : (update.quantity > 0) ? "UPDATE"
                                                     : "INVALID";

    if (stats_.incremental_updates % 100 == 0) {
      std::cout << "[Update seq=" << header.sequence << "] [" << symbol_str
                << "] " << side_str << " " << action_str << " $" << update.price
                << " @ " << update.quantity << std::endl;

      // Print current top of book
      order_book_.print_top_of_book(symbol_str);
    }
  }

  ConnectionManagerV2 conn_manager_;
  SequenceTracker sequence_tracker_;
  RingBuffer buffer_;
  OrderBook order_book_;
  std::atomic<bool> should_stop_;
  FeedStatsV2 stats_;
  std::string symbol_;
  uint64_t client_sequence_;
};

int main(int argc, char *argv[]) {
  std::string host = "127.0.0.1";
  int port = 9999;
  std::string symbol = "AAPL";

  if (argc > 1) {
    port = std::atoi(argv[1]);
  }
  if (argc > 2) {
    symbol = argv[2];
  }

  SnapshotFeedHandler handler(host, port, symbol);

  // Run for a while then stop (or wait for Ctrl+C)
  std::thread handler_thread([&]() { handler.run(); });

  std::this_thread::sleep_for(std::chrono::seconds(60));
  handler.stop();

  handler_thread.join();

  return 0;
}
