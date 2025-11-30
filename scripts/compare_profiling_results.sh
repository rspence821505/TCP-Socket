#!/bin/bash
# Compare profiling results between baseline and optimized versions
# Works on both Linux (perf) and macOS (sample)

PROJECT_ROOT="${PROJECT_ROOT:-$(pwd)}"
RESULTS_DIR="$PROJECT_ROOT/profiling_results"
PLATFORM="$(uname -s)"

echo "==================================================================="
echo "Profiling Results Comparison"
echo "==================================================================="
echo ""
echo "Platform: $PLATFORM"
echo "Results directory: $RESULTS_DIR"
echo ""

if [ ! -d "$RESULTS_DIR" ]; then
    echo "❌ No profiling results found at $RESULTS_DIR"
    echo "   Run: ./scripts/profile_feed_handler.sh"
    exit 1
fi

#=============================================================================
# macOS (sample) comparison
#=============================================================================

if [ "$PLATFORM" = "Darwin" ]; then
    echo "Using macOS sample output format"
    echo ""

    # Check for sample files
    BASELINE_SAMPLE="$RESULTS_DIR/baseline_sample.txt"
    OPTIMIZED_SAMPLE="$RESULTS_DIR/optimized_sample.txt"
    BASELINE_OUTPUT="$RESULTS_DIR/baseline_output.txt"
    OPTIMIZED_OUTPUT="$RESULTS_DIR/optimized_output.txt"

    # Function to display sample results
    display_sample_results() {
        local VERSION=$1
        local VERSION_UPPER=$(echo "$VERSION" | tr '[:lower:]' '[:upper:]')
        local SAMPLE_FILE="$RESULTS_DIR/${VERSION}_sample.txt"
        local OUTPUT_FILE="$RESULTS_DIR/${VERSION}_output.txt"

        echo "==================================================================="
        echo "$VERSION_UPPER VERSION"
        echo "==================================================================="
        echo ""

        if [ -f "$SAMPLE_FILE" ]; then
            echo "Sample Profile (Top Functions):"
            echo "-------------------------------------------------------------------"
            # Extract the heaviest stack frames from sample output
            grep -A 100 "Call graph:" "$SAMPLE_FILE" 2>/dev/null | \
                grep -E "^\s+[0-9]+" | \
                head -20 || echo "  (Could not parse sample output)"
            echo ""
        else
            echo "  ⚠️  Sample file not found: $SAMPLE_FILE"
            echo ""
        fi

        if [ -f "$OUTPUT_FILE" ]; then
            echo "Throughput Statistics:"
            echo "-------------------------------------------------------------------"
            # Extract key metrics from output
            grep -E "(Messages|Throughput|Latency|msg/s|bytes)" "$OUTPUT_FILE" 2>/dev/null | \
                head -10 | sed 's/^/  /'
            echo ""

            # Show any timing information
            if grep -q "elapsed" "$OUTPUT_FILE" 2>/dev/null; then
                echo "Timing:"
                grep "elapsed" "$OUTPUT_FILE" | head -5 | sed 's/^/  /'
                echo ""
            fi
        else
            echo "  ⚠️  Output file not found: $OUTPUT_FILE"
            echo ""
        fi
    }

    # Display baseline results
    if [ -f "$BASELINE_SAMPLE" ] || [ -f "$BASELINE_OUTPUT" ]; then
        display_sample_results "baseline"
    else
        echo "❌ No baseline profiling results found."
        echo "   Run: make profile"
        echo ""
    fi

    # Display optimized results
    if [ -f "$OPTIMIZED_SAMPLE" ] || [ -f "$OPTIMIZED_OUTPUT" ]; then
        display_sample_results "optimized"
    else
        echo "⚠️  No optimized profiling results found."
        echo "   This is expected if you haven't created the optimized version yet."
        echo ""
    fi

    echo "==================================================================="
    echo "MACOS PROFILING TIPS"
    echo "==================================================================="
    echo ""
    echo "For more detailed profiling on macOS, use Instruments:"
    echo ""
    echo "  1. Time Profiler (CPU sampling):"
    echo "     instruments -t 'Time Profiler' ./build/feed_handler_heartbeat_profile 9999"
    echo ""
    echo "  2. Allocations (memory profiling):"
    echo "     instruments -t 'Allocations' ./build/feed_handler_heartbeat_profile 9999"
    echo ""
    echo "  3. System Trace (detailed system calls):"
    echo "     instruments -t 'System Trace' ./build/feed_handler_heartbeat_profile 9999"
    echo ""
    echo "Or use Xcode's Instruments GUI for interactive analysis."
    echo ""

    # Quick throughput comparison if both outputs exist
    if [ -f "$BASELINE_OUTPUT" ] && [ -f "$OPTIMIZED_OUTPUT" ]; then
        echo "==================================================================="
        echo "QUICK COMPARISON"
        echo "==================================================================="
        echo ""
        echo "Run the throughput benchmark for direct comparison:"
        echo "  ./scripts/benchmark_throughput.sh"
        echo ""
    fi

    exit 0
