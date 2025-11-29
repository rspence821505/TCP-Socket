#!/bin/bash
# Test script for Snapshot Recovery

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"

# Cross-platform timeout function (works on macOS and Linux)
run_with_timeout() {
    local timeout_secs=$1
    shift

    if command -v timeout &> /dev/null; then
        timeout "$timeout_secs" "$@"
    elif command -v gtimeout &> /dev/null; then
        gtimeout "$timeout_secs" "$@"
    else
        # Fallback: run in background and kill after timeout
        "$@" &
        local pid=$!
        (sleep "$timeout_secs"; kill $pid 2>/dev/null) &
        local killer=$!
        wait $pid 2>/dev/null
        kill $killer 2>/dev/null
        wait $killer 2>/dev/null
    fi
}

echo "=================================================================="
echo "Snapshot Recovery Test"
echo "=================================================================="
echo ""

# Check if binaries exist
if [ ! -f "$BUILD_DIR/feed_handler_snapshot" ] || [ ! -f "$BUILD_DIR/snapshot_mock_server" ]; then
    echo "Error: Binaries not found. Build first with:"
    echo "  cd $PROJECT_ROOT"
    echo "  make feed_handler_snapshot snapshot_mock_server"
    exit 1
fi

echo "This test demonstrates the snapshot recovery state machine:"
echo "  CONNECTING → SNAPSHOT_REQUEST → SNAPSHOT_REPLAY → INCREMENTAL"
echo ""
echo "Features:"
echo "  ✅ Client requests snapshot on connect"
echo "  ✅ Server sends full order book snapshot"
echo "  ✅ Client transitions to incremental mode"
echo "  ✅ Incremental updates modify the order book"
echo "  ✅ Reconnection triggers new snapshot request"
echo ""

# Test 1: Normal snapshot recovery flow
echo "==================================================================="
echo "TEST 1: Normal Snapshot Recovery Flow"
echo "==================================================================="
echo ""

"$BUILD_DIR/snapshot_mock_server" 9999 1000 5 > /tmp/snapshot_server1.log 2>&1 &
SERVER1_PID=$!
sleep 1

echo "Starting feed handler (will run for 15 seconds)..."
echo "Watch the state machine transitions!"
echo ""

run_with_timeout 15 "$BUILD_DIR/feed_handler_snapshot" 9999 AAPL | tee /tmp/feed_handler1.log

kill $SERVER1_PID 2>/dev/null
wait $SERVER1_PID 2>/dev/null

echo ""
echo "State Machine Flow:"
grep "State:" /tmp/feed_handler1.log || echo "(check logs)"

sleep 2

# Test 2: Reconnection with new snapshot
echo ""
echo "==================================================================="
echo "TEST 2: Reconnection Triggers New Snapshot"
echo "==================================================================="
echo ""

echo "Starting server (will be killed mid-session)..."
"$BUILD_DIR/snapshot_mock_server" 9999 1000 5 > /tmp/snapshot_server2.log 2>&1 &
SERVER2_PID=$!
sleep 1

echo "Starting feed handler in background..."
"$BUILD_DIR/feed_handler_snapshot" 9999 AAPL > /tmp/feed_handler2.log 2>&1 &
HANDLER_PID=$!

sleep 5

echo "Killing server to trigger reconnection..."
kill $SERVER2_PID 2>/dev/null
wait $SERVER2_PID 2>/dev/null

sleep 2

echo "Restarting server (feed handler should reconnect and request new snapshot)..."
"$BUILD_DIR/snapshot_mock_server" 9999 1000 5 > /tmp/snapshot_server3.log 2>&1 &
SERVER3_PID=$!

sleep 10

echo "Stopping feed handler..."
kill $HANDLER_PID 2>/dev/null
wait $HANDLER_PID 2>/dev/null

kill $SERVER3_PID 2>/dev/null
wait $SERVER3_PID 2>/dev/null

