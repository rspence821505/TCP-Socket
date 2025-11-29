#!/bin/bash
# Comprehensive socket tuning benchmark for Exercise 6
# Tests different combinations of TCP_NODELAY, buffer sizes, and TCP_QUICKACK

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"

HOST="127.0.0.1"
PORT=9999
MESSAGES=10000

echo "=================================================================="
echo "Socket Tuning Benchmark (Exercise 6)"
echo "=================================================================="
echo ""
echo "This benchmark will test various socket configurations:"
echo "  1. Baseline (default settings)"
echo "  2. TCP_NODELAY only"
echo "  3. TCP_NODELAY + SO_RCVBUF variations (64KB, 256KB, 1MB)"
echo "  4. TCP_NODELAY + TCP_QUICKACK (Linux only)"
echo ""
echo "Each test receives $MESSAGES messages and measures latency."
echo ""

# Check if binaries exist
if [ ! -f "$BUILD_DIR/socket_tuning_benchmark" ] || [ ! -f "$BUILD_DIR/binary_mock_server" ]; then
    echo "Error: Binaries not found. Run 'make' first!"
    echo ""
    echo "Run this from project root:"
    echo "  make socket_tuning_benchmark"
    exit 1
fi

# CSV file for results
RESULTS_FILE="/tmp/socket_benchmark_results.csv"
echo "config,mean_us,p50_us,p95_us,p99_us,max_us" > "$RESULTS_FILE"

# Helper function to run a test
run_test() {
    local name="$1"
    local nodelay="$2"
    local rcvbuf="$3"
    local sndbuf="$4"
    local quickack="${5:-0}"
    
    echo "Testing: $name"
    echo "  Parameters: nodelay=$nodelay rcvbuf=$rcvbuf sndbuf=$sndbuf quickack=$quickack"
    
    # Start server
    "$BUILD_DIR/binary_mock_server" "$PORT" > /dev/null 2>&1 &
    SERVER_PID=$!
    sleep 1
    
    # Run benchmark
    "$BUILD_DIR/socket_tuning_benchmark" \
        --host "$HOST" \
        --port "$PORT" \
        --messages "$MESSAGES" \
        --nodelay "$nodelay" \
        --rcvbuf "$rcvbuf" \
        --sndbuf "$sndbuf" \
        --quickack "$quickack" \
        --csv >> "$RESULTS_FILE"
    
    # Stop server
    kill "$SERVER_PID" 2>/dev/null
    wait "$SERVER_PID" 2>/dev/null
    
    sleep 1
    echo ""
}

echo "Starting benchmark runs..."
echo ""

# Test 1: Baseline (all defaults)
run_test "Baseline (defaults)" 0 0 0 0

# Test 2: TCP_NODELAY only
run_test "TCP_NODELAY only" 1 0 0 0

# Test 3: TCP_NODELAY + Buffer sizes
run_test "TCP_NODELAY + 64KB buffers" 1 65536 65536 0
run_test "TCP_NODELAY + 256KB buffers" 1 262144 262144 0
run_test "TCP_NODELAY + 1MB buffers" 1 1048576 1048576 0

# Test 4: TCP_NODELAY + TCP_QUICKACK (Linux only)
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    run_test "TCP_NODELAY + TCP_QUICKACK" 1 0 0 1
    run_test "TCP_NODELAY + 64KB + QUICKACK" 1 65536 65536 1
fi

echo "=================================================================="
echo "RESULTS SUMMARY"
echo "=================================================================="
echo ""

# Pretty-print results table
column -t -s ',' "$RESULTS_FILE"

echo ""
echo "=================================================================="
echo "ANALYSIS"
echo "=================================================================="
echo ""

# Extract baseline and best p99
BASELINE_P99=$(awk -F',' 'NR==2 {print $5}' "$RESULTS_FILE")
BEST_P99=$(awk -F',' 'NR>1 {print $5}' "$RESULTS_FILE" | sort -n | head -1)

if [ -n "$BASELINE_P99" ] && [ -n "$BEST_P99" ]; then
    IMPROVEMENT=$(echo "scale=1; 100 * ($BASELINE_P99 - $BEST_P99) / $BASELINE_P99" | bc)
    
    echo "Baseline p99 latency: ${BASELINE_P99} µs"
    echo "Best p99 latency:     ${BEST_P99} µs"
    echo "Improvement:          ${IMPROVEMENT}%"
    echo ""
fi

echo "Key Observations:"
echo ""
echo "1. TCP_NODELAY effect:"
echo "   - Disables Nagle's algorithm (no 200ms delay for small packets)"
echo "   - Should show significant improvement for small messages"
echo ""
echo "2. Buffer size effect:"
echo "   - Larger buffers can reduce system calls but may increase latency"
echo "   - Smaller buffers (64KB) often best for low-latency workloads"
echo ""
echo "3. TCP_QUICKACK effect (Linux only):"
echo "   - Sends ACKs immediately instead of delaying up to 40ms"
echo "   - Most effective when combined with TCP_NODELAY"
echo ""
echo "Results saved to: $RESULTS_FILE"
echo ""

# Create a simple bar chart comparison
echo "=================================================================="
echo "LATENCY COMPARISON (p99)"
echo "=================================================================="
echo ""

# Print a simple text bar chart
MAX_P99=$(awk -F',' 'NR>1 {print $5}' "$RESULTS_FILE" | sort -n | tail -1)

awk -F',' -v max="$MAX_P99" '
NR>1 {
    config = $1
    p99 = $5
    # Scale to 50 chars max
    bars = int((p99 / max) * 50)
    printf "%-35s ", config
    for (i = 0; i < bars; i++) printf "█"
    printf " %.1f µs\n", p99
}' "$RESULTS_FILE"

echo ""
echo "Benchmark complete!"
