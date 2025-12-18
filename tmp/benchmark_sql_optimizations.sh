#!/bin/bash
# Benchmark SQLite optimizations: WITHOUT ROWID + Composite Indexes

set -e

echo "=========================================="
echo "SQLite Optimization Benchmark"
echo "=========================================="
echo ""
echo "Comparing:"
echo "  OLD: /usr/local/bin/index-c (with ROWID, single-column indexes)"
echo "  NEW: ./index-c (WITHOUT ROWID, composite indexes)"
echo ""

# Cleanup function
cleanup() {
    rm -f /tmp/benchmark-old.db /tmp/benchmark-new.db
}
trap cleanup EXIT

echo "=========================================="
echo "Test 1: Indexing Performance"
echo "=========================================="
echo ""

echo "OLD binary (with ROWID):"
time /usr/local/bin/index-c ./shared ./c --once --silent --db-file /tmp/benchmark-old.db

echo ""
echo "NEW binary (WITHOUT ROWID):"
time ./index-c ./shared ./c --once --silent --db-file /tmp/benchmark-new.db

echo ""
echo "=========================================="
echo "Test 2: Database Size Comparison"
echo "=========================================="
echo ""

OLD_SIZE=$(stat -c%s /tmp/benchmark-old.db)
NEW_SIZE=$(stat -c%s /tmp/benchmark-new.db)
SAVINGS=$((OLD_SIZE - NEW_SIZE))
PERCENT=$(awk "BEGIN {printf \"%.2f\", ($SAVINGS / $OLD_SIZE) * 100}")

echo "OLD database: $(numfmt --to=iec-i --suffix=B $OLD_SIZE)"
echo "NEW database: $(numfmt --to=iec-i --suffix=B $NEW_SIZE)"
echo "Savings:      $(numfmt --to=iec-i --suffix=B $SAVINGS) ($PERCENT%)"

echo ""
echo "=========================================="
echo "Test 3: Query Performance"
echo "=========================================="
echo ""

# Test various query patterns
QUERIES=(
    "qi test --limit 100"
    "qi test -i func --limit 100"
    "qi test --def --limit 100"
    "qi test -i func --def --limit 100"
    "qi '*' -i func --limit 1000"
    "qi '*' -i func --def --limit 500"
)

for query in "${QUERIES[@]}"; do
    echo "Query: $query"
    echo "  OLD:"
    time /usr/local/bin/qi $query --db-file /tmp/benchmark-old.db > /dev/null 2>&1
    echo "  NEW:"
    time ./qi $query --db-file /tmp/benchmark-new.db > /dev/null 2>&1
    echo ""
done

echo "=========================================="
echo "Test 4: Index Usage Analysis"
echo "=========================================="
echo ""

echo "OLD database indexes:"
sqlite3 /tmp/benchmark-old.db "SELECT name FROM sqlite_master WHERE type='index' AND tbl_name='code_index' ORDER BY name;"

echo ""
echo "NEW database indexes:"
sqlite3 /tmp/benchmark-new.db "SELECT name FROM sqlite_master WHERE type='index' AND tbl_name='code_index' ORDER BY name;"

echo ""
echo "=========================================="
echo "Test 5: Query Plan Comparison"
echo "=========================================="
echo ""

# Test query plan for common patterns
echo "Query: SELECT * FROM code_index WHERE context = 'FUNC' AND is_definition = 1"
echo ""
echo "OLD plan:"
sqlite3 /tmp/benchmark-old.db "EXPLAIN QUERY PLAN SELECT * FROM code_index WHERE context = 'FUNC' AND is_definition = 1 LIMIT 100;"

echo ""
echo "NEW plan (should use idx_context_definition):"
sqlite3 /tmp/benchmark-new.db "EXPLAIN QUERY PLAN SELECT * FROM code_index WHERE context = 'FUNC' AND is_definition = 1 LIMIT 100;"

echo ""
echo "=========================================="
echo "Test 6: WITHOUT ROWID Verification"
echo "=========================================="
echo ""

echo "OLD table (should have 'rowid' column):"
sqlite3 /tmp/benchmark-old.db "PRAGMA table_info(code_index);" | grep -i rowid || echo "  (implicit rowid, not visible in schema)"

echo ""
echo "NEW table (should be WITHOUT ROWID):"
sqlite3 /tmp/benchmark-new.db "SELECT sql FROM sqlite_master WHERE type='table' AND name='code_index';" | grep -o "WITHOUT ROWID" && echo "  ✓ WITHOUT ROWID confirmed!" || echo "  ✗ WITHOUT ROWID not found!"

echo ""
echo "=========================================="
echo "Benchmark Complete!"
echo "=========================================="
