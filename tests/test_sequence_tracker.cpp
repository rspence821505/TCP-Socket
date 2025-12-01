#include <gtest/gtest.h>
#include <sstream>
#include <iostream>

#include "common.hpp"
#include "sequence_tracker.hpp"

// =============================================================================
// Basic Functionality Tests
// =============================================================================

class SequenceTrackerBasicTest : public ::testing::Test {
protected:
  SequenceTracker tracker_;

  void SetUp() override {
    // Suppress cout output during tests
  }
};

TEST_F(SequenceTrackerBasicTest, InitialState) {
  // Initially no messages received
  EXPECT_FALSE(tracker_.has_received_message());
  EXPECT_FALSE(tracker_.last_sequence().has_value());
  EXPECT_EQ(tracker_.gaps_detected(), 0);
}

TEST_F(SequenceTrackerBasicTest, FirstMessage) {
  // First message should always succeed
  EXPECT_TRUE(tracker_.process_sequence(1));
  EXPECT_TRUE(tracker_.has_received_message());
  EXPECT_TRUE(tracker_.last_sequence().has_value());
  EXPECT_EQ(tracker_.last_sequence().value(), 1);
  EXPECT_EQ(tracker_.gaps_detected(), 0);
}

TEST_F(SequenceTrackerBasicTest, FirstMessageZero) {
  // First message can be zero
  EXPECT_TRUE(tracker_.process_sequence(0));
  EXPECT_TRUE(tracker_.has_received_message());
  EXPECT_EQ(tracker_.last_sequence().value(), 0);
}

TEST_F(SequenceTrackerBasicTest, FirstMessageLargeNumber) {
  // First message can be any valid sequence
  EXPECT_TRUE(tracker_.process_sequence(1000000));
  EXPECT_EQ(tracker_.last_sequence().value(), 1000000);
}

TEST_F(SequenceTrackerBasicTest, SequentialMessages) {
  // Sequential messages should all succeed (hot path)
  EXPECT_TRUE(tracker_.process_sequence(1));
  EXPECT_TRUE(tracker_.process_sequence(2));
  EXPECT_TRUE(tracker_.process_sequence(3));
  EXPECT_TRUE(tracker_.process_sequence(4));
  EXPECT_TRUE(tracker_.process_sequence(5));

  EXPECT_EQ(tracker_.last_sequence().value(), 5);
  EXPECT_EQ(tracker_.gaps_detected(), 0);
}

TEST_F(SequenceTrackerBasicTest, SequentialMessagesFromZero) {
  EXPECT_TRUE(tracker_.process_sequence(0));
  EXPECT_TRUE(tracker_.process_sequence(1));
  EXPECT_TRUE(tracker_.process_sequence(2));

  EXPECT_EQ(tracker_.last_sequence().value(), 2);
  EXPECT_EQ(tracker_.gaps_detected(), 0);
}

TEST_F(SequenceTrackerBasicTest, ManySequentialMessages) {
  // Test many sequential messages (performance hot path)
  for (uint64_t i = 0; i < 10000; ++i) {
    EXPECT_TRUE(tracker_.process_sequence(i));
  }

  EXPECT_EQ(tracker_.last_sequence().value(), 9999);
  EXPECT_EQ(tracker_.gaps_detected(), 0);
}

// =============================================================================
// Gap Detection Tests
// =============================================================================

class SequenceTrackerGapTest : public ::testing::Test {
protected:
  SequenceTracker tracker_;
};

TEST_F(SequenceTrackerGapTest, SingleMessageGap) {
  EXPECT_TRUE(tracker_.process_sequence(1));

  // Skip sequence 2
  EXPECT_FALSE(tracker_.process_sequence(3));

  EXPECT_EQ(tracker_.gaps_detected(), 1);
  EXPECT_EQ(tracker_.last_sequence().value(), 3);
}

TEST_F(SequenceTrackerGapTest, MultipleMessageGap) {
  EXPECT_TRUE(tracker_.process_sequence(1));

  // Skip sequences 2, 3, 4, 5
  EXPECT_FALSE(tracker_.process_sequence(6));

  EXPECT_EQ(tracker_.gaps_detected(), 1);
  EXPECT_EQ(tracker_.last_sequence().value(), 6);
}

