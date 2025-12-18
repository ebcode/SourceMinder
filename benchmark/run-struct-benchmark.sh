#!/bin/bash
set -e

# Benchmark: Compare original vs optimized struct layout
# Tests cache line optimization hypothesis from performance analysis

RUNS=10
ORIGINAL_BIN="./benchmark/struct-original"
OPTIMIZED_BIN="./benchmark/struct-optimized"

echo "Compiling struct layout benchmarks..."
echo "────────────────────────────────────────────────────────────"

# Compile both versions with same optimization flags
gcc -O3 -march=native benchmark/struct-layout-original.c -o "$ORIGINAL_BIN"
gcc -O3 -march=native benchmark/struct-layout-optimized.c -o "$OPTIMIZED_BIN"

echo "✓ Compiled with -O3 -march=native"
echo ""

# Arrays to store results
original_times=()
optimized_times=()

echo "Running benchmarks ($RUNS runs each)..."
echo "────────────────────────────────────────────────────────────"

# Run original layout benchmark
for i in $(seq 1 $RUNS); do
    output=$("$ORIGINAL_BIN")
    time=$(echo "$output" | grep "Original layout:" | awk '{print $3}')
    original_times+=("$time")
    printf "Original run %2d: %ss\n" "$i" "$time"
done

echo ""

# Run optimized layout benchmark
for i in $(seq 1 $RUNS); do
    output=$("$OPTIMIZED_BIN")
    time=$(echo "$output" | grep "Optimized layout:" | awk '{print $3}')
    optimized_times+=("$time")
    printf "Optimized run %2d: %ss\n" "$i" "$time"
done

echo ""
echo "────────────────────────────────────────────────────────────"

# Calculate statistics for original
original_sum=0
original_min=${original_times[0]}
original_max=${original_times[0]}

for time in "${original_times[@]}"; do
    original_sum=$(echo "$original_sum + $time" | bc)
    if (( $(echo "$time < $original_min" | bc -l) )); then
        original_min=$time
    fi
    if (( $(echo "$time > $original_max" | bc -l) )); then
        original_max=$time
    fi
done

original_avg=$(echo "scale=4; $original_sum / $RUNS" | bc)

# Calculate statistics for optimized
optimized_sum=0
optimized_min=${optimized_times[0]}
optimized_max=${optimized_times[0]}

for time in "${optimized_times[@]}"; do
    optimized_sum=$(echo "$optimized_sum + $time" | bc)
    if (( $(echo "$time < $optimized_min" | bc -l) )); then
        optimized_min=$time
    fi
    if (( $(echo "$time > $optimized_max" | bc -l) )); then
        optimized_max=$time
    fi
done

optimized_avg=$(echo "scale=4; $optimized_sum / $RUNS" | bc)

# Calculate speedup relative to original (baseline)
speedup=$(echo "scale=2; (1 - $optimized_avg / $original_avg) * 100" | bc)

# Print results
echo "RESULTS ($RUNS runs each):"
echo ""
printf "Original layout:  Avg: %ss  Min: %ss  Max: %ss  (baseline)\n" \
    "$original_avg" "$original_min" "$original_max"
printf "Optimized layout: Avg: %ss  Min: %ss  Max: %ss\n" \
    "$optimized_avg" "$optimized_min" "$optimized_max"
echo ""

# Speedup comparison
if (( $(echo "$speedup > 0" | bc -l) )); then
    printf "✓ Optimized layout is %.1f%% faster (hot fields first wins!)\n" "$speedup"
elif (( $(echo "$speedup < 0" | bc -l) )); then
    speedup_negative=$(echo "$speedup * -1" | bc)
    printf "✗ Optimized layout is %.1f%% slower (no benefit)\n" "$speedup_negative"
else
    echo "≈ Performance is equivalent"
fi

echo ""
echo "────────────────────────────────────────────────────────────"
echo "Analysis:"
echo "- 10M filter operations per run"
echo "- Filtering accesses only: line, context, is_definition (hot fields)"
echo "- Original: ~3,000 byte struct, hot fields scattered"
echo "- Optimized: Hot fields in first 64 bytes (1 cache line)"
echo ""
echo "Expected result: 5-15% speedup if cache line optimization matters"
echo "Actual result: See above"

# Cleanup
rm -f "$ORIGINAL_BIN" "$OPTIMIZED_BIN"
