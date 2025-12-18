# Window Function & CTE Approach for Line Range Search

## Current Two-Step Approach (test_line_range.c)

**Step 1:** Find all anchor matches
```sql
SELECT * FROM code_index WHERE symbol LIKE 'anchor_pattern'
```

**Step 2:** For each anchor, query secondaries within range
```sql
SELECT * FROM code_index
WHERE filename = ? AND directory = ?
  AND line_number BETWEEN (anchor_line - range) AND (anchor_line + range)
  AND (symbol LIKE 'pattern2' OR symbol LIKE 'pattern3' ...)
```

**Validation:** C code loops through results to verify ALL patterns found

**Drawbacks:**
- N+1 query problem (1 anchor query + N queries for each anchor)
- Pattern matching logic in C code
- Multiple round trips to database

---

## Alternative 1: Window Functions with LAG/LEAD

**Strategy:** Use window functions to check proximity between rows in a single query

**Concept:**
```sql
WITH matches AS (
  SELECT
    symbol,
    directory,
    filename,
    line_number,
    context_type,
    CASE
      WHEN symbol LIKE 'malloc' THEN 1  -- anchor
      WHEN symbol LIKE 'free' THEN 2    -- secondary
    END as pattern_id,
    LAG(line_number) OVER (PARTITION BY directory, filename ORDER BY line_number) as prev_line,
    LEAD(line_number) OVER (PARTITION BY directory, filename ORDER BY line_number) as next_line
  FROM code_index
  WHERE symbol LIKE 'malloc' OR symbol LIKE 'free'
)
SELECT * FROM matches
WHERE pattern_id = 1  -- anchor rows
  AND (
    ABS(next_line - line_number) <= 10  -- secondary within range after
    OR ABS(prev_line - line_number) <= 10  -- secondary within range before
  )
```

**Challenges:**
- LAG/LEAD only look at immediate neighbors, not all rows within range
- Doesn't verify ALL patterns found (only checks proximity)
- Complex CASE statements for pattern identification
- Difficult to ensure all secondary patterns present

**Verdict:** ❌ Not suitable - window functions work on row-by-row basis, not range-based matching

---

## Alternative 2: Self-Join with Range Constraints

**Strategy:** Join table to itself to find patterns within range

```sql
WITH anchors AS (
  SELECT directory, filename, line_number as anchor_line, symbol
  FROM code_index
  WHERE symbol LIKE 'malloc'
),
secondaries AS (
  SELECT directory, filename, line_number, symbol, 'free' as pattern_name
  FROM code_index
  WHERE symbol LIKE 'free'
  UNION ALL
  SELECT directory, filename, line_number, symbol, 'realloc' as pattern_name
  FROM code_index
  WHERE symbol LIKE 'realloc'
)
SELECT
  a.directory,
  a.filename,
  a.anchor_line,
  a.symbol as anchor_symbol,
  s.line_number,
  s.symbol,
  s.pattern_name
FROM anchors a
INNER JOIN secondaries s
  ON a.directory = s.directory
  AND a.filename = s.filename
  AND s.line_number BETWEEN (a.anchor_line - 10) AND (a.anchor_line + 10)
GROUP BY a.directory, a.filename, a.anchor_line
HAVING COUNT(DISTINCT s.pattern_name) = 2  -- ALL patterns must be found
```

**Benefits:**
- Single query handles all anchors
- SQL enforces "ALL patterns found" via HAVING clause
- No N+1 problem
- Leverages database indexes efficiently

**Challenges:**
- Need to track distinct pattern names
- UNION ALL for each secondary pattern (dynamic SQL required)
- More complex query construction

**Verdict:** ✅ **PROMISING** - Best candidate for single-query approach

---

## Alternative 3: CTE with Anchor Results + Window Function

**Strategy:** Find anchors in CTE, then use window to partition by file and check range

```sql
WITH anchors AS (
  -- Step 1: Find all anchor matches
  SELECT directory, filename, line_number as anchor_line, symbol
  FROM code_index
  WHERE symbol LIKE 'malloc'
),
all_symbols AS (
  -- Step 2: Get all symbols in files with anchors, within potential range
  SELECT
    s.directory,
    s.filename,
    s.line_number,
    s.symbol,
    s.context_type,
    a.anchor_line,
    CASE
      WHEN s.symbol LIKE 'malloc' THEN 'anchor'
      WHEN s.symbol LIKE 'free' THEN 'free'
      WHEN s.symbol LIKE 'realloc' THEN 'realloc'
    END as pattern_type
  FROM code_index s
  INNER JOIN anchors a
    ON s.directory = a.directory
    AND s.filename = a.filename
    AND s.line_number BETWEEN (a.anchor_line - 10) AND (a.anchor_line + 10)
  WHERE s.symbol LIKE 'malloc'
     OR s.symbol LIKE 'free'
     OR s.symbol LIKE 'realloc'
)
SELECT
  directory, filename, anchor_line, symbol, line_number, pattern_type
FROM all_symbols
WHERE anchor_line IN (
  -- Only keep anchors where ALL secondary patterns found
  SELECT anchor_line
  FROM all_symbols
  WHERE pattern_type != 'anchor'
  GROUP BY directory, filename, anchor_line
  HAVING COUNT(DISTINCT pattern_type) = 2  -- number of secondary patterns
)
ORDER BY directory, filename, anchor_line, line_number
```