fi

#=============================================================================
# Linux (perf) comparison - original logic
#=============================================================================

# Function to extract key metrics from perf stat
extract_metric() {
    local FILE=$1
    local METRIC=$2

    grep "$METRIC" "$FILE" 2>/dev/null | \
        awk '{print $1}' | \
        sed 's/,//g' | \
        head -1
}

# Function to calculate IPC
calculate_ipc() {
    local INSTRUCTIONS=$1
    local CYCLES=$2

    if [ -n "$INSTRUCTIONS" ] && [ -n "$CYCLES" ] && [ "$CYCLES" != "0" ]; then
        echo "scale=2; $INSTRUCTIONS / $CYCLES" | bc
    else
        echo "N/A"
    fi
}

# Function to calculate cache miss rate
calculate_cache_miss_rate() {
    local CACHE_MISSES=$1
    local CACHE_REFS=$2

    if [ -n "$CACHE_MISSES" ] && [ -n "$CACHE_REFS" ] && [ "$CACHE_REFS" != "0" ]; then
        echo "scale=2; 100 * $CACHE_MISSES / $CACHE_REFS" | bc
    else
        echo "N/A"
    fi
}

# Function to display metrics for a version
display_metrics() {
    local VERSION=$1
    local STATS_FILE="$RESULTS_DIR/${VERSION}_stats.txt"

    if [ ! -f "$STATS_FILE" ]; then
        echo "  ⚠️  Stats file not found: $STATS_FILE"
        echo "     Run: make profile"
        return 1
    fi

    echo "$VERSION Version:"
    echo "-------------------------------------------------------------------"

    # Extract metrics
    local INSTRUCTIONS=$(extract_metric "$STATS_FILE" "instructions")
    local CYCLES=$(extract_metric "$STATS_FILE" "cycles")
    local CACHE_REFS=$(extract_metric "$STATS_FILE" "cache-references")
    local CACHE_MISSES=$(extract_metric "$STATS_FILE" "cache-misses")
    local BRANCHES=$(extract_metric "$STATS_FILE" "branches")
    local BRANCH_MISSES=$(extract_metric "$STATS_FILE" "branch-misses")

    # Calculate derived metrics
    local IPC=$(calculate_ipc "$INSTRUCTIONS" "$CYCLES")
    local CACHE_MISS_RATE=$(calculate_cache_miss_rate "$CACHE_MISSES" "$CACHE_REFS")

    if [ -n "$BRANCH_MISSES" ] && [ -n "$BRANCHES" ] && [ "$BRANCHES" != "0" ]; then
        local BRANCH_MISS_RATE=$(echo "scale=2; 100 * $BRANCH_MISSES / $BRANCHES" | bc)
    else
        local BRANCH_MISS_RATE="N/A"
    fi

    # Format with commas for readability
    format_number() {
        echo "$1" | sed ':a;s/\B[0-9]\{3\}\>/,&/;ta'
    }

    echo "  Instructions:        $(format_number $INSTRUCTIONS)"
    echo "  Cycles:              $(format_number $CYCLES)"
    echo "  IPC:                 $IPC"
    echo ""
    echo "  Cache references:    $(format_number $CACHE_REFS)"
    echo "  Cache misses:        $(format_number $CACHE_MISSES)"
    echo "  Cache miss rate:     ${CACHE_MISS_RATE}%"
    echo ""
    echo "  Branches:            $(format_number $BRANCHES)"
    echo "  Branch misses:       $(format_number $BRANCH_MISSES)"
    echo "  Branch miss rate:    ${BRANCH_MISS_RATE}%"
    echo ""

    # Store for comparison
    local VERSION_UPPER=$(echo "$VERSION" | tr '[:lower:]' '[:upper:]')
    eval "${VERSION_UPPER}_IPC=$IPC"
    eval "${VERSION_UPPER}_CACHE_MISS_RATE=$CACHE_MISS_RATE"
    eval "${VERSION_UPPER}_BRANCH_MISS_RATE=$BRANCH_MISS_RATE"

    return 0
}

# Display baseline metrics
display_metrics "baseline"

