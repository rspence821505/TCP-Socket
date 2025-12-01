#!/bin/bash
# Simple benchmark comparing padded vs unpadded performance
# Works on both Linux and macOS (doesn't require perf)

# Script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
SRC_DIR="$PROJECT_ROOT/src"
INCLUDE_DIR="$PROJECT_ROOT/include"

echo "==================================================================="
echo "False Sharing Benchmark (Simple Version)"
echo "==================================================================="
echo ""

# Check if binaries exist (use make to build them if needed)
if [ ! -f "$BUILD_DIR/feed_handler_spmc" ] || [ ! -f "$BUILD_DIR/binary_mock_server" ]; then
    echo "Building required binaries..."
    cd "$PROJECT_ROOT"
    make feed_handler_spmc binary_mock_server
    if [ $? -ne 0 ]; then
        echo "Compilation failed!"
        exit 1
    fi
fi

echo "Binaries ready"
echo ""

# Run 5 trials of each
TRIALS=3

echo "Running $TRIALS trials of each configuration..."
echo ""

# Arrays to store results
declare -a UNPADDED_TIMES
declare -a PADDED_TIMES

for trial in $(seq 1 $TRIALS); do
    echo "Trial $trial/$TRIALS..."

    # Test WITHOUT padding
    "$BUILD_DIR/binary_mock_server" 9999 > /dev/null 2>&1 &
    SERVER_PID=$!
    sleep 1

    START=$(date +%s%N)
    "$BUILD_DIR/feed_handler_spmc" 9999 2048 unpadded 2>&1 | grep -E "Total:|msgs/sec" > /tmp/unpadded_$trial.txt
    END=$(date +%s%N)
    UNPADDED_TIME=$(( (END - START) / 1000000 ))  # Convert to ms
    UNPADDED_TIMES[$trial]=$UNPADDED_TIME

    kill $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null
    sleep 1

    # Test WITH padding
    "$BUILD_DIR/binary_mock_server" 9999 > /dev/null 2>&1 &
    SERVER_PID=$!
    sleep 1

    START=$(date +%s%N)
    "$BUILD_DIR/feed_handler_spmc" 9999 2048 padded 2>&1 | grep -E "Total:|msgs/sec" > /tmp/padded_$trial.txt
    END=$(date +%s%N)
    PADDED_TIME=$(( (END - START) / 1000000 ))  # Convert to ms
    PADDED_TIMES[$trial]=$PADDED_TIME

    kill $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null
    sleep 1

    echo "  Unpadded: ${UNPADDED_TIME}ms, Padded: ${PADDED_TIME}ms"
done

echo ""
echo "==================================================================="
echo "RESULTS"
echo "==================================================================="
echo ""

# Calculate averages
UNPADDED_SUM=0
PADDED_SUM=0

for time in "${UNPADDED_TIMES[@]}"; do
    UNPADDED_SUM=$((UNPADDED_SUM + time))
done

for time in "${PADDED_TIMES[@]}"; do
    PADDED_SUM=$((PADDED_SUM + time))
done

UNPADDED_AVG=$((UNPADDED_SUM / TRIALS))
PADDED_AVG=$((PADDED_SUM / TRIALS))

SPEEDUP=$(echo "scale=2; 100 * ($UNPADDED_AVG - $PADDED_AVG) / $UNPADDED_AVG" | bc)

echo "Average Runtime:"
echo "  WITHOUT padding (false sharing): ${UNPADDED_AVG}ms"
echo "  WITH padding (fixed):            ${PADDED_AVG}ms"
echo ""
echo "Speedup: ${SPEEDUP}% improvement with padding"
echo ""

if (( UNPADDED_AVG > PADDED_AVG )); then
    echo "SUCCESS: Padding eliminated false sharing!"
    echo "   Padded version is faster as expected."
else
    echo "Unexpected: Unpadded version not slower."
    echo "   This can happen with low message rates or few cores."
    echo "   Try running with more messages or on a machine with more cores."
fi

echo ""
echo "==================================================================="
echo "EXPLANATION"
echo "==================================================================="
echo ""
echo "What is False Sharing?"
echo ""
echo "Without Padding:"
echo "  +---------------------------------------------------+"
echo "  |  Cache Line (64 bytes)                            |"
echo "  +-------------------------+-------------------------+"
echo "  | Consumer 0 counter      | Consumer 1 counter      |"
echo "  +-------------------------+-------------------------+"
echo "       ^                           ^"
echo "  CPU Core 0 writes here    CPU Core 1 writes here"
echo ""
echo "  Problem: Both consumers write to the SAME cache line!"
echo "  -> When one writes, it invalidates the other's cache"
echo "  -> Constant cache-line ping-pong between cores"
echo "  -> Poor performance"
echo ""
echo "With Padding:"
echo "  +---------------------------------------------------+"
echo "  |  Cache Line 0 (64 bytes)                          |"
echo "  |  Consumer 0 counter + padding                     |"
echo "  +---------------------------------------------------+"
echo "       ^"
echo "  CPU Core 0 writes here"
echo ""
echo "  +---------------------------------------------------+"
echo "  |  Cache Line 1 (64 bytes)                          |"
echo "  |  Consumer 1 counter + padding                     |"
echo "  +---------------------------------------------------+"
echo "       ^"
echo "  CPU Core 1 writes here"
echo ""
echo "  Solution: Each consumer owns its own cache line!"
echo "  -> No invalidation -> No ping-pong -> Better performance"
echo ""
