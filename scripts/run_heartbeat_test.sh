#!/bin/bash
# Heartbeat and Connection Management Test Script

# Script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"

PORT=9999

echo "=================================================================="
echo "Heartbeat & Connection Management Test"
echo "=================================================================="
echo ""

# Check if binaries exist
if [ ! -f "$BUILD_DIR/feed_handler_heartbeat" ] || [ ! -f "$BUILD_DIR/heartbeat_mock_server" ]; then
    echo "Error: Binaries not found. Run 'make heartbeat-benchmark' first!"
    exit 1
fi

echo "Starting heartbeat mock server on port $PORT..."
"$BUILD_DIR/heartbeat_mock_server" "$PORT" &
SERVER_PID=$!

# Give server time to start
sleep 2

echo "Starting feed handler with heartbeat support..."
echo ""

"$BUILD_DIR/feed_handler_heartbeat" "$PORT"

# Cleanup
echo ""
echo "Cleaning up..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo "Test complete!"
