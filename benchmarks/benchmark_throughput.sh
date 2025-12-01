#!/bin/bash
# Simple throughput benchmark for comparing baseline vs optimized

PROJECT_ROOT="${PROJECT_ROOT:-$(pwd)}"
BUILD_DIR="$PROJECT_ROOT/build"
PORT=9999
DURATION=30

echo "==================================================================="
echo "Throughput Benchmark: Baseline vs Optimized"
echo "==================================================================="
echo ""

# Check binaries
if [ ! -f "$BUILD_DIR/feed_handler_heartbeat_profile" ]; then
    echo "❌ Baseline binary not found. Build with: make profiling"
    exit 1
fi

if [ ! -f "$BUILD_DIR/heartbeat_mock_server" ]; then
    echo "❌ Server binary not found. Build with: make"
    exit 1
fi

# Function to run benchmark
run_benchmark() {
    local BINARY=$1
    local LABEL=$2
    
    echo "Testing: $LABEL"
    echo "-------------------------------------------------------------------"
    
    # Start server
    "$BUILD_DIR/heartbeat_mock_server" "$PORT" 1000 100 > /tmp/server_bench.log 2>&1 &
    SERVER_PID=$!
    sleep 2
    
    # Run feed handler
    timeout $DURATION "$BUILD_DIR/$BINARY" "$PORT" 2>&1 | \
        grep -E "Ticks received|msgs/sec" | tail -5
    
    # Stop server
    kill $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null
    
    echo ""
}

# Baseline
run_benchmark "feed_handler_heartbeat_profile" "Baseline (Original)"

# Optimized (now the default feed_handler_heartbeat)
if [ -f "$BUILD_DIR/feed_handler_heartbeat" ]; then
    run_benchmark "feed_handler_heartbeat" "Optimized"
else
    echo "⚠️  Optimized binary not found. Skipping."
fi

echo "==================================================================="
echo "Benchmark complete!"
echo ""
echo "To see detailed profiling data:"
echo "  ./profiling/profile_feed_handler.sh"
echo ""