TEST_F(SequenceTrackerGapTest, MultipleGaps) {
  EXPECT_TRUE(tracker_.process_sequence(1));
  EXPECT_TRUE(tracker_.process_sequence(2));

  // First gap: skip 3
  EXPECT_FALSE(tracker_.process_sequence(4));
  EXPECT_EQ(tracker_.gaps_detected(), 1);

  EXPECT_TRUE(tracker_.process_sequence(5));
  EXPECT_TRUE(tracker_.process_sequence(6));

  // Second gap: skip 7, 8
  EXPECT_FALSE(tracker_.process_sequence(9));
  EXPECT_EQ(tracker_.gaps_detected(), 2);

  // Third gap: skip 10, 11, 12, 13, 14
  EXPECT_FALSE(tracker_.process_sequence(15));
  EXPECT_EQ(tracker_.gaps_detected(), 3);
}

TEST_F(SequenceTrackerGapTest, ContinueAfterGap) {
  EXPECT_TRUE(tracker_.process_sequence(1));
  EXPECT_FALSE(tracker_.process_sequence(5));  // Gap

  // Continue from new sequence
  EXPECT_TRUE(tracker_.process_sequence(6));
  EXPECT_TRUE(tracker_.process_sequence(7));
  EXPECT_TRUE(tracker_.process_sequence(8));

  EXPECT_EQ(tracker_.gaps_detected(), 1);
  EXPECT_EQ(tracker_.last_sequence().value(), 8);
}

TEST_F(SequenceTrackerGapTest, LargeGap) {
  EXPECT_TRUE(tracker_.process_sequence(1));

  // Very large gap
  EXPECT_FALSE(tracker_.process_sequence(1000001));

  EXPECT_EQ(tracker_.gaps_detected(), 1);
  EXPECT_EQ(tracker_.last_sequence().value(), 1000001);
}

// =============================================================================
// Out-of-Order and Duplicate Tests
// =============================================================================

class SequenceTrackerOutOfOrderTest : public ::testing::Test {
protected:
  SequenceTracker tracker_;
};

TEST_F(SequenceTrackerOutOfOrderTest, DuplicateSequence) {
  EXPECT_TRUE(tracker_.process_sequence(1));
  EXPECT_TRUE(tracker_.process_sequence(2));

  // Duplicate of sequence 2
  EXPECT_FALSE(tracker_.process_sequence(2));

  // Should not count as gap
  EXPECT_EQ(tracker_.gaps_detected(), 0);

  // last_sequence should still be 2
  EXPECT_EQ(tracker_.last_sequence().value(), 2);
}

TEST_F(SequenceTrackerOutOfOrderTest, OldSequence) {
  EXPECT_TRUE(tracker_.process_sequence(1));
  EXPECT_TRUE(tracker_.process_sequence(2));
  EXPECT_TRUE(tracker_.process_sequence(3));

  // Old sequence
  EXPECT_FALSE(tracker_.process_sequence(1));

  EXPECT_EQ(tracker_.gaps_detected(), 0);
  EXPECT_EQ(tracker_.last_sequence().value(), 3);
}

TEST_F(SequenceTrackerOutOfOrderTest, OutOfOrderDoesNotUpdateLastSequence) {
  EXPECT_TRUE(tracker_.process_sequence(5));
  EXPECT_TRUE(tracker_.process_sequence(6));
  EXPECT_TRUE(tracker_.process_sequence(7));

  // Out-of-order message
  EXPECT_FALSE(tracker_.process_sequence(4));

  // Last sequence should still be 7, not 4
  EXPECT_EQ(tracker_.last_sequence().value(), 7);

  // Continue normally
  EXPECT_TRUE(tracker_.process_sequence(8));
  EXPECT_EQ(tracker_.last_sequence().value(), 8);
}

TEST_F(SequenceTrackerOutOfOrderTest, MultipleDuplicates) {
  EXPECT_TRUE(tracker_.process_sequence(1));

  // Multiple duplicates
  EXPECT_FALSE(tracker_.process_sequence(1));
  EXPECT_FALSE(tracker_.process_sequence(1));
  EXPECT_FALSE(tracker_.process_sequence(1));

  EXPECT_EQ(tracker_.gaps_detected(), 0);
  EXPECT_EQ(tracker_.last_sequence().value(), 1);

  // Can still continue
  EXPECT_TRUE(tracker_.process_sequence(2));
}

// =============================================================================
// Reset Tests
// =============================================================================

class SequenceTrackerResetTest : public ::testing::Test {
protected:
  SequenceTracker tracker_;
};

TEST_F(SequenceTrackerResetTest, ResetClearsState) {
  EXPECT_TRUE(tracker_.process_sequence(1));
  EXPECT_TRUE(tracker_.process_sequence(2));
  EXPECT_TRUE(tracker_.process_sequence(3));

  tracker_.reset();

  EXPECT_FALSE(tracker_.has_received_message());
  EXPECT_FALSE(tracker_.last_sequence().has_value());
}

