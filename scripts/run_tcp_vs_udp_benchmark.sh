#!/bin/bash
# TCP vs UDP Benchmark Comparison Script

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"

echo "==================================================================="
echo "TCP vs UDP Feed Comparison "
echo "==================================================================="
echo ""
echo "This benchmark compares TCP vs UDP for market data feeds:"
echo "  - TCP: Reliable, higher latency, guaranteed delivery"
echo "  - UDP: Lower latency, packet loss, requires retransmit logic"
echo ""

# Check if binaries exist
if [ ! -f "$BUILD_DIR/tcp_vs_udp_benchmark" ]; then
    echo "Error: tcp_vs_udp_benchmark not found. Build first with:"
    echo "  cd $PROJECT_ROOT"
    echo "  make tcp_vs_udp_benchmark"
    exit 1
fi

if [ ! -f "$BUILD_DIR/binary_mock_server" ]; then
    echo "Error: binary_mock_server not found."
    exit 1
fi

if [ ! -f "$BUILD_DIR/udp_mock_server" ]; then
    echo "Error: udp_mock_server not found."
    exit 1
fi

# Test configurations
TCP_PORT=9999
UDP_PORT=9998
MESSAGES=20000

echo "Configuration:"
echo "  TCP port: $TCP_PORT"
echo "  UDP port: $UDP_PORT (data) + $TCP_PORT (control)"
echo "  Messages per test: $MESSAGES"
echo ""

# Test 1: TCP baseline
echo "==================================================================="
echo "TEST 1: TCP with TCP_NODELAY"
echo "==================================================================="
echo ""

"$BUILD_DIR/binary_mock_server" $TCP_PORT > /tmp/tcp_server.log 2>&1 &
TCP_SERVER_PID=$!
sleep 2

echo "Running TCP benchmark..."
"$BUILD_DIR/tcp_vs_udp_benchmark" --tcp-port $TCP_PORT --messages $MESSAGES --tcp-only 2>&1

kill $TCP_SERVER_PID 2>/dev/null
wait $TCP_SERVER_PID 2>/dev/null
sleep 2

# Test 2: UDP with 1% packet loss
echo ""
echo "==================================================================="
echo "TEST 2: UDP with 1% packet loss"
echo "==================================================================="
echo ""

"$BUILD_DIR/udp_mock_server" $UDP_PORT $TCP_PORT 0.01 > /tmp/udp_server.log 2>&1 &
UDP_SERVER_PID=$!
sleep 2

echo "Running UDP benchmark..."
"$BUILD_DIR/udp_feed_handler" $UDP_PORT $TCP_PORT 30 2>&1 | tee /tmp/udp_test.log

kill $UDP_SERVER_PID 2>/dev/null
wait $UDP_SERVER_PID 2>/dev/null
sleep 2

# Test 3: UDP with 5% packet loss
echo ""
echo "==================================================================="
echo "TEST 3: UDP with 5% packet loss"
echo "==================================================================="
echo ""

"$BUILD_DIR/udp_mock_server" $UDP_PORT $TCP_PORT 0.05 > /tmp/udp_server_5pct.log 2>&1 &
UDP_SERVER_PID=$!
sleep 2

echo "Running UDP benchmark with higher loss..."
"$BUILD_DIR/udp_feed_handler" $UDP_PORT $TCP_PORT 30 2>&1 | tee /tmp/udp_test_5pct.log

kill $UDP_SERVER_PID 2>/dev/null
wait $UDP_SERVER_PID 2>/dev/null
sleep 2

echo ""
echo "==================================================================="
echo "ANALYSIS: When Does UDP Win?"
echo "==================================================================="
echo ""

cat << 'EOF'
UDP's lower latency justifies the complexity when:

1️⃣  **Latency Requirements are Critical:**
   - High-frequency trading where microseconds matter
   - UDP typically 20-50% lower latency than TCP
   - No ACK delays, no Nagle's algorithm issues

2️⃣  **Network Infrastructure is Excellent:**
   - Low packet loss (<0.1% on dedicated networks)
   - Multicast support for one-to-many distribution
   - High-quality data center networks

3️⃣  **Application Can Handle Gaps:**
   - Market data can tolerate missing individual ticks
   - Periodic snapshots provide state recovery
   - Gap filling is acceptable (retransmit on control channel)

4️⃣  **Throughput is Very High:**
   - TCP's congestion control becomes bottleneck
   - UDP scales better for multicast scenarios
   - Reduced CPU overhead per packet

❌ When TCP is Better:

- Unreliable network (WiFi, WAN, public internet)
- Packet loss >1% makes retransmit overhead too high
- Application requires guaranteed delivery (orders, trades)
- Simplicity matters more than microseconds

📊 Real-World Usage:

Exchanges use BOTH:
- Primary feed: UDP multicast (low latency, 99.9% of data)
- Control channel: TCP (retransmit requests, snapshots)
- Critical messages: TCP (order confirmations, fills)

Typical packet loss on exchange networks: 0.001% - 0.01%
Latency improvement vs TCP: 30-50% (p99)
Complexity cost: 3-5x more code vs TCP-only solution

EOF

echo "==================================================================="
echo "Test Results Summary"
echo "==================================================================="
echo ""

echo "TCP Results:"
grep -E "Mean:|p99:" /tmp/tcp_server.log 2>/dev/null || echo "(check logs)"

echo ""
echo "UDP Results (1% loss):"
grep -E "Mean:|p99:|Packet loss" /tmp/udp_test.log 2>/dev/null || echo "(check logs)"

echo ""
echo "UDP Results (5% loss):"
grep -E "Mean:|p99:|Packet loss" /tmp/udp_test_5pct.log 2>/dev/null || echo "(check logs)"

echo ""
echo "Logs saved to /tmp/{tcp,udp}_*.log"
echo ""
echo "Run individual tests with:"
echo "  Terminal 1: $BUILD_DIR/udp_mock_server 9998 9999 0.01"
echo "  Terminal 2: $BUILD_DIR/udp_feed_handler 9998 9999 30"
echo ""
