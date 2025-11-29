#!/bin/bash

echo "Zero-Copy Ring Buffer Benchmark"
echo "===================================="
echo ""

# Start servers
echo "Starting 3 mock exchanges..."
./binary_mock_server 9999 > /dev/null 2>&1 &
PID1=$!
./binary_mock_server 10000 > /dev/null 2>&1 &
PID2=$!
./binary_mock_server 10001 > /dev/null 2>&1 &
PID3=$!

sleep 1

echo ""
echo "Test 1: Original Version (with buffer.erase())"
echo "=================================================="
timeout 15 ./binary_client 2>&1 | grep -A 5 "=== Statistics ==="

# Restart servers
killall binary_mock_server 2>/dev/null
sleep 1

./binary_mock_server 9999 > /dev/null 2>&1 &
PID1=$!
./binary_mock_server 10000 > /dev/null 2>&1 &
PID2=$!
./binary_mock_server 10001 > /dev/null 2>&1 &
PID3=$!

sleep 1

echo ""
echo "Test 2: Zero-Copy Version (with ring buffer)"
echo "================================================"
timeout 15 ./binary_client_zerocopy 2>&1 | grep -A 10 "=== Statistics ==="

echo ""
echo "🧹 Cleanup..."
killall binary_mock_server 2>/dev/null

echo ""
echo "Benchmark complete!"
echo ""
echo "To measure memory allocations, run:"
echo "  valgrind --tool=massif ./binary_client"
echo "  valgrind --tool=massif ./binary_client_zerocopy"
echo ""
echo "Then compare with:"
echo "  ms_print massif.out.XXXXX"