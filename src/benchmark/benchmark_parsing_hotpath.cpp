#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

#include "binary_protocol.hpp"
#include "common.hpp"

/**
 * Parsing Hot Path Benchmark
 *
 * Focuses on the tick deserialization path to measure:
 * - Instructions per cycle (IPC)
 * - Cache miss rates
 * - Branch prediction accuracy
 *
 * Target metrics:
 * - IPC > 2.0 (CPU-bound, efficient)
 * - Cache miss rate < 1%
 * - Branch miss rate < 3%
 */

// Test data structure
struct BenchmarkData {
  std::vector<char> serialized_ticks;
  size_t num_ticks;
  size_t tick_size;
};

/**
 * Generate test data - serialized tick messages
 */
BenchmarkData generate_test_data(size_t num_ticks) {
  BenchmarkData data;

  const size_t message_size = MessageHeader::HEADER_SIZE + TickPayload::PAYLOAD_SIZE;
  data.num_ticks = num_ticks;
  data.tick_size = message_size;
  data.serialized_ticks.resize(num_ticks * message_size);

  std::mt19937_64 gen(42);  // Fixed seed for reproducibility
  std::uniform_int_distribution<uint64_t> timestamp_dist(1000000, 9999999);
  std::uniform_real_distribution<float> price_dist(100.0f, 500.0f);
  std::uniform_int_distribution<int32_t> volume_dist(100, 10000);

  const char symbols[][5] = {"AAPL", "GOOG", "MSFT", "AMZN", "TSLA", "META", "NVDA", "AMD"};
  const size_t num_symbols = sizeof(symbols) / sizeof(symbols[0]);

  char* ptr = data.serialized_ticks.data();

  for (size_t i = 0; i < num_ticks; ++i) {
    // Serialize tick message using the protocol functions
    std::string msg = serialize_tick(
      i,                          // sequence
      timestamp_dist(gen),        // timestamp
      symbols[i % num_symbols],   // symbol
      price_dist(gen),            // price
      volume_dist(gen)            // volume
    );

    memcpy(ptr, msg.data(), message_size);
    ptr += message_size;
  }

  return data;
}

/**
 * Baseline parsing - standard approach
 */
struct BaselineParsingResult {
  uint64_t total_volume;
  double total_price;
  size_t ticks_processed;
  double time_ms;
  double ticks_per_second;
  double ns_per_tick;
};