TEST_F(SequenceTrackerResetTest, ResetPreservesGapCount) {
  EXPECT_TRUE(tracker_.process_sequence(1));
  EXPECT_FALSE(tracker_.process_sequence(5));  // Gap
  EXPECT_EQ(tracker_.gaps_detected(), 1);

  tracker_.reset();

  // Gap count is NOT reset (designed to track total gaps)
  // Note: Current implementation doesn't reset gaps_detected_
  // This test documents the current behavior
  EXPECT_EQ(tracker_.gaps_detected(), 1);
}

TEST_F(SequenceTrackerResetTest, ProcessAfterReset) {
  EXPECT_TRUE(tracker_.process_sequence(100));
  EXPECT_TRUE(tracker_.process_sequence(101));

  tracker_.reset();

  // Can start with any sequence after reset
  EXPECT_TRUE(tracker_.process_sequence(1));
  EXPECT_EQ(tracker_.last_sequence().value(), 1);

  EXPECT_TRUE(tracker_.process_sequence(2));
  EXPECT_EQ(tracker_.last_sequence().value(), 2);
}

TEST_F(SequenceTrackerResetTest, MultipleResets) {
  EXPECT_TRUE(tracker_.process_sequence(1));
  tracker_.reset();

  EXPECT_TRUE(tracker_.process_sequence(50));
  tracker_.reset();

  EXPECT_TRUE(tracker_.process_sequence(999));
  EXPECT_EQ(tracker_.last_sequence().value(), 999);
}

// =============================================================================
// Boundary Value Tests
// =============================================================================

class SequenceTrackerBoundaryTest : public ::testing::Test {
protected:
  SequenceTracker tracker_;
};

TEST_F(SequenceTrackerBoundaryTest, ZeroSequence) {
  EXPECT_TRUE(tracker_.process_sequence(0));
  EXPECT_EQ(tracker_.last_sequence().value(), 0);

  EXPECT_TRUE(tracker_.process_sequence(1));
  EXPECT_EQ(tracker_.last_sequence().value(), 1);
}

TEST_F(SequenceTrackerBoundaryTest, LargeSequenceNumbers) {
  uint64_t large = 0xFFFFFFFFFFFF0000ULL;  // Near max but not UINT64_MAX

  EXPECT_TRUE(tracker_.process_sequence(large));
  EXPECT_EQ(tracker_.last_sequence().value(), large);

  EXPECT_TRUE(tracker_.process_sequence(large + 1));
  EXPECT_EQ(tracker_.last_sequence().value(), large + 1);
}

TEST_F(SequenceTrackerBoundaryTest, NearMaxSequence) {
  // UINT64_MAX - 2 to avoid sentinel value issues
  uint64_t near_max = UINT64_MAX - 2;

  EXPECT_TRUE(tracker_.process_sequence(near_max));
  EXPECT_EQ(tracker_.last_sequence().value(), near_max);

  EXPECT_TRUE(tracker_.process_sequence(near_max + 1));
  EXPECT_EQ(tracker_.last_sequence().value(), near_max + 1);
}

TEST_F(SequenceTrackerBoundaryTest, WrapAroundDetectedAsOutOfOrder) {
  // Start near the max
  uint64_t near_max = UINT64_MAX - 5;
  EXPECT_TRUE(tracker_.process_sequence(near_max));

  // Simulate wrap-around: sequence goes from near_max to 0
  // This will be detected as out-of-order (0 < near_max)
  // This is expected behavior - wrap-around needs special handling at protocol level
  EXPECT_FALSE(tracker_.process_sequence(0));

  // last_sequence should still be near_max (out-of-order doesn't update)
  EXPECT_EQ(tracker_.last_sequence().value(), near_max);
}

// =============================================================================
// API Compatibility Tests
// =============================================================================

class SequenceTrackerAPITest : public ::testing::Test {
protected:
  void SetUp() override {}
};

TEST_F(SequenceTrackerAPITest, OptionalReturnType) {
  SequenceTracker tracker;

  // Should return std::optional
  std::optional<uint64_t> result = tracker.last_sequence();
  EXPECT_FALSE(result.has_value());

  tracker.process_sequence(42);
  result = tracker.last_sequence();
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), 42);
}

TEST_F(SequenceTrackerAPITest, TypeAliasCompatibility) {
  // SequenceTrackerOptimized should work as alias
  SequenceTrackerOptimized tracker;

  EXPECT_TRUE(tracker.process_sequence(1));
  EXPECT_TRUE(tracker.process_sequence(2));
  EXPECT_EQ(tracker.last_sequence().value(), 2);
}

