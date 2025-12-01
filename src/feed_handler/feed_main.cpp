#include <iostream>

#include "cli_parser.hpp"
#include "net/feed.hpp"

/**
 * Unified Feed Handler
 *
 * Uses the consolidated net/feed.hpp module for all feed handling functionality.
 * Supports both text and binary protocols with configurable threading.
 *
 * Usage:
 *   ./feed_handler --host localhost --port 9999 --threads=1,2,1
 */

int main(int argc, char* argv[]) {
  auto config_opt = CLIParser::parse(argc, argv);
  if (!config_opt) {
    LOG_ERROR("Main", "Run '%s --help' for usage information.", argv[0]);
    return 1;
  }

  FeedConfig cli_config = *config_opt;

  if (cli_config.help_requested) {
    CLIParser::print_usage(argv[0]);
    return 0;
  }

  if (!cli_config.is_valid()) {
    LOG_ERROR("Main", "--port is required");
    CLIParser::print_usage(argv[0]);
    return 1;
  }

  if (cli_config.verbose) {
    CLIParser::print_config(cli_config);
  }

  // Convert CLI config to net::FeedConfig
  net::FeedConfig feed_config;
  feed_config.host = cli_config.host;
  feed_config.port = cli_config.port;
  feed_config.protocol = (cli_config.protocol == Protocol::TEXT)
                          ? net::Protocol::TEXT
                          : net::Protocol::BINARY;
  feed_config.queue_size = cli_config.queue_size;
  feed_config.verbose = cli_config.verbose;

  // Create and run feed handler with order book integration
  net::BookUpdatingFeedHandler handler(feed_config);

  if (!handler.start()) {
    LOG_ERROR("Main", "Failed to start feed handler");
    return 1;
  }

  // Wait for completion (server disconnect or error)
  handler.wait();

  // Print statistics
  handler.print_stats();

  return 0;
}
