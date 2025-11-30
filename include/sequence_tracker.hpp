#ifndef SEQUENCE_TRACKER_HPP
#define SEQUENCE_TRACKER_HPP

#include <cstdint>
#include <iostream>
#include <optional>

/**
 * Optimized Sequence Tracker
 *
 * Optimizations applied:
 * 1. Uses sentinel value (UINT64_MAX) instead of std::optional for storage
 * 2. Branch prediction hints for common case (sequential messages)
 * 3. Inline critical functions with always_inline attribute
 * 4. Reduced branching in hot path
 *
 * API maintained for backwards compatibility with original SequenceTracker.
 */

class SequenceTracker {
public:
  SequenceTracker()
      : last_sequence_(UINT64_MAX), // Sentinel: not initialized
        gaps_detected_(0) {}

  /**
   * Process a new sequence number
   * Returns true if this is a valid next sequence, false if there's a gap
   *
   * OPTIMIZED: Inlined, branch hints, reduced branching
   */
  inline __attribute__((always_inline)) bool
  process_sequence(uint64_t sequence) {
    // First message case - branch predicted as unlikely
    if (__builtin_expect(last_sequence_ == UINT64_MAX, 0)) {
      last_sequence_ = sequence;
      return true;
    }

    const uint64_t expected = last_sequence_ + 1;

    // HOT PATH: Sequential message (common case)
    // Branch predicted as likely
    if (__builtin_expect(sequence == expected, 1)) {
      last_sequence_ = sequence;
      return true;
    }

    // COLD PATH: Gap or out-of-order
    // Branch predicted as unlikely
    if (__builtin_expect(sequence > expected, 1)) {
      // Gap detected
      const uint64_t gap_size = sequence - expected;
      gaps_detected_++;

      std::cout << "[SequenceTracker] Gap detected: expected seq=" << expected
                << ", got seq=" << sequence << " (" << gap_size
                << " messages missing)" << std::endl;

      last_sequence_ = sequence;
      return false;
    }

    // Out-of-order or duplicate (sequence < expected)
    std::cout << "[SequenceTracker] Out-of-order message: expected seq=" << expected
              << ", got seq=" << sequence << std::endl;
    // Don't update last_sequence_ for out-of-order messages
    return false;
  }

  /**
   * Reset sequence tracking (e.g., after reconnect)
   */
  inline void reset() {
    last_sequence_ = UINT64_MAX;
    std::cout << "[SequenceTracker] Sequence tracking reset" << std::endl;
  }

  // Getters - returns std::optional for backwards compatibility
  inline std::optional<uint64_t> last_sequence() const {
    if (last_sequence_ == UINT64_MAX) {
      return std::nullopt;
    }
    return last_sequence_;
  }

  inline uint64_t gaps_detected() const { return gaps_detected_; }

  // Additional method from optimized version
  inline bool has_received_message() const {
    return last_sequence_ != UINT64_MAX;
  }

private:
  uint64_t last_sequence_; // UINT64_MAX = not initialized (sentinel value)
  uint64_t gaps_detected_;
};

// Type alias for backwards compatibility with code using SequenceTrackerOptimized
using SequenceTrackerOptimized = SequenceTracker;

#endif // SEQUENCE_TRACKER_HPP
