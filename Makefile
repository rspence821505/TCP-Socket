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

# Google Test configuration (Homebrew installation)
GTEST_DIR = /opt/homebrew/opt/googletest
GTEST_INCLUDES = -I$(GTEST_DIR)/include
GTEST_LIBS = -L$(GTEST_DIR)/lib -lgtest -lgtest_main -pthread

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
           text_mock_server feed_handler_text feed_handler \
           benchmark_pool_vs_malloc false_sharing_demo benchmark_parsing_hotpath

TEST_BINARIES = test_spsc_queue test_text_protocol test_binary_protocol test_order_book test_ring_buffer test_malformed_input test_stress test_sequence_tracker test_common
INTEGRATION_TEST_BINARIES = test_integration

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
# Text Protocol (newline-delimited ticks)
#=============================================================================

# Text mock server - sends "timestamp symbol price volume\n" format
text_mock_server: $(BUILD_DIR) $(SRC_DIR)/text_mock_server.cpp $(INCLUDE_DIR)/text_protocol.hpp
	@echo "Building text mock server..."
	$(CXX) $(CXXFLAGS) -O3 $(INCLUDES) $(SRC_DIR)/text_mock_server.cpp -o $(BUILD_DIR)/text_mock_server

# Text feed handler - receives and parses text ticks
feed_handler_text: $(BUILD_DIR) $(SRC_DIR)/feed_handler_text.cpp $(INCLUDE_DIR)/text_protocol.hpp $(INCLUDE_DIR)/spsc_queue.hpp
	@echo "Building text feed handler..."
	$(CXX) $(CXXFLAGS) -O3 -pthread $(INCLUDES) $(SRC_DIR)/feed_handler_text.cpp -o $(BUILD_DIR)/feed_handler_text

# Unified feed handler with CLI interface (uses consolidated net/feed.hpp)
feed_handler: $(BUILD_DIR) $(SRC_DIR)/feed_main.cpp $(INCLUDE_DIR)/cli_parser.hpp $(INCLUDE_DIR)/net/feed.hpp $(INCLUDE_DIR)/text_protocol.hpp $(INCLUDE_DIR)/binary_protocol.hpp $(INCLUDE_DIR)/spsc_queue.hpp $(INCLUDE_DIR)/order_book.hpp
	@echo "Building unified feed handler..."
	$(CXX) $(CXXFLAGS) -O3 -pthread $(INCLUDES) $(SRC_DIR)/feed_main.cpp -o $(BUILD_DIR)/feed_handler

# Run text protocol test
test-text-protocol: text_mock_server feed_handler_text
	@echo "==================================================================="
	@echo "Text Protocol Test"
	@echo "==================================================================="
	@echo ""
	@echo "Starting text mock server on port 9999..."
	@./$(BUILD_DIR)/text_mock_server 9999 10000 5 & \
	SERVER_PID=$$!; \
	sleep 1; \
	echo "Starting text feed handler..."; \
	./$(BUILD_DIR)/feed_handler_text 9999 127.0.0.1; \
	kill $$SERVER_PID 2>/dev/null; \
	echo ""; \
	echo "Test complete!"

#=============================================================================
# Profiling Infrastructure
#=============================================================================

