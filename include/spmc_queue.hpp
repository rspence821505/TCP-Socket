#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>

/**
 * Lock-Free Single-Producer Multiple-Consumer (SPMC) Ring Buffer
 * 
 * Key differences from SPSC:
 * 1. Multiple consumers compete for tail_ using compare-and-swap (CAS)
 * 2. Producer still owns head_ exclusively (single producer)
 * 3. More complex memory ordering needed for correctness
 * 
 * Cache coherency considerations:
 * - head_ and tail_ on separate cache lines (prevent producer/consumer ping-pong)
 * - Each consumer should pad their local state to prevent false sharing
 */

template <typename T> 
class SPMCQueue {
public:
  explicit SPMCQueue(size_t capacity)
      : capacity_(round_up_to_power_of_2(capacity)), 
        mask_(capacity_ - 1),
        buffer_(std::make_unique<T[]>(capacity_)), 
        head_(0), 
        tail_(0) {}

  // Non-copyable, non-movable
  SPMCQueue(const SPMCQueue &) = delete;
  SPMCQueue &operator=(const SPMCQueue &) = delete;

  /**
   * Producer-side: Push an item
   * Same as SPSC - only one producer, so no contention on head_
   */
  bool push(const T &item) {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t next_head = (head + 1) & mask_;

    // Check if queue is full
    // Use acquire to see latest tail_ from any consumer
    if (next_head == tail_.load(std::memory_order_acquire)) {
      return false; // Queue full
    }

    buffer_[head] = item;

    // Release: make buffer write visible to all consumers
    head_.store(next_head, std::memory_order_release);
    return true;
  }

  /**
   * Producer-side: Push with move semantics
   */
  bool push(T &&item) {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t next_head = (head + 1) & mask_;

    if (next_head == tail_.load(std::memory_order_acquire)) {
      return false; // Queue full
    }

    buffer_[head] = std::move(item);
    head_.store(next_head, std::memory_order_release);
    return true;
  }

  /**
   * Consumer-side: Pop an item (multiple consumers compete via CAS)
   * 
   * Algorithm:
   * 1. Load current tail
   * 2. Check if queue is empty (tail == head)
   * 3. Try to claim the item at tail using CAS
   * 4. If CAS succeeds, read the item and return it
   * 5. If CAS fails, another consumer claimed it - retry
   */
  std::optional<T> pop() {
    while (true) {
      // Load tail - this is where consumers compete
      size_t tail = tail_.load(std::memory_order_relaxed);

      // Check if queue is empty
      // Acquire: synchronize with producer's release of head_
      const size_t head = head_.load(std::memory_order_acquire);
      
      if (tail == head) {
        return std::nullopt; // Queue empty
      }

      // Try to claim this item by advancing tail
      // This is the critical section where consumers compete
      const size_t next_tail = (tail + 1) & mask_;
      
      // CAS: Only one consumer will succeed in claiming this slot
      // If successful, we own buffer_[tail]
      // Use release: make our tail update visible to producer
      // Note: tail variable is updated on failure (retry loop)
      if (tail_.compare_exchange_weak(
            tail,   // Expected value (updated on failure)
            next_tail,
            std::memory_order_release,  // Success ordering
            std::memory_order_relaxed   // Failure ordering
          )) {
        // We successfully claimed buffer_[tail]
        // The producer's release of head_ already made the data visible
        return std::move(buffer_[tail]);
      }
      
      // CAS failed - another consumer claimed this item
      // Loop and try again with the updated tail value
      // (compare_exchange_weak updates 'tail' on failure)
    }
  }

  /**
   * Check if queue is empty
   * Note: This is a snapshot and may be immediately stale in SPMC
   */
  bool empty() const {
    return tail_.load(std::memory_order_acquire) ==
           head_.load(std::memory_order_acquire);
  }

  /**
   * Get approximate size
   * Note: May be stale due to concurrent updates
   */
  size_t size() const {
    const size_t head = head_.load(std::memory_order_acquire);
    const size_t tail = tail_.load(std::memory_order_acquire);
    return (head - tail) & mask_;
  }

  size_t capacity() const { return capacity_; }

private:
  static size_t round_up_to_power_of_2(size_t n) {
    if (n == 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    return n + 1;
  }

  const size_t capacity_;
  const size_t mask_;
  std::unique_ptr<T[]> buffer_;

  // Cache line padding: prevent false sharing between producer and consumers
  alignas(64) std::atomic<size_t> head_;  // Producer writes, consumers read

  alignas(64) std::atomic<size_t> tail_;  // Consumers write (compete via CAS), producer reads
};