**Benefits:**
- Single query
- CTE makes logic readable
- Enforces ALL patterns found via HAVING
- Returns both anchor and secondary matches

**Challenges:**
- Still requires dynamic pattern_type CASE construction
- Subquery for filtering anchors adds complexity

**Verdict:** ✅ **GOOD** - Cleaner than self-join, similar performance

---

## Alternative 4: Recursive CTE (Overkill)

**Strategy:** Use recursive CTE to expand from anchor

**Verdict:** ❌ Not applicable - SQLite recursive CTEs are for hierarchical data, not range searching

---

## Recommended Approach: **Self-Join with CTE (Alternative 2)**

### Pseudocode Implementation

```c
/* Build query with self-join approach */
char* build_range_query(PatternList *patterns, int range) {
    // Step 1: Build anchor CTE
    "WITH anchors AS ("
    "  SELECT directory, filename, line_number as anchor_line, symbol"
    "  FROM code_index"
    "  WHERE symbol LIKE ?"  // First pattern is anchor
    "), "

    // Step 2: Build secondaries CTE (UNION ALL for each pattern)
    "secondaries AS ("
    "  SELECT directory, filename, line_number, symbol, ? as pattern_name"
    "  FROM code_index WHERE symbol LIKE ?"
    "  UNION ALL"
    "  SELECT directory, filename, line_number, symbol, ? as pattern_name"
    "  FROM code_index WHERE symbol LIKE ?"
    // ... repeat for each secondary pattern
    ") "

    // Step 3: Join and filter
    "SELECT DISTINCT"
    "  a.directory, a.filename, a.anchor_line,"
    "  COALESCE(s.line_number, a.anchor_line) as line_number,"
    "  COALESCE(s.symbol, a.symbol) as symbol"
    "FROM anchors a"
    "LEFT JOIN secondaries s"
    "  ON a.directory = s.directory"
    "  AND a.filename = s.filename"
    "  AND s.line_number BETWEEN (a.anchor_line - ?) AND (a.anchor_line + ?)"
    "WHERE a.anchor_line IN ("
    "  SELECT anchor_line FROM anchors a2"
    "  INNER JOIN secondaries s2"
    "    ON a2.directory = s2.directory"
    "    AND a2.filename = s2.filename"
    "    AND s2.line_number BETWEEN (a2.anchor_line - ?) AND (a2.anchor_line + ?)"
    "  GROUP BY a2.directory, a2.filename, a2.anchor_line"
    "  HAVING COUNT(DISTINCT s2.pattern_name) = ?"  // num secondary patterns
    ")"
    "ORDER BY directory, filename, anchor_line, line_number";
}
```

### Benefits Over Two-Step Approach

1. **Single query** - No N+1 problem
2. **Database-side filtering** - SQL does the heavy lifting
3. **Better performance** - One database round trip
4. **Cleaner separation** - SQL handles logic, C handles display
5. **Testable** - Can test query directly in sqlite3

### Performance Comparison

**Two-step approach:**
- 1 query for anchors
- N queries for each anchor (if 100 anchors = 100 queries)
- Pattern matching in C

**Single query approach:**
- 1 query total
- All pattern matching in SQL (indexed)
- Results already filtered

### Implementation Plan for test_line_range_window.c

1. **Keep current structure** - Two functions remain:
   - `find_anchor_matches()` - Can be removed/simplified
   - `search_within_range()` - Becomes `search_with_single_query()`

2. **New function:** `build_window_query()`
   - Takes anchor pattern, secondary patterns, range
   - Returns complete SQL with CTEs
   - Binds pattern names as parameters

3. **Simplify main():**
   - Remove two-step loop
   - Single query execution
   - Results already validated by SQL

4. **Test both approaches:**
   - Keep original two-step as reference
   - Add new single-query approach
   - Compare results and performance

---

## Decision Matrix

| Approach | Queries | Complexity | Performance | SQL Features |
|----------|---------|------------|-------------|--------------|
| Two-step (current) | N+1 | Low | ⭐⭐ | Basic SELECT |
| Self-Join CTE | 1 | Medium | ⭐⭐⭐⭐ | CTE, JOIN, HAVING |
| Window Functions | 1 | High | ⭐⭐ | LAG/LEAD (limited) |
| CTE + Subquery | 1 | Medium-High | ⭐⭐⭐⭐ | CTE, Subquery |

**Recommendation:** **Self-Join with CTE** - Best balance of performance and maintainability

---

## Next Steps

1. Implement self-join CTE approach in `test_line_range_window.c`
2. Test with same patterns as original (malloc/free, etc.)
3. Compare query execution time
4. Verify results match two-step approach
5. If successful, integrate into `build_query_filters()` in query-index.c
