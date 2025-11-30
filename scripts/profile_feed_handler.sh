#!/bin/bash
# Profiling Hot Paths
# This script profiles the feed handler using perf (Linux) or sample (macOS)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
FLAMEGRAPH_DIR="${FLAMEGRAPH_DIR:-$HOME/FlameGraph}"
RESULTS_DIR="$PROJECT_ROOT/profiling_results"

# Configuration
PORT=9999
DURATION=15  # seconds (reduced for macOS sample)
FREQUENCY=999  # Hz (sampling frequency)

# Detect platform
PLATFORM="$(uname -s)"

echo "==================================================================="
echo "Feed Handler Profiling"
echo "==================================================================="
echo ""
echo "Configuration:"
echo "  Platform: $PLATFORM"
echo "  Duration: ${DURATION}s"
echo "  Results: $RESULTS_DIR"
echo ""

# Create results directory
mkdir -p "$RESULTS_DIR"

# Cross-platform timeout function
run_with_timeout() {
    local timeout_secs=$1
    shift
    if command -v timeout &> /dev/null; then
        timeout "$timeout_secs" "$@"
    elif command -v gtimeout &> /dev/null; then
        gtimeout "$timeout_secs" "$@"
    else
        "$@" &
        local pid=$!
        (sleep "$timeout_secs"; kill $pid 2>/dev/null) &
        local killer=$!
        wait $pid 2>/dev/null
        kill $killer 2>/dev/null
        wait $killer 2>/dev/null
    fi
}

# Check for profiling tools
USE_PERF=0
USE_SAMPLE=0
FLAMEGRAPH_AVAILABLE=0

if [ "$PLATFORM" = "Linux" ]; then
    if command -v perf &> /dev/null; then
        USE_PERF=1
        echo "✅ Using perf for profiling"
    else
        echo "⚠️  perf not found. Install with:"
        echo "   sudo apt-get install linux-tools-common linux-tools-generic linux-tools-\$(uname -r)"
        echo ""
        echo "Falling back to simple throughput benchmark..."
    fi

    # Check for FlameGraph tools
    if [ -d "$FLAMEGRAPH_DIR" ]; then
        echo "✅ FlameGraph tools found"
        FLAMEGRAPH_AVAILABLE=1
    fi
elif [ "$PLATFORM" = "Darwin" ]; then
    # macOS - use sample command
    USE_SAMPLE=1
    echo "✅ Using sample for profiling (macOS)"
    echo ""
    echo "Note: For detailed profiling with GUI, use Instruments:"
    echo "   instruments -t 'Time Profiler' ./build/feed_handler_heartbeat_profile"
    echo ""
fi