# Display optimized metrics
if [ -f "$RESULTS_DIR/optimized_stats.txt" ]; then
    display_metrics "optimized"

    # Calculate improvements
    echo "==================================================================="
    echo "IMPROVEMENTS"
    echo "==================================================================="
    echo ""

    if [ -n "$BASELINE_IPC" ] && [ -n "$OPTIMIZED_IPC" ] && [ "$BASELINE_IPC" != "N/A" ] && [ "$OPTIMIZED_IPC" != "N/A" ]; then
        IPC_IMPROVEMENT=$(echo "scale=1; 100 * ($OPTIMIZED_IPC - $BASELINE_IPC) / $BASELINE_IPC" | bc)
        echo "IPC:              $BASELINE_IPC → $OPTIMIZED_IPC  (${IPC_IMPROVEMENT}% improvement)"
    fi

    if [ -n "$BASELINE_CACHE_MISS_RATE" ] && [ -n "$OPTIMIZED_CACHE_MISS_RATE" ] && [ "$BASELINE_CACHE_MISS_RATE" != "N/A" ] && [ "$OPTIMIZED_CACHE_MISS_RATE" != "N/A" ]; then
        CACHE_REDUCTION=$(echo "scale=1; 100 * ($BASELINE_CACHE_MISS_RATE - $OPTIMIZED_CACHE_MISS_RATE) / $BASELINE_CACHE_MISS_RATE" | bc)
        echo "Cache misses:     ${BASELINE_CACHE_MISS_RATE}% → ${OPTIMIZED_CACHE_MISS_RATE}%  (${CACHE_REDUCTION}% reduction)"
    fi

    if [ -n "$BASELINE_BRANCH_MISS_RATE" ] && [ -n "$OPTIMIZED_BRANCH_MISS_RATE" ] && [ "$BASELINE_BRANCH_MISS_RATE" != "N/A" ] && [ "$OPTIMIZED_BRANCH_MISS_RATE" != "N/A" ]; then
        BRANCH_REDUCTION=$(echo "scale=1; 100 * ($BASELINE_BRANCH_MISS_RATE - $OPTIMIZED_BRANCH_MISS_RATE) / $BASELINE_BRANCH_MISS_RATE" | bc)
        echo "Branch misses:    ${BASELINE_BRANCH_MISS_RATE}% → ${OPTIMIZED_BRANCH_MISS_RATE}%  (${BRANCH_REDUCTION}% reduction)"
    fi

    echo ""
fi

# Top functions comparison
echo "==================================================================="
echo "TOP FUNCTIONS BY CPU TIME"
echo "==================================================================="
echo ""

if [ -f "$RESULTS_DIR/baseline_report.txt" ]; then
    echo "Baseline:"
    echo "-------------------------------------------------------------------"
    grep -A 20 "Overhead.*Symbol" "$RESULTS_DIR/baseline_report.txt" | \
        head -25 | tail -20 | \
        awk '{printf "  %-10s %s\n", $1, $NF}' | \
        head -10
    echo ""
fi

if [ -f "$RESULTS_DIR/optimized_report.txt" ]; then
    echo "Optimized:"
    echo "-------------------------------------------------------------------"
    grep -A 20 "Overhead.*Symbol" "$RESULTS_DIR/optimized_report.txt" | \
        head -25 | tail -20 | \
        awk '{printf "  %-10s %s\n", $1, $NF}' | \
        head -10
    echo ""
fi

# Annotations summary
echo "==================================================================="
echo "HOT LINES (from annotations)"
echo "==================================================================="
echo ""

if [ -f "$RESULTS_DIR/baseline_annotate.txt" ]; then
    echo "Baseline - Top 10 hottest lines:"
    echo "-------------------------------------------------------------------"
    grep -E "^\s+[0-9]+\.[0-9]+.*:" "$RESULTS_DIR/baseline_annotate.txt" | \
        head -10 | \
        sed 's/^/  /'
    echo ""
fi

if [ -f "$RESULTS_DIR/optimized_annotate.txt" ]; then
    echo "Optimized - Top 10 hottest lines:"
    echo "-------------------------------------------------------------------"
    grep -E "^\s+[0-9]+\.[0-9]+.*:" "$RESULTS_DIR/optimized_annotate.txt" | \
        head -10 | \
        sed 's/^/  /'
    echo ""
fi

echo "==================================================================="
echo "SUMMARY"
echo "==================================================================="
echo ""
echo "Full reports available in: $RESULTS_DIR/"
echo ""
echo "Interactive analysis:"
echo "  perf report -i $RESULTS_DIR/perf_baseline.data"
echo "  perf report -i $RESULTS_DIR/perf_optimized.data"
echo ""
echo "Side-by-side flamegraphs:"
echo "  open $RESULTS_DIR/baseline_flame.svg"
echo "  open $RESULTS_DIR/optimized_flame.svg"
echo ""
