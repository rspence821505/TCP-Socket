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

# Start servers
echo "Starting 3 mock exchanges..."
"$BUILD_DIR/binary_mock_server" 9999 > /dev/null 2>&1 &
PID1=$!
"$BUILD_DIR/binary_mock_server" 10000 > /dev/null 2>&1 &
PID2=$!
"$BUILD_DIR/binary_mock_server" 10001 > /dev/null 2>&1 &
PID3=$!

sleep 1

echo ""
echo "Test 1: Original Version (with buffer.erase())"
echo "=================================================="
timeout 15 "$BUILD_DIR/binary_client" 2>&1 | grep -A 5 "=== Statistics ==="

# Restart servers
kill $PID1 $PID2 $PID3 2>/dev/null
wait $PID1 $PID2 $PID3 2>/dev/null
sleep 1

"$BUILD_DIR/binary_mock_server" 9999 > /dev/null 2>&1 &
PID1=$!
"$BUILD_DIR/binary_mock_server" 10000 > /dev/null 2>&1 &
PID2=$!
"$BUILD_DIR/binary_mock_server" 10001 > /dev/null 2>&1 &
PID3=$!

sleep 1

echo ""
echo "Test 2: Zero-Copy Version (with ring buffer)"
echo "================================================"
timeout 15 "$BUILD_DIR/binary_client_zerocopy" 2>&1 | grep -A 10 "=== Statistics ==="

echo ""
echo "Cleanup..."
kill $PID1 $PID2 $PID3 2>/dev/null
wait $PID1 $PID2 $PID3 2>/dev/null

echo ""
echo "Benchmark complete!"
echo ""
echo "To measure memory allocations, run:"
echo "  valgrind --tool=massif $BUILD_DIR/binary_client"
echo "  valgrind --tool=massif $BUILD_DIR/binary_client_zerocopy"
echo ""
echo "Then compare with:"
echo "  ms_print massif.out.XXXXX"
