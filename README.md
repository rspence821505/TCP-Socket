# TCP Feed Handler

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/std/the-standard)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()
[![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey.svg)]()

A high-performance market data feed handler in C++17 with sub-microsecond latency. Features lock-free SPSC/SPMC queues, binary/text protocols, order book tracking, connection state machine with automatic reconnection, and comprehensive benchmarks.

## Features

- **Lock-Free Queues** - SPSC and SPMC queues with cache-line padding to prevent false sharing
- **Multiple Protocols** - Binary (13-byte header), text (newline-delimited), and UDP multicast support
- **Zero-Copy Parsing** - Ring buffer with `string_view` parsing eliminates allocations
- **Connection Management** - State machine with heartbeat monitoring, exponential backoff reconnection
- **Order Book** - Real-time bid/ask tracking with snapshot recovery and incremental updates
- **Sequence Tracking** - Gap detection for reliable message processing
- **Performance Monitoring** - Latency breakdown with p50/p99/p99.9 percentiles

## Performance

| Metric | Target | Description |
|--------|--------|-------------|
| Throughput | 200-500k msg/sec | On modern laptop hardware |
| End-to-End Latency | <2ms | recv → parse → process |
| p99 Latency | <5ms | Under sustained load |
| Queue Handoff | <100ns | Lock-free SPSC median |

## Quick Start

### Build

```bash
make all              # Build all binaries (43 total)
make tests            # Build test suite
make run-tests        # Run all unit tests
```

### Run Feed Handler

```bash
# Terminal 1: Start mock server
./build/binary_mock_server 9999 1000 10

# Terminal 2: Run feed handler
./build/feed_handler --port 9999 --protocol binary --verbose
```

### Example Output

```
[Connection] Connecting to 127.0.0.1:9999...
[Connection] Connected successfully
[Reader] Received 1000 messages in 100ms (10000 msgs/sec)

=== Latency Breakdown ===
Recv → Parse: p50=10µs, p99=50µs, p99.9=100µs
Parse → Process: p50=5µs, p99=20µs, p99.9=50µs

[AAPL] BID: $150.23 @ 1000 | ASK: $150.25 @ 500
```

## Architecture

```
┌─────────────┐
│   Socket    │  Network I/O
└──────┬──────┘
       │ recv()
       ▼
┌──────────────────────┐
│   Ring Buffer        │  1MB circular buffer (zero-copy)
└──────┬───────────────┘
       │
       ▼
┌──────────────────────┐
│  SPSC/SPMC Queue     │  Lock-free handoff
└──────┬───────────────┘
       │
       ▼
┌──────────────────────┐
│  Parser Thread(s)    │  Protocol decoding
└──────┬───────────────┘
       │
       ▼
┌──────────────────────┐
│  Order Book          │  Bid/ask tracking
└──────────────────────┘
```

## Components

### Protocols

**Binary Protocol** - Compact wire format for high-throughput feeds:
- 13-byte header (length + type + sequence)
- Message types: TICK, HEARTBEAT, SNAPSHOT_REQUEST, SNAPSHOT_RESPONSE
- Network byte order for cross-platform compatibility

**Text Protocol** - Human-readable format:
```
timestamp symbol price volume\n
1234567890 AAPL 150.25 100\n
```

### Lock-Free Queues

```cpp
#include "spsc_queue.hpp"

SPSCQueue<Tick, 65536> queue;

// Producer thread
queue.push(tick);

// Consumer thread
if (auto tick = queue.pop()) {
    process(*tick);
}
```

### Connection Manager

```cpp
#include "connection_manager.hpp"

ConnectionManager conn("127.0.0.1", 9999);
conn.set_heartbeat_timeout(std::chrono::seconds(2));
conn.set_max_reconnect_backoff(std::chrono::seconds(30));

conn.connect();
// Automatic reconnection on failure with exponential backoff
```

### Order Book

```cpp
#include "order_book.hpp"

OrderBook book;
book.load_snapshot(snapshot);      // Full state restoration
book.apply_update(update);         // Incremental updates

auto best_bid = book.get_best_bid();
auto best_ask = book.get_best_ask();
```

## Build Targets

### Binaries

