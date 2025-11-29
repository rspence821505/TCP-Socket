#ifndef BINARY_PROTOCOL_HPP
#define BINARY_PROTOCOL_HPP

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Message types
enum class MessageType : uint8_t {
  TICK = 0x01,
  HEARTBEAT = 0xFF,
  SNAPSHOT_REQUEST = 0x10,
  SNAPSHOT_RESPONSE = 0x11,
  ORDER_BOOK_UPDATE = 0x02  // Incremental update
};

// Base message header: [4-byte length][1-byte type][8-byte sequence]
struct MessageHeader {
  uint32_t length;      // Payload length (not including header)
  MessageType type;
  uint64_t sequence;    // Monotonically increasing sequence number
  
  static constexpr size_t HEADER_SIZE = 4 + 1 + 8; // 13 bytes
};

// Tick message payload
struct TickPayload {
  uint64_t timestamp;
  char symbol[4];
  float price;
  int32_t volume;
  
  static constexpr size_t PAYLOAD_SIZE = 8 + 4 + 4 + 4; // 20 bytes
};

// Backward compatibility alias
using BinaryTick = TickPayload;

// Heartbeat message payload
struct HeartbeatPayload {
  uint64_t timestamp;
  
  static constexpr size_t PAYLOAD_SIZE = 8; // 8 bytes
};

// Snapshot request payload
struct SnapshotRequestPayload {
  char symbol[4];  // Symbol to request (or "ALL\0" for all symbols)
  
  static constexpr size_t PAYLOAD_SIZE = 4; // 4 bytes
};

// Order book level (for snapshot)
struct OrderBookLevel {
  float price;
  uint64_t quantity;
  
  static constexpr size_t SIZE = 4 + 8; // 12 bytes
};

// Snapshot response payload (variable length)
struct SnapshotResponsePayload {
  char symbol[4];
  uint8_t num_bid_levels;   // Number of bid levels
  uint8_t num_ask_levels;   // Number of ask levels
  // Followed by: bid_levels[num_bid_levels] then ask_levels[num_ask_levels]
  
  static constexpr size_t HEADER_SIZE = 4 + 1 + 1; // 6 bytes (before levels)
};

// Order book update payload (incremental)
struct OrderBookUpdatePayload {
  char symbol[4];
  uint8_t side;      // 0 = bid, 1 = ask
  float price;
  int64_t quantity;  // Signed: positive = add/update, 0 = delete
  
  static constexpr size_t PAYLOAD_SIZE = 4 + 1 + 4 + 8; // 17 bytes
};

// Helper to convert uint64_t to/from network byte order
#ifndef htonll
inline uint64_t htonll(uint64_t value) {
  static const int num = 42;
  if (*reinterpret_cast<const char*>(&num) == num) {
    const uint32_t high_part = htonl(static_cast<uint32_t>(value >> 32));
    const uint32_t low_part = htonl(static_cast<uint32_t>(value & 0xFFFFFFFFLL));
    return (static_cast<uint64_t>(low_part) << 32) | high_part;
  }
  return value;
}
#endif

#ifndef ntohll
inline uint64_t ntohll(uint64_t value) {
  return htonll(value);
}
#endif

// Serialize message header
inline void serialize_header(std::string& message, MessageType type, 
                             uint64_t sequence, uint32_t payload_size) {
  uint32_t length_net = htonl(payload_size);
  message.append(reinterpret_cast<const char*>(&length_net), 4);
  
  uint8_t type_byte = static_cast<uint8_t>(type);
  message.append(reinterpret_cast<const char*>(&type_byte), 1);
  
  uint64_t sequence_net = htonll(sequence);
  message.append(reinterpret_cast<const char*>(&sequence_net), 8);
}

// Serialize tick message
inline std::string serialize_tick(uint64_t sequence, uint64_t timestamp,
                                 const char symbol[4], float price, int32_t volume) {
  std::string message;
  message.reserve(MessageHeader::HEADER_SIZE + TickPayload::PAYLOAD_SIZE);
  
  serialize_header(message, MessageType::TICK, sequence, TickPayload::PAYLOAD_SIZE);
  
  uint64_t timestamp_net = htonll(timestamp);
  message.append(reinterpret_cast<const char*>(&timestamp_net), 8);
  message.append(symbol, 4);
  
  uint32_t price_bits;
  memcpy(&price_bits, &price, 4);
  uint32_t price_net = htonl(price_bits);
  message.append(reinterpret_cast<const char*>(&price_net), 4);
  
  int32_t volume_net = htonl(volume);
  message.append(reinterpret_cast<const char*>(&volume_net), 4);
  
  return message;
}

