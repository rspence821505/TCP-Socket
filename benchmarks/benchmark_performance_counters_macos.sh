#!/bin/bash
# macOS Performance Counter Measurement Script
# Alternative to Linux 'perf stat' for IPC and cache analysis

set -e

BINARY="${1:-./build/benchmark_parsing_hotpath}"
DURATION="${2:-30}"
OUTPUT_DIR="${3:-perf_counters}"

echo "=================================================================="
echo "macOS Performance Counter Analysis"
echo "=================================================================="
echo ""
echo "Binary:   $BINARY"
echo "Duration: ${DURATION}s"
echo "Output:   $OUTPUT_DIR/"
echo ""

# Create output directory
mkdir -p "$OUTPUT_DIR"

if [ ! -f "$BINARY" ]; then
    echo "❌ Binary not found: $BINARY"
    echo "   Build with: make benchmark-parsing-hotpath"
    exit 1
fi

#==============================================================================
# Method 1: Instruments System Trace (GUI required)
#==============================================================================

echo "=================================================================="
echo "Method 1: Instruments System Trace"
echo "=================================================================="
echo ""

if command -v instruments &> /dev/null; then
    echo "Running Instruments System Trace for ${DURATION}s..."
    echo "(This requires sudo for performance counters)"
    echo ""
    
    # Run with System Trace
    sudo instruments -t 'System Trace' \
        -D "$OUTPUT_DIR/system_trace.trace" \
        -l $DURATION \
        "$BINARY" 10000000 &
    
    INSTRUMENTS_PID=$!
    
    echo "Instruments running (PID: $INSTRUMENTS_PID)..."
    echo "This will take ${DURATION} seconds..."
    
    # Wait for completion
    wait $INSTRUMENTS_PID 2>/dev/null || true
    
    echo "✅ System Trace complete"
    echo ""
    echo "To view results:"
    echo "  open $OUTPUT_DIR/system_trace.trace"
    echo ""
    echo "In Instruments, check:"
    echo "  1. Select 'CPU Counters' in left panel"
    echo "  2. Look for 'Instructions Retired'"
    echo "  3. Look for 'Cycles'"
    echo "  4. Calculate: IPC = Instructions / Cycles"
    echo ""
else
    echo "⚠️  Instruments not found (Xcode not installed?)"
    echo "   Install Xcode or use Method 2"
    echo ""
fi

#==============================================================================
# Method 2: Sample with detailed profile
#==============================================================================

echo "=================================================================="
echo "Method 2: Sample Profiling"
echo "=================================================================="
echo ""

echo "Running sample for ${DURATION}s..."
sample "$BINARY" $DURATION -file "$OUTPUT_DIR/sample_profile.txt" 10000000 &
SAMPLE_PID=$!

echo "Sample running (PID: $SAMPLE_PID)..."
wait $SAMPLE_PID 2>/dev/null || true

echo "✅ Sample profile complete"
echo ""
echo "To view results:"
echo "  cat $OUTPUT_DIR/sample_profile.txt"
echo ""

#==============================================================================
# Method 3: Activity Monitor snapshot (macOS 12+)
#==============================================================================

echo "=================================================================="
echo "Method 3: Activity Monitor Metrics"
echo "=================================================================="
echo ""

echo "Running benchmark and capturing metrics..."

# Start the benchmark in background
"$BINARY" 10000000 > "$OUTPUT_DIR/benchmark_output.txt" 2>&1 &
BENCH_PID=$!

# Give it a moment to start
sleep 2

# Capture metrics while running (if available)
if command -v powermetrics &> /dev/null; then
    echo "Capturing powermetrics (requires sudo)..."
    sudo powermetrics --samplers cpu_power,tasks -n 10 -i 1000 \
        --show-process-coalition --show-process-gpu \
        --show-process-energy \
        > "$OUTPUT_DIR/powermetrics.txt" 2>&1 &
    POWER_PID=$!
    
    # Wait for benchmark
    wait $BENCH_PID 2>/dev/null || true
    
    # Stop powermetrics
    sudo kill $POWER_PID 2>/dev/null || true
    wait $POWER_PID 2>/dev/null || true
    
    echo "✅ Powermetrics captured"
    echo ""
else
    # Just wait for benchmark
    wait $BENCH_PID 2>/dev/null || true
    echo "⚠️  powermetrics not available"
    echo ""
fi

#==============================================================================
# Method 4: DTrace performance counters (advanced)
#==============================================================================

echo "=================================================================="
echo "Method 4: DTrace Performance Counters"
echo "=================================================================="
echo ""

if command -v dtrace &> /dev/null; then
    echo "Running dtrace sampling for ${DURATION}s..."
    echo "(This requires sudo)"
    echo ""
    
    # Create dtrace script
    cat > "$OUTPUT_DIR/dtrace_profile.d" << 'EOF'
