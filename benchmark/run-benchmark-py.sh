#!/bin/bash
set -e

# Configuration
#TEST_FILE="benchmark/test-files/small.ts"
TEST_FILE="benchmark/django-main"
INDEXER="./index-python"
DB_FILE="code-index.db"
DB_FILE2="code-index.db*"
RUNS=10

# Check prerequisites
if [ ! -f "$INDEXER" ]; then
    echo "Error: $INDEXER not found. Run 'make' first."
    exit 1
fi

if [ ! -f "$TEST_FILE" ] && [ ! -d "$TEST_FILE" ]; then
    echo "Error: $TEST_FILE not found."
    exit 1
fi

# Get file info
#LINE_COUNT=$(wc -l < "$TEST_FILE")

# Arrays to store results
times=()

# Run benchmark
for i in $(seq 1 $RUNS); do
    # Clean database
    rm -f "$DB_FILE"
    rm -f "$DB_FILE2"

    # Measure execution time
    start=$(date +%s%N)
    "$INDEXER" "$TEST_FILE" --once --quiet > /dev/null 2>&1
    end=$(date +%s%N)

    # Calculate elapsed time in seconds (with 3 decimal precision)
    elapsed=$(echo "scale=3; ($end - $start) / 1000000000" | bc)
    times+=("$elapsed")
done

# Get symbol count from database
SYMBOL_COUNT=$(sqlite3 "$DB_FILE" "SELECT COUNT(*) FROM code_index;" 2>/dev/null || echo "0")

# Calculate statistics
sum=0
min=${times[0]}
max=${times[0]}

for time in "${times[@]}"; do
    sum=$(echo "$sum + $time" | bc)

    # Update min
    if (( $(echo "$time < $min" | bc -l) )); then
        min=$time
    fi

    # Update max
    if (( $(echo "$time > $max" | bc -l) )); then
        max=$time
    fi
done

avg=$(echo "scale=4; $sum / $RUNS" | bc)

# Print results in compact format
echo "Benchmark: $TEST_FILE ($RUNS runs)"
echo "────────────────────────────────────────────────────────────"

# Print runs in two columns
for i in $(seq 0 4); do
    idx1=$i
    idx2=$((i + 5))
    printf "Run %2d: %ss   Run %2d: %ss\n" \
        $((idx1 + 1)) "${times[$idx1]}" \
        $((idx2 + 1)) "${times[$idx2]}"
done

echo "────────────────────────────────────────────────────────────"
printf "Avg: %ss  Min: %ss  Max: %ss  Symbols: %d\n" "$avg" "$min" "$max" "$SYMBOL_COUNT"
