#!/bin/bash

# Benchmark script for comparing two-step vs CTE approach

ANCHOR="type_size"
SECONDARY="source_code"
RANGE=30
ITERATIONS=20

echo "=== Benchmarking Line Range Search ==="
echo "Query: $ANCHOR + $SECONDARY within ±$RANGE lines"
echo "Iterations: $ITERATIONS"
echo ""

# Benchmark two-step approach
echo "Two-step approach (N+1 queries):"
start=$(date +%s%N)
for i in $(seq 1 $ITERATIONS); do
    ./scratch/tools/test_line_range "$ANCHOR" "$SECONDARY" $RANGE > /dev/null 2>&1
done
end=$(date +%s%N)
two_step_total=$((($end - $start) / 1000000))
two_step_avg=$(($two_step_total / $ITERATIONS))
echo "  Total time: ${two_step_total}ms"
echo "  Average per run: ${two_step_avg}ms"
echo ""

# Benchmark CTE approach
echo "CTE approach (single query):"
start=$(date +%s%N)
for i in $(seq 1 $ITERATIONS); do
    ./scratch/tools/test_line_range_cte "$ANCHOR" "$SECONDARY" $RANGE > /dev/null 2>&1
done
end=$(date +%s%N)
cte_total=$((($end - $start) / 1000000))
cte_avg=$(($cte_total / $ITERATIONS))
echo "  Total time: ${cte_total}ms"
echo "  Average per run: ${cte_avg}ms"
echo ""

# Calculate speedup
if [ $cte_avg -lt $two_step_avg ]; then
    speedup=$((($two_step_avg * 100) / $cte_avg))
    echo "Result: CTE is ${speedup}% the speed of two-step ($(($speedup - 100))% faster)"
else
    speedup=$((($cte_avg * 100) / $two_step_avg))
    echo "Result: Two-step is ${speedup}% the speed of CTE ($(($speedup - 100))% faster)"
fi