#!/usr/sbin/dtrace -s

#pragma D option quiet
#pragma D option switchrate=10hz

BEGIN
{
    printf("Profiling started...\n");
    start = timestamp;
}

profile-997
/pid == $target/
{
    @samples[ustack(5)] = count();
    @total = count();
}

tick-1s
{
    elapsed = (timestamp - start) / 1000000000;
    printf("Elapsed: %d seconds\n", elapsed);
}

END
{
    printf("\nTop 20 stacks:\n");
    printf("================================================================\n");
    trunc(@samples, 20);
    printa(@samples);
    
    printf("\nTotal samples: ");
    printa(@total);
    printf("\n");
}
EOF
    
    chmod +x "$OUTPUT_DIR/dtrace_profile.d"
    
    # Run dtrace
    sudo dtrace -s "$OUTPUT_DIR/dtrace_profile.d" \
        -c "$BINARY 10000000" \
        > "$OUTPUT_DIR/dtrace_output.txt" 2>&1 || true
    
    echo "✅ DTrace profile complete"
    echo ""
    echo "To view results:"
    echo "  cat $OUTPUT_DIR/dtrace_output.txt"
    echo ""
else
    echo "⚠️  DTrace not available"
    echo ""
fi

#==============================================================================
# Analysis Summary
#==============================================================================

echo "=================================================================="
echo "ANALYSIS COMPLETE"
echo "=================================================================="
echo ""
echo "Results saved to: $OUTPUT_DIR/"
echo ""
echo "Files generated:"
ls -lh "$OUTPUT_DIR/" | tail -n +2 | awk '{print "  " $9 " (" $5 ")"}'
echo ""

echo "=================================================================="
echo "HOW TO ANALYZE RESULTS"
echo "=================================================================="
echo ""

echo "1. IPC (Instructions Per Cycle):"
echo "   --------------------------------"
echo "   Open: $OUTPUT_DIR/system_trace.trace (in Instruments)"
echo "   Steps:"
echo "     a) Select 'CPU Counters' in left panel"
echo "     b) Find 'Instructions Retired' metric"
echo "     c) Find 'Cycles' metric"
echo "     d) Calculate: IPC = Instructions / Cycles"
echo ""
echo "   Target: IPC > 2.0"
echo "   Good:   IPC 2.0-3.0 (CPU-bound, efficient)"
echo "   Bad:    IPC < 1.0 (memory-bound, cache misses)"
echo ""

echo "2. Cache Miss Rate:"
echo "   --------------------------------"
echo "   Open: $OUTPUT_DIR/system_trace.trace (in Instruments)"
echo "   Steps:"
echo "     a) Select 'CPU Counters' in left panel"
echo "     b) Find 'L1D Cache Misses' or 'Cache References/Misses'"
echo "     c) Calculate: Miss Rate = (Misses / References) × 100%"
echo ""
echo "   Target: Cache miss rate < 1%"
echo "   Good:   < 2%"
echo "   Bad:    > 5%"
echo ""

echo "3. Hot Functions:"
echo "   --------------------------------"
echo "   View: $OUTPUT_DIR/sample_profile.txt"
echo "   Look for functions with high sample counts"
echo "   Focus optimization on top 3-5 functions"
echo ""

echo "4. Detailed Metrics (if powermetrics available):"
echo "   --------------------------------"
echo "   View: $OUTPUT_DIR/powermetrics.txt"
echo "   Check CPU utilization, power usage, thermal state"
echo ""

echo "=================================================================="
echo "EXPECTED RESULTS"
echo "=================================================================="
echo ""
echo "Baseline parsing:"
echo "  IPC:              ~1.5-1.8"
echo "  Cache miss rate:  ~3-5%"
echo "  ns/tick:          ~15-25ns"
echo ""
echo "Optimized parsing:"
echo "  IPC:              ~2.0-2.5 ✅"
echo "  Cache miss rate:  ~0.5-1.5% ✅"
echo "  ns/tick:          ~8-12ns"
echo ""

echo "=================================================================="
echo "NEXT STEPS"
echo "=================================================================="
echo ""
echo "1. Open System Trace in Instruments:"
echo "   open $OUTPUT_DIR/system_trace.trace"
echo ""
echo "2. Check CPU Counters for IPC and cache metrics"
echo ""
echo "3. If IPC < 2.0 or cache misses > 1%:"
echo "   - Review hot functions in sample profile"
echo "   - Apply optimizations (inlining, prefetch, etc.)"
echo "   - Re-run this script to measure improvement"
echo ""
echo "4. Iterate until targets achieved"
echo ""
