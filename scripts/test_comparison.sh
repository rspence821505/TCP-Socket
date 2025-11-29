#!/bin/bash

# Script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"

echo "Zero-Copy Ring Buffer Benchmark"
echo "===================================="
echo ""

# Check if binaries exist
if [ ! -f "$BUILD_DIR/binary_client" ] || [ ! -f "$BUILD_DIR/binary_client_zerocopy" ]; then
    echo "Binaries not found. Run 'make' from project root first!"
    exit 1
fi

echo "Test 1: Original Version (with buffer.erase())"
echo "=================================================="
echo ""

# Start servers
"$BUILD_DIR/binary_mock_server" 9999 > /dev/null 2>&1 &
PID1=$!
"$BUILD_DIR/binary_mock_server" 10000 > /dev/null 2>&1 &
PID2=$!
"$BUILD_DIR/binary_mock_server" 10001 > /dev/null 2>&1 &
PID3=$!

sleep 2

# Run original client and capture last 20 lines
"$BUILD_DIR/binary_client" 2>&1 | tail -20

echo ""
echo "Restarting servers..."
kill $PID1 $PID2 $PID3 2>/dev/null
wait $PID1 $PID2 $PID3 2>/dev/null
sleep 2

echo ""
echo "Test 2: Zero-Copy Version (with ring buffer)"
echo "================================================"
echo ""

# Start servers again
"$BUILD_DIR/binary_mock_server" 9999 > /dev/null 2>&1 &
PID1=$!
"$BUILD_DIR/binary_mock_server" 10000 > /dev/null 2>&1 &
PID2=$!
"$BUILD_DIR/binary_mock_server" 10001 > /dev/null 2>&1 &
PID3=$!

sleep 2

# Run zero-copy client and capture last 25 lines
"$BUILD_DIR/binary_client_zerocopy" 2>&1 | tail -25

echo ""
echo "Cleanup..."
kill $PID1 $PID2 $PID3 2>/dev/null
wait $PID1 $PID2 $PID3 2>/dev/null

echo ""
echo "Done!"
