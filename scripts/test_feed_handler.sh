#!/bin/bash

# Script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
SRC_DIR="$PROJECT_ROOT/src"
INCLUDE_DIR="$PROJECT_ROOT/include"

echo "=== Feed Handler with Lock-Free SPSC Queue ==="
echo ""
echo "This script will:"
echo "1. Compile the feed handler and mock server"
echo "2. Run a test with latency measurements"
echo ""

# Create build directory
mkdir -p "$BUILD_DIR"

# Compile binaries
echo "Compiling binaries..."
g++ -std=c++17 -O3 -pthread -I"$INCLUDE_DIR" "$SRC_DIR/feed_handler_spsc.cpp" -o "$BUILD_DIR/feed_handler_spsc"
if [ $? -ne 0 ]; then
    echo "Failed to compile feed_handler_spsc!"
    exit 1
fi

g++ -std=c++17 -O3 -I"$INCLUDE_DIR" "$SRC_DIR/binary_mock_server.cpp" -o "$BUILD_DIR/binary_mock_server"
if [ $? -ne 0 ]; then
    echo "Failed to compile binary_mock_server!"
    exit 1
fi

echo "Compilation successful"
echo ""

# Start server in background
echo "Starting mock exchange server on port 9999..."
"$BUILD_DIR/binary_mock_server" 9999 > /tmp/server.log 2>&1 &
SERVER_PID=$!

# Give server time to start
sleep 2

# Run feed handler
echo "Running feed handler..."
echo ""
"$BUILD_DIR/feed_handler_spsc" 9999

# Cleanup
echo ""
echo "Cleaning up..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo ""
echo "Test complete!"
echo ""
echo "To run with different queue size:"
echo "  $BUILD_DIR/feed_handler_spsc 9999 2048"
echo ""
echo "To see different latency profiles, try:"
echo "  - Small queue (128): More queue backpressure"
echo "  - Large queue (4096): Less queue backpressure"