TEST_F(SequenceTrackerAPITest, HasReceivedMessageMethod) {
  SequenceTracker tracker;

  EXPECT_FALSE(tracker.has_received_message());

  tracker.process_sequence(1);
  EXPECT_TRUE(tracker.has_received_message());

  tracker.reset();
  EXPECT_FALSE(tracker.has_received_message());
}

TEST_F(SequenceTrackerAPITest, GapsDetectedCounter) {
  SequenceTracker tracker;

  EXPECT_EQ(tracker.gaps_detected(), 0);

  tracker.process_sequence(1);
  EXPECT_EQ(tracker.gaps_detected(), 0);

  tracker.process_sequence(5);  // Gap of 3
  EXPECT_EQ(tracker.gaps_detected(), 1);

  tracker.process_sequence(10);  // Gap of 4
  EXPECT_EQ(tracker.gaps_detected(), 2);
}

// =============================================================================
// Performance/Stress Tests
// =============================================================================

class SequenceTrackerPerformanceTest : public ::testing::Test {
protected:
  SequenceTracker tracker_;
};

TEST_F(SequenceTrackerPerformanceTest, HighVolume) {
  // Process many sequential messages quickly
  constexpr size_t NUM_MESSAGES = 1000000;

  uint64_t start = now_us();

  for (uint64_t i = 0; i < NUM_MESSAGES; ++i) {
    tracker_.process_sequence(i);
  }

  uint64_t end = now_us();
  uint64_t duration_us = end - start;

  double msgs_per_sec = (static_cast<double>(NUM_MESSAGES) / duration_us) * 1000000.0;

  std::cout << "  Sequence tracking throughput: " << static_cast<size_t>(msgs_per_sec) << " msgs/sec" << std::endl;

  EXPECT_EQ(tracker_.last_sequence().value(), NUM_MESSAGES - 1);
  EXPECT_EQ(tracker_.gaps_detected(), 0);

  // Should be able to process at least 10M msgs/sec on hot path
  EXPECT_GT(msgs_per_sec, 10000000) << "Sequence tracking throughput too low";
}

TEST_F(SequenceTrackerPerformanceTest, MixedGapsAndSequential) {
  // Realistic scenario with occasional gaps
  constexpr size_t NUM_ITERATIONS = 100000;
  uint64_t seq = 0;

  for (size_t i = 0; i < NUM_ITERATIONS; ++i) {
    // Every 100th message, introduce a small gap
    if (i % 100 == 99) {
      seq += 3;  // Skip 2 sequences
      tracker_.process_sequence(seq);
    } else {
      seq++;
      tracker_.process_sequence(seq);
    }
  }

  // Should have detected gaps
  EXPECT_EQ(tracker_.gaps_detected(), NUM_ITERATIONS / 100);
}

// =============================================================================
// Edge Case Tests
// =============================================================================

class SequenceTrackerEdgeCaseTest : public ::testing::Test {
protected:
  SequenceTracker tracker_;
};

TEST_F(SequenceTrackerEdgeCaseTest, GapThenDuplicate) {
  EXPECT_TRUE(tracker_.process_sequence(1));
  EXPECT_FALSE(tracker_.process_sequence(5));  // Gap, advances to 5
  EXPECT_FALSE(tracker_.process_sequence(3));  // Out-of-order (old)

  EXPECT_EQ(tracker_.gaps_detected(), 1);
  EXPECT_EQ(tracker_.last_sequence().value(), 5);
}

TEST_F(SequenceTrackerEdgeCaseTest, ImmediateGapOnFirstMessage) {
  // First message at sequence 100 is valid
  EXPECT_TRUE(tracker_.process_sequence(100));

  // No gap on first message
  EXPECT_EQ(tracker_.gaps_detected(), 0);
}

TEST_F(SequenceTrackerEdgeCaseTest, GapOfOne) {
  EXPECT_TRUE(tracker_.process_sequence(1));
  EXPECT_FALSE(tracker_.process_sequence(3));  // Gap of 1 (missing seq 2)

  EXPECT_EQ(tracker_.gaps_detected(), 1);
}

TEST_F(SequenceTrackerEdgeCaseTest, ResetAndGap) {
  EXPECT_TRUE(tracker_.process_sequence(1));
  EXPECT_TRUE(tracker_.process_sequence(2));

  tracker_.reset();

  // After reset, first message is always valid (no gap)
  EXPECT_TRUE(tracker_.process_sequence(50));
  EXPECT_EQ(tracker_.gaps_detected(), 0);  // No new gap detected after reset

  // But now gaps are tracked from 50
  EXPECT_FALSE(tracker_.process_sequence(55));
  // Gap count includes the one from before reset in current implementation
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