```bash
# Clients
make binary_client              # Basic binary protocol client
make binary_client_zerocopy     # Zero-copy variant

# Mock Servers
make binary_mock_server         # Binary protocol server
make text_mock_server           # Text protocol server
make heartbeat_mock_server      # With heartbeat messages
make snapshot_mock_server       # Snapshot support

# Feed Handlers
make feed_handler               # Unified CLI handler
make feed_handler_spsc          # SPSC queue variant
make feed_handler_spmc          # SPMC queue variant
make feed_handler_heartbeat     # Heartbeat monitoring
make feed_handler_snapshot      # Snapshot recovery
```

### Tests

```bash
make run-tests                  # All unit tests
make run-integration-tests      # Server/client integration
make run-test TEST=test_spsc_queue  # Single test
```

### Benchmarks

```bash
make socket-benchmark           # TCP tuning impact
make tcp-vs-udp                 # Protocol comparison
make benchmark-pool             # Memory pool efficiency
make false-sharing-demo         # Cache contention demo
```

## Configuration

### Feed Handler Options

```bash
./build/feed_handler [options]

Options:
  --host <hostname>       Server address (default: 127.0.0.1)
  --port <port>           Server port (required)
  --protocol {text|binary}  Protocol selection
  --threads=R,P,B         Reader, parser, book-updater thread counts
  --queue-size <size>     SPSC queue capacity
  --verbose               Enable debug output
```

### Socket Tuning

```cpp
#include "socket_config.hpp"

SocketConfig config;
config.tcp_nodelay = true;      // Disable Nagle's algorithm
config.recv_buffer_size = 262144;  // 256KB receive buffer
config.send_buffer_size = 262144;  // 256KB send buffer

apply_socket_config(socket_fd, config);
```

## Directory Structure

```
TCP-Socket/
├── include/              # Header files
│   ├── binary_protocol.hpp    # Binary wire format
│   ├── text_protocol.hpp      # Text format parser
│   ├── spsc_queue.hpp         # Lock-free SPSC queue
│   ├── spmc_queue.hpp         # Lock-free SPMC queue
│   ├── ring_buffer.hpp        # Zero-copy socket buffer
│   ├── connection_manager.hpp # TCP lifecycle management
│   ├── order_book.hpp         # Bid/ask tracking
│   ├── sequence_tracker.hpp   # Gap detection
│   └── net/feed.hpp           # Unified feed interface
├── src/
│   ├── feed_handler/     # Feed handler implementations
│   ├── mock_server/      # Test servers
│   ├── client/           # Client implementations
│   └── benchmark/        # Performance tools
├── tests/                # Google Test suite
├── benchmarks/           # Benchmark scripts
├── scripts/              # Utility scripts
└── Makefile              # Build system
```

## Test Coverage

| Test Suite | Description |
|------------|-------------|
| test_spsc_queue | Lock-free queue correctness and contention |
| test_binary_protocol | Serialization, network byte order |
| test_text_protocol | Parsing, edge cases, partial lines |
| test_order_book | Snapshots, updates, queries |
| test_ring_buffer | Wrap-around, peek/consume |
| test_sequence_tracker | Gap detection, duplicates |
| test_feed_handler | End-to-end processing |
| test_stress | High-load, backpressure, failures |
| test_malformed_input | Protocol error recovery |

## Performance Optimization

### Zero-Copy Techniques

- Ring buffer direct socket reads via `peek()`
- `string_view` text parsing (no allocations)
- Binary protocol direct `memcpy` (no serialization overhead)

### Cache Optimization

- 64-byte cache line padding between producer/consumer pointers
- Prevents false sharing (demonstrated 100x impact in benchmarks)
- `__builtin_expect()` branch hints for hot paths

### Socket Tuning

- `TCP_NODELAY` disables Nagle's algorithm
- Configurable receive/send buffer sizes
- Platform-specific optimizations (TCP_QUICKACK on Linux)

## Requirements

- **Compiler:** clang++ or g++ with C++17 support
- **Platform:** macOS or Linux
- **Testing:** Google Test (optional, for test suite)
- **Build:** Make

## License

MIT License - see [LICENSE](LICENSE) for details.
