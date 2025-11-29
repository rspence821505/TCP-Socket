#!/bin/bash
# Simulate realistic network conditions for socket tuning benchmarks
# This adds artificial latency to the loopback interface to demonstrate
# TCP_NODELAY effects that aren't visible on ultra-fast localhost

echo "==================================================================="
echo "Network Simulation for Socket Tuning"
echo "==================================================================="
echo ""
echo "This script adds artificial network delay to localhost to simulate"
echo "realistic network conditions where TCP_NODELAY makes a difference."
echo ""

# Check if running as root (needed for tc)
if [ "$EUID" -ne 0 ]; then 
    echo "❌ This script requires root privileges to modify network settings."
    echo ""
    echo "Run with: sudo bash $0"
    echo ""
    echo "Or, skip network simulation and see the explanation below."
    exit 1
fi

# Check if tc is available
if ! command -v tc &> /dev/null; then
    echo "❌ 'tc' (traffic control) command not found."
    echo ""
    echo "On Linux, install with:"
    echo "  apt-get install iproute2"
    echo ""
    exit 1
fi

# Detect OS
if [[ "$OSTYPE" != "linux-gnu"* ]]; then
    echo "❌ Network simulation only works on Linux (uses 'tc' command)."
    echo ""
    echo "On macOS, TCP_NODELAY effects are less pronounced on localhost."
    echo "See explanation at the end of this script."
    echo ""
    exit 1
fi

# Add 5ms delay to loopback (simulates ~10ms RTT total)
echo "Adding 5ms delay to loopback interface (simulates 10ms RTT)..."
tc qdisc add dev lo root netem delay 5ms

if [ $? -eq 0 ]; then
    echo "✅ Network delay added successfully"
    echo ""
    echo "Run your benchmark now:"
    echo "  ./scripts/run_socket_benchmark.sh"
    echo ""
    echo "When done, remove the delay with:"
    echo "  sudo tc qdisc del dev lo root"
    echo ""
else
    echo "❌ Failed to add network delay"
    echo ""
    echo "You may need to remove existing qdisc first:"
    echo "  sudo tc qdisc del dev lo root"
    echo "Then try again."
fi

cat << 'EOF'

=================================================================== 
WHY THIS HELPS
===================================================================

On bare localhost (no delay):
  - RTT is ~1µs
  - Nagle's algorithm never activates (ACKs arrive instantly)
  - TCP_NODELAY has no benefit, just overhead

With simulated 10ms RTT:
  - More realistic network conditions
  - Nagle will batch small packets
  - TCP_NODELAY prevents 200ms timeouts

Expected results with delay:
  Baseline (Nagle on):     p99 ~15ms  (includes Nagle batching)
  TCP_NODELAY:             p99 ~10ms  (no batching delay)
  Improvement:             ~33% faster

===================================================================
CLEANUP
===================================================================

To remove the network delay and restore normal localhost:

  sudo tc qdisc del dev lo root

===================================================================

EOF
