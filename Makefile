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
BINARIES = binary_client binary_client_zerocopy binary_mock_server mock_server blocking_client feed_handler_spsc feed_handler_spmc
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

feed-handler: feed_handler_spsc binary_mock_server
	./scripts/test_feed_handler.sh

false-sharing: feed_handler_spmc binary_mock_server
	./scripts/benchmark_false_sharing.sh

.PHONY: all clean tests run-tests benchmark comparison feed-handler false-sharing
