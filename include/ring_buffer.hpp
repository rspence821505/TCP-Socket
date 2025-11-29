#ifndef RING_BUFFER_HPP
#define RING_BUFFER_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string_view>

class RingBuffer {
private:
  static constexpr size_t BUFFER_SIZE = 1024 * 1024; // 1 MB
  char buffer_[BUFFER_SIZE];
  size_t read_pos_;
  size_t write_pos_;
  size_t size_;

public:
  RingBuffer() : read_pos_(0), write_pos_(0), size_(0) {}

  std::pair<char *, size_t> get_write_ptr() {
    if (write_pos_ >= read_pos_) {
      size_t space = BUFFER_SIZE - write_pos_;
      if (read_pos_ == 0 && space > 0) {
        space--;
      }
      return {buffer_ + write_pos_, space};
    } else {
      return {buffer_ + write_pos_, read_pos_ - write_pos_ - 1};
    }
  }

  void commit_write(size_t n) {
    write_pos_ = (write_pos_ + n) % BUFFER_SIZE;
    size_ += n;
  }

  size_t available() const { return size_; }

  std::string_view peek(size_t n) const {
    if (n > size_) {
      return {};
    }

    size_t contiguous = BUFFER_SIZE - read_pos_;
    if (n <= contiguous) {
      return std::string_view(buffer_ + read_pos_, n);
    } else {
      return {};
    }
  }

  bool peek_bytes(char *output, size_t n) const {
    if (n > size_) {
      return false;
    }

    size_t contiguous = BUFFER_SIZE - read_pos_;
    if (n <= contiguous) {
      memcpy(output, buffer_ + read_pos_, n);
    } else {
      memcpy(output, buffer_ + read_pos_, contiguous);
      memcpy(output + contiguous, buffer_, n - contiguous);
    }
    return true;
  }

  bool read_bytes(char *output, size_t n) {
    if (!peek_bytes(output, n)) {
      return false;
    }
    consume(n);
    return true;
  }

  void consume(size_t n) {
    if (n > size_) {
      n = size_;
    }
    read_pos_ = (read_pos_ + n) % BUFFER_SIZE;
    size_ -= n;
  }

  size_t capacity() const { return BUFFER_SIZE; }

  size_t free_space() const {
    return BUFFER_SIZE - size_ - 1;
  }

  void clear() {
    read_pos_ = 0;
    write_pos_ = 0;
    size_ = 0;
  }
};

#endif // RING_BUFFER_HPP
