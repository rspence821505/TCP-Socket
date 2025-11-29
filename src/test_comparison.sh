#!/bin/bash

echo "🔬 Zero-Copy Ring Buffer Benchmark"
echo "===================================="
echo ""

# Check if binaries exist
if [ ! -f "./binary_client" ] || [ ! -f "./binary_client_zerocopy" ]; then
    echo "❌ Binaries not found. Run 'make' first!"
    exit 1
fi

echo "📊 Test 1: Original Version (with buffer.erase())"
echo "=================================================="
echo ""

# Start servers
./binary_mock_server 9999 > /dev/null 2>&1 &
./binary_mock_server 10000 > /dev/null 2>&1 &
./binary_mock_server 10001 > /dev/null 2>&1 &

sleep 2

# Run original client and capture last 20 lines
./binary_client 2>&1 | tail -20

echo ""
echo "🔄 Restarting servers..."
killall binary_mock_server 2>/dev/null
wait
sleep 2

echo ""
echo "🚀 Test 2: Zero-Copy Version (with ring buffer)"
echo "================================================"
echo ""

# Start servers again
./binary_mock_server 9999 > /dev/null 2>&1 &
./binary_mock_server 10000 > /dev/null 2>&1 &
./binary_mock_server 10001 > /dev/null 2>&1 &

sleep 2

# Run zero-copy client and capture last 25 lines
./binary_client_zerocopy 2>&1 | tail -25

echo ""
echo "🧹 Cleanup..."
killall binary_mock_server 2>/dev/null
wait

echo ""
echo "✅ Done!"
