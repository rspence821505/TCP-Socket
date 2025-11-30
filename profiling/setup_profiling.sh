#!/bin/bash
# Setup script for Exercise 8 profiling infrastructure

set -e  # Exit on error

echo "==================================================================="
echo "Profiling Infrastructure Setup"
echo "==================================================================="
echo ""

# Detect OS
OS="$(uname -s)"
echo "Detected OS: $OS"
echo ""

#=============================================================================
# 1. Check/Install perf
#=============================================================================

echo "==================================================================="
echo "Step 1: Checking for perf profiler"
echo "==================================================================="
echo ""

if command -v perf &> /dev/null; then
    echo "✅ perf is already installed"
    perf --version
else
    echo "❌ perf not found"
    echo ""
    
    if [[ "$OS" == "Linux" ]]; then
        echo "Installing perf on Linux..."
        echo ""
        echo "You may need to run:"
        echo "  sudo apt-get install linux-tools-common linux-tools-generic linux-tools-\$(uname -r)"
        echo ""
        read -p "Install now? (y/n) " -n 1 -r
        echo ""
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            sudo apt-get update
            sudo apt-get install -y linux-tools-common linux-tools-generic linux-tools-$(uname -r)
            echo "✅ perf installed"
        else
            echo "⚠️  Skipping perf installation. Install manually later."
        fi
    elif [[ "$OS" == "Darwin" ]]; then
        echo "⚠️  macOS doesn't have perf. Alternatives:"
        echo "   - Use Instruments (built-in)"
        echo "   - Use sample command"
        echo ""
        echo "Example with Instruments:"
        echo "  instruments -t 'Time Profiler' ./build/feed_handler_heartbeat_profile"
        echo ""
    fi
fi

echo ""

#=============================================================================
# 2. Check/Install FlameGraph tools
#=============================================================================

echo "==================================================================="
echo "Step 2: Checking for FlameGraph tools"
echo "==================================================================="
echo ""

FLAMEGRAPH_DIR="$HOME/FlameGraph"

if [ -d "$FLAMEGRAPH_DIR" ]; then
    echo "✅ FlameGraph tools found at $FLAMEGRAPH_DIR"
else
    echo "❌ FlameGraph tools not found"
    echo ""
    read -p "Clone FlameGraph repository to ~/FlameGraph? (y/n) " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        git clone https://github.com/brendangregg/FlameGraph.git "$FLAMEGRAPH_DIR"
        echo "✅ FlameGraph tools installed to $FLAMEGRAPH_DIR"
    else
        echo "⚠️  Skipping FlameGraph installation. Flamegraph generation will be unavailable."
        echo ""
        echo "To install later:"
        echo "  git clone https://github.com/brendangregg/FlameGraph.git ~/FlameGraph"
    fi
fi

echo ""

#=============================================================================
# 3. Configure perf permissions (Linux only)
#=============================================================================

if [[ "$OS" == "Linux" ]]; then
    echo "==================================================================="
    echo "Step 3: Configuring perf permissions"
    echo "==================================================================="
    echo ""
    
    CURRENT_PARANOID=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo "unknown")
    
    echo "Current perf_event_paranoid: $CURRENT_PARANOID"
    echo ""
    echo "Recommended value: -1 (allows profiling without sudo)"
    echo "Default value: 2-4 (requires sudo for profiling)"
    echo ""
    
    if [ "$CURRENT_PARANOID" != "-1" ]; then
        read -p "Set perf_event_paranoid to -1? (y/n) " -n 1 -r
        echo ""
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            # Temporary (until reboot)
            sudo sysctl -w kernel.perf_event_paranoid=-1
            
            # Permanent
            echo 'kernel.perf_event_paranoid=-1' | sudo tee -a /etc/sysctl.conf
            
            echo "✅ perf permissions configured"
            echo "   You can now run perf without sudo"
        else
            echo "⚠️  Skipping permission configuration"
            echo "   You'll need to run perf with sudo"
        fi
    else
        echo "✅ perf permissions already configured"
    fi
    
    echo ""
fi

#=============================================================================
# 4. Create directory structure
#=============================================================================

echo "==================================================================="
echo "Step 4: Creating directory structure"
echo "==================================================================="
echo ""

PROJECT_ROOT="$(pwd)"

mkdir -p "$PROJECT_ROOT/build"
mkdir -p "$PROJECT_ROOT/profiling_results"
mkdir -p "$PROJECT_ROOT/scripts"
mkdir -p "$PROJECT_ROOT/include"
mkdir -p "$PROJECT_ROOT/src"

