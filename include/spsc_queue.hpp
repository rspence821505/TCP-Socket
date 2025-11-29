#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>

/**
 * Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer
 */

template <typename T> class SPSCQueue {
public:
  explicit SPSCQueue(size_t capacity)
      : capacity_(round_up_to_power_of_2(capacity)), mask_(capacity_ - 1),
        buffer_(std::make_unique<T[]>(capacity_)), head_(0), tail_(0) {
    // Ensure capacity is power of 2 for efficient modulo
  }

  // Non-copyable, non-movable (contains atomics)
  SPSCQueue(const SPSCQueue &) = delete;
  SPSCQueue &operator=(const SPSCQueue &) = delete;

  /**
   * Producer-side: Push an item (blocks if queue is full)
   * Returns false only if you implement a try_push variant
   */
  bool push(const T &item) {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t next_head = (head + 1) & mask_;

    // Check if queue is full (head would catch up to tail)
    // Use acquire to synchronize with consumer's release in pop()
    if (next_head == tail_.load(std::memory_order_acquire)) {
      return false; // Queue full
    }

    buffer_[head] = item;

    // Release: make the write to buffer visible to consumer
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
   * Consumer-side: Pop an item (returns empty optional if queue is empty)
   */
  std::optional<T> pop() {
    const size_t tail = tail_.load(std::memory_order_relaxed);

    // Check if queue is empty
    // Use acquire to synchronize with producer's release in push()
    if (tail == head_.load(std::memory_order_acquire)) {
      return std::nullopt; // Queue empty
    }

    T item = std::move(buffer_[tail]);

    // Release: make the tail update visible to producer
    tail_.store((tail + 1) & mask_, std::memory_order_release);
    return item;
  }

  /**
   * Check if queue is empty (consumer-side)
   * Note: This is a snapshot and may be stale immediately
   */
  bool empty() const {
    return tail_.load(std::memory_order_acquire) ==
           head_.load(std::memory_order_acquire);
  }

  /**
   * Get current size (approximate, may be stale)
   */
  size_t size() const {
    const size_t head = head_.load(std::memory_order_acquire);
    const size_t tail = tail_.load(std::memory_order_acquire);
    return (head - tail) & mask_;
  }

  size_t capacity() const { return capacity_; }

private:
  static size_t round_up_to_power_of_2(size_t n) {
    if (n == 0)
      return 1;
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
  const size_t mask_; // capacity_ - 1, for fast modulo
  std::unique_ptr<T[]> buffer_;

  // Cache line padding: separate producer/consumer data to different cache
  // lines Modern x86 cache lines are 64 bytes

  alignas(64) std::atomic<size_t> head_; // Producer writes, consumer reads

  alignas(64) std::atomic<size_t> tail_; // Consumer writes, producer reads
};