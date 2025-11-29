#!/bin/bash
# Measure cache coherency traffic with and without padding
# Demonstrates the impact of false sharing on performance

echo "==================================================================="
echo "False Sharing Detection with perf"
echo "==================================================================="
echo ""
echo "This script will:"
echo "  1. Compile the SPMC feed handler"
echo "  2. Run WITHOUT padding (false sharing expected)"
echo "  3. Run WITH padding (false sharing fixed)"
echo "  4. Compare cache miss rates"
echo ""

# Check if perf is available
if ! command -v perf &> /dev/null; then
    echo "❌ ERROR: 'perf' command not found!"
    echo ""
    echo "On Linux, install with:"
    echo "  sudo apt-get install linux-tools-common linux-tools-generic"
    echo ""
    echo "On macOS, perf is not available. Use Instruments instead:"
    echo "  instruments -t 'System Trace' ./feed_handler_spmc"
    echo ""
    exit 1
fi

# Create build directory
mkdir -p build

# Compile
echo "📦 Compiling SPMC feed handler..."
g++ -std=c++17 -O3 -pthread -Iinclude \
    src/feed_handler_spmc.cpp -o build/feed_handler_spmc

if [ $? -ne 0 ]; then
    echo "❌ Compilation failed!"
    exit 1
fi

echo "✅ Compilation successful"
echo ""

# Start mock server
echo "🚀 Starting mock exchange server on port 9999..."
./build/binary_mock_server 9999 > /tmp/server.log 2>&1 &
SERVER_PID=$!
sleep 2

echo ""
echo "==================================================================="
echo "TEST 1: WITHOUT PADDING (False Sharing Expected)"
echo "==================================================================="
echo ""
echo "Running with perf to measure cache coherency traffic..."
echo ""

# Run without padding
perf stat -e cache-references,cache-misses,cycles,instructions \
    ./build/feed_handler_spmc 9999 2048 unpadded 2>&1 | tee /tmp/unpadded_output.txt

echo ""
echo "Saving results to: /tmp/unpadded_perf.txt"
grep -A 10 "Performance counter" /tmp/unpadded_output.txt > /tmp/unpadded_perf.txt || true

# Restart server
echo ""
echo "🔄 Restarting mock server..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null
sleep 2

./build/binary_mock_server 9999 > /tmp/server.log 2>&1 &
SERVER_PID=$!
sleep 2

echo ""
echo "==================================================================="
echo "TEST 2: WITH PADDING (False Sharing Fixed)"
echo "==================================================================="
echo ""
echo "Running with perf to measure cache coherency traffic..."
echo ""

# Run with padding
perf stat -e cache-references,cache-misses,cycles,instructions \
    ./build/feed_handler_spmc 9999 2048 padded 2>&1 | tee /tmp/padded_output.txt

echo ""
echo "Saving results to: /tmp/padded_perf.txt"
grep -A 10 "Performance counter" /tmp/padded_output.txt > /tmp/padded_perf.txt || true

# Cleanup
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo ""
echo "==================================================================="
echo "COMPARISON"
echo "==================================================================="
echo ""

# Extract cache miss rate
echo "📊 Cache Performance:"
echo ""

echo "WITHOUT PADDING (False Sharing):"
grep -E "cache-misses|cache-references" /tmp/unpadded_perf.txt || \
    grep -E "cache-misses|cache-references" /tmp/unpadded_output.txt | head -2

echo ""
echo "WITH PADDING (Fixed):"
grep -E "cache-misses|cache-references" /tmp/padded_perf.txt || \
    grep -E "cache-misses|cache-references" /tmp/padded_output.txt | head -2

echo ""
echo "==================================================================="
echo "ANALYSIS"
echo "==================================================================="
echo ""
echo "Key Metrics to Compare:"
echo "  1. cache-misses: Lower is better (less cache coherency traffic)"
echo "  2. cache-miss rate: % of cache references that miss"
echo "  3. Total runtime: Padded version should be faster"
echo ""
echo "Expected Results:"
echo "  • WITHOUT padding: Higher cache-miss rate (5-15%)"
echo "  • WITH padding: Lower cache-miss rate (1-3%)"
echo "  • Speedup: 10-30% improvement with padding"
echo ""
echo "Why?"
echo "  Without padding, when Consumer 0 updates its counter, it invalidates"
echo "  Consumer 1's cache line (false sharing). With padding, each consumer"
echo "  owns its own cache line → no invalidation → better performance."
echo ""
echo "Full output saved to:"
echo "  /tmp/unpadded_output.txt"
echo "  /tmp/padded_output.txt"
echo ""