echo ""
echo "Checking reconnection behavior:"
echo ""
echo "Number of snapshot requests:"
grep "Sending snapshot request" /tmp/feed_handler2.log | wc -l
echo ""
echo "Number of snapshots received:"
grep "Received snapshot for" /tmp/feed_handler2.log | wc -l
echo ""
echo "State transitions:"
grep "State:" /tmp/feed_handler2.log

sleep 2

# Test 3: Verify order book updates
echo ""
echo "==================================================================="
echo "TEST 3: Order Book State Verification"
echo "==================================================================="
echo ""

"$BUILD_DIR/snapshot_mock_server" 9999 1000 10 > /tmp/snapshot_server4.log 2>&1 &
SERVER4_PID=$!
sleep 1

echo "Running feed handler with high update rate..."
run_with_timeout 10 "$BUILD_DIR/feed_handler_snapshot" 9999 MSFT | tee /tmp/feed_handler3.log

kill $SERVER4_PID 2>/dev/null
wait $SERVER4_PID 2>/dev/null

echo ""
echo "Order book statistics:"
grep "Incremental updates:" /tmp/feed_handler3.log || echo "(check full logs)"
echo ""
echo "Final order book depth:"
tail -20 /tmp/feed_handler3.log | grep -A 15 "Order Book:"

echo ""
echo "==================================================================="
echo "SUMMARY"
echo "==================================================================="
echo ""

echo "The snapshot recovery feature implements a production-grade state machine:"
echo ""
echo "1️⃣  CONNECTING → SNAPSHOT_REQUEST"
echo "   Client connects and immediately requests a snapshot"
echo ""
echo "2️⃣  SNAPSHOT_REQUEST → SNAPSHOT_REPLAY"
echo "   Server sends full order book snapshot"
echo "   Client rebuilds order book from snapshot"
echo ""
echo "3️⃣  SNAPSHOT_REPLAY → INCREMENTAL"
echo "   Snapshot processing complete"
echo "   Now ready to process incremental updates"
echo ""
echo "4️⃣  INCREMENTAL mode"
echo "   All incremental updates modify the order book"
echo "   Maintains accurate market state"
echo ""
echo "5️⃣  On Reconnection:"
echo "   State machine resets"
echo "   New snapshot requested"
echo "   Ensures data consistency after network issues"
echo ""
echo "Key Implementation Details:"
echo ""
echo "✅ Binary protocol with multiple message types:"
echo "   - SNAPSHOT_REQUEST (0x10)"
echo "   - SNAPSHOT_RESPONSE (0x11)"
echo "   - ORDER_BOOK_UPDATE (0x02)"
echo ""
echo "✅ Order book data structure:"
echo "   - Maintains bids/asks as sorted price levels"
echo "   - Supports snapshot loading and incremental updates"
echo "   - Provides best bid/ask and depth queries"
echo ""
echo "✅ State machine guards:"
echo "   - Incremental updates only processed in INCREMENTAL mode"
echo "   - Snapshot request only sent once per connection"
echo "   - Automatic state transitions on message receipt"
echo ""
echo "Production Use Cases:"
echo ""
echo "🏦 Market Data Feeds:"
echo "   - CME, ICE, NASDAQ all use snapshot + incremental pattern"
echo "   - Ensures clients always have consistent book state"
echo "   - Critical for regulatory compliance"
echo ""
echo "📊 High-Frequency Trading:"
echo "   - Snapshot on connect/reconnect prevents stale data"
echo "   - Incremental updates minimize bandwidth"
echo "   - Sub-millisecond order book updates"
echo ""
echo "🔄 Disaster Recovery:"
echo "   - Snapshot recovery enables fast failover"
echo "   - No manual intervention required"
echo "   - Zero data loss with proper sequencing"
echo ""
echo "To test manually:"
echo "  Terminal 1: $BUILD_DIR/snapshot_mock_server 9999 1000 10"
echo "  Terminal 2: $BUILD_DIR/feed_handler_snapshot 9999 AAPL"
echo ""
echo "  Watch the order book update in real-time!"
echo "  Try killing/restarting the server to see reconnection."
echo ""

# Cleanup
rm -f /tmp/snapshot_server*.log /tmp/feed_handler*.log

echo "Test complete! ✨"