BaselineParsingResult benchmark_baseline_parsing(const BenchmarkData& data) {
  BaselineParsingResult result = {0, 0.0, 0, 0.0, 0.0, 0.0};

  const char* ptr = data.serialized_ticks.data();
  const char* end = ptr + data.serialized_ticks.size();

  uint64_t start = now_ns();

  while (ptr + data.tick_size <= end) {
    // Deserialize header
    MessageHeader header = deserialize_header(ptr);
    ptr += MessageHeader::HEADER_SIZE;

    // Deserialize tick
    TickPayload tick = deserialize_tick_payload(ptr);
    ptr += TickPayload::PAYLOAD_SIZE;

    // Process (simulate work)
    result.total_volume += tick.volume;
    result.total_price += tick.price;
    result.ticks_processed++;
  }

  uint64_t end_time = now_ns();
  uint64_t duration_ns = end_time - start;

  result.time_ms = duration_ns / 1'000'000.0;
  result.ticks_per_second = (result.ticks_processed * 1'000'000'000.0) / duration_ns;
  result.ns_per_tick = duration_ns / static_cast<double>(result.ticks_processed);

  return result;
}

/**
 * Optimized parsing - cache-friendly, IPC-optimized
 *
 * Optimizations:
 * 1. Inline deserialization
 * 2. Reduced branching
 * 3. Sequential memory access
 * 4. Prefetch hints
 */
struct __attribute__((aligned(64))) OptimizedParsingResult {
  uint64_t total_volume;
  double total_price;
  size_t ticks_processed;
  double time_ms;
  double ticks_per_second;
  double ns_per_tick;
};

// Optimized inline deserialization
inline __attribute__((always_inline))
void parse_tick_optimized(const char* data, uint64_t& total_volume, double& total_price) {
  // Skip header (we know it's a tick)
  const char* payload = data + MessageHeader::HEADER_SIZE;

  // Inline deserialization - no function call overhead
  // TickPayload layout: timestamp(8) + symbol(4) + price(4) + volume(4) = 20 bytes

  // Read price (at offset 12: after timestamp + symbol)
  uint32_t price_bits_net;
  memcpy(&price_bits_net, payload + 12, 4);
  uint32_t price_bits = ntohl(price_bits_net);
  float price;
  memcpy(&price, &price_bits, 4);

  // Read volume (at offset 16)
  int32_t volume_net;
  memcpy(&volume_net, payload + 16, 4);
  int32_t volume = ntohl(volume_net);

  // Accumulate
  total_volume += volume;
  total_price += price;
}

OptimizedParsingResult benchmark_optimized_parsing(const BenchmarkData& data) {
  OptimizedParsingResult result = {0, 0.0, 0, 0.0, 0.0, 0.0};

  const char* ptr = data.serialized_ticks.data();
  const char* end = ptr + data.serialized_ticks.size();
  const size_t tick_size = data.tick_size;

  uint64_t start = now_ns();

  // Main loop - highly optimized
  while (ptr + tick_size <= end) {
    // Prefetch next tick (64 bytes ahead)
    __builtin_prefetch(ptr + tick_size, 0, 3);

    // Parse current tick (inlined)
    parse_tick_optimized(ptr, result.total_volume, result.total_price);

    ptr += tick_size;
    result.ticks_processed++;
  }

  uint64_t end_time = now_ns();
  uint64_t duration_ns = end_time - start;

  result.time_ms = duration_ns / 1'000'000.0;
  result.ticks_per_second = (result.ticks_processed * 1'000'000'000.0) / duration_ns;
  result.ns_per_tick = duration_ns / static_cast<double>(result.ticks_processed);

  return result;
}

/**
 * Print results
 */
void print_result(const std::string& name, const BaselineParsingResult& result) {
  std::cout << name << ":" << std::endl;
  std::cout << "  Ticks processed:   " << result.ticks_processed << std::endl;
  std::cout << "  Time:              " << std::fixed << std::setprecision(2)
            << result.time_ms << " ms" << std::endl;
  std::cout << "  Ticks/sec:         " << std::fixed << std::setprecision(2)
            << result.ticks_per_second / 1'000'000.0 << " M" << std::endl;
  std::cout << "  ns/tick:           " << std::fixed << std::setprecision(2)
            << result.ns_per_tick << " ns" << std::endl;
  std::cout << std::endl;
}

void print_result_optimized(const std::string& name, const OptimizedParsingResult& result) {
  std::cout << name << ":" << std::endl;
  std::cout << "  Ticks processed:   " << result.ticks_processed << std::endl;
  std::cout << "  Time:              " << std::fixed << std::setprecision(2)
            << result.time_ms << " ms" << std::endl;
  std::cout << "  Ticks/sec:         " << std::fixed << std::setprecision(2)
            << result.ticks_per_second / 1'000'000.0 << " M" << std::endl;
  std::cout << "  ns/tick:           " << std::fixed << std::setprecision(2)
            << result.ns_per_tick << " ns" << std::endl;
  std::cout << std::endl;
}

int main(int argc, char* argv[]) {
  std::cout << "==================================================================" << std::endl;
  std::cout << "Parsing Hot Path Benchmark" << std::endl;
  std::cout << "==================================================================" << std::endl;
  std::cout << std::endl;

  // Configuration
  size_t num_ticks = 10'000'000;  // 10M ticks for stable measurements

  if (argc > 1) {
    num_ticks = std::atoll(argv[1]);
  }

  std::cout << "Configuration:" << std::endl;
  std::cout << "  Ticks:             " << num_ticks << std::endl;
  std::cout << "  Message size:      " << (MessageHeader::HEADER_SIZE + TickPayload::PAYLOAD_SIZE)
            << " bytes" << std::endl;
  std::cout << "  Total data:        " << (num_ticks * (MessageHeader::HEADER_SIZE + TickPayload::PAYLOAD_SIZE)) / (1024 * 1024)
            << " MB" << std::endl;
  std::cout << std::endl;

  // Generate test data
  std::cout << "Generating test data..." << std::endl;
  BenchmarkData data = generate_test_data(num_ticks);
  std::cout << "Test data ready" << std::endl;
  std::cout << std::endl;

  // Warm up cache
  std::cout << "Warming up cache..." << std::endl;
  benchmark_baseline_parsing(data);
  std::cout << "Cache warmed" << std::endl;
  std::cout << std::endl;

  // Benchmark baseline
  std::cout << "Running baseline parsing..." << std::endl;
  BaselineParsingResult baseline = benchmark_baseline_parsing(data);
  print_result("Baseline", baseline);

  // Benchmark optimized
  std::cout << "Running optimized parsing..." << std::endl;
  OptimizedParsingResult optimized = benchmark_optimized_parsing(data);
  print_result_optimized("Optimized", optimized);

  // Comparison
  double speedup = baseline.ns_per_tick / optimized.ns_per_tick;
  double improvement = (baseline.ns_per_tick - optimized.ns_per_tick) / baseline.ns_per_tick * 100.0;

  std::cout << "==================================================================" << std::endl;
  std::cout << "COMPARISON" << std::endl;
  std::cout << "==================================================================" << std::endl;
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "Speedup:           " << speedup << "x" << std::endl;
  std::cout << "Improvement:       " << improvement << "%" << std::endl;
  std::cout << "ns/tick:           " << baseline.ns_per_tick << " -> "
            << optimized.ns_per_tick << " ns" << std::endl;
  std::cout << std::endl;

  // Guidance for profiling
  std::cout << "==================================================================" << std::endl;
  std::cout << "NEXT STEPS: Profile with Instruments" << std::endl;
  std::cout << "==================================================================" << std::endl;
  std::cout << std::endl;
  std::cout << "To measure IPC and cache metrics on macOS:" << std::endl;
  std::cout << std::endl;
  std::cout << "1. Profile with sample:" << std::endl;
  std::cout << "   sample ./build/benchmark_parsing_hotpath 30" << std::endl;
  std::cout << std::endl;
  std::cout << "2. Or use xctrace:" << std::endl;
  std::cout << "   xctrace record --template 'Time Profiler' --launch -- \\" << std::endl;
  std::cout << "     ./build/benchmark_parsing_hotpath 10000000" << std::endl;
  std::cout << std::endl;
  std::cout << "Target metrics:" << std::endl;
  std::cout << "  IPC > 2.0" << std::endl;
  std::cout << "  Cache miss rate < 1%" << std::endl;
  std::cout << std::endl;

  return 0;
}
