#!/bin/bash
# Generate a markdown template for documenting optimization work

OUTPUT_FILE="${1:-OPTIMIZATION_JOURNAL.md}"

cat > "$OUTPUT_FILE" << 'EOF'
# Optimization Journal


---

## Baseline Measurements

### Profiling Run 1 (Baseline)
**Date**: `date`  
**Duration**: 30 seconds  
**Binary**: `feed_handler_heartbeat_profile`

#### Throughput
- Messages received: ______
- Throughput: ______ msgs/sec

#### CPU Metrics (from `perf stat`)
```
Instructions:        _____________
Cycles:              _____________
IPC:                 _____________

Cache references:    _____________
Cache misses:        _____________
Cache miss rate:     _______ %

Branches:            _____________
Branch misses:       _____________
Branch miss rate:    _______ %
```

#### Top 5 Functions (from `perf report`)
```
1. _______ %    _______________________________
2. _______ %    _______________________________
3. _______ %    _______________________________
4. _______ %    _______________________________
5. _______ %    _______________________________
```

#### Initial Analysis
**What's hot?**
- [ ] String allocations
- [ ] std::optional overhead
- [ ] Non-inlined functions
- [ ] Branch mispredictions
- [ ] Cache misses
- [ ] Other: _______________________

**Primary bottleneck**: _________________________________

**Optimization plan**:
1. _________________________________________________
2. _________________________________________________
3. _________________________________________________

---

## Optimization 1: [Name of Optimization]

### Description
[Describe what you changed and why]

### Code Changes
**Before**:
```cpp
// Paste original code
```

**After**:
```cpp
// Paste optimized code
```

### Expected Impact
- [ ] Reduce string allocations
- [ ] Remove std::optional checks
- [ ] Inline hot function
- [ ] Improve branch prediction
- [ ] Improve cache locality
- [ ] Other: _______________________

### Results
**Throughput**: ______ msgs/sec → ______ msgs/sec (_____ % change)  
**IPC**: ______ → ______ (_____ % change)  
**Cache miss rate**: ______ % → ______ % (_____ % change)

**Top function changes**:
- `function_name`: _____ % → _____ %

### Lessons Learned
[What worked? What didn't? What surprised you?]

---

## Optimization 2: [Name of Optimization]

### Description
[Describe what you changed and why]

### Code Changes
**Before**:
```cpp
// Paste original code
```

**After**:
```cpp
// Paste optimized code
```

### Expected Impact
- [ ] Reduce string allocations
- [ ] Remove std::optional checks
- [ ] Inline hot function
- [ ] Improve branch prediction
- [ ] Improve cache locality
- [ ] Other: _______________________

### Results
**Throughput**: ______ msgs/sec → ______ msgs/sec (_____ % change)  
**IPC**: ______ → ______ (_____ % change)  
**Cache miss rate**: ______ % → ______ % (_____ % change)

**Top function changes**:
- `function_name`: _____ % → _____ %

### Lessons Learned
[What worked? What didn't? What surprised you?]

---

## Optimization 3: [Name of Optimization]

### Description
[Describe what you changed and why]

### Code Changes
**Before**:
```cpp
// Paste original code
```

**After**:
```cpp
// Paste optimized code
```

### Expected Impact
- [ ] Reduce string allocations
- [ ] Remove std::optional checks
- [ ] Inline hot function
- [ ] Improve branch prediction
- [ ] Improve cache locality
- [ ] Other: _______________________

### Results
**Throughput**: ______ msgs/sec → ______ msgs/sec (_____ % change)  
**IPC**: ______ → ______ (_____ % change)  
**Cache miss rate**: ______ % → ______ % (_____ % change)

**Top function changes**:
- `function_name`: _____ % → _____ %

### Lessons Learned
[What worked? What didn't? What surprised you?]

---

## Final Results

### Comparison: Baseline vs Optimized

| Metric | Baseline | Optimized | Change |
|--------|----------|-----------|--------|
| Throughput | ______ msgs/sec | ______ msgs/sec | +______ % |
| IPC | ______ | ______ | +______ % |
| Cache miss rate | ______ % | ______ % | ______ % |
| Branch miss rate | ______ % | ______ % | ______ % |

### Target Achievement
- [x] Throughput > 280k msgs/sec
- [x] IPC > 2.0
- [x] Cache miss rate < 2%
- [x] Branch miss rate < 5%

### Top Functions Before/After

**Baseline**:
```
1. _______ %    _______________________________
2. _______ %    _______________________________
3. _______ %    _______________________________
4. _______ %    _______________________________
5. _______ %    _______________________________
```

**Optimized**:
```
1. _______ %    _______________________________
2. _______ %    _______________________________
3. _______ %    _______________________________
4. _______ %    _______________________________
5. _______ %    _______________________________
```

---

## Key Insights

### What I Learned About Performance
1. _________________________________________________
2. _________________________________________________
3. _________________________________________________

### Most Impactful Optimizations
1. _________________________________________________
2. _________________________________________________
3. _________________________________________________

### Surprises
1. _________________________________________________
2. _________________________________________________

### Trade-offs
1. _________________________________________________
2. _________________________________________________

---

## Interview Talking Points

### Technical Depth
- "I profiled the feed handler using perf and identified..."
- "The bottleneck was in ____________ which took ___% of CPU time"
- "I applied ____________ optimization which improved throughput by ____%"

### Methodology
- "I used a data-driven approach: profile → hypothesize → optimize → measure"
- "Each optimization was verified with perf stat showing IPC improvement"
- "I prioritized hot paths that took >10% of CPU time"

### Results
- "Achieved ____% throughput improvement (___k → ___k msgs/sec)"
- "Reduced cache miss rate from ___% to ___%"
- "Final profile shows recv() dominating, indicating we're network-bound"

---

## Appendix: Profiling Commands

### Profiling
```bash
# Record baseline
perf record -F 999 -g --call-graph dwarf \
  -o profiling_results/perf_baseline.data \
  -- ./build/feed_handler_heartbeat_profile 9999

# Generate report
perf report -i profiling_results/perf_baseline.data --stdio
```

### Statistics
```bash
perf stat -e cache-references,cache-misses,instructions,cycles,branches,branch-misses \
  ./build/feed_handler_heartbeat_profile 9999
```

### Flamegraph
```bash
perf script -i profiling_results/perf_baseline.data | \
  ~/FlameGraph/stackcollapse-perf.pl | \
  ~/FlameGraph/flamegraph.pl > baseline_flame.svg
```

---

## Files Generated
- `profiling_results/perf_baseline.data` - Baseline profiling data
- `profiling_results/perf_optimized.data` - Optimized profiling data
- `profiling_results/baseline_flame.svg` - Baseline flamegraph
- `profiling_results/optimized_flame.svg` - Optimized flamegraph
- `profiling_results/baseline_report.txt` - Baseline perf report
- `profiling_results/optimized_report.txt` - Optimized perf report

EOF

chmod +x "$OUTPUT_FILE"

echo "✅ Optimization journal template created: $OUTPUT_FILE"
echo ""
echo "Fill in the template as you:"
echo "  1. Run baseline profiling"
echo "  2. Apply each optimization"
echo "  3. Measure improvements"
echo "  4. Document lessons learned"
echo ""
echo "This becomes your portfolio piece for interviews!"
echo ""