echo "✅ Created directories:"
echo "   - build/"
echo "   - profiling_results/"
echo "   - scripts/"
echo "   - include/"
echo "   - src/"
echo ""

#=============================================================================
# 5. Make scripts executable
#=============================================================================

echo "==================================================================="
echo "Step 5: Making scripts executable"
echo "==================================================================="
echo ""

if [ -d "$PROJECT_ROOT/scripts" ]; then
    chmod +x "$PROJECT_ROOT"/scripts/*.sh 2>/dev/null || true
    echo "✅ Scripts are now executable"
else
    echo "⚠️  scripts/ directory not found"
fi

echo ""

#=============================================================================
# 6. Check compiler
#=============================================================================

echo "==================================================================="
echo "Step 6: Checking compiler"
echo "==================================================================="
echo ""

if command -v g++ &> /dev/null; then
    echo "✅ g++ found"
    g++ --version | head -1
else
    echo "❌ g++ not found"
    echo ""
    if [[ "$OS" == "Linux" ]]; then
        echo "Install with: sudo apt-get install g++"
    elif [[ "$OS" == "Darwin" ]]; then
        echo "Install Xcode Command Line Tools: xcode-select --install"
    fi
fi

echo ""

if command -v clang++ &> /dev/null; then
    echo "✅ clang++ found"
    clang++ --version | head -1
else
    echo "ℹ️  clang++ not found (optional)"
fi

echo ""

#=============================================================================
# 7. Verify build system
#=============================================================================

echo "==================================================================="
echo "Step 7: Verifying build system"
echo "==================================================================="
echo ""

if [ -f "$PROJECT_ROOT/Makefile" ]; then
    echo "✅ Makefile found"
    
    # Try to build
    echo ""
    echo "Testing build system..."
    if make -n profiling &> /dev/null; then
        echo "✅ Build system is ready"
    else
        echo "⚠️  Build system may have issues. Check Makefile."
    fi
else
    echo "⚠️  Makefile not found"
    echo "   Copy Makefile to $PROJECT_ROOT"
fi

echo ""

#=============================================================================
# 8. System information
#=============================================================================

echo "==================================================================="
echo "Step 8: System Information"
echo "==================================================================="
echo ""

echo "CPU Info:"
if [[ "$OS" == "Linux" ]]; then
    lscpu | grep -E "Model name|CPU\(s\)|Thread|Core|MHz" || \
    cat /proc/cpuinfo | grep -m 1 "model name"
elif [[ "$OS" == "Darwin" ]]; then
    sysctl -n machdep.cpu.brand_string
    sysctl -n hw.ncpu | xargs echo "CPUs:"
fi

echo ""
echo "Memory:"
if [[ "$OS" == "Linux" ]]; then
    free -h | grep -E "Mem:"
elif [[ "$OS" == "Darwin" ]]; then
    sysctl -n hw.memsize | awk '{print $0/1024/1024/1024 " GB"}'
fi

echo ""

#=============================================================================
# Summary
#=============================================================================

echo "==================================================================="
echo "Setup Complete!"
echo "==================================================================="
echo ""
echo "✅ Environment is ready for Exercise 8"
echo ""
echo "Next steps:"
echo "  1. Copy your source files to src/ and include/"
echo "  2. Build profiling binaries:  make profiling"
echo "  3. Run profiling analysis:    ./profiling/profile_feed_handler.sh"
echo "  4. Compare results:           ./profiling/compare_profiling_results.sh"
echo ""
echo "Documentation:"
echo "  - PROFILING_README.md     - Complete profiling guide"
echo "  - OPTIMIZATION_GUIDE.md   - Optimization techniques"
echo ""
echo "Quick reference:"
echo "  make profiling           - Build baseline + optimized"
echo "  make profile             - Run profiling (30 sec)"
echo "  make compare             - Compare results"
echo "  make benchmark           - Quick throughput test"
echo ""

# Export environment variables
echo "Setting environment variables..."
echo "export FLAMEGRAPH_DIR=$FLAMEGRAPH_DIR" >> ~/.bashrc 2>/dev/null || true
echo "export PROJECT_ROOT=$PROJECT_ROOT" >> ~/.bashrc 2>/dev/null || true

echo ""
echo "Environment variables set in ~/.bashrc"
echo "Run 'source ~/.bashrc' or restart your terminal"
echo ""