# Function to profile a binary on Linux with perf
profile_binary_perf() {
    local BINARY_NAME=$1
    local OUTPUT_PREFIX=$2
    local BINARY_PATH="$BUILD_DIR/$BINARY_NAME"

    if [ ! -f "$BINARY_PATH" ]; then
        echo "❌ Binary not found: $BINARY_PATH"
        echo "   Build with: make profiling"
        return 1
    fi

    echo ""
    echo "==================================================================="
    echo "Profiling: $BINARY_NAME (perf)"
    echo "==================================================================="
    echo ""

    # Start server
    echo "Starting heartbeat mock server on port $PORT..."
    "$BUILD_DIR/heartbeat_mock_server" "$PORT" 1000 10 > /tmp/server_${OUTPUT_PREFIX}.log 2>&1 &
    SERVER_PID=$!
    sleep 2

    # Check if server started
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo "❌ Server failed to start"
        cat /tmp/server_${OUTPUT_PREFIX}.log
        return 1
    fi

    echo "Running $BINARY_NAME with perf for ${DURATION}s..."

    # Record with perf
    local PERF_DATA="$RESULTS_DIR/perf_${OUTPUT_PREFIX}.data"

    timeout $DURATION perf record \
        -F $FREQUENCY \
        -g \
        --call-graph dwarf \
        -o "$PERF_DATA" \
        -- "$BINARY_PATH" "$PORT" > "$RESULTS_DIR/${OUTPUT_PREFIX}_output.txt" 2>&1 &

    HANDLER_PID=$!

    # Wait for completion
    wait $HANDLER_PID 2>/dev/null

    # Stop server
    kill $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null

    echo "✅ Profiling complete"
    echo ""

    # Generate reports
    echo "Generating reports..."

    # 1. Text report (top functions)
    perf report -i "$PERF_DATA" --stdio --sort=overhead,symbol \
        > "$RESULTS_DIR/${OUTPUT_PREFIX}_report.txt" 2>&1

    # 2. Annotated source (top 5 functions)
    perf annotate -i "$PERF_DATA" --stdio \
        > "$RESULTS_DIR/${OUTPUT_PREFIX}_annotate.txt" 2>&1

    # 3. Flamegraph (if available)
    if [ $FLAMEGRAPH_AVAILABLE -eq 1 ]; then
        perf script -i "$PERF_DATA" | \
            "$FLAMEGRAPH_DIR/stackcollapse-perf.pl" | \
            "$FLAMEGRAPH_DIR/flamegraph.pl" \
            > "$RESULTS_DIR/${OUTPUT_PREFIX}_flame.svg"
        echo "  ✅ Flamegraph: ${OUTPUT_PREFIX}_flame.svg"
    fi

    echo "  ✅ Text report: ${OUTPUT_PREFIX}_report.txt"
    echo "  ✅ Annotations: ${OUTPUT_PREFIX}_annotate.txt"
    echo ""

    # Show top 10 functions
    echo "Top 10 functions by CPU time:"
    echo "-------------------------------------------------------------------"
    grep -A 30 "Overhead.*Symbol" "$RESULTS_DIR/${OUTPUT_PREFIX}_report.txt" | \
        head -40 | tail -30 || echo "Could not extract top functions"
    echo ""

    return 0
}

# Function to profile a binary on macOS with sample
profile_binary_sample() {
    local BINARY_NAME=$1
    local OUTPUT_PREFIX=$2
    local BINARY_PATH="$BUILD_DIR/$BINARY_NAME"

    if [ ! -f "$BINARY_PATH" ]; then
        echo "❌ Binary not found: $BINARY_PATH"
        echo "   Build with: make profiling"
        return 1
    fi

    echo ""
    echo "==================================================================="
    echo "Profiling: $BINARY_NAME (sample)"
    echo "==================================================================="
    echo ""

    # Start server
    echo "Starting heartbeat mock server on port $PORT..."
    "$BUILD_DIR/heartbeat_mock_server" "$PORT" 1000 10 > /tmp/server_${OUTPUT_PREFIX}.log 2>&1 &
    SERVER_PID=$!
    sleep 2

    # Check if server started
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo "❌ Server failed to start"
        cat /tmp/server_${OUTPUT_PREFIX}.log
        return 1
    fi

    echo "Running $BINARY_NAME for ${DURATION}s..."

    # Start the binary in background
    "$BINARY_PATH" "$PORT" > "$RESULTS_DIR/${OUTPUT_PREFIX}_output.txt" 2>&1 &
    HANDLER_PID=$!

    sleep 1  # Let it warm up

    # Sample the process
    echo "Sampling process $HANDLER_PID..."
    sample $HANDLER_PID $((DURATION - 2)) -f "$RESULTS_DIR/${OUTPUT_PREFIX}_sample.txt" 2>/dev/null || \
        echo "Note: sample may require running as root for full details"

    # Stop handler and server
    kill $HANDLER_PID 2>/dev/null
    wait $HANDLER_PID 2>/dev/null
    kill $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null

    echo "✅ Sampling complete"
    echo ""

    # Show results
    if [ -f "$RESULTS_DIR/${OUTPUT_PREFIX}_sample.txt" ]; then
        echo "  ✅ Sample report: ${OUTPUT_PREFIX}_sample.txt"
        echo ""
        echo "Top functions (from sample output):"
        echo "-------------------------------------------------------------------"
        grep -A 50 "Call graph:" "$RESULTS_DIR/${OUTPUT_PREFIX}_sample.txt" 2>/dev/null | \
            head -30 || echo "Sample data captured. View with: cat $RESULTS_DIR/${OUTPUT_PREFIX}_sample.txt"
    fi
    echo ""

    return 0
}

