# TCP-Socket Project Makefile
# Builds all binaries from src/ using headers from include/

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

# Directories
SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build
TESTS_DIR = tests

# Include path
INCLUDES = -I$(INCLUDE_DIR)

# Source files
SRCS = $(wildcard $(SRC_DIR)/*.cpp)
TEST_SRCS = $(wildcard $(TESTS_DIR)/*.cpp)

# Main targets
BINARIES = binary_client binary_client_zerocopy binary_mock_server mock_server \
           blocking_client feed_handler_spsc feed_handler_spmc \
           socket_tuning_benchmark feed_handler_heartbeat heartbeat_mock_server \
           feed_handler_snapshot snapshot_mock_server \
           udp_mock_server udp_feed_handler tcp_vs_udp_benchmark

TEST_BINARIES = test_spsc_queue

# Default target
all: $(BUILD_DIR) $(BINARIES)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

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

# Exercise 7: Heartbeat binaries
feed_handler_heartbeat: $(SRC_DIR)/feed_handler_heartbeat.cpp $(INCLUDE_DIR)/binary_protocol.hpp $(INCLUDE_DIR)/connection_manager.hpp $(INCLUDE_DIR)/sequence_tracker.hpp $(INCLUDE_DIR)/ring_buffer.hpp
	$(CXX) $(CXXFLAGS) -O3 -pthread $(INCLUDES) $(SRC_DIR)/feed_handler_heartbeat.cpp -o $(BUILD_DIR)/feed_handler_heartbeat

heartbeat_mock_server: $(SRC_DIR)/heartbeat_mock_server.cpp $(INCLUDE_DIR)/binary_protocol.hpp
	$(CXX) $(CXXFLAGS) -O3 $(INCLUDES) $(SRC_DIR)/heartbeat_mock_server.cpp -o $(BUILD_DIR)/heartbeat_mock_server

# Exercise 7 Extension: Snapshot Recovery
feed_handler_snapshot: $(SRC_DIR)/feed_handler_snapshot.cpp $(INCLUDE_DIR)/binary_protocol.hpp $(INCLUDE_DIR)/connection_manager.hpp $(INCLUDE_DIR)/order_book.hpp $(INCLUDE_DIR)/sequence_tracker.hpp $(INCLUDE_DIR)/ring_buffer.hpp
	$(CXX) $(CXXFLAGS) -O3 -pthread $(INCLUDES) $(SRC_DIR)/feed_handler_snapshot.cpp -o $(BUILD_DIR)/feed_handler_snapshot

snapshot_mock_server: $(SRC_DIR)/snapshot_mock_server.cpp $(INCLUDE_DIR)/binary_protocol.hpp
	$(CXX) $(CXXFLAGS) -O3 $(INCLUDES) $(SRC_DIR)/snapshot_mock_server.cpp -o $(BUILD_DIR)/snapshot_mock_server

# Exercise 8: UDP Benchmark
udp_mock_server: $(SRC_DIR)/udp_mock_server.cpp $(INCLUDE_DIR)/udp_protocol.hpp
	$(CXX) $(CXXFLAGS) -O3 $(INCLUDES) $(SRC_DIR)/udp_mock_server.cpp -o $(BUILD_DIR)/udp_mock_server

udp_feed_handler: $(SRC_DIR)/udp_feed_handler.cpp $(INCLUDE_DIR)/udp_protocol.hpp
	$(CXX) $(CXXFLAGS) -O3 $(INCLUDES) $(SRC_DIR)/udp_feed_handler.cpp -o $(BUILD_DIR)/udp_feed_handler

tcp_vs_udp_benchmark: $(SRC_DIR)/tcp_vs_udp_benchmark.cpp $(INCLUDE_DIR)/binary_protocol.hpp $(INCLUDE_DIR)/udp_protocol.hpp
	$(CXX) $(CXXFLAGS) -O3 -pthread $(INCLUDES) $(SRC_DIR)/tcp_vs_udp_benchmark.cpp -o $(BUILD_DIR)/tcp_vs_udp_benchmark

# Test targets
tests: $(BUILD_DIR) $(TEST_BINARIES)

test_spsc_queue: $(TESTS_DIR)/test_spsc_queue.cpp $(INCLUDE_DIR)/spsc_queue.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(TESTS_DIR)/test_spsc_queue.cpp -o $(BUILD_DIR)/test_spsc_queue

# Run tests
run-tests: tests
	./$(BUILD_DIR)/test_spsc_queue

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)

# Convenience targets to run benchmarks
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
	@echo "✅ Snapshot recovery extension built!"

test-snapshot: snapshot-recovery
	@echo "Running snapshot recovery tests..."
	./scripts/test_snapshot_recovery.sh

# UDP benchmark targets
udp-benchmark: $(BUILD_DIR) udp_mock_server udp_feed_handler
	@echo "✅ UDP benchmark binaries built!"

tcp-vs-udp: $(BUILD_DIR) tcp_vs_udp_benchmark udp_mock_server udp_feed_handler binary_mock_server
	@echo "✅ TCP vs UDP comparison benchmark built!"
	@echo "Run: ./scripts/run_tcp_vs_udp_benchmark.sh"

.PHONY: all clean tests run-tests benchmark comparison feed-handler false-sharing \
        measure-false-sharing socket-benchmark heartbeat-benchmark \
        snapshot-recovery test-snapshot udp-benchmark tcp-vs-udp