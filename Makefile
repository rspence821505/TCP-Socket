# TCP-Socket Project Makefile
# Builds all binaries from src/ using headers from include/

# Use system clang++ to avoid conda environment conflicts with LTO
CXX = /usr/bin/clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

# Directories
SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build
TESTS_DIR = tests

# Include path
INCLUDES = -I$(INCLUDE_DIR)

# Profiling flags
PROFILE_FLAGS = -O2 -g -fno-omit-frame-pointer
OPTIMIZE_FLAGS = -O3 -g -fno-omit-frame-pointer

# Source files
SRCS = $(wildcard $(SRC_DIR)/*.cpp)
TEST_SRCS = $(wildcard $(TESTS_DIR)/*.cpp)

# Main targets
BINARIES = binary_client binary_client_zerocopy binary_mock_server mock_server \
           blocking_client feed_handler_spsc feed_handler_spmc \
           socket_tuning_benchmark feed_handler_heartbeat heartbeat_mock_server \
           feed_handler_snapshot snapshot_mock_server \
           udp_mock_server udp_feed_handler tcp_vs_udp_benchmark \
           benchmark_pool_vs_malloc false_sharing_demo benchmark_parsing_hotpath

TEST_BINARIES = test_spsc_queue

# Default target
all: $(BUILD_DIR) $(BINARIES)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Alias for build directory
directories: $(BUILD_DIR)

# Binary targets
binary_client: $(SRC_DIR)/binary_client.cpp $(INCLUDE_DIR)/binary_protocol.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRC_DIR)/binary_client.cpp -o $(BUILD_DIR)/binary_client

binary_client_zerocopy: $(SRC_DIR)/binary_client_zerocopy.cpp $(INCLUDE_DIR)/binary_protocol.hpp $(INCLUDE_DIR)/ring_buffer.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRC_DIR)/binary_client_zerocopy.cpp -o $(BUILD_DIR)/binary_client_zerocopy

binary_mock_server: $(SRC_DIR)/binary_mock_server.cpp $(INCLUDE_DIR)/binary_protocol.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRC_DIR)/binary_mock_server.cpp -o $(BUILD_DIR)/binary_mock_server

mock_server: $(SRC_DIR)/mock_server.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRC_DIR)/mock_server.cpp -o $(BUILD_DIR)/mock_server

blocking_client: $(SRC_DIR)/blocking_client.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRC_DIR)/blocking_client.cpp -o $(BUILD_DIR)/blocking_client

feed_handler_spsc: $(SRC_DIR)/feed_handler_spsc.cpp $(INCLUDE_DIR)/binary_protocol.hpp $(INCLUDE_DIR)/spsc_queue.hpp
	$(CXX) $(CXXFLAGS) -O3 -pthread $(INCLUDES) $(SRC_DIR)/feed_handler_spsc.cpp -o $(BUILD_DIR)/feed_handler_spsc

feed_handler_spmc: $(SRC_DIR)/feed_handler_spmc.cpp $(INCLUDE_DIR)/binary_protocol.hpp
	$(CXX) $(CXXFLAGS) -O3 -pthread $(INCLUDES) $(SRC_DIR)/feed_handler_spmc.cpp -o $(BUILD_DIR)/feed_handler_spmc

socket_tuning_benchmark: $(SRC_DIR)/socket_tuning_benchmark.cpp $(INCLUDE_DIR)/binary_protocol.hpp $(INCLUDE_DIR)/socket_config.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRC_DIR)/socket_tuning_benchmark.cpp -o $(BUILD_DIR)/socket_tuning_benchmark

# Heartbeat binaries
feed_handler_heartbeat: $(SRC_DIR)/feed_handler_heartbeat.cpp $(INCLUDE_DIR)/binary_protocol.hpp $(INCLUDE_DIR)/connection_manager.hpp $(INCLUDE_DIR)/sequence_tracker.hpp $(INCLUDE_DIR)/ring_buffer.hpp
	$(CXX) $(CXXFLAGS) -O3 -pthread $(INCLUDES) $(SRC_DIR)/feed_handler_heartbeat.cpp -o $(BUILD_DIR)/feed_handler_heartbeat

heartbeat_mock_server: $(SRC_DIR)/heartbeat_mock_server.cpp $(INCLUDE_DIR)/binary_protocol.hpp
	$(CXX) $(CXXFLAGS) -O3 $(INCLUDES) $(SRC_DIR)/heartbeat_mock_server.cpp -o $(BUILD_DIR)/heartbeat_mock_server

# Extension: Snapshot Recovery
feed_handler_snapshot: $(SRC_DIR)/feed_handler_snapshot.cpp $(INCLUDE_DIR)/binary_protocol.hpp $(INCLUDE_DIR)/connection_manager.hpp $(INCLUDE_DIR)/order_book.hpp $(INCLUDE_DIR)/sequence_tracker.hpp $(INCLUDE_DIR)/ring_buffer.hpp
	$(CXX) $(CXXFLAGS) -O3 -pthread $(INCLUDES) $(SRC_DIR)/feed_handler_snapshot.cpp -o $(BUILD_DIR)/feed_handler_snapshot

snapshot_mock_server: $(SRC_DIR)/snapshot_mock_server.cpp $(INCLUDE_DIR)/binary_protocol.hpp
	$(CXX) $(CXXFLAGS) -O3 $(INCLUDES) $(SRC_DIR)/snapshot_mock_server.cpp -o $(BUILD_DIR)/snapshot_mock_server

# UDP Benchmark
udp_mock_server: $(SRC_DIR)/udp_mock_server.cpp $(INCLUDE_DIR)/udp_protocol.hpp
	$(CXX) $(CXXFLAGS) -O3 $(INCLUDES) $(SRC_DIR)/udp_mock_server.cpp -o $(BUILD_DIR)/udp_mock_server

udp_feed_handler: $(SRC_DIR)/udp_feed_handler.cpp $(INCLUDE_DIR)/udp_protocol.hpp
	$(CXX) $(CXXFLAGS) -O3 $(INCLUDES) $(SRC_DIR)/udp_feed_handler.cpp -o $(BUILD_DIR)/udp_feed_handler

tcp_vs_udp_benchmark: $(SRC_DIR)/tcp_vs_udp_benchmark.cpp $(INCLUDE_DIR)/binary_protocol.hpp $(INCLUDE_DIR)/udp_protocol.hpp
	$(CXX) $(CXXFLAGS) -O3 -pthread $(INCLUDES) $(SRC_DIR)/tcp_vs_udp_benchmark.cpp -o $(BUILD_DIR)/tcp_vs_udp_benchmark

#=============================================================================
# Profiling Infrastructure
#=============================================================================

# Baseline with profiling symbols (for perf/sample)
feed_handler_heartbeat_profile: $(SRC_DIR)/feed_handler_heartbeat.cpp $(INCLUDE_DIR)/*.hpp
	@echo "Building baseline with profiling symbols..."
	$(CXX) $(CXXFLAGS) $(PROFILE_FLAGS) -pthread $(INCLUDES) \
		$(SRC_DIR)/feed_handler_heartbeat.cpp \
		-o $(BUILD_DIR)/feed_handler_heartbeat_profile

# Optimized version with all improvements
feed_handler_heartbeat_optimized: $(SRC_DIR)/feed_handler_heartbeat_optimized.cpp $(INCLUDE_DIR)/sequence_tracker_optimized.hpp $(INCLUDE_DIR)/*.hpp
	@echo "Building optimized version improvements..."
	$(CXX) $(CXXFLAGS) $(OPTIMIZE_FLAGS) -pthread $(INCLUDES) \
		$(SRC_DIR)/feed_handler_heartbeat_optimized.cpp \
		-o $(BUILD_DIR)/feed_handler_heartbeat_optimized

# Build both versions for comparison
profiling: $(BUILD_DIR) heartbeat_mock_server feed_handler_heartbeat_profile feed_handler_heartbeat_optimized
	@echo ""
	@echo "Profiling builds complete!"
	@echo "   Baseline:  $(BUILD_DIR)/feed_handler_heartbeat_profile"
	@echo "   Optimized: $(BUILD_DIR)/feed_handler_heartbeat_optimized"
	@echo ""
	@echo "Next steps:"
	@echo "   1. Run profiling:  ./scripts/profile_feed_handler.sh"
	@echo "   2. Compare results: ./scripts/compare_profiling_results.sh"
	@echo "   3. Quick benchmark: ./scripts/benchmark_throughput.sh"
	@echo ""

# Run profiling analysis
profile: profiling
	@./scripts/profile_feed_handler.sh

# Compare profiling results
compare-profiling:
	@./scripts/compare_profiling_results.sh

# Quick throughput test
throughput-benchmark: profiling
	@./scripts/benchmark_throughput.sh

# Interactive perf report (Linux)
perf-baseline:
	@if [ -f profiling_results/perf_baseline.data ]; then \
		perf report -i profiling_results/perf_baseline.data; \
	else \
		echo "No baseline profiling data found. Run: make profile"; \
	fi

perf-optimized:
	@if [ -f profiling_results/perf_optimized.data ]; then \
		perf report -i profiling_results/perf_optimized.data; \
	else \
		echo "No optimized profiling data found. Run: make profile"; \
	fi

# Open flamegraphs
flamegraph-baseline:
	@if [ -f profiling_results/baseline_flame.svg ]; then \
		open profiling_results/baseline_flame.svg 2>/dev/null || xdg-open profiling_results/baseline_flame.svg 2>/dev/null || echo "Open: profiling_results/baseline_flame.svg"; \
	else \
		echo "No baseline flamegraph found. Run: make profile"; \
	fi

flamegraph-optimized:
	@if [ -f profiling_results/optimized_flame.svg ]; then \
		open profiling_results/optimized_flame.svg 2>/dev/null || xdg-open profiling_results/optimized_flame.svg 2>/dev/null || echo "Open: profiling_results/optimized_flame.svg"; \
	else \
		echo "No optimized flamegraph found. Run: make profile"; \
	fi

#=============================================================================
# Extension: Memory Pool & False Sharing
#=============================================================================

# Memory pool benchmark (pool vs malloc)
benchmark_pool_vs_malloc: $(SRC_DIR)/benchmark_pool_vs_malloc.cpp $(INCLUDE_DIR)/thread_local_pool.hpp
	@echo "Building memory pool benchmark..."
	$(CXX) $(CXXFLAGS) $(OPTIMIZE_FLAGS) -pthread $(INCLUDES) \
		$(SRC_DIR)/benchmark_pool_vs_malloc.cpp \
		-o $(BUILD_DIR)/benchmark_pool_vs_malloc

# False sharing demonstration
false_sharing_demo: $(SRC_DIR)/false_sharing_demo.cpp
	@echo "Building false sharing demonstration..."
	$(CXX) $(CXXFLAGS) $(OPTIMIZE_FLAGS) -pthread $(INCLUDES) \
		$(SRC_DIR)/false_sharing_demo.cpp \
		-o $(BUILD_DIR)/false_sharing_demo

# Build all memory pool extension components
memory-pool-extension: $(BUILD_DIR) benchmark_pool_vs_malloc false_sharing_demo
	@echo ""
	@echo "✅ Memory Pool Extension builds complete!"
	@echo ""
	@echo "Run benchmarks:"
	@echo "  ./$(BUILD_DIR)/benchmark_pool_vs_malloc 4 100000"
	@echo "  ./$(BUILD_DIR)/false_sharing_demo 10000000"
	@echo ""
	@echo "Profile with sample (macOS):"
	@echo "  sample ./$(BUILD_DIR)/benchmark_pool_vs_malloc 30 -file pool_profile.txt"
	@echo ""

# Run pool benchmark
benchmark-pool: benchmark_pool_vs_malloc
	@echo "Running memory pool benchmark..."
	@./$(BUILD_DIR)/benchmark_pool_vs_malloc 4 100000

# Run false sharing demo
false-sharing-demo: false_sharing_demo
	@echo "Running false sharing demonstration..."
	@./$(BUILD_DIR)/false_sharing_demo 10000000

# Profile pool with sample (macOS)
profile-pool-sample: benchmark_pool_vs_malloc
	@echo "Profiling with sample (30 seconds)..."
	@./$(BUILD_DIR)/benchmark_pool_vs_malloc 4 1000000 & \
	PID=$$!; \
	sleep 1; \
	sample $$PID 30 -file pool_profile.txt; \
	kill $$PID 2>/dev/null; \
	echo "Profile saved to pool_profile.txt"

#=============================================================================
# Extension: IPC & Cache Analysis
#=============================================================================

# Parsing hot path benchmark
benchmark_parsing_hotpath: $(BUILD_DIR) $(SRC_DIR)/benchmark_parsing_hotpath.cpp $(INCLUDE_DIR)/binary_protocol.hpp
	@echo "Building parsing hot path benchmark..."
	$(CXX) $(CXXFLAGS) $(OPTIMIZE_FLAGS) $(INCLUDES) \
		$(SRC_DIR)/benchmark_parsing_hotpath.cpp \
		-o $(BUILD_DIR)/benchmark_parsing_hotpath

# Build all IPC/cache analysis components
ipc-cache-extension: $(BUILD_DIR) benchmark_parsing_hotpath
	@echo ""
	@echo "✅ IPC/Cache Analysis Extension built!"
	@echo ""
	@echo "Run benchmark:"
	@echo "  ./$(BUILD_DIR)/benchmark_parsing_hotpath 10000000"
	@echo ""
	@echo "Measure performance counters (macOS):"
	@echo "  chmod +x scripts/measure_performance_counters_macos.sh"
	@echo "  ./scripts/measure_performance_counters_macos.sh"
	@echo ""
	@echo "Profile with Instruments System Trace (macOS):"
	@echo "  sudo instruments -t 'System Trace' \\"
	@echo "    -D perf_counters/system_trace.trace \\"
	@echo "    -l 30 \\"
	@echo "    ./$(BUILD_DIR)/benchmark_parsing_hotpath 10000000"
	@echo ""
	@echo "Then open: perf_counters/system_trace.trace"
	@echo ""
	@echo "Targets:"
	@echo "  IPC > 2.0"
	@echo "  Cache miss rate < 1%"
	@echo ""

# Quick benchmark run
benchmark-ipc: benchmark_parsing_hotpath
	@echo "Running parsing hot path benchmark..."
	@./$(BUILD_DIR)/benchmark_parsing_hotpath 10000000

# Measure all performance counters (comprehensive)
measure-perf-counters: benchmark_parsing_hotpath
	@echo "Running comprehensive performance counter measurement..."
	@mkdir -p perf_counters
	@chmod +x scripts/measure_performance_counters_macos.sh
	@./scripts/measure_performance_counters_macos.sh \
		$(BUILD_DIR)/benchmark_parsing_hotpath 30

# Profile with Instruments System Trace (macOS) - requires sudo
profile-ipc-instruments: benchmark_parsing_hotpath
	@echo "Profiling with Instruments System Trace..."
	@echo "This will take ~30 seconds and requires sudo..."
	@mkdir -p perf_counters
	@sudo instruments -t 'System Trace' \
		-D perf_counters/system_trace.trace \
		-l 30 \
		$(BUILD_DIR)/benchmark_parsing_hotpath 10000000 || true
	@echo ""
	@echo "✅ Profile complete!"
	@echo "   Open: perf_counters/system_trace.trace"
	@echo ""
	@echo "In Instruments, check CPU Counters for:"
	@echo "  - Instructions Retired"
	@echo "  - Cycles"
	@echo "  - IPC = Instructions / Cycles (target: > 2.0)"
	@echo "  - L1D Cache References/Misses"
	@echo "  - Cache Miss Rate (target: < 1%)"
	@echo ""

# Profile with sample (macOS, lighter alternative)
profile-ipc-sample: benchmark_parsing_hotpath
	@echo "Profiling with sample (30 seconds)..."
	@mkdir -p perf_counters
	@sample $(BUILD_DIR)/benchmark_parsing_hotpath 30 \
		-file perf_counters/sample_profile.txt 10000000
	@echo "Profile saved to perf_counters/sample_profile.txt"

# Build all extensions
all-extensions: memory-pool-extension ipc-cache-extension
	@echo ""
	@echo "✅ All extensions built successfully!"
	@echo ""

#=============================================================================
# Test targets
#=============================================================================

tests: $(BUILD_DIR) $(TEST_BINARIES)

test_spsc_queue: $(TESTS_DIR)/test_spsc_queue.cpp $(INCLUDE_DIR)/spsc_queue.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(TESTS_DIR)/test_spsc_queue.cpp -o $(BUILD_DIR)/test_spsc_queue

# Run tests
run-tests: tests
	./$(BUILD_DIR)/test_spsc_queue

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)
	rm -rf profiling_results/*
	rm -rf perf_counters/*

#=============================================================================
# Convenience targets to run benchmarks
#=============================================================================

benchmark: all
	./scripts/benchmark_zerocopy.sh

comparison: all
	./scripts/test_comparison.sh

feed-handler: $(BUILD_DIR) feed_handler_spsc binary_mock_server
	./scripts/test_feed_handler.sh

false-sharing: $(BUILD_DIR) feed_handler_spmc binary_mock_server
	./scripts/benchmark_false_sharing.sh

measure-false-sharing: $(BUILD_DIR) feed_handler_spmc binary_mock_server
	./scripts/measure_false_sharing.sh

# Socket tuning benchmark
socket-benchmark: $(BUILD_DIR) socket_tuning_benchmark binary_mock_server
	./scripts/run_socket_benchmark.sh

# Heartbeat and connection management
heartbeat-benchmark: $(BUILD_DIR) feed_handler_heartbeat heartbeat_mock_server
	./scripts/run_heartbeat_test.sh

# Snapshot recovery targets
snapshot-recovery: $(BUILD_DIR) feed_handler_snapshot snapshot_mock_server
	@echo "Snapshot recovery extension built!"

test-snapshot: snapshot-recovery
	@echo "Running snapshot recovery tests..."
	./scripts/test_snapshot_recovery.sh

# UDP benchmark targets
udp-benchmark: $(BUILD_DIR) udp_mock_server udp_feed_handler
	@echo "UDP benchmark binaries built!"

tcp-vs-udp: $(BUILD_DIR) tcp_vs_udp_benchmark udp_mock_server udp_feed_handler binary_mock_server
	@echo "TCP vs UDP comparison benchmark built!"
	@echo "Run: ./scripts/run_tcp_vs_udp_benchmark.sh"

#=============================================================================
# Help
#=============================================================================

help:
	@echo "TCP-Socket Project Makefile"
	@echo ""
	@echo "Build Targets:"
	@echo "  make all                  - Build all main binaries"
	@echo "  make tests                - Build test binaries"
	@echo "  make profiling            - Build baseline + optimized versions for profiling"
	@echo "  make all-extensions       - Build all extensions (memory pool + IPC/cache)"
	@echo "  make clean                - Remove build artifacts"
	@echo ""
	@echo "Profiling Targets:"
	@echo "  make profile              - Run full profiling analysis"
	@echo "  make throughput-benchmark - Quick throughput comparison"
	@echo "  make compare-profiling    - Compare profiling results"
	@echo "  make perf-baseline        - Interactive perf report (baseline, Linux)"
	@echo "  make perf-optimized       - Interactive perf report (optimized, Linux)"
	@echo "  make flamegraph-baseline  - View baseline flamegraph"
	@echo "  make flamegraph-optimized - View optimized flamegraph"
	@echo ""
	@echo "Extension: Memory Pool & False Sharing"
	@echo "  make memory-pool-extension - Build memory pool benchmark + false sharing demo"
	@echo "  make benchmark-pool       - Run pool vs malloc benchmark"
	@echo "  make false-sharing-demo   - Run false sharing demonstration"
	@echo "  make profile-pool-sample  - Profile pool with sample (macOS)"
	@echo ""
	@echo "Extension: IPC & Cache Analysis"
	@echo "  make ipc-cache-extension  - Build parsing hot path benchmark"
	@echo "  make benchmark-ipc        - Run IPC/cache benchmark"
	@echo "  make measure-perf-counters - Comprehensive performance measurement (macOS)"
	@echo "  make profile-ipc-instruments - Profile with Instruments System Trace (macOS)"
	@echo "  make profile-ipc-sample   - Profile with sample (macOS)"
	@echo ""
	@echo "Benchmark Targets:"
	@echo "  make benchmark            - Run zerocopy benchmark"
	@echo "  make socket-benchmark     - Run socket tuning benchmark"
	@echo "  make heartbeat-benchmark  - Run heartbeat test"
	@echo "  make tcp-vs-udp           - Build TCP vs UDP comparison"
	@echo "  make test-snapshot        - Run snapshot recovery tests"
	@echo ""
	@echo "Quick Start (Extensions):"
	@echo "  Memory Pool:  make memory-pool-extension && make benchmark-pool"
	@echo "  IPC/Cache:    make ipc-cache-extension && make benchmark-ipc"
	@echo ""

.PHONY: all clean tests run-tests benchmark comparison feed-handler false-sharing \
        measure-false-sharing socket-benchmark heartbeat-benchmark \
        snapshot-recovery test-snapshot udp-benchmark tcp-vs-udp \
        profiling profile compare-profiling throughput-benchmark \
        perf-baseline perf-optimized flamegraph-baseline flamegraph-optimized \
        memory-pool-extension benchmark-pool false-sharing-demo profile-pool-sample \
        ipc-cache-extension benchmark-ipc measure-perf-counters \
        profile-ipc-instruments profile-ipc-sample all-extensions \
        directories help