// Backward compatibility overload (auto-increment sequence)
inline std::string serialize_tick(const BinaryTick& tick) {
  static uint64_t sequence = 0;
  return serialize_tick(++sequence, tick.timestamp, tick.symbol, tick.price, tick.volume);
}

// Serialize heartbeat message
inline std::string serialize_heartbeat(uint64_t sequence, uint64_t timestamp) {
  std::string message;
  message.reserve(MessageHeader::HEADER_SIZE + HeartbeatPayload::PAYLOAD_SIZE);
  
  serialize_header(message, MessageType::HEARTBEAT, sequence, HeartbeatPayload::PAYLOAD_SIZE);
  
  uint64_t timestamp_net = htonll(timestamp);
  message.append(reinterpret_cast<const char*>(&timestamp_net), 8);
  
  return message;
}

// Serialize snapshot request
inline std::string serialize_snapshot_request(uint64_t sequence, const char symbol[4]) {
  std::string message;
  message.reserve(MessageHeader::HEADER_SIZE + SnapshotRequestPayload::PAYLOAD_SIZE);
  
  serialize_header(message, MessageType::SNAPSHOT_REQUEST, sequence, 
                  SnapshotRequestPayload::PAYLOAD_SIZE);
  
  message.append(symbol, 4);
  
  return message;
}

// Serialize snapshot response
inline std::string serialize_snapshot_response(uint64_t sequence, const char symbol[4],
                                              const std::vector<OrderBookLevel>& bids,
                                              const std::vector<OrderBookLevel>& asks) {
  std::string message;
  uint32_t payload_size = SnapshotResponsePayload::HEADER_SIZE + 
                         (bids.size() + asks.size()) * OrderBookLevel::SIZE;
  message.reserve(MessageHeader::HEADER_SIZE + payload_size);
  
  serialize_header(message, MessageType::SNAPSHOT_RESPONSE, sequence, payload_size);
  
  // Snapshot header
  message.append(symbol, 4);
  uint8_t num_bids = static_cast<uint8_t>(bids.size());
  uint8_t num_asks = static_cast<uint8_t>(asks.size());
  message.append(reinterpret_cast<const char*>(&num_bids), 1);
  message.append(reinterpret_cast<const char*>(&num_asks), 1);
  
  // Bid levels
  for (const auto& level : bids) {
    uint32_t price_bits;
    memcpy(&price_bits, &level.price, 4);
    uint32_t price_net = htonl(price_bits);
    message.append(reinterpret_cast<const char*>(&price_net), 4);
    
    uint64_t qty_net = htonll(level.quantity);
    message.append(reinterpret_cast<const char*>(&qty_net), 8);
  }
  
  // Ask levels
  for (const auto& level : asks) {
    uint32_t price_bits;
    memcpy(&price_bits, &level.price, 4);
    uint32_t price_net = htonl(price_bits);
    message.append(reinterpret_cast<const char*>(&price_net), 4);
    
    uint64_t qty_net = htonll(level.quantity);
    message.append(reinterpret_cast<const char*>(&qty_net), 8);
  }
  
  return message;
}

// Serialize order book update
inline std::string serialize_order_book_update(uint64_t sequence, const char symbol[4],
                                              uint8_t side, float price, int64_t quantity) {
  std::string message;
  message.reserve(MessageHeader::HEADER_SIZE + OrderBookUpdatePayload::PAYLOAD_SIZE);
  
  serialize_header(message, MessageType::ORDER_BOOK_UPDATE, sequence, 
                  OrderBookUpdatePayload::PAYLOAD_SIZE);
  
  message.append(symbol, 4);
  message.append(reinterpret_cast<const char*>(&side), 1);
  
  uint32_t price_bits;
  memcpy(&price_bits, &price, 4);
  uint32_t price_net = htonl(price_bits);
  message.append(reinterpret_cast<const char*>(&price_net), 4);
  
  int64_t qty_net = htonll(static_cast<uint64_t>(quantity));
  message.append(reinterpret_cast<const char*>(&qty_net), 8);
  
  return message;
}

