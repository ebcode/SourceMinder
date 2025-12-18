#!/bin/bash

ANCHOR="type_size"
PATTERN2="source_code"
PATTERN3="parseresult"
RANGE=50
ITERATIONS=20

echo "=== Benchmarking with 3 patterns ==="
echo "Query: $ANCHOR + $PATTERN2 + $PATTERN3 within ±$RANGE lines"
echo "Iterations: $ITERATIONS"
echo ""

echo "Two-step approach:"
start=$(date +%s%N)
for i in $(seq 1 $ITERATIONS); do
    ./scratch/tools/test_line_range "$ANCHOR" "$PATTERN2" "$PATTERN3" $RANGE > /dev/null 2>&1
done
end=$(date +%s%N)
two_step_total=$((($end - $start) / 1000000))
two_step_avg=$(($two_step_total / $ITERATIONS))
echo "  Average: ${two_step_avg}ms"

echo ""
echo "CTE approach:"
start=$(date +%s%N)
for i in $(seq 1 $ITERATIONS); do
    ./scratch/tools/test_line_range_cte "$ANCHOR" "$PATTERN2" "$PATTERN3" $RANGE > /dev/null 2>&1
done
end=$(date +%s%N)
cte_total=$((($end - $start) / 1000000))
cte_avg=$(($cte_total / $ITERATIONS))
echo "  Average: ${cte_avg}ms"

echo ""
if [ $cte_avg -lt $two_step_avg ]; then
    improvement=$((($two_step_avg - $cte_avg) * 100 / $two_step_avg))
    echo "Winner: CTE (${improvement}% faster)"
else
    improvement=$((($cte_avg - $two_step_avg) * 100 / $cte_avg))
    echo "Winner: Two-step (${improvement}% faster)"
fi
