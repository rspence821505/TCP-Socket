#ifndef THREAD_LOCAL_POOL_HPP
#define THREAD_LOCAL_POOL_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <stdexcept>

/**
 * Thread-Local Memory Pool - Extension
 *
 * A fast, thread-local arena allocator for small, short-lived allocations.
 * Designed for scenarios where malloc overhead is significant but allocations
 * are rare.
 *
 * Features:
 * - Thread-local (no synchronization overhead)
 * - Bump allocator (fast allocation, O(1))
 * - Cache-line aligned to avoid false sharing
 * - Configurable chunk size
 * - Optional alignment support
 *
 * Use cases:
 * - Symbol string storage in trading systems
 * - Temporary buffers in hot paths
 * - Message parsing scratchpad
 *
 * Limitations:
 * - No individual deallocation (arena-style)
 * - Must reset() periodically to reclaim memory
 * - Not suitable for long-lived allocations
 */

class ThreadLocalPool {
public:
  // Configuration
  static constexpr size_t DEFAULT_CHUNK_SIZE = 64 * 1024; // 64KB chunks
  static constexpr size_t CACHE_LINE_SIZE = 64;
  static constexpr size_t MAX_ALIGNMENT = 64;

  /**
   * Constructor
   * @param chunk_size Size of each memory chunk (default 64KB)
   */
  explicit ThreadLocalPool(size_t chunk_size = DEFAULT_CHUNK_SIZE)
      : chunk_size_(chunk_size), current_chunk_(nullptr), current_offset_(0),
        total_allocated_(0), allocation_count_(0) {
    if (chunk_size < 1024) {
      throw std::invalid_argument("Chunk size must be at least 1KB");
    }

    // Allocate first chunk
    allocate_new_chunk();
  }

  /**
   * Destructor - frees all chunks
   */
  ~ThreadLocalPool() { reset(); }

  // Disable copy/move (thread-local resource)
  ThreadLocalPool(const ThreadLocalPool &) = delete;
  ThreadLocalPool &operator=(const ThreadLocalPool &) = delete;
  ThreadLocalPool(ThreadLocalPool &&) = delete;
  ThreadLocalPool &operator=(ThreadLocalPool &&) = delete;

  /**
   * Allocate memory from the pool
   * @param size Number of bytes to allocate
   * @param alignment Required alignment (default: natural)
   * @return Pointer to allocated memory
   */
  void *allocate(size_t size, size_t alignment = alignof(std::max_align_t)) {
    if (size == 0)
      return nullptr;
    if (alignment > MAX_ALIGNMENT) {
      throw std::invalid_argument("Alignment too large");
    }

    // Align current offset
    const size_t aligned_offset = align_up(current_offset_, alignment);
    const size_t new_offset = aligned_offset + size;

    // Check if allocation fits in current chunk
    if (new_offset > chunk_size_) {
      // Allocation doesn't fit, need new chunk
      if (size > chunk_size_) {
        // Single allocation larger than chunk size - allocate separately
        return allocate_large(size, alignment);
      }

      // Allocate new chunk and try again
      allocate_new_chunk();
      return allocate(size, alignment);
    }

    // Allocation fits in current chunk
    void *ptr = current_chunk_ + aligned_offset;
    current_offset_ = new_offset;

    // Update statistics
    total_allocated_ += size;
    allocation_count_++;

    return ptr;
  }

  /**
   * Allocate and construct an object
   * @tparam T Object type
   * @tparam Args Constructor argument types
   * @param args Constructor arguments
   * @return Pointer to constructed object
   */
  template <typename T, typename... Args> T *construct(Args &&...args) {
    void *ptr = allocate(sizeof(T), alignof(T));
    return new (ptr) T(std::forward<Args>(args)...);
  }

  /**
   * Allocate a string copy
   * @param str Source string
   * @param len String length (excluding null terminator)
   * @return Pointer to null-terminated string copy
   */
  char *allocate_string(const char *str, size_t len) {
    char *copy = static_cast<char *>(allocate(len + 1, 1));
    memcpy(copy, str, len);
    copy[len] = '\0';
    return copy;
  }

  /**
   * Reset the pool, freeing all memory
   * NOTE: All previously allocated pointers become invalid!
   */
  void reset() {
    // Free all chunks except the first one
    for (size_t i = 1; i < chunks_.size(); ++i) {
      std::free(chunks_[i]);
    }

    // Keep first chunk, reset offset
    if (!chunks_.empty()) {
      current_chunk_ = chunks_[0];
      current_offset_ = 0;
      chunks_.resize(1);
    }

    // Reset statistics
    total_allocated_ = 0;
    allocation_count_ = 0;
  }

  // Statistics
  size_t total_allocated() const { return total_allocated_; }
  size_t allocation_count() const { return allocation_count_; }
  size_t chunk_count() const { return chunks_.size(); }
  size_t memory_usage() const { return chunks_.size() * chunk_size_; }

  // Utilization percentage
  double utilization() const {
    if (chunks_.empty())
      return 0.0;
    return 100.0 * total_allocated_ / memory_usage();
  }

private:
  /**
   * Allocate a new chunk
   */
  void allocate_new_chunk() {
    // Allocate aligned memory for cache-line alignment
    void *chunk = std::aligned_alloc(CACHE_LINE_SIZE, chunk_size_);
    if (!chunk) {
      throw std::bad_alloc();
    }

    chunks_.push_back(static_cast<char *>(chunk));
    current_chunk_ = static_cast<char *>(chunk);
    current_offset_ = 0;
  }

  /**
   * Allocate a large block that doesn't fit in a chunk
   */
  void *allocate_large(size_t size, size_t alignment) {
    // Allocate directly with system allocator
    void *ptr = std::aligned_alloc(alignment, size);
    if (!ptr) {
      throw std::bad_alloc();
    }

    // Track it separately (so we can free it on reset)
    large_allocations_.push_back(ptr);

    total_allocated_ += size;
    allocation_count_++;

    return ptr;
  }

  /**
   * Align a value up to the nearest multiple of alignment
   */
  static constexpr size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
  }

  // Configuration
  const size_t chunk_size_;

  // Current state
  std::vector<char *> chunks_;            // All allocated chunks
  std::vector<void *> large_allocations_; // Large allocations outside chunks
  char *current_chunk_;                   // Current chunk for allocation
  size_t current_offset_;                 // Current offset in chunk

  // Statistics
  size_t total_allocated_;
  size_t allocation_count_;
};

/**
 * Thread-local pool instance
 * Each thread gets its own pool - no synchronization needed!
 */
inline thread_local ThreadLocalPool g_thread_pool;

/**
 * Convenience allocator for use with STL containers
 */
template <typename T> class PoolAllocator {
public:
  using value_type = T;

  PoolAllocator() noexcept = default;

  template <typename U> PoolAllocator(const PoolAllocator<U> &) noexcept {}

  T *allocate(size_t n) {
    return static_cast<T *>(g_thread_pool.allocate(n * sizeof(T), alignof(T)));
  }

  void deallocate(T *, size_t) noexcept {
    // Pool doesn't support individual deallocation
  }

  template <typename U>
  bool operator==(const PoolAllocator<U> &) const noexcept {
    return true;
  }

  template <typename U>
  bool operator!=(const PoolAllocator<U> &) const noexcept {
    return false;
  }
};

#endif // THREAD_LOCAL_POOL_HPP
