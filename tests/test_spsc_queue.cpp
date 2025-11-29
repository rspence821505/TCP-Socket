#include "spsc_queue.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

using namespace std::chrono;

struct Message {
  uint64_t timestamp_ns; // When received
  uint32_t symbol_id;
  double price;
  uint64_t volume;
};

// Helper: Get current timestamp in nanoseconds
inline uint64_t now_ns() {
  return duration_cast<nanoseconds>(
             high_resolution_clock::now().time_since_epoch())
      .count();
}

void test_basic_correctness() {
  std::cout << "=== Basic Correctness Test ===" << std::endl;

  SPSCQueue<int> queue(16);

  // Test push/pop
  for (int i = 0; i < 10; ++i) {
    bool success = queue.push(i);
    if (!success) {
      std::cout << "Failed to push " << i << std::endl;
    }
  }

  std::cout << "Pushed 10 items, queue size: " << queue.size() << std::endl;

  for (int i = 0; i < 10; ++i) {
    auto item = queue.pop();
    if (item) {
      std::cout << "Popped: " << *item << std::endl;
      if (*item != i) {
        std::cout << "ERROR: Expected " << i << ", got " << *item << std::endl;
      }
    }
  }

  std::cout << "Queue empty: " << queue.empty() << std::endl;
  std::cout << std::endl;
}

void test_latency_measurement() {
  std::cout << "=== Latency Measurement Test ===" << std::endl;

  constexpr size_t NUM_MESSAGES = 100000;
  SPSCQueue<Message> queue(1024);
  std::vector<uint64_t> latencies;
  latencies.reserve(NUM_MESSAGES);

  std::atomic<bool> producer_done{false};

  // Consumer thread: pop and measure latency
  std::thread consumer([&]() {
    while (!producer_done.load(std::memory_order_acquire) || !queue.empty()) {
      auto msg = queue.pop();
      if (msg) {
        uint64_t end_ns = now_ns();
        uint64_t latency_ns = end_ns - msg->timestamp_ns;
        latencies.push_back(latency_ns);
      } else {
        // Queue empty, yield to producer
        std::this_thread::yield();
      }
    }
  });

  // Producer thread: generate and push messages
  std::thread producer([&]() {
    for (size_t i = 0; i < NUM_MESSAGES; ++i) {
      Message msg{now_ns(), static_cast<uint32_t>(i % 100), 100.0 + i, 1000};

      // Spin until we can push (blocking on full)
      while (!queue.push(std::move(msg))) {
        std::this_thread::yield();
      }
    }
    producer_done.store(true, std::memory_order_release);
  });

  producer.join();
  consumer.join();

  // Compute statistics
  std::sort(latencies.begin(), latencies.end());

  uint64_t sum = std::accumulate(latencies.begin(), latencies.end(), 0ULL);
  double mean_ns = static_cast<double>(sum) / latencies.size();

  uint64_t p50 = latencies[latencies.size() * 50 / 100];
  uint64_t p95 = latencies[latencies.size() * 95 / 100];
  uint64_t p99 = latencies[latencies.size() * 99 / 100];
  uint64_t max = latencies.back();

  std::cout << "Messages processed: " << latencies.size() << std::endl;
  std::cout << "Latency (push -> pop):" << std::endl;
  std::cout << "  Mean: " << mean_ns << " ns" << std::endl;
  std::cout << "  p50:  " << p50 << " ns" << std::endl;
  std::cout << "  p95:  " << p95 << " ns" << std::endl;
  std::cout << "  p99:  " << p99 << " ns" << std::endl;
  std::cout << "  Max:  " << max << " ns" << std::endl;
  std::cout << std::endl;
}

void test_throughput() {
  std::cout << "=== Throughput Test ===" << std::endl;

  constexpr size_t NUM_MESSAGES = 10000000;
  SPSCQueue<int> queue(4096);

  std::atomic<bool> producer_done{false};
  std::atomic<size_t> consumed{0};

  auto start = high_resolution_clock::now();

  std::thread consumer([&]() {
    size_t count = 0;
    while (!producer_done.load(std::memory_order_acquire) || !queue.empty()) {
      auto item = queue.pop();
      if (item) {
        count++;
      } else {
        std::this_thread::yield();
      }
    }
    consumed.store(count, std::memory_order_release);
  });

  std::thread producer([&]() {
    for (size_t i = 0; i < NUM_MESSAGES; ++i) {
      while (!queue.push(static_cast<int>(i))) {
        std::this_thread::yield();
      }
    }
    producer_done.store(true, std::memory_order_release);
  });

  producer.join();
  consumer.join();

  auto end = high_resolution_clock::now();
  auto duration_ms = duration_cast<milliseconds>(end - start).count();

  double msgs_per_sec = (static_cast<double>(consumed) / duration_ms) * 1000.0;

  std::cout << "Processed: " << consumed << " messages" << std::endl;
  std::cout << "Time: " << duration_ms << " ms" << std::endl;
  std::cout << "Throughput: " << static_cast<size_t>(msgs_per_sec)
            << " msgs/sec" << std::endl;
  std::cout << std::endl;
}

int main() {
  test_basic_correctness();
  test_latency_measurement();
  test_throughput();

  return 0;
}