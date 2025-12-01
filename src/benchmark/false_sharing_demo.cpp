#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#include "common.hpp"

// Cache line size (typically 64 bytes on modern CPUs)
constexpr size_t CACHE_LINE_SIZE = 64;

/**
 * FALSE SHARING DEMONSTRATION
 *
 * False sharing occurs when multiple threads modify variables that
 * reside on the same cache line. Even though the variables are
 * logically independent, the CPU must invalidate the entire cache
 * line when any part of it is modified, causing expensive cache
 * coherency traffic.
 */

//=============================================================================
// Bad: Counters packed together (false sharing)
//=============================================================================
struct PackedCounters {
  std::atomic<uint64_t> counter1{0};
  std::atomic<uint64_t> counter2{0};
  std::atomic<uint64_t> counter3{0};
  std::atomic<uint64_t> counter4{0};
};

//=============================================================================
// Good: Counters padded to separate cache lines
//=============================================================================
struct alignas(CACHE_LINE_SIZE) PaddedCounter {
  std::atomic<uint64_t> value{0};
  char padding[CACHE_LINE_SIZE - sizeof(std::atomic<uint64_t>)];
};

struct PaddedCounters {
  PaddedCounter counter1;
  PaddedCounter counter2;
  PaddedCounter counter3;
  PaddedCounter counter4;
};

//=============================================================================
// Benchmark functions
//=============================================================================

void increment_packed(std::atomic<uint64_t>& counter, size_t iterations) {
  for (size_t i = 0; i < iterations; ++i) {
    counter.fetch_add(1, std::memory_order_relaxed);
  }
}

void increment_padded(PaddedCounter& counter, size_t iterations) {
  for (size_t i = 0; i < iterations; ++i) {
    counter.value.fetch_add(1, std::memory_order_relaxed);
  }
}

double benchmark_packed(size_t iterations, int num_threads) {
  PackedCounters counters;
  std::vector<std::thread> threads;

  uint64_t start = now_ms();

  // Thread 0 -> counter1, Thread 1 -> counter2, etc.
  threads.emplace_back(increment_packed, std::ref(counters.counter1), iterations);
  if (num_threads > 1)
    threads.emplace_back(increment_packed, std::ref(counters.counter2), iterations);
  if (num_threads > 2)
    threads.emplace_back(increment_packed, std::ref(counters.counter3), iterations);
  if (num_threads > 3)
    threads.emplace_back(increment_packed, std::ref(counters.counter4), iterations);

  for (auto& t : threads) {
    t.join();
  }

  uint64_t end = now_ms();

  // Verify
  uint64_t expected = iterations * threads.size();
  uint64_t actual = counters.counter1 + counters.counter2 +
                    counters.counter3 + counters.counter4;
  if (actual != expected) {
    LOG_ERROR("FalseSharing", "Expected %lu, got %lu", expected, actual);
  }

  return static_cast<double>(end - start);
}

double benchmark_padded(size_t iterations, int num_threads) {
  PaddedCounters counters;
  std::vector<std::thread> threads;

  uint64_t start = now_ms();

  threads.emplace_back(increment_padded, std::ref(counters.counter1), iterations);
  if (num_threads > 1)
    threads.emplace_back(increment_padded, std::ref(counters.counter2), iterations);
  if (num_threads > 2)
    threads.emplace_back(increment_padded, std::ref(counters.counter3), iterations);
  if (num_threads > 3)
    threads.emplace_back(increment_padded, std::ref(counters.counter4), iterations);

  for (auto& t : threads) {
    t.join();
  }

  uint64_t end = now_ms();

  // Verify
  uint64_t expected = iterations * threads.size();
  uint64_t actual = counters.counter1.value + counters.counter2.value +
                    counters.counter3.value + counters.counter4.value;
  if (actual != expected) {
    LOG_ERROR("FalseSharing", "Expected %lu, got %lu", expected, actual);
  }

  return static_cast<double>(end - start);
}

//=============================================================================
// Main
//=============================================================================