// Deserialize header from raw bytes
inline MessageHeader deserialize_header(const char* data) {
  MessageHeader header;
  
  uint32_t length_net;
  memcpy(&length_net, data, 4);
  header.length = ntohl(length_net);
  data += 4;
  
  uint8_t type_byte;
  memcpy(&type_byte, data, 1);
  header.type = static_cast<MessageType>(type_byte);
  data += 1;
  
  uint64_t sequence_net;
  memcpy(&sequence_net, data, 8);
  header.sequence = ntohll(sequence_net);
  
  return header;
}

// Deserialize tick payload
inline TickPayload deserialize_tick_payload(const char* payload) {
  TickPayload tick;
  
  uint64_t timestamp_net;
  memcpy(&timestamp_net, payload, 8);
  tick.timestamp = ntohll(timestamp_net);
  payload += 8;
  
  memcpy(tick.symbol, payload, 4);
  payload += 4;
  
  uint32_t price_net;
  memcpy(&price_net, payload, 4);
  uint32_t price_bits = ntohl(price_net);
  memcpy(&tick.price, &price_bits, 4);
  payload += 4;
  
  int32_t volume_net;
  memcpy(&volume_net, payload, 4);
  tick.volume = ntohl(volume_net);
  
  return tick;
}

// Backward compatibility wrapper
inline BinaryTick deserialize_tick(const char* payload) {
  return deserialize_tick_payload(payload);
}

// Deserialize heartbeat payload
inline HeartbeatPayload deserialize_heartbeat_payload(const char* payload) {
  HeartbeatPayload heartbeat;
  
  uint64_t timestamp_net;
  memcpy(&timestamp_net, payload, 8);
  heartbeat.timestamp = ntohll(timestamp_net);
  
  return heartbeat;
}

// Deserialize snapshot request
inline SnapshotRequestPayload deserialize_snapshot_request(const char* payload) {
  SnapshotRequestPayload request;
  memcpy(request.symbol, payload, 4);
  return request;
}

// Deserialize snapshot response
inline void deserialize_snapshot_response(const char* payload, uint32_t /*payload_length*/,
                                         char symbol_out[4],
                                         std::vector<OrderBookLevel>& bids_out,
                                         std::vector<OrderBookLevel>& asks_out) {
  // Read header
  memcpy(symbol_out, payload, 4);
  payload += 4;
  
  uint8_t num_bids = *reinterpret_cast<const uint8_t*>(payload++);
  uint8_t num_asks = *reinterpret_cast<const uint8_t*>(payload++);
  
  bids_out.clear();
  asks_out.clear();
  bids_out.reserve(num_bids);
  asks_out.reserve(num_asks);
  
  // Read bid levels
  for (int i = 0; i < num_bids; ++i) {
    OrderBookLevel level;
    
    uint32_t price_net;
    memcpy(&price_net, payload, 4);
    uint32_t price_bits = ntohl(price_net);
    memcpy(&level.price, &price_bits, 4);
    payload += 4;
    
    uint64_t qty_net;
    memcpy(&qty_net, payload, 8);
    level.quantity = ntohll(qty_net);
    payload += 8;
    
    bids_out.push_back(level);
  }
  
  // Read ask levels
  for (int i = 0; i < num_asks; ++i) {
    OrderBookLevel level;
    
    uint32_t price_net;
    memcpy(&price_net, payload, 4);
    uint32_t price_bits = ntohl(price_net);
    memcpy(&level.price, &price_bits, 4);
    payload += 4;
    
    uint64_t qty_net;
    memcpy(&qty_net, payload, 8);
    level.quantity = ntohll(qty_net);
    payload += 8;
    
    asks_out.push_back(level);
  }
}

// Deserialize order book update
inline OrderBookUpdatePayload deserialize_order_book_update(const char* payload) {
  OrderBookUpdatePayload update;
  
  memcpy(update.symbol, payload, 4);
  payload += 4;
  
  update.side = *reinterpret_cast<const uint8_t*>(payload++);
  
  uint32_t price_net;
  memcpy(&price_net, payload, 4);
  uint32_t price_bits = ntohl(price_net);
  memcpy(&update.price, &price_bits, 4);
  payload += 4;
  
  int64_t qty_net;
  memcpy(&qty_net, payload, 8);
  update.quantity = static_cast<int64_t>(ntohll(static_cast<uint64_t>(qty_net)));
  
  return update;
}

#endif // BINARY_PROTOCOL_HPP
