#!/bin/bash
# Analyze current SQLite schema and query patterns

set -e

echo "=========================================="
echo "SQLite Schema & Index Analysis"
echo "=========================================="
echo ""

# Make sure we have a populated database
if [ ! -f code-index.db ]; then
    echo "Building index first..."
    ./index-c ./shared ./c --once --silent
fi

echo "=========================================="
echo "1. Current Table Schema"
echo "=========================================="
sqlite3 code-index.db << 'EOF'
.schema code_index
EOF

echo ""
echo "=========================================="
echo "2. Current Indexes"
echo "=========================================="
sqlite3 code-index.db << 'EOF'
SELECT name, sql FROM sqlite_master WHERE type='index' AND tbl_name='code_index';
EOF

echo ""
echo "=========================================="
echo "3. Table Statistics"
echo "=========================================="
sqlite3 code-index.db << 'EOF'
SELECT
    COUNT(*) as total_rows,
    COUNT(DISTINCT symbol) as unique_symbols,
    COUNT(DISTINCT directory) as unique_directories,
    COUNT(DISTINCT filename) as unique_files,
    COUNT(DISTINCT context) as unique_contexts
FROM code_index;
EOF

echo ""
echo "=========================================="
echo "4. Index Usage Analysis"
echo "=========================================="
echo "Running EXPLAIN QUERY PLAN on common queries..."
echo ""

echo "Query 1: Simple symbol search (qi test)"
sqlite3 code-index.db << 'EOF'
EXPLAIN QUERY PLAN
SELECT * FROM code_index WHERE symbol LIKE '%test%';
EOF

echo ""
echo "Query 2: Symbol + context filter (qi test -i func)"
sqlite3 code-index.db << 'EOF'
EXPLAIN QUERY PLAN
SELECT * FROM code_index
WHERE symbol LIKE '%test%'
  AND context = 'FUNC';
EOF

echo ""
echo "Query 3: File filter (qi test -f shared/)"
sqlite3 code-index.db << 'EOF'
EXPLAIN QUERY PLAN
SELECT * FROM code_index
WHERE symbol LIKE '%test%'
  AND directory LIKE '%shared%';
EOF

echo ""
echo "Query 4: Definition filter (qi test --def)"
sqlite3 code-index.db << 'EOF'
EXPLAIN QUERY PLAN
SELECT * FROM code_index
WHERE symbol LIKE '%test%'
  AND is_definition = 1;
EOF

echo ""
echo "Query 5: Combined filters (qi test -i func --def -f shared/)"
sqlite3 code-index.db << 'EOF'
EXPLAIN QUERY PLAN
SELECT * FROM code_index
WHERE symbol LIKE '%test%'
  AND context = 'FUNC'
  AND is_definition = 1
  AND directory LIKE '%shared%';
EOF

echo ""
echo "=========================================="
echo "5. Table Size Analysis"
echo "=========================================="
sqlite3 code-index.db << 'EOF'
SELECT
    'Table' as type,
    name,
    (pgsize * pgcount) / 1024.0 / 1024.0 as size_mb
FROM dbstat
WHERE name = 'code_index'
LIMIT 1;

SELECT
    'Index' as type,
    name,
    (pgsize * pgcount) / 1024.0 / 1024.0 as size_mb
FROM dbstat
WHERE name LIKE 'idx_%'
ORDER BY size_mb DESC;
EOF

echo ""
echo "=========================================="
echo "6. WITHOUT ROWID Analysis"
echo "=========================================="
echo ""
echo "Current table uses implicit ROWID (8 bytes per row overhead)"
echo "Estimated ROWID overhead for this database:"
sqlite3 code-index.db << 'EOF'
SELECT
    COUNT(*) || ' rows × 8 bytes = ' ||
    (COUNT(*) * 8.0 / 1024.0) || ' KB ROWID overhead'
FROM code_index;
EOF

echo ""
echo "WITHOUT ROWID is beneficial when:"
echo "  - Primary key is frequently used in WHERE clauses"
echo "  - No need for auto-incrementing IDs"
echo "  - Composite primary key is natural fit"
echo ""
echo "Potential composite primary key for WITHOUT ROWID:"
echo "  PRIMARY KEY (directory, filename, line, symbol)"
echo "  Rationale: These 4 columns uniquely identify an entry"
echo ""

echo "=========================================="
echo "7. Missing Index Analysis"
echo "=========================================="
echo ""
echo "Checking for frequently combined filters..."

# Check what combinations appear in query logs
echo "Common query patterns require multi-column indexes on:"
echo "  - (symbol, context) - for 'qi symbol -i context'"
echo "  - (symbol, is_definition) - for 'qi symbol --def'"
echo "  - (context, is_definition) - for 'qi * -i func --def'"
echo "  - (directory, filename) - for file-based lookups"
echo ""

echo "=========================================="
echo "8. Page Size Analysis"
echo "=========================================="
sqlite3 code-index.db << 'EOF'
PRAGMA page_size;
PRAGMA page_count;
SELECT
    'Database size: ' ||
    (page_count * page_size / 1024.0 / 1024.0) || ' MB'
FROM (SELECT * FROM pragma_page_count, pragma_page_size);
EOF

echo ""
echo "Default page size: 4096 bytes"
echo "Recommendation: For read-heavy workloads, consider 8192 or 16384"
echo ""

echo "=========================================="
echo "Analysis Complete!"
echo "=========================================="
