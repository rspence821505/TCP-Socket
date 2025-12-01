#!/bin/bash
#
# Performance Regression Test Suite
# Validates that feed handler meets performance targets:
#   - 200-500k msgs/sec throughput
#   - <2ms end-to-end latency (p99)
#
# Usage: ./benchmarks/run_perf_regression.sh [--quick]
#

set -e

# Kill any leftover processes from previous runs
pkill -9 -f text_mock_server 2>/dev/null || true
pkill -9 -f "feed_handler.*--port" 2>/dev/null || true
sleep 1

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
BUILD_DIR="./build"
FEED_HANDLER="${BUILD_DIR}/feed_handler"
MOCK_SERVER="${BUILD_DIR}/text_mock_server"

# Performance thresholds (in microseconds)
P99_THRESHOLD_US=2000  # 2ms target

# Test configurations: rate duration port
QUICK_TESTS=(
    "100000 3 9901"
    "300000 3 9902"
)

FULL_TESTS=(
    "100000 5 9901"
    "300000 5 9902"
    "500000 5 9903"
)

# Parse arguments
QUICK_MODE=false
if [[ "$1" == "--quick" ]]; then
    QUICK_MODE=true
    TESTS=("${QUICK_TESTS[@]}")
    echo -e "${YELLOW}Running quick performance regression tests...${NC}"
else
    TESTS=("${FULL_TESTS[@]}")
    echo -e "${YELLOW}Running full performance regression tests...${NC}"
fi

# Check binaries exist
if [[ ! -x "$FEED_HANDLER" ]] || [[ ! -x "$MOCK_SERVER" ]]; then
    echo -e "${RED}Error: Binaries not found. Run 'make all' first.${NC}"
    exit 1
fi

# Results tracking
PASSED=0
FAILED=0
RESULTS=()

# Cleanup function
cleanup() {
    # Kill any remaining mock servers
    pkill -f text_mock_server 2>/dev/null || true
}
trap cleanup EXIT

# Run a single performance test
run_test() {
    local rate=$1
    local duration=$2
    local port=$3

    echo ""
    echo "========================================"
    echo "Testing: ${rate} msgs/sec for ${duration}s (port ${port})"
    echo "========================================"

    # Create temp file for output
    local output_file="/tmp/perf_test_$$_${port}.txt"

    echo "[DEBUG] output_file=$output_file"
    echo "[DEBUG] Starting mock server: $MOCK_SERVER $port $rate $duration"

    # Start mock server in background (suppress output)
    $MOCK_SERVER $port $rate $duration >/dev/null 2>&1 &
    local server_pid=$!
    echo "[DEBUG] server_pid=$server_pid"

    # Give server time to start listening
    sleep 2

    echo "[DEBUG] Starting feed handler: $FEED_HANDLER --port $port --verbose"
    # Run feed handler in background
    $FEED_HANDLER --port $port --verbose >$output_file 2>&1 &
    local handler_pid=$!
    echo "[DEBUG] handler_pid=$handler_pid"

    # Wait for test duration plus buffer
    local wait_time=$((duration + 5))
    sleep "$wait_time"

    # Cleanup both processes
    kill "$handler_pid" 2>/dev/null || true
    kill "$server_pid" 2>/dev/null || true
    wait "$handler_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true

    # Read output from file
    local output=""
    if [[ -f "$output_file" ]]; then
        output=$(cat "$output_file")
        rm -f "$output_file"
    fi

    # Parse results
    local throughput
    local p99_latency
    local parse_errors

    throughput=$(echo "$output" | grep "Throughput:" | awk '{print $2}')
    p99_latency=$(echo "$output" | grep "p99:" | awk '{print $2}')
    parse_errors=$(echo "$output" | grep "Parse errors:" | awk '{print $3}')

    # Remove 'us' suffix from p99 if present
    p99_latency=${p99_latency%% us}

    # Validate results
    local test_passed=true
    local status_msg=""

    # Check throughput (should be at least 90% of target rate)
    local min_throughput=$((rate * 9 / 10))
    if [[ -z "$throughput" ]] || [[ "$throughput" -lt "$min_throughput" ]]; then
        test_passed=false
        status_msg+="Throughput ${throughput:-N/A} < ${min_throughput} (target: ${rate}). "
    fi

    # Check p99 latency
    # Convert to integer for comparison (remove decimal)
    local p99_int=${p99_latency%%.*}
    if [[ -z "$p99_int" ]] || [[ "$p99_int" -gt "$P99_THRESHOLD_US" ]]; then
        test_passed=false
        status_msg+="p99 ${p99_latency:-N/A}us > ${P99_THRESHOLD_US}us. "
    fi

    # Check parse errors
    if [[ -n "$parse_errors" ]] && [[ "$parse_errors" -gt 0 ]]; then
        test_passed=false
        status_msg+="Parse errors: ${parse_errors}. "
    fi

    # Report result
    if $test_passed; then
        echo -e "${GREEN}PASS${NC}: ${rate} msgs/sec - Throughput: ${throughput}, p99: ${p99_latency}us, Errors: ${parse_errors:-0}"
        ((PASSED++))
        RESULTS+=("PASS: ${rate} msgs/sec")
    else
        echo -e "${RED}FAIL${NC}: ${rate} msgs/sec - ${status_msg}"
        ((FAILED++))
        RESULTS+=("FAIL: ${rate} msgs/sec - ${status_msg}")
    fi
}

# Run all tests
echo ""
echo "Performance Targets:"
echo "  - Throughput: 200-500k msgs/sec"
echo "  - p99 Latency: <${P99_THRESHOLD_US}us (2ms)"
echo "  - Parse Errors: 0"
echo ""

for test_config in "${TESTS[@]}"; do
    read -r rate duration port <<< "$test_config"
    run_test "$rate" "$duration" "$port"
    sleep 1  # Brief pause between tests
done

# Summary
echo ""
echo "========================================"
echo "Performance Regression Test Summary"
echo "========================================"
echo ""
for result in "${RESULTS[@]}"; do
    if [[ "$result" == PASS* ]]; then
        echo -e "${GREEN}$result${NC}"
    else
        echo -e "${RED}$result${NC}"
    fi
done
echo ""
echo "Total: $((PASSED + FAILED)) tests"
echo -e "Passed: ${GREEN}${PASSED}${NC}"
echo -e "Failed: ${RED}${FAILED}${NC}"
echo ""

if [[ "$FAILED" -gt 0 ]]; then
    echo -e "${RED}Performance regression detected!${NC}"
    exit 1
else
    echo -e "${GREEN}All performance targets met!${NC}"
    exit 0
fi
