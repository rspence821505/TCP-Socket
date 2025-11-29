#ifndef BINARY_PROTOCOL_V2_HPP
#define BINARY_PROTOCOL_V2_HPP

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <string>

// Message types
enum class MessageType : uint8_t {
  TICK = 0x01,
  HEARTBEAT = 0xFF
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

// Heartbeat message payload (minimal)
struct HeartbeatPayload {
  uint64_t timestamp;
  
  static constexpr size_t PAYLOAD_SIZE = 8; // 8 bytes
};

// Complete message structures
struct TickMessage {
  MessageHeader header;
  TickPayload payload;
};

struct HeartbeatMessage {
  MessageHeader header;
  HeartbeatPayload payload;
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
  // Length prefix
  uint32_t length_net = htonl(payload_size);
  message.append(reinterpret_cast<const char*>(&length_net), 4);
  
  // Message type
  uint8_t type_byte = static_cast<uint8_t>(type);
  message.append(reinterpret_cast<const char*>(&type_byte), 1);
  
  // Sequence number
  uint64_t sequence_net = htonll(sequence);
  message.append(reinterpret_cast<const char*>(&sequence_net), 8);
}

// Serialize tick message
inline std::string serialize_tick(uint64_t sequence, uint64_t timestamp,
                                 const char symbol[4], float price, int32_t volume) {
  std::string message;
  message.reserve(MessageHeader::HEADER_SIZE + TickPayload::PAYLOAD_SIZE);
  
  // Header
  serialize_header(message, MessageType::TICK, sequence, TickPayload::PAYLOAD_SIZE);
  
  // Payload
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

// Serialize heartbeat message
inline std::string serialize_heartbeat(uint64_t sequence, uint64_t timestamp) {
  std::string message;
  message.reserve(MessageHeader::HEADER_SIZE + HeartbeatPayload::PAYLOAD_SIZE);
  
  // Header
  serialize_header(message, MessageType::HEARTBEAT, sequence, HeartbeatPayload::PAYLOAD_SIZE);
  
  // Payload
  uint64_t timestamp_net = htonll(timestamp);
  message.append(reinterpret_cast<const char*>(&timestamp_net), 8);
  
  return message;
}

// Deserialize header from raw bytes
inline MessageHeader deserialize_header(const char* data) {
  MessageHeader header;
  
  // Length
  uint32_t length_net;
  memcpy(&length_net, data, 4);
  header.length = ntohl(length_net);
  data += 4;
  
  // Type
  uint8_t type_byte;
  memcpy(&type_byte, data, 1);
  header.type = static_cast<MessageType>(type_byte);
  data += 1;
  
  // Sequence
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

// Deserialize heartbeat payload
inline HeartbeatPayload deserialize_heartbeat_payload(const char* payload) {
  HeartbeatPayload heartbeat;
  
  uint64_t timestamp_net;
  memcpy(&timestamp_net, payload, 8);
  heartbeat.timestamp = ntohll(timestamp_net);
  
  return heartbeat;
}

#endif // BINARY_PROTOCOL_V2_HPP
