#ifndef BINARY_PROTOCOL_HPP
#define BINARY_PROTOCOL_HPP

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <string>

// Message format: [4-byte length][payload]
// Payload: [8-byte timestamp][4-byte symbol][4-byte price][4-byte volume]
struct BinaryTick {
  uint64_t timestamp;
  char symbol[4]; // Fixed 4 bytes, padded with nulls if needed
  float price;
  int32_t volume;

  static constexpr size_t PAYLOAD_SIZE = 8 + 4 + 4 + 4; // 20 bytes
};

// Helper to convert uint64_t to/from network byte order
// On macOS/BSD, htonll/ntohll are already defined
// On Linux, we need to define them ourselves
#ifndef htonll
inline uint64_t htonll(uint64_t value) {
  // Check if system is little-endian
  static const int num = 42;
  if (*reinterpret_cast<const char *>(&num) == num) {
    // Little-endian: need to swap bytes
    const uint32_t high_part = htonl(static_cast<uint32_t>(value >> 32));
    const uint32_t low_part =
        htonl(static_cast<uint32_t>(value & 0xFFFFFFFFLL));
    return (static_cast<uint64_t>(low_part) << 32) | high_part;
  }
  return value; // Big-endian: no swap needed
}
#endif

#ifndef ntohll
inline uint64_t ntohll(uint64_t value) {
  return htonll(value); // Same operation for reversing
}
#endif

// Serialize a BinaryTick into a length-prefixed message
inline std::string serialize_tick(const BinaryTick &tick) {
  std::string message;
  message.reserve(4 + BinaryTick::PAYLOAD_SIZE); // Length prefix + payload

  // 1. Write length prefix (4 bytes)
  uint32_t length = static_cast<uint32_t>(BinaryTick::PAYLOAD_SIZE);
  uint32_t length_net = htonl(length);
  message.append(reinterpret_cast<const char *>(&length_net), 4);

  // 2. Write timestamp (8 bytes)
  uint64_t timestamp_net = htonll(tick.timestamp);
  message.append(reinterpret_cast<const char *>(&timestamp_net), 8);

  // 3. Write symbol (4 bytes)
  message.append(tick.symbol, 4);

  // 4. Write price (4 bytes) - floats don't need byte swapping in most cases
  // but for correctness we should swap (this is simplified)
  uint32_t price_bits;
  memcpy(&price_bits, &tick.price, 4);
  uint32_t price_net = htonl(price_bits);
  message.append(reinterpret_cast<const char *>(&price_net), 4);

  // 5. Write volume (4 bytes)
  int32_t volume_net = htonl(tick.volume);
  message.append(reinterpret_cast<const char *>(&volume_net), 4);

  return message;
}

// Deserialize a BinaryTick from raw payload (assumes length already checked)
inline BinaryTick deserialize_tick(const char *payload) {
  BinaryTick tick;

  // 1. Read timestamp (8 bytes)
  uint64_t timestamp_net;
  memcpy(&timestamp_net, payload, 8);
  tick.timestamp = ntohll(timestamp_net);
  payload += 8;

  // 2. Read symbol (4 bytes)
  memcpy(tick.symbol, payload, 4);
  payload += 4;

  // 3. Read price (4 bytes)
  uint32_t price_net;
  memcpy(&price_net, payload, 4);
  uint32_t price_bits = ntohl(price_net);
  memcpy(&tick.price, &price_bits, 4);
  payload += 4;

  // 4. Read volume (4 bytes)
  int32_t volume_net;
  memcpy(&volume_net, payload, 4);
  tick.volume = ntohl(volume_net);

  return tick;
}

#endif // BINARY_PROTOCOL_HPP