int main(int argc, char* argv[]) {
  size_t iterations = 10'000'000;
  int num_threads = 4;

  if (argc > 1) iterations = std::atol(argv[1]);
  if (argc > 2) num_threads = std::atoi(argv[2]);

  // Clamp threads to 4 max for this demo
  if (num_threads > 4) num_threads = 4;
  if (num_threads < 1) num_threads = 1;

  std::cout << "=================================================================\n";
  std::cout << "False Sharing Demonstration\n";
  std::cout << "=================================================================\n";
  std::cout << "\n";
  std::cout << "Configuration:\n";
  std::cout << "  Iterations per thread: " << iterations << "\n";
  std::cout << "  Number of threads:     " << num_threads << "\n";
  std::cout << "  Cache line size:       " << CACHE_LINE_SIZE << " bytes\n";
  std::cout << "\n";

  // Show struct sizes
  std::cout << "Data Structure Sizes:\n";
  std::cout << "  PackedCounters:  " << sizeof(PackedCounters) << " bytes ";
  std::cout << "(all counters in ~1 cache line)\n";
  std::cout << "  PaddedCounters:  " << sizeof(PaddedCounters) << " bytes ";
  std::cout << "(each counter in separate cache line)\n";
  std::cout << "\n";

  // Warmup
  std::cout << "Warming up...\n";
  benchmark_packed(iterations / 10, num_threads);
  benchmark_padded(iterations / 10, num_threads);

  // Run benchmarks multiple times
  const int runs = 5;
  double packed_total = 0, padded_total = 0;

  std::cout << "\nRunning benchmarks (" << runs << " runs each)...\n";
  std::cout << "\n";

  for (int i = 0; i < runs; ++i) {
    double packed_time = benchmark_packed(iterations, num_threads);
    double padded_time = benchmark_padded(iterations, num_threads);

    packed_total += packed_time;
    padded_total += padded_time;

    std::cout << "Run " << (i + 1) << ": ";
    std::cout << "Packed=" << packed_time << "ms, ";
    std::cout << "Padded=" << padded_time << "ms, ";
    std::cout << "Speedup=" << (packed_time / padded_time) << "x\n";
  }

  double packed_avg = packed_total / runs;
  double padded_avg = padded_total / runs;
  double speedup = packed_avg / padded_avg;

  std::cout << "\n";
  std::cout << "=================================================================\n";
  std::cout << "RESULTS\n";
  std::cout << "=================================================================\n";
  std::cout << "\n";
  std::cout << "Average times:\n";
  std::cout << "  Packed (false sharing):    " << packed_avg << " ms\n";
  std::cout << "  Padded (no false sharing): " << padded_avg << " ms\n";
  std::cout << "\n";
  std::cout << "Speedup from eliminating false sharing: " << speedup << "x\n";
  std::cout << "\n";

  if (speedup > 1.5) {
    std::cout << "SIGNIFICANT false sharing detected!\n";
    std::cout << "Padding counters to separate cache lines improves performance.\n";
  } else if (speedup > 1.1) {
    std::cout << "MODERATE false sharing detected.\n";
    std::cout << "Some benefit from cache line padding.\n";
  } else {
    std::cout << "MINIMAL false sharing impact detected.\n";
    std::cout << "(This can happen with few threads or fast cache coherency)\n";
  }

  std::cout << "\n";
  std::cout << "=================================================================\n";
  std::cout << "EXPLANATION\n";
  std::cout << "=================================================================\n";
  std::cout << "\n";
  std::cout << "False sharing occurs when:\n";
  std::cout << "  1. Multiple threads access different variables\n";
  std::cout << "  2. Those variables share the same cache line (64 bytes)\n";
  std::cout << "  3. At least one thread is writing\n";
  std::cout << "\n";
  std::cout << "The CPU must invalidate the entire cache line on each write,\n";
  std::cout << "causing cache coherency traffic between cores.\n";
  std::cout << "\n";
  std::cout << "Solution: Pad data structures so that frequently-modified\n";
  std::cout << "variables are on separate cache lines.\n";
  std::cout << "\n";
  std::cout << "In C++17, use alignas(64) or std::hardware_destructive_interference_size\n";
  std::cout << "\n";

  return 0;
}