# Function for simple throughput benchmark (fallback)
benchmark_binary() {
    local BINARY_NAME=$1
    local OUTPUT_PREFIX=$2
    local BINARY_PATH="$BUILD_DIR/$BINARY_NAME"

    if [ ! -f "$BINARY_PATH" ]; then
        echo "❌ Binary not found: $BINARY_PATH"
        return 1
    fi

    echo ""
    echo "==================================================================="
    echo "Benchmarking: $BINARY_NAME"
    echo "==================================================================="
    echo ""

    # Start server
    "$BUILD_DIR/heartbeat_mock_server" "$PORT" 1000 100 > /tmp/server_${OUTPUT_PREFIX}.log 2>&1 &
    SERVER_PID=$!
    sleep 2

    echo "Running for ${DURATION}s..."

    # Run and capture output
    run_with_timeout $DURATION "$BINARY_PATH" "$PORT" 2>&1 | tee "$RESULTS_DIR/${OUTPUT_PREFIX}_output.txt"

    kill $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null

    echo ""
    echo "Results saved to: $RESULTS_DIR/${OUTPUT_PREFIX}_output.txt"
    echo ""
}

# Main execution
echo "Building profiling binaries..."
cd "$PROJECT_ROOT"
make profiling 2>&1 | tail -10

if [ $? -ne 0 ]; then
    echo "❌ Build failed. Fix compilation errors first."
    exit 1
fi

echo ""
echo "==================================================================="
echo "STEP 1: Baseline Profile"
echo "==================================================================="

if [ $USE_PERF -eq 1 ]; then
    profile_binary_perf "feed_handler_heartbeat_profile" "baseline"
elif [ $USE_SAMPLE -eq 1 ]; then
    profile_binary_sample "feed_handler_heartbeat_profile" "baseline"
else
    benchmark_binary "feed_handler_heartbeat_profile" "baseline"
fi

echo ""
echo "==================================================================="
echo "STEP 2: Optimized Profile"
echo "==================================================================="

if [ -f "$BUILD_DIR/feed_handler_heartbeat_optimized" ]; then
    if [ $USE_PERF -eq 1 ]; then
        profile_binary_perf "feed_handler_heartbeat_optimized" "optimized"
    elif [ $USE_SAMPLE -eq 1 ]; then
        profile_binary_sample "feed_handler_heartbeat_optimized" "optimized"
    else
        benchmark_binary "feed_handler_heartbeat_optimized" "optimized"
    fi
else
    echo "⚠️  Optimized binary not found. Build it first with: make profiling"
fi

echo ""
echo "==================================================================="
echo "PROFILING COMPLETE"
echo "==================================================================="
echo ""
echo "Results saved to: $RESULTS_DIR/"
echo ""
echo "View results:"
if [ $USE_PERF -eq 1 ]; then
    echo "  Top functions:  cat $RESULTS_DIR/baseline_report.txt"
    echo "  Annotations:    cat $RESULTS_DIR/baseline_annotate.txt"
    if [ $FLAMEGRAPH_AVAILABLE -eq 1 ]; then
        echo "  Flamegraph:     open $RESULTS_DIR/baseline_flame.svg"
    fi
    echo ""
    echo "Interactive analysis:"
    echo "  perf report -i $RESULTS_DIR/perf_baseline.data"
elif [ $USE_SAMPLE -eq 1 ]; then
    echo "  Sample report:  cat $RESULTS_DIR/baseline_sample.txt"
    echo "  Output log:     cat $RESULTS_DIR/baseline_output.txt"
    echo ""
    echo "For detailed GUI profiling on macOS:"
    echo "  instruments -t 'Time Profiler' $BUILD_DIR/feed_handler_heartbeat_profile $PORT"
else
    echo "  Output log:     cat $RESULTS_DIR/baseline_output.txt"
fi
echo ""
echo "Quick throughput comparison:"
echo "  ./scripts/benchmark_throughput.sh"
echo ""
