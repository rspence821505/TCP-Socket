#ifndef RING_BUFFER_HPP
#define RING_BUFFER_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string_view>

// Fixed-size ring buffer for zero-copy network message reassembly
// Optimized for the pattern: read from socket -> parse -> consume
class RingBuffer {
private:
  static constexpr size_t BUFFER_SIZE = 1024 * 1024; // 1 MB
  char buffer_[BUFFER_SIZE];
  size_t read_pos_;  // Where we read data from (consumer)
  size_t write_pos_; // Where we write data to (producer)
  size_t size_;      // Number of bytes available to read

public:
  RingBuffer() : read_pos_(0), write_pos_(0), size_(0) {}

  // Returns a pointer where new data can be written
  // and the maximum number of contiguous bytes available
  std::pair<char *, size_t> get_write_ptr() {
    if (write_pos_ >= read_pos_) {
      // Write space is from write_pos to end, or wrap around
      size_t space = BUFFER_SIZE - write_pos_;
      // If read_pos is 0, we can't write to the very last byte
      // (would make buffer appear empty when write_pos wraps to 0)
      if (read_pos_ == 0 && space > 0) {
        space--;
      }
      return {buffer_ + write_pos_, space};
    } else {
      // Write space is from write_pos to read_pos - 1
      return {buffer_ + write_pos_, read_pos_ - write_pos_ - 1};
    }
  }

  // Commit 'n' bytes that were written to the write pointer
  void commit_write(size_t n) {
    write_pos_ = (write_pos_ + n) % BUFFER_SIZE;
    size_ += n;
  }

  // Returns the number of bytes available to read
  size_t available() const { return size_; }

  // Zero-copy peek at the first 'n' bytes without consuming them
  // Returns empty string_view if not enough data available
  // NOTE: This may not be contiguous! Check if we need to handle wrap-around
  std::string_view peek(size_t n) const {
    if (n > size_) {
      return {};
    }

    // Check if data is contiguous
    size_t contiguous = BUFFER_SIZE - read_pos_;
    if (n <= contiguous) {
      // Data is contiguous
      return std::string_view(buffer_ + read_pos_, n);
    } else {
      // Data wraps around - we can't return a single string_view
      // Caller must handle this case
      return {};
    }
  }

  // Peek at specific bytes (handles wrap-around by copying to output buffer)
  // Returns false if not enough data available
  bool peek_bytes(char *output, size_t n) const {
    if (n > size_) {
      return false;
    }

    size_t contiguous = BUFFER_SIZE - read_pos_;
    if (n <= contiguous) {
      // Simple case: data is contiguous
      memcpy(output, buffer_ + read_pos_, n);
    } else {
      // Data wraps around
      memcpy(output, buffer_ + read_pos_, contiguous);
      memcpy(output + contiguous, buffer_, n - contiguous);
    }
    return true;
  }

  // Read (peek + consume) 'n' bytes into output buffer
  bool read_bytes(char *output, size_t n) {
    if (!peek_bytes(output, n)) {
      return false;
    }
    consume(n);
    return true;
  }

  // Consume (discard) the first 'n' bytes
  void consume(size_t n) {
    if (n > size_) {
      n = size_;
    }
    read_pos_ = (read_pos_ + n) % BUFFER_SIZE;
    size_ -= n;
  }

  // Get total buffer capacity
  size_t capacity() const { return BUFFER_SIZE; }

  // Get free space
  size_t free_space() const {
    return BUFFER_SIZE - size_ - 1; // -1 to distinguish full from empty
  }

  // Reset buffer to empty state
  void clear() {
    read_pos_ = 0;
    write_pos_ = 0;
    size_ = 0;
  }
};

#endif // RING_BUFFER_HPP