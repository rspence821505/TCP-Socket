#!/bin/bash
# Sanity check script - verifies profiling infrastructure is ready

set -e

echo "==================================================================="
echo "Profiling Infrastructure Sanity Check"
echo "==================================================================="
echo ""

ERRORS=0
WARNINGS=0

# Color codes
RED='\033[0;31m'
YELLOW='\033[1;33m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

error() {
    echo -e "${RED}❌ ERROR${NC}: $1"
    ((ERRORS++))
}

warning() {
    echo -e "${YELLOW}⚠️  WARNING${NC}: $1"
    ((WARNINGS++))
}

success() {
    echo -e "${GREEN}✅${NC} $1"
}

#=============================================================================
# Check 1: Required tools
#=============================================================================

echo "Checking required tools..."
echo "-------------------------------------------------------------------"

# perf
if command -v perf &> /dev/null; then
    success "perf found: $(perf --version 2>&1 | head -1)"
else
    error "perf not found. Install with: sudo apt-get install linux-tools-\$(uname -r)"
fi

# g++
if command -v g++ &> /dev/null; then
    success "g++ found: $(g++ --version | head -1)"
else
    error "g++ not found. Install with: sudo apt-get install g++"
fi

# make
if command -v make &> /dev/null; then
    success "make found: $(make --version | head -1)"
else
    error "make not found. Install with: sudo apt-get install build-essential"
fi

echo ""

#=============================================================================
# Check 2: Directory structure
#=============================================================================

echo "Checking directory structure..."
echo "-------------------------------------------------------------------"

PROJECT_ROOT="${PROJECT_ROOT:-$(pwd)}"

for dir in build profiling_results scripts include src; do
    if [ -d "$PROJECT_ROOT/$dir" ]; then
        success "$dir/ directory exists"
    else
        warning "$dir/ directory missing. Run: mkdir -p $dir"
    fi
done

echo ""

#=============================================================================
# Check 3: Required files
#=============================================================================

echo "Checking required files..."
echo "-------------------------------------------------------------------"

# Makefile
if [ -f "$PROJECT_ROOT/Makefile" ]; then
    success "Makefile exists"
    
    # Check for profiling target
    if grep -q "^profiling:" "$PROJECT_ROOT/Makefile"; then
        success "  - Contains 'profiling' target"
    else
        warning "  - Missing 'profiling' target in Makefile"
    fi
else
    error "Makefile not found"
fi

# Scripts - profiling scripts
for script in profile_feed_handler.sh compare_profiling_results.sh; do
    if [ -f "$PROJECT_ROOT/profiling/$script" ]; then
        success "profiling/$script exists"
        if [ -x "$PROJECT_ROOT/profiling/$script" ]; then
            success "  - Is executable"
        else
            warning "  - Not executable. Run: chmod +x profiling/$script"
        fi
    else
        warning "profiling/$script missing"
    fi
done

# Scripts - benchmark scripts
for script in benchmark_throughput.sh; do
    if [ -f "$PROJECT_ROOT/benchmarks/$script" ]; then
        success "benchmarks/$script exists"
        if [ -x "$PROJECT_ROOT/benchmarks/$script" ]; then
            success "  - Is executable"
        else
            warning "  - Not executable. Run: chmod +x benchmarks/$script"
        fi
    else
        warning "benchmarks/$script missing"
    fi
done

echo ""

#=============================================================================
# Check 4: Source files
#=============================================================================

echo "Checking source files..."
echo "-------------------------------------------------------------------"

# Headers
if [ -d "$PROJECT_ROOT/include" ]; then
    HEADER_COUNT=$(find "$PROJECT_ROOT/include" -name "*.hpp" | wc -l)
    if [ "$HEADER_COUNT" -gt 0 ]; then
        success "Found $HEADER_COUNT header file(s) in include/"
    else
        warning "No header files found in include/"
    fi
fi

# Source
if [ -d "$PROJECT_ROOT/src" ]; then
    SOURCE_COUNT=$(find "$PROJECT_ROOT/src" -name "*.cpp" | wc -l)
    if [ "$SOURCE_COUNT" -gt 0 ]; then
        success "Found $SOURCE_COUNT source file(s) in src/"
    else
        warning "No source files found in src/"
    fi
fi

# Optimized versions
if [ -f "$PROJECT_ROOT/include/sequence_tracker_optimized.hpp" ]; then
    success "sequence_tracker_optimized.hpp exists"
else
    warning "sequence_tracker_optimized.hpp missing (needed for optimized build)"
fi

# feed_handler_heartbeat.cpp now contains the optimized implementation
if [ -f "$PROJECT_ROOT/src/feed_handler_heartbeat.cpp" ]; then
    success "feed_handler_heartbeat.cpp exists (with optimizations)"
else
    warning "feed_handler_heartbeat.cpp missing"
fi

echo ""

#=============================================================================
# Check 5: FlameGraph tools
#=============================================================================

echo "Checking FlameGraph tools..."
echo "-------------------------------------------------------------------"

FLAMEGRAPH_DIR="${FLAMEGRAPH_DIR:-$HOME/FlameGraph}"

if [ -d "$FLAMEGRAPH_DIR" ]; then
    success "FlameGraph directory found: $FLAMEGRAPH_DIR"
    
    for script in stackcollapse-perf.pl flamegraph.pl; do
        if [ -f "$FLAMEGRAPH_DIR/$script" ]; then
            success "  - $script exists"
        else
            error "  - $script missing"
        fi
    done
else
    warning "FlameGraph tools not found at $FLAMEGRAPH_DIR"
    echo "    Install with: git clone https://github.com/brendangregg/FlameGraph.git ~/FlameGraph"
fi

echo ""

#=============================================================================
# Check 6: perf permissions
#=============================================================================

echo "Checking perf permissions..."
echo "-------------------------------------------------------------------"

if [ -f /proc/sys/kernel/perf_event_paranoid ]; then
    PARANOID=$(cat /proc/sys/kernel/perf_event_paranoid)
    
    if [ "$PARANOID" -le 0 ]; then
        success "perf_event_paranoid = $PARANOID (can profile without sudo)"
    else
        warning "perf_event_paranoid = $PARANOID (may need sudo for profiling)"
        echo "    To fix: sudo sysctl -w kernel.perf_event_paranoid=-1"
    fi
else
    warning "Cannot check perf_event_paranoid (not on Linux?)"
fi

echo ""

#=============================================================================
# Check 7: Build test
#=============================================================================

echo "Testing build system..."
echo "-------------------------------------------------------------------"

cd "$PROJECT_ROOT"

if make -n profiling &> /dev/null; then
    success "Build system is configured correctly"
    
    # Try dry-run to see what would be built
    echo ""
    echo "Build plan:"
    make -n profiling 2>&1 | grep -E "^(g\+\+|clang)" | sed 's/^/  /'
else
    error "Build system has issues. Check Makefile."
fi

echo ""

#=============================================================================
# Check 8: Documentation
#=============================================================================

echo "Checking documentation..."
echo "-------------------------------------------------------------------"

for doc in PROFILING_README.md OPTIMIZATION_GUIDE.md RESULTS_INTERPRETATION.md; do
    if [ -f "$PROJECT_ROOT/$doc" ]; then
        success "$doc exists"
    else
        warning "$doc missing (helpful but not required)"
    fi
done

echo ""

#=============================================================================
# Summary
#=============================================================================

echo "==================================================================="
echo "SUMMARY"
echo "==================================================================="
echo ""

if [ $ERRORS -eq 0 ] && [ $WARNINGS -eq 0 ]; then
    echo -e "${GREEN}✅ All checks passed!${NC}"
    echo ""
    echo "Your profiling infrastructure is ready!"
    echo ""
    echo "Next steps:"
    echo "  1. Build:    make profiling"
    echo "  2. Profile:  ./profiling/profile_feed_handler.sh"
    echo "  3. Compare:  ./profiling/compare_profiling_results.sh"
    echo ""
elif [ $ERRORS -eq 0 ]; then
    echo -e "${YELLOW}⚠️  ${WARNINGS} warning(s) found${NC}"
    echo ""
    echo "Your infrastructure is mostly ready, but some optional components are missing."
    echo "Review warnings above and fix if needed."
    echo ""
else
    echo -e "${RED}❌ ${ERRORS} error(s) and ${WARNINGS} warning(s) found${NC}"
    echo ""
    echo "Please fix the errors above before proceeding."
    echo ""
    echo "Common fixes:"
    echo "  - Install perf: sudo apt-get install linux-tools-\$(uname -r)"
    echo "  - Create directories: mkdir -p build profiling_results scripts include src"
    echo "  - Copy Makefile to project root"
    echo "  - Make scripts executable: chmod +x scripts/*.sh"
    echo ""
    exit 1
fi

# System info
echo "System Information:"
echo "-------------------------------------------------------------------"
echo "OS: $(uname -s)"
echo "Kernel: $(uname -r)"
echo "CPU: $(lscpu 2>/dev/null | grep 'Model name' | cut -d: -f2 | xargs || sysctl -n machdep.cpu.brand_string 2>/dev/null)"
echo "Cores: $(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null)"
echo ""

exit 0