# Baseline with profiling symbols (for perf/sample)
feed_handler_heartbeat_profile: $(SRC_DIR)/feed_handler_heartbeat.cpp $(INCLUDE_DIR)/*.hpp
	@echo "Building baseline with profiling symbols..."
	$(CXX) $(CXXFLAGS) $(PROFILE_FLAGS) -pthread $(INCLUDES) \
		$(SRC_DIR)/feed_handler_heartbeat.cpp \
		-o $(BUILD_DIR)/feed_handler_heartbeat_profile

# Build profiling version for analysis
profiling: $(BUILD_DIR) heartbeat_mock_server feed_handler_heartbeat_profile feed_handler_heartbeat
	@echo ""
	@echo "Profiling builds complete!"
	@echo "   Profile:   $(BUILD_DIR)/feed_handler_heartbeat_profile"
	@echo "   Optimized: $(BUILD_DIR)/feed_handler_heartbeat"
	@echo ""
	@echo "Next steps:"
	@echo "   1. Run profiling:  ./profiling/profile_feed_handler.sh"
	@echo "   2. Compare results: ./profiling/compare_profiling_results.sh"
	@echo "   3. Quick benchmark: ./benchmarks/benchmark_throughput.sh"
	@echo ""

# Run profiling analysis
profile: profiling
	@./profiling/profile_feed_handler.sh

# Compare profiling results
compare-profiling:
	@./profiling/compare_profiling_results.sh

# Quick throughput test
throughput-benchmark: profiling
	@./benchmarks/benchmark_throughput.sh

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
	@echo "  chmod +x benchmarks/benchmark_performance_counters_macos.sh"
	@echo "  ./benchmarks/benchmark_performance_counters_macos.sh"
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
	@chmod +x benchmarks/benchmark_performance_counters_macos.sh
	@./benchmarks/benchmark_performance_counters_macos.sh \
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
# Test targets (Google Test)
#=============================================================================

tests: $(BUILD_DIR) $(addprefix $(BUILD_DIR)/,$(TEST_BINARIES))

# SPSC Queue tests
$(BUILD_DIR)/test_spsc_queue: $(TESTS_DIR)/test_spsc_queue.cpp $(INCLUDE_DIR)/spsc_queue.hpp
	@echo "Building test_spsc_queue..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(GTEST_INCLUDES) \
		$(TESTS_DIR)/test_spsc_queue.cpp \
		$(GTEST_LIBS) -o $(BUILD_DIR)/test_spsc_queue

# Text Protocol tests
$(BUILD_DIR)/test_text_protocol: $(TESTS_DIR)/test_text_protocol.cpp $(INCLUDE_DIR)/text_protocol.hpp
	@echo "Building test_text_protocol..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(GTEST_INCLUDES) \
		$(TESTS_DIR)/test_text_protocol.cpp \
		$(GTEST_LIBS) -o $(BUILD_DIR)/test_text_protocol

# Binary Protocol tests
$(BUILD_DIR)/test_binary_protocol: $(TESTS_DIR)/test_binary_protocol.cpp $(INCLUDE_DIR)/binary_protocol.hpp
	@echo "Building test_binary_protocol..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(GTEST_INCLUDES) \
		$(TESTS_DIR)/test_binary_protocol.cpp \
		$(GTEST_LIBS) -o $(BUILD_DIR)/test_binary_protocol

# Order Book tests
$(BUILD_DIR)/test_order_book: $(TESTS_DIR)/test_order_book.cpp $(INCLUDE_DIR)/order_book.hpp $(INCLUDE_DIR)/binary_protocol.hpp
	@echo "Building test_order_book..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(GTEST_INCLUDES) \
		$(TESTS_DIR)/test_order_book.cpp \
		$(GTEST_LIBS) -o $(BUILD_DIR)/test_order_book

# Ring Buffer tests
$(BUILD_DIR)/test_ring_buffer: $(TESTS_DIR)/test_ring_buffer.cpp $(INCLUDE_DIR)/ring_buffer.hpp
	@echo "Building test_ring_buffer..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(GTEST_INCLUDES) \
		$(TESTS_DIR)/test_ring_buffer.cpp \
		$(GTEST_LIBS) -o $(BUILD_DIR)/test_ring_buffer

# Malformed Input tests
$(BUILD_DIR)/test_malformed_input: $(TESTS_DIR)/test_malformed_input.cpp $(INCLUDE_DIR)/text_protocol.hpp $(INCLUDE_DIR)/binary_protocol.hpp $(INCLUDE_DIR)/ring_buffer.hpp
	@echo "Building test_malformed_input..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(GTEST_INCLUDES) \
		$(TESTS_DIR)/test_malformed_input.cpp \
		$(GTEST_LIBS) -o $(BUILD_DIR)/test_malformed_input

# Stress tests (high load, backpressure, failure modes)
$(BUILD_DIR)/test_stress: $(TESTS_DIR)/test_stress.cpp $(INCLUDE_DIR)/order_book.hpp $(INCLUDE_DIR)/spsc_queue.hpp $(INCLUDE_DIR)/connection_manager.hpp $(INCLUDE_DIR)/binary_protocol.hpp
	@echo "Building test_stress..."
	$(CXX) $(CXXFLAGS) -O3 -pthread $(INCLUDES) $(GTEST_INCLUDES) \
		$(TESTS_DIR)/test_stress.cpp \
		$(GTEST_LIBS) -o $(BUILD_DIR)/test_stress

# Sequence Tracker tests
$(BUILD_DIR)/test_sequence_tracker: $(TESTS_DIR)/test_sequence_tracker.cpp $(INCLUDE_DIR)/sequence_tracker.hpp
	@echo "Building test_sequence_tracker..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(GTEST_INCLUDES) \
		$(TESTS_DIR)/test_sequence_tracker.cpp \
		$(GTEST_LIBS) -o $(BUILD_DIR)/test_sequence_tracker

# Common utilities tests
$(BUILD_DIR)/test_common: $(TESTS_DIR)/test_common.cpp $(INCLUDE_DIR)/common.hpp
	@echo "Building test_common..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(GTEST_INCLUDES) \
		$(TESTS_DIR)/test_common.cpp \
		$(GTEST_LIBS) -o $(BUILD_DIR)/test_common

# Integration tests (server/client communication tests)
$(BUILD_DIR)/test_integration: $(TESTS_DIR)/test_integration.cpp
	@echo "Building test_integration..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(GTEST_INCLUDES) \
		$(TESTS_DIR)/test_integration.cpp \
		$(GTEST_LIBS) -o $(BUILD_DIR)/test_integration

# Build integration tests (requires binaries to be built first)
integration-tests: $(BUILD_DIR) all $(addprefix $(BUILD_DIR)/,$(INTEGRATION_TEST_BINARIES))

# Run integration tests
run-integration-tests: integration-tests
	@echo "==================================================================="
	@echo "Running Integration Tests"
	@echo "==================================================================="
	@echo "Note: These tests spawn server/client processes"
	@echo ""
	@for test in $(INTEGRATION_TEST_BINARIES); do \
		echo ""; \
		echo "--- Running $$test ---"; \
		./$(BUILD_DIR)/$$test --gtest_color=yes || exit 1; \
	done
	@echo ""
	@echo "==================================================================="
	@echo "Integration tests passed!"
	@echo "==================================================================="

# Run all tests (unit + integration)
run-all-tests: run-tests run-integration-tests
	@echo ""
	@echo "==================================================================="
	@echo "All unit and integration tests passed!"
	@echo "==================================================================="

# Run all tests
run-tests: tests
	@echo "==================================================================="
	@echo "Running all Google Tests"
	@echo "==================================================================="
	@for test in $(TEST_BINARIES); do \
		echo ""; \
		echo "--- Running $$test ---"; \
		./$(BUILD_DIR)/$$test --gtest_color=yes || exit 1; \
	done
	@echo ""
	@echo "==================================================================="
	@echo "All tests passed!"
	@echo "==================================================================="

# Run tests with verbose output
run-tests-verbose: tests
	@for test in $(TEST_BINARIES); do \
		echo ""; \
		echo "--- Running $$test ---"; \
		./$(BUILD_DIR)/$$test --gtest_color=yes -v || exit 1; \
	done

# Run a single test binary (usage: make run-test TEST=test_spsc_queue)
run-test: tests
	@if [ -z "$(TEST)" ]; then \
		echo "Usage: make run-test TEST=<test_name>"; \
		echo "Available tests: $(TEST_BINARIES)"; \
	else \
		./$(BUILD_DIR)/$(TEST) --gtest_color=yes; \
	fi

# Run tests with filter (usage: make run-test-filter FILTER="*Throughput*")
run-test-filter: tests
	@if [ -z "$(FILTER)" ]; then \
		echo "Usage: make run-test-filter FILTER=\"*Pattern*\""; \
	else \
		for test in $(TEST_BINARIES); do \
			echo "--- Running $$test with filter $(FILTER) ---"; \
			./$(BUILD_DIR)/$$test --gtest_color=yes --gtest_filter="$(FILTER)" || true; \
		done; \
	fi

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)
	rm -rf profiling_results/*
	rm -rf perf_counters/*

#=============================================================================
# Convenience targets to run benchmarks
#=============================================================================

benchmark: all
	./benchmarks/benchmark_zerocopy.sh

comparison: all
	./scripts/test_comparison.sh

feed-handler: $(BUILD_DIR) feed_handler_spsc binary_mock_server
	./scripts/test_feed_handler.sh

false-sharing: $(BUILD_DIR) feed_handler_spmc binary_mock_server
	./benchmarks/benchmark_false_sharing.sh

measure-false-sharing: $(BUILD_DIR) feed_handler_spmc binary_mock_server
	./benchmarks/benchmark_false_sharing_measure.sh

# Socket tuning benchmark
socket-benchmark: $(BUILD_DIR) socket_tuning_benchmark binary_mock_server
	./scripts/run_socket_benchmark.sh

# Heartbeat and connection management
heartbeat-benchmark: $(BUILD_DIR) feed_handler_heartbeat heartbeat_mock_server
	./scripts/run_heartbeat_test.sh

# Performance regression tests (validates performance targets)
run-perf-test: all
	@echo "Running performance regression tests..."
	@./benchmarks/run_perf_regression.sh

run-perf-test-quick: all
	@echo "Running quick performance regression tests..."
	@./benchmarks/run_perf_regression.sh --quick

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
	@echo "  make tests                - Build test binaries (Google Test)"
	@echo "  make profiling            - Build baseline + optimized versions for profiling"
	@echo "  make all-extensions       - Build all extensions (memory pool + IPC/cache)"
	@echo "  make clean                - Remove build artifacts"
	@echo ""
	@echo "Test Targets (Google Test):"
	@echo "  make run-tests            - Run all tests"
	@echo "  make run-tests-verbose    - Run all tests with verbose output"
	@echo "  make run-test TEST=name   - Run a single test (e.g., TEST=test_spsc_queue)"
	@echo "  make run-test-filter FILTER=\"*Pattern*\" - Run tests matching filter"
	@echo ""
	@echo "Available unit tests:"
	@echo "  test_spsc_queue           - Lock-free SPSC queue tests"
	@echo "  test_text_protocol        - Text protocol parser tests"
	@echo "  test_binary_protocol      - Binary protocol serialization tests"
	@echo "  test_order_book           - Order book tests"
	@echo "  test_ring_buffer          - Ring buffer tests"
	@echo ""
	@echo "Integration Tests (Google Test):"
	@echo "  make integration-tests    - Build integration tests"
	@echo "  make run-integration-tests - Run integration tests (spawns server/client)"
	@echo "  make run-all-tests        - Run unit + integration tests"
	@echo ""
	@echo "Available integration tests:"
	@echo "  test_integration          - Server/client communication tests"
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
	@echo "  make run-perf-test        - Run performance regression tests (full)"
	@echo "  make run-perf-test-quick  - Run quick performance regression tests"
	@echo ""
	@echo "Quick Start (Extensions):"
	@echo "  Memory Pool:  make memory-pool-extension && make benchmark-pool"
	@echo "  IPC/Cache:    make ipc-cache-extension && make benchmark-ipc"
	@echo ""

.PHONY: all clean tests run-tests run-tests-verbose run-test run-test-filter \
        integration-tests run-integration-tests run-all-tests \
        benchmark comparison feed-handler false-sharing \
        measure-false-sharing socket-benchmark heartbeat-benchmark \
        snapshot-recovery test-snapshot udp-benchmark tcp-vs-udp \
        profiling profile compare-profiling throughput-benchmark \
        perf-baseline perf-optimized flamegraph-baseline flamegraph-optimized \
        memory-pool-extension benchmark-pool false-sharing-demo profile-pool-sample \
        ipc-cache-extension benchmark-ipc measure-perf-counters \
        profile-ipc-instruments profile-ipc-sample all-extensions \
        directories help test-text-protocol run-perf-test run-perf-test-quick
