#ifndef TEXT_PROTOCOL_HPP
#define TEXT_PROTOCOL_HPP

#include <charconv>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

/**
 * Text Protocol Parser
 *
 * Parses newline-delimited tick messages in the format:
 *   timestamp symbol price volume\n
 *
 * Example:
 *   1234567890 AAPL 150.25 100\n
 *   1234567891 GOOG 2750.50 50\n
 */

struct TextTick {
  uint64_t timestamp;
  char symbol[8];  // Up to 8 chars, null-terminated
  double price;
  int64_t volume;

  TextTick() : timestamp(0), price(0.0), volume(0) {
    symbol[0] = '\0';
  }
};

/**
 * Parse a single text tick from a string_view (zero-copy)
 *
 * @param line The line to parse (without trailing newline)
 * @return Parsed tick, or nullopt if parsing failed
 */
inline std::optional<TextTick> parse_text_tick(std::string_view line) {
  TextTick tick;

  // Skip leading whitespace
  size_t pos = 0;
  while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) {
    ++pos;
  }

  if (pos >= line.size()) {
    return std::nullopt;  // Empty line
  }

  // Parse timestamp
  size_t field_start = pos;
  while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t') {
    ++pos;
  }

  if (pos == field_start) {
    return std::nullopt;  // No timestamp
  }

  auto ts_result = std::from_chars(line.data() + field_start,
                                    line.data() + pos,
                                    tick.timestamp);
  if (ts_result.ec != std::errc{}) {
    return std::nullopt;  // Invalid timestamp
  }

  // Skip whitespace
  while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) {
    ++pos;
  }

  // Parse symbol
  field_start = pos;
  while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t') {
    ++pos;
  }

  size_t symbol_len = pos - field_start;
  if (symbol_len == 0 || symbol_len > 7) {
    return std::nullopt;  // Invalid symbol length
  }

  memcpy(tick.symbol, line.data() + field_start, symbol_len);
  tick.symbol[symbol_len] = '\0';

  // Skip whitespace
  while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) {
    ++pos;
  }

  // Parse price
  field_start = pos;
  while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t') {
    ++pos;
  }

  if (pos == field_start) {
    return std::nullopt;  // No price
  }

  // std::from_chars for double may not be available on all compilers
  // Use a simple manual parse or strtod
  std::string price_str(line.data() + field_start, pos - field_start);
  char* end_ptr;
  tick.price = std::strtod(price_str.c_str(), &end_ptr);
  if (end_ptr == price_str.c_str()) {
    return std::nullopt;  // Invalid price
  }

  // Skip whitespace
  while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) {
    ++pos;
  }

  // Parse volume
  field_start = pos;
  while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t' &&
         line[pos] != '\n' && line[pos] != '\r') {
    ++pos;
  }

  if (pos == field_start) {
    return std::nullopt;  // No volume
  }

  auto vol_result = std::from_chars(line.data() + field_start,
                                     line.data() + pos,
                                     tick.volume);
  if (vol_result.ec != std::errc{}) {
    return std::nullopt;  // Invalid volume
  }

  return tick;
}

/**
 * Find the next complete line in a buffer
 *
 * @param buffer The buffer to search
 * @param length The length of valid data in buffer
 * @param line_end Output: position of newline character
 * @return true if a complete line was found
 */
inline bool find_line(const char* buffer, size_t length, size_t& line_end) {
  for (size_t i = 0; i < length; ++i) {
    if (buffer[i] == '\n') {
      line_end = i;
      return true;
    }
  }
  return false;
}

/**
 * Serialize a tick to text format
 *
 * @param tick The tick to serialize
 * @return String in format "timestamp symbol price volume\n"
 */
inline std::string serialize_text_tick(const TextTick& tick) {
  char buffer[128];
  int len = snprintf(buffer, sizeof(buffer), "%lu %s %.2f %ld\n",
                     static_cast<unsigned long>(tick.timestamp),
                     tick.symbol,
                     tick.price,
                     static_cast<long>(tick.volume));
  return std::string(buffer, len);
}

/**
 * Serialize tick fields directly to text format
 */
inline std::string serialize_text_tick(uint64_t timestamp,
                                        const char* symbol,
                                        double price,
                                        int64_t volume) {
  char buffer[128];
  int len = snprintf(buffer, sizeof(buffer), "%lu %s %.2f %ld\n",
                     static_cast<unsigned long>(timestamp),
                     symbol,
                     price,
                     static_cast<long>(volume));
  return std::string(buffer, len);
}

/**
 * Text line buffer for accumulating partial lines
 *
 * Handles the case where recv() returns partial lines
 */
class TextLineBuffer {
public:
  static constexpr size_t MAX_LINE_LENGTH = 256;
  static constexpr size_t BUFFER_SIZE = 64 * 1024;  // 64KB

  TextLineBuffer() : write_pos_(0), read_pos_(0) {}

  // Add data to the buffer
  bool append(const char* data, size_t length) {
    // Compact buffer if needed
    if (write_pos_ + length > BUFFER_SIZE) {
      compact();
    }

    if (write_pos_ + length > BUFFER_SIZE) {
      return false;  // Buffer overflow
    }

    memcpy(buffer_ + write_pos_, data, length);
    write_pos_ += length;
    return true;
  }

  // Try to extract the next complete line
  bool get_line(std::string_view& line) {
    size_t search_start = read_pos_;

    for (size_t i = search_start; i < write_pos_; ++i) {
      if (buffer_[i] == '\n') {
        // Found a complete line
        size_t line_length = i - read_pos_;

        // Handle \r\n
        if (line_length > 0 && buffer_[i - 1] == '\r') {
          line_length--;
        }

        line = std::string_view(buffer_ + read_pos_, line_length);
        read_pos_ = i + 1;
        return true;
      }
    }

    return false;  // No complete line
  }

  // Check if buffer has data
  bool has_data() const {
    return write_pos_ > read_pos_;
  }

  // Get available space for writing
  size_t available_space() const {
    return BUFFER_SIZE - write_pos_;
  }

  // Get write pointer for direct recv()
  char* write_ptr() {
    return buffer_ + write_pos_;
  }

  // Advance write position after recv()
  void advance_write(size_t bytes) {
    write_pos_ += bytes;
  }

  // Reset buffer
  void reset() {
    read_pos_ = 0;
    write_pos_ = 0;
  }

private:
  void compact() {
    if (read_pos_ > 0) {
      size_t remaining = write_pos_ - read_pos_;
      memmove(buffer_, buffer_ + read_pos_, remaining);
      read_pos_ = 0;
      write_pos_ = remaining;
    }
  }

  char buffer_[BUFFER_SIZE];
  size_t write_pos_;
  size_t read_pos_;
};

#endif // TEXT_PROTOCOL_HPP
