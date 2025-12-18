#!/bin/bash
# Performance Investigation Script
# Analyzes cache behavior and struct access patterns for IndexEntry

set -e

# Create tmp directory if it doesn't exist
mkdir -p ./tmp

echo "=========================================="
echo "IndexEntry Performance Investigation"
echo "=========================================="
echo ""

# Check if perf is available
if ! command -v perf &> /dev/null; then
    echo "WARNING: 'perf' not found. Install with: sudo apt-get install linux-tools-generic"
    echo "Skipping perf analysis..."
    HAVE_PERF=0
else
    HAVE_PERF=1
fi

# Check if valgrind is available
if ! command -v valgrind &> /dev/null; then
    echo "WARNING: 'valgrind' not found. Install with: sudo apt-get install valgrind"
    echo "Skipping cachegrind analysis..."
    HAVE_VALGRIND=0
else
    HAVE_VALGRIND=1
fi

echo ""
echo "=========================================="
echo "Test 1: Cache Miss Analysis (Indexing)"
echo "=========================================="
echo ""

if [ $HAVE_VALGRIND -eq 1 ]; then
    # Test indexing performance with cachegrind
    rm -f code-index.db
    echo "Running cachegrind on indexing operation..."
    valgrind --tool=cachegrind --cachegrind-out-file=./tmp/cachegrind-index.out \
        ./index-c ./shared ./c --once --verbose 2>&1 | grep -E "(Indexed|Found)" | head -20

    echo ""
    echo "Cache statistics for INDEXING:"
    cg_annotate ./tmp/cachegrind-index.out 2>&1 | head -50

    echo ""
    echo "Hottest functions (indexing):"
    cg_annotate ./tmp/cachegrind-index.out 2>&1 | grep -E "db_insert|sqlite3_bind" | head -20
fi

echo ""
echo "=========================================="
echo "Test 2: Cache Miss Analysis (Querying)"
echo "=========================================="
echo ""

if [ $HAVE_VALGRIND -eq 1 ]; then
    # Make sure we have an indexed database
    if [ ! -f code-index.db ]; then
        echo "Building index first..."
        ./index-c ./shared ./c --once --silent
    fi

    echo "Running cachegrind on query operation..."
    valgrind --tool=cachegrind --cachegrind-out-file=./tmp/cachegrind-query.out \
        ./qi '*' -i func --limit 100 2>&1 > /dev/null

    echo ""
    echo "Cache statistics for QUERYING:"
    cg_annotate ./tmp/cachegrind-query.out 2>&1 | head -50

    echo ""
    echo "Hottest functions (querying):"
    cg_annotate ./tmp/cachegrind-query.out 2>&1 | grep -E "print_|filter_|sqlite3" | head -20
fi

echo ""
echo "=========================================="
echo "Test 3: CPU Hotspot Analysis (perf)"
echo "=========================================="
echo ""

if [ $HAVE_PERF -eq 1 ]; then
    # Indexing hotspots
    rm -f code-index.db
    echo "Profiling indexing with perf..."
    sudo perf record -g -o ./tmp/perf-index.data ./index-c ./shared ./c --once --silent 2>&1 > /dev/null || true

    echo ""
    echo "Top indexing hotspots:"
    sudo perf report -i ./tmp/perf-index.data --stdio 2>&1 | head -60

    echo ""
    echo "Profiling querying with perf..."
    sudo perf record -g -o ./tmp/perf-query.data ./qi '*' -i func --limit 1000 2>&1 > /dev/null || true

    echo ""
    echo "Top querying hotspots:"
    sudo perf report -i ./tmp/perf-query.data --stdio 2>&1 | head -60
fi

echo ""
echo "=========================================="
echo "Test 4: Memory Bandwidth Analysis"
echo "=========================================="
echo ""

if [ $HAVE_PERF -eq 1 ]; then
    rm -f code-index.db
    echo "Measuring memory events during indexing..."
    sudo perf stat -e cache-references,cache-misses,L1-dcache-loads,L1-dcache-load-misses \
        ./index-c ./shared ./c --once --silent 2>&1 | tail -20

    echo ""
    echo "Measuring memory events during querying..."
    sudo perf stat -e cache-references,cache-misses,L1-dcache-loads,L1-dcache-load-misses \
        ./qi '*' -i func --limit 1000 2>&1 | tail -20
fi

echo ""
echo "=========================================="
echo "Test 5: Struct Size Analysis"
echo "=========================================="
echo ""

cat > ./tmp/struct_size_test.c << 'EOF'
#include <stdio.h>
#include <stddef.h>
#include "../shared/database.h"

int main() {
    printf("IndexEntry struct analysis:\n");
    printf("==============================\n");
    printf("Total size: %zu bytes\n\n", sizeof(IndexEntry));

    IndexEntry e;
    printf("Field offsets (hot fields should be at start):\n");
    printf("  line:          offset %3zu (size %zu)\n", offsetof(IndexEntry, line), sizeof(e.line));
    printf("  context:       offset %3zu (size %zu)\n", offsetof(IndexEntry, context), sizeof(e.context));
    printf("  is_definition: offset %3zu (size %zu)\n", offsetof(IndexEntry, is_definition), sizeof(e.is_definition));
    printf("\n");
    printf("  symbol:        offset %3zu (size %zu)\n", offsetof(IndexEntry, symbol), sizeof(e.symbol));
    printf("  directory:     offset %3zu (size %zu)\n", offsetof(IndexEntry, directory), sizeof(e.directory));
    printf("  filename:      offset %3zu (size %zu)\n", offsetof(IndexEntry, filename), sizeof(e.filename));
    printf("\n");

    // Calculate which cache line each hot field is in
    int line_cacheline = offsetof(IndexEntry, line) / 64;
    int context_cacheline = offsetof(IndexEntry, context) / 64;
    int is_def_cacheline = offsetof(IndexEntry, is_definition) / 64;

    printf("Cache line analysis (64-byte cache lines):\n");
    printf("  Hot fields in cache line(s): %d, %d, %d\n",
           line_cacheline, context_cacheline, is_def_cacheline);

    if (line_cacheline == 0 && context_cacheline == 0 && is_def_cacheline == 0) {
        printf("  ✓ All hot fields in first cache line!\n");
    } else {
        printf("  ✗ Hot fields span multiple cache lines (suboptimal)\n");
    }

    return 0;
}
EOF

echo "Compiling struct size test..."
gcc -o ./tmp/struct_size_test ./tmp/struct_size_test.c -I. 2>&1

if [ -f ./tmp/struct_size_test ]; then
    ./tmp/struct_size_test
fi

echo ""
echo "=========================================="
echo "Investigation Complete!"
echo "=========================================="
echo ""
echo "Summary files created in ./tmp/:"
echo "  - cachegrind-index.out (cache analysis for indexing)"
echo "  - cachegrind-query.out (cache analysis for querying)"
echo "  - perf-index.data (CPU profiling for indexing)"
echo "  - perf-query.data (CPU profiling for querying)"
echo ""
echo "To view detailed reports:"
echo "  cg_annotate ./tmp/cachegrind-index.out | less"
echo "  sudo perf report -i ./tmp/perf-index.data"
