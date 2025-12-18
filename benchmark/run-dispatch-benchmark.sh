#!/bin/bash
set -e

# Benchmark: Compare if-else vs jump table vs switch dispatch performance
# Tests the hypothesis from performance_analysis_20251205_103655.md

RUNS=10
IFELSE_BIN="./benchmark/test-ifelse"
JUMPTABLE_BIN="./benchmark/test-jump"
SWITCH_BIN="./benchmark/test-switch"

echo "Running dispatch benchmarks ($RUNS runs each)..."
echo "────────────────────────────────────────────────────────────"

# Arrays to store results
ifelse_times=()
jumptable_times=()
switch_times=()

# Run if-else benchmark
for i in $(seq 1 $RUNS); do
    output=$("$IFELSE_BIN")
    time=$(echo "$output" | grep "If-else dispatch:" | awk '{print $3}')
    ifelse_times+=("$time")
    printf "If-else run %2d: %ss\n" "$i" "$time"
done

echo ""

# Run jump table benchmark
for i in $(seq 1 $RUNS); do
    output=$("$JUMPTABLE_BIN")
    time=$(echo "$output" | grep "Jump table dispatch:" | awk '{print $4}')
    jumptable_times+=("$time")
    printf "Jump table run %2d: %ss\n" "$i" "$time"
done

echo ""

# Run switch benchmark
for i in $(seq 1 $RUNS); do
    output=$("$SWITCH_BIN")
    time=$(echo "$output" | grep "Switch dispatch:" | awk '{print $3}')
    switch_times+=("$time")
    printf "Switch run %2d: %ss\n" "$i" "$time"
done

echo ""
echo "────────────────────────────────────────────────────────────"

# Calculate statistics for if-else
ifelse_sum=0
ifelse_min=${ifelse_times[0]}
ifelse_max=${ifelse_times[0]}

for time in "${ifelse_times[@]}"; do
    ifelse_sum=$(echo "$ifelse_sum + $time" | bc)
    if (( $(echo "$time < $ifelse_min" | bc -l) )); then
        ifelse_min=$time
    fi
    if (( $(echo "$time > $ifelse_max" | bc -l) )); then
        ifelse_max=$time
    fi
done

ifelse_avg=$(echo "scale=4; $ifelse_sum / $RUNS" | bc)

# Calculate statistics for jump table
jumptable_sum=0
jumptable_min=${jumptable_times[0]}
jumptable_max=${jumptable_times[0]}

for time in "${jumptable_times[@]}"; do
    jumptable_sum=$(echo "$jumptable_sum + $time" | bc)
    if (( $(echo "$time < $jumptable_min" | bc -l) )); then
        jumptable_min=$time
    fi
    if (( $(echo "$time > $jumptable_max" | bc -l) )); then
        jumptable_max=$time
    fi
done

jumptable_avg=$(echo "scale=4; $jumptable_sum / $RUNS" | bc)

# Calculate statistics for switch
switch_sum=0
switch_min=${switch_times[0]}
switch_max=${switch_times[0]}

for time in "${switch_times[@]}"; do
    switch_sum=$(echo "$switch_sum + $time" | bc)
    if (( $(echo "$time < $switch_min" | bc -l) )); then
        switch_min=$time
    fi
    if (( $(echo "$time > $switch_max" | bc -l) )); then
        switch_max=$time
    fi
done

switch_avg=$(echo "scale=4; $switch_sum / $RUNS" | bc)

# Calculate speedup relative to if-else (baseline)
jumptable_speedup=$(echo "scale=2; (1 - $jumptable_avg / $ifelse_avg) * 100" | bc)
switch_speedup=$(echo "scale=2; (1 - $switch_avg / $ifelse_avg) * 100" | bc)

# Print results
echo "RESULTS ($RUNS runs each):"
echo ""
printf "If-else chain:    Avg: %ss  Min: %ss  Max: %ss  (baseline)\n" \
    "$ifelse_avg" "$ifelse_min" "$ifelse_max"
printf "Jump table:       Avg: %ss  Min: %ss  Max: %ss\n" \
    "$jumptable_avg" "$jumptable_min" "$jumptable_max"
printf "Switch:           Avg: %ss  Min: %ss  Max: %ss\n" \
    "$switch_avg" "$switch_min" "$switch_max"
echo ""

# Jump table comparison
if (( $(echo "$jumptable_speedup > 0" | bc -l) )); then
    printf "Jump table: %.1f%% faster than if-else\n" "$jumptable_speedup"
elif (( $(echo "$jumptable_speedup < 0" | bc -l) )); then
    speedup_negative=$(echo "$jumptable_speedup * -1" | bc)
    printf "Jump table: %.1f%% slower than if-else\n" "$speedup_negative"
else
    echo "Jump table: equivalent to if-else"
fi

# Switch comparison
if (( $(echo "$switch_speedup > 0" | bc -l) )); then
    printf "Switch:     %.1f%% faster than if-else\n" "$switch_speedup"
elif (( $(echo "$switch_speedup < 0" | bc -l) )); then
    speedup_negative=$(echo "$switch_speedup * -1" | bc)
    printf "Switch:     %.1f%% slower than if-else\n" "$speedup_negative"
else
    echo "Switch:     equivalent to if-else"
fi

echo ""
echo "────────────────────────────────────────────────────────────"
echo "Analysis:"
echo "- 10M dispatch operations per run"
echo "- Random node type distribution (uniform)"
echo "- Compiled with -O3 -march=native"
echo "- Fixed random seed for reproducibility"
echo ""
echo "Hypothesis from performance analysis: Jump table should be 20-40% faster"
echo "Actual result: See above (empirical testing beats theory!)"
