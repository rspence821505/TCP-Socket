#ifndef SEQUENCE_TRACKER_OPTIMIZED_HPP
#define SEQUENCE_TRACKER_OPTIMIZED_HPP

#include <cstdint>
#include <iostream>

/**
 * Optimized Sequence Tracker
 *
 * Optimizations applied:
 * 1. Removed std::optional - use sentinel value instead (UINT64_MAX)
 * 2. Branch prediction hints for common case (sequential messages)
 * 3. Inline critical functions
 * 4. Reduced branching in hot path
 */

class SequenceTrackerOptimized {
public:
  SequenceTrackerOptimized()
      : last_sequence_(UINT64_MAX) // Sentinel: not initialized
        ,
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

      // Only print gaps occasionally to avoid I/O overhead
      if (__builtin_expect((gaps_detected_ & 0xFF) == 0, 0)) {
        std::cout << "[SequenceTracker] Gap detected: expected seq=" << expected
                  << ", got seq=" << sequence << " (" << gap_size
                  << " messages missing)" << std::endl;
      }

      last_sequence_ = sequence;
      return false;
    }

    // Out-of-order or duplicate (sequence < expected)
    // Very rare - don't update last_sequence_
    return false;
  }

  /**
   * Reset sequence tracking (e.g., after reconnect)
   */
  inline void reset() {
    last_sequence_ = UINT64_MAX;
    // Don't reset gaps_detected_ - keep cumulative stats

    if (__builtin_expect((gaps_detected_ > 0), 1)) {
      std::cout
          << "[SequenceTracker] Sequence tracking reset (total gaps detected: "
          << gaps_detected_ << ")" << std::endl;
    }
  }

  // Getters
  inline uint64_t last_sequence() const {
    return (last_sequence_ == UINT64_MAX) ? 0 : last_sequence_;
  }

  inline uint64_t gaps_detected() const { return gaps_detected_; }

  inline bool has_received_message() const {
    return last_sequence_ != UINT64_MAX;
  }

private:
  uint64_t last_sequence_; // UINT64_MAX = not initialized
  uint64_t gaps_detected_; // Cumulative gap count
};

#endif // SEQUENCE_TRACKER_OPTIMIZED_HPP
