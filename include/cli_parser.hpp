#ifndef CLI_PARSER_HPP
#define CLI_PARSER_HPP

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

/**
 * CLI Parser for Feed Handler
 *
 * Supports unified command-line interface:
 *   ./feed_handler --host localhost --port 9999 --threads=1,2,1
 *
 * Options:
 *   --host <hostname>     Server hostname or IP (default: 127.0.0.1)
 *   --port <port>         Server port (required)
 *   --threads=R,P,B       Thread counts: reader, parser, book-updater (default: 1,1,1)
 *   --protocol <type>     Protocol: text or binary (default: text)
 *   --queue-size <size>   Queue capacity (default: 1048576)
 *   --verbose             Enable verbose output
 *   --help                Show help message
 */

struct ThreadConfig {
  int reader_threads = 1;
  int parser_threads = 1;
  int book_updater_threads = 1;

  int total() const {
    return reader_threads + parser_threads + book_updater_threads;
  }
};

enum class Protocol {
  TEXT,
  BINARY
};

struct FeedConfig {
  std::string host = "127.0.0.1";
  uint16_t port = 0;
  ThreadConfig threads;
  Protocol protocol = Protocol::TEXT;
  size_t queue_size = 1024 * 1024;  // 1M entries
  bool verbose = false;
  bool help_requested = false;

  bool is_valid() const {
    return port != 0 &&
           threads.reader_threads > 0 &&
           threads.parser_threads > 0 &&
           threads.book_updater_threads > 0;
  }
};

class CLIParser {
public:
  static void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " --port <port> [options]\n"
              << "\n"
              << "TCP Feed Handler - Receives and processes market data ticks\n"
              << "\n"
              << "Required:\n"
              << "  --port <port>         Server port number\n"
              << "\n"
              << "Options:\n"
              << "  --host <hostname>     Server hostname or IP (default: 127.0.0.1)\n"
              << "  --threads=R,P,B       Thread counts: reader,parser,book-updater\n"
              << "                        (default: 1,1,1)\n"
              << "  --protocol <type>     Protocol type: text or binary (default: text)\n"
              << "  --queue-size <size>   Queue capacity in entries (default: 1048576)\n"
              << "  --verbose             Enable verbose output\n"
              << "  --help                Show this help message\n"
              << "\n"
              << "Examples:\n"
              << "  " << program_name << " --port 9999\n"
              << "  " << program_name << " --host localhost --port 9999 --threads=1,2,1\n"
              << "  " << program_name << " --port 9999 --protocol binary --verbose\n"
              << "\n"
              << "Thread Configuration:\n"
              << "  The --threads option accepts comma-separated counts:\n"
              << "    R = Reader threads (receive data from socket)\n"
              << "    P = Parser threads (parse protocol messages)\n"
              << "    B = Book updater threads (update order book)\n"
              << "\n"
              << "  Example: --threads=1,2,1 means:\n"
              << "    1 reader thread\n"
              << "    2 parser threads\n"
              << "    1 book updater thread\n"
              << std::endl;
  }

  static std::optional<FeedConfig> parse(int argc, char* argv[]) {
    FeedConfig config;

    for (int i = 1; i < argc; ++i) {
      std::string_view arg(argv[i]);

      if (arg == "--help" || arg == "-h") {
        config.help_requested = true;
        return config;
      }
      else if (arg == "--host" && i + 1 < argc) {
        config.host = argv[++i];
      }
      else if (arg == "--port" && i + 1 < argc) {
        config.port = static_cast<uint16_t>(std::atoi(argv[++i]));
      }
      else if (arg.substr(0, 10) == "--threads=") {
        if (!parse_threads(arg.substr(10), config.threads)) {
          std::cerr << "Error: Invalid thread configuration: " << arg << "\n";
          std::cerr << "Expected format: --threads=R,P,B (e.g., --threads=1,2,1)\n";
          return std::nullopt;
        }
      }
      else if (arg == "--threads" && i + 1 < argc) {
        if (!parse_threads(argv[++i], config.threads)) {
          std::cerr << "Error: Invalid thread configuration: " << argv[i] << "\n";
          std::cerr << "Expected format: R,P,B (e.g., 1,2,1)\n";
          return std::nullopt;
        }
      }
      else if (arg == "--protocol" && i + 1 < argc) {
        std::string_view proto(argv[++i]);
        if (proto == "text") {
          config.protocol = Protocol::TEXT;
        } else if (proto == "binary") {
          config.protocol = Protocol::BINARY;
        } else {
          std::cerr << "Error: Unknown protocol: " << proto << "\n";
          std::cerr << "Supported protocols: text, binary\n";
          return std::nullopt;
        }
      }
      else if (arg == "--queue-size" && i + 1 < argc) {
        config.queue_size = static_cast<size_t>(std::atol(argv[++i]));
      }
      else if (arg == "--verbose" || arg == "-v") {
        config.verbose = true;
      }
      else if (arg[0] == '-') {
        std::cerr << "Error: Unknown option: " << arg << "\n";
        return std::nullopt;
      }
      else {
        // Positional argument - treat as port for backward compatibility
        if (config.port == 0) {
          config.port = static_cast<uint16_t>(std::atoi(argv[i]));
        }
      }
    }

    return config;
  }

  static void print_config(const FeedConfig& config) {
    std::cout << "=== Feed Handler Configuration ===\n"
              << "Host:           " << config.host << "\n"
              << "Port:           " << config.port << "\n"
              << "Protocol:       " << (config.protocol == Protocol::TEXT ? "text" : "binary") << "\n"
              << "Threads:\n"
              << "  Reader:       " << config.threads.reader_threads << "\n"
              << "  Parser:       " << config.threads.parser_threads << "\n"
              << "  Book Updater: " << config.threads.book_updater_threads << "\n"
              << "Queue Size:     " << config.queue_size << "\n"
              << "Verbose:        " << (config.verbose ? "yes" : "no") << "\n"
              << "==================================\n"
              << std::endl;
  }

private:
  static bool parse_threads(std::string_view spec, ThreadConfig& threads) {
    // Parse "R,P,B" format
    size_t pos1 = spec.find(',');
    if (pos1 == std::string_view::npos) {
      return false;
    }

    size_t pos2 = spec.find(',', pos1 + 1);
    if (pos2 == std::string_view::npos) {
      return false;
    }

    auto parse_int = [](std::string_view s) -> int {
      int result = 0;
      for (char c : s) {
        if (c < '0' || c > '9') return -1;
        result = result * 10 + (c - '0');
      }
      return result;
    };

    int r = parse_int(spec.substr(0, pos1));
    int p = parse_int(spec.substr(pos1 + 1, pos2 - pos1 - 1));
    int b = parse_int(spec.substr(pos2 + 1));

    if (r <= 0 || p <= 0 || b <= 0) {
      return false;
    }

    threads.reader_threads = r;
    threads.parser_threads = p;
    threads.book_updater_threads = b;
    return true;
  }
};

#endif // CLI_PARSER_HPP
