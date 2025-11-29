#ifndef SEQUENCE_TRACKER_HPP
#define SEQUENCE_TRACKER_HPP

#include <cstdint>
#include <iostream>
#include <optional>

class SequenceTracker {
public:
  SequenceTracker() : last_sequence_(std::nullopt), gaps_detected_(0) {}
  
  // Process a new sequence number
  // Returns true if this is a valid next sequence, false if there's a gap
  bool process_sequence(uint64_t sequence) {
    if (!last_sequence_.has_value()) {
      // First message
      last_sequence_ = sequence;
      return true;
    }
    
    uint64_t expected = last_sequence_.value() + 1;
    
    if (sequence == expected) {
      // Correct sequence
      last_sequence_ = sequence;
      return true;
    } else if (sequence > expected) {
      // Gap detected
      uint64_t gap_size = sequence - expected;
      gaps_detected_++;
      
      std::cout << "[SequenceTracker] Gap detected: expected seq=" << expected 
                << ", got seq=" << sequence 
                << " (" << gap_size << " messages missing)" << std::endl;
      
      last_sequence_ = sequence;
      return false;
    } else {
      // Out-of-order or duplicate (sequence < expected)
      std::cout << "[SequenceTracker] Out-of-order message: expected seq=" << expected
                << ", got seq=" << sequence << std::endl;
      // Don't update last_sequence_ for out-of-order messages
      return false;
    }
  }
  
  // Reset sequence tracking (e.g., after reconnect)
  void reset() {
    last_sequence_ = std::nullopt;
    std::cout << "[SequenceTracker] Sequence tracking reset" << std::endl;
  }
  
  // Getters
  std::optional<uint64_t> last_sequence() const { return last_sequence_; }
  uint64_t gaps_detected() const { return gaps_detected_; }
  
private:
  std::optional<uint64_t> last_sequence_;
  uint64_t gaps_detected_;
};

#endif // SEQUENCE_TRACKER_HPP
