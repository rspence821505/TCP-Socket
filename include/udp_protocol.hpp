#ifndef UDP_PROTOCOL_HPP
#define UDP_PROTOCOL_HPP

#include <cstdint>
#include <cstring>
#include <set>
#include <vector>

#include "binary_protocol.hpp"

// UDP-specific message types (extends binary_protocol.hpp)
enum class UDPMessageType : uint8_t {
  RETRANSMIT_REQUEST = 0x20,
  RETRANSMIT_RESPONSE = 0x21
};

// Retransmit request payload (sent over TCP control channel)
struct RetransmitRequest {
  uint64_t start_sequence;
  uint64_t end_sequence;  // Inclusive range
  
  static constexpr size_t PAYLOAD_SIZE = 8 + 8;  // 16 bytes
};

// Sequence gap tracker for UDP receiver
class SequenceGapTracker {
public:
  SequenceGapTracker() : last_sequence_(0), first_message_(true), total_gaps_(0) {}
  
  // Process a sequence number and detect gaps
  // Returns true if this is the expected next sequence, false if gap detected
  bool process_sequence(uint64_t sequence) {
    if (first_message_) {
      last_sequence_ = sequence;
      first_message_ = false;
      return true;
    }
    
    uint64_t expected = last_sequence_ + 1;
    
    if (sequence == expected) {
      // Perfect sequence
      last_sequence_ = sequence;
      
      // Check if this fills any gaps
      auto it = gaps_.find(sequence);
      if (it != gaps_.end()) {
        gaps_.erase(it);
      }
      
      return true;
    } else if (sequence > expected) {
      // Gap detected - record missing sequences
      for (uint64_t missing = expected; missing < sequence; ++missing) {
        gaps_.insert(missing);
        total_gaps_++;
      }
      
      last_sequence_ = sequence;
      return false;
    } else {
      // Out-of-order or duplicate (sequence < expected)
      // This might be a retransmitted packet filling a gap
      auto it = gaps_.find(sequence);
      if (it != gaps_.end()) {
        gaps_.erase(it);
        return true;  // Gap filled!
      }
      return false;  // Duplicate
    }
  }
  
  // Get current gaps (for retransmit requests)
  std::vector<std::pair<uint64_t, uint64_t>> get_gap_ranges() const {
    std::vector<std::pair<uint64_t, uint64_t>> ranges;
    
    if (gaps_.empty()) {
      return ranges;
    }
    
    // Compress consecutive gaps into ranges
    auto it = gaps_.begin();
    uint64_t range_start = *it;
    uint64_t range_end = *it;
    ++it;
    
    for (; it != gaps_.end(); ++it) {
      if (*it == range_end + 1) {
        // Extend current range
        range_end = *it;
      } else {
        // Save current range and start new one
        ranges.emplace_back(range_start, range_end);
        range_start = *it;
        range_end = *it;
      }
    }
    
    // Don't forget the last range
    ranges.emplace_back(range_start, range_end);
    
    return ranges;
  }
  
  // Statistics
  size_t active_gaps() const { return gaps_.size(); }
  uint64_t total_gaps_detected() const { return total_gaps_; }
  uint64_t last_sequence() const { return last_sequence_; }
  
  void reset() {
    last_sequence_ = 0;
    first_message_ = true;
    total_gaps_ = 0;
    gaps_.clear();
  }
  
private:
  uint64_t last_sequence_;
  bool first_message_;
  uint64_t total_gaps_;
  std::set<uint64_t> gaps_;  // Set of missing sequence numbers
};

// Serialize retransmit request
inline std::string serialize_retransmit_request(uint64_t start_seq, uint64_t end_seq) {
  std::string message;
  message.reserve(MessageHeader::HEADER_SIZE + RetransmitRequest::PAYLOAD_SIZE);
  
  // Header
  serialize_header(message, static_cast<MessageType>(UDPMessageType::RETRANSMIT_REQUEST), 
                  0, RetransmitRequest::PAYLOAD_SIZE);
  
  // Payload
  uint64_t start_net = htonll(start_seq);
  message.append(reinterpret_cast<const char*>(&start_net), 8);
  
  uint64_t end_net = htonll(end_seq);
  message.append(reinterpret_cast<const char*>(&end_net), 8);
  
  return message;
}

// Deserialize retransmit request
inline RetransmitRequest deserialize_retransmit_request(const char* payload) {
  RetransmitRequest request;
  
  uint64_t start_net;
  memcpy(&start_net, payload, 8);
  request.start_sequence = ntohll(start_net);
  payload += 8;
  
  uint64_t end_net;
  memcpy(&end_net, payload, 8);
  request.end_sequence = ntohll(end_net);
  
  return request;
}

// UDP packet loss simulator configuration
struct PacketLossConfig {
  double loss_rate;        // Probability of dropping a packet (0.0 - 1.0)
  uint32_t burst_size;     // Number of consecutive packets to drop in a burst
  double burst_probability; // Probability of starting a burst loss
  
  PacketLossConfig() 
    : loss_rate(0.0), burst_size(1), burst_probability(0.0) {}
  
  PacketLossConfig(double rate, uint32_t burst = 1, double burst_prob = 0.0)
    : loss_rate(rate), burst_size(burst), burst_probability(burst_prob) {}
};

#endif // UDP_PROTOCOL_HPP
