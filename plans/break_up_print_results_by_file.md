# Plan: Break Up print_results_by_file Function

## Current State

The `print_results_by_file()` function in `query-index.c` is **274 lines** (lines 1622-1896).

**Current responsibilities:**
1. Query building (proximity search vs normal)
2. SQL execution and parameter binding
3. Column width calculation
4. Result iteration and printing
5. No-results diagnostics (with auto-retry logic)
6. Pattern validation and filtering
7. File grouping logic
8. Context/definition expansion
9. Summary statistics

## Problem

This function violates the Single Responsibility Principle and is difficult to:
- Test individual pieces
- Understand at a glance
- Modify without risk of breaking other parts
- Reuse components in other contexts

## Proposed Refactoring

### Extract Functions (in order of complexity):

#### 1. `print_no_results_diagnostics()` (~100 lines)
**Lines:** 1678-1769
**Purpose:** Handle the "No results" case with pattern analysis and auto-retry
**Parameters:**
```c
static void print_no_results_diagnostics(
    CodeIndexDatabase *db,
    PatternList *patterns,
    ContextTypeList *include,
    ContextTypeList *exclude,
    QueryFilters *filters,
    FileFilterList *file_filter,
    int line_range,
    const FileExtensions *known_exts
);
```
**Contains:**
- Filter initialization
- Per-pattern match counting
- Wildcard retry logic (with goto retry_query)
- Filter reason reporting
- Extension warnings

**Note:** The `goto retry_query` is problematic here. Options:
- Return a flag indicating "retry needed" and handle in caller
- Pass the pattern to modify as an out-parameter
- Refactor to eliminate goto entirely

---

#### 2. `print_result_row()` (~80 lines)
**Lines:** 1780-1876
**Purpose:** Print a single result row with optional expansion
**Parameters:**
```c
static void print_result_row(
    sqlite3_stmt *stmt,
    const char *filepath,
    int show_all_columns,
    int compact,
    int expand,
    int context_before,
    int context_after,
    PatternList *patterns
);
```
**Contains:**
- Column extraction from sqlite3_stmt
- File header printing (when file changes)
- Row printing (normal vs verbose)
- Expansion/context printing

---

#### 3. `execute_query_and_iterate()` (~50 lines)
**Lines:** 1634-1876 (middle portion)
**Purpose:** Execute query, iterate results, print rows
**Parameters:**
```c
static int execute_query_and_iterate(
    CodeIndexDatabase *db,
    const char *sql,
    PatternList *patterns,
    int limit,
    int limit_per_file,
    int line_range,
    int show_all_columns,
    int compact,
    int expand,
    int context_before,
    int context_after
);
```
**Returns:** Count of results printed
**Contains:**
- Query preparation
- Parameter binding
- Result iteration loop
- Limit checking

---

#### 4. `print_summary_stats()` (~15 lines)
**Lines:** 1880-1896
**Purpose:** Print final summary with context breakdown
**Parameters:**
```c
static void print_summary_stats(
    CodeIndexDatabase *db,
    PatternList *patterns,
    ContextTypeList *include,
    ContextTypeList *exclude,
    QueryFilters *filters,
    FileFilterList *file_filter,
    int line_range,
    int total_count,
    int limit,
    int debug
);
```

---

### Refactored `print_results_by_file()` Structure

After extraction, the main function becomes ~40 lines:

```c
static void print_results_by_file(...) {
    /* 1. Two-step proximity search if needed */
    if (line_range > 0 && patterns->count > 1) {
        if (execute_proximity_to_temp_table(...) != 0) {
            return;
        }
    }

    /* 2. Build SQL query */
    char sql[SQL_QUERY_BUFFER];
    build_query_sql(sql, sizeof(sql), patterns, include, exclude,
                    filters, file_filter, line_range);

    if (debug) {
        printf("SQL: [Main query] %s\n", sql);
    }

    /* 3. Calculate column widths */
    calculate_column_widths_from_query(db, patterns, include, exclude,
                                       filters, file_filter, limit,
                                       line_range, compact, debug);

    /* 4. Execute query and print results */
    int result_count = execute_query_and_iterate(db, sql, patterns, limit,
                                                  limit_per_file, line_range,
                                                  show_all_columns, compact,
                                                  expand, context_before,
                                                  context_after);

    /* 5. Handle no results case */
    if (result_count == 0) {
        print_no_results_diagnostics(db, patterns, include, exclude,
                                     filters, file_filter, line_range,
                                     known_exts);
        return;
    }

    /* 6. Print summary */
    print_summary_stats(db, patterns, include, exclude, filters,
                       file_filter, line_range, result_count, limit, debug);
}
```

---

## Additional Helper Needed

### `build_query_sql()`
**Purpose:** Consolidate query building logic
**Lines:** 1637-1647
Extract the if/else that chooses between proximity query and normal query.

---

## Implementation Order

1. **Start with easiest:** `print_summary_stats()` (self-contained, no side effects)
2. **Next:** `build_query_sql()` (simple extraction)
3. **Then:** `print_result_row()` (moderate complexity, needs RowData struct)
4. **Next:** `execute_query_and_iterate()` (uses print_result_row)
5. **Last:** `print_no_results_diagnostics()` (most complex, has goto issue)

---

## Goto Retry Challenge

The auto-retry logic at line 1718 uses `goto retry_query`. Options:

### Option A: Return retry signal
```c
typedef struct {
    int retry_needed;
    char *new_pattern;  // If retry needed, what pattern to use
    int pattern_index;  // Which pattern to replace
} DiagnosticResult;

DiagnosticResult print_no_results_diagnostics(...);

// In caller:
DiagnosticResult diag = print_no_results_diagnostics(...);
if (diag.retry_needed) {
    free(patterns->patterns[diag.pattern_index]);
    patterns->patterns[diag.pattern_index] = diag.new_pattern;
    goto retry_query;  // Or use a while loop
}
```

### Option B: Wrap in loop
```c
int retry = 1;
while (retry) {
    retry = 0;
    // Execute query
    if (result_count == 0) {
        retry = print_no_results_diagnostics_with_retry(...);
    }
}
```

### Option C: Keep goto in main function
Don't extract the retry logic - keep it inline with the goto.

**Recommendation:** Option C for simplicity. The goto is well-commented and not problematic in this context.

---

## Testing Strategy

After refactoring:
1. Compile with no warnings
2. Run existing queries to verify identical output
3. Test edge cases:
   - No results
   - Auto-retry with wildcards
   - Limit exceeded
   - Proximity search
   - TOC mode

---

## Benefits

- **Readability:** Each function has one clear purpose
- **Testability:** Individual functions can be tested
- **Maintainability:** Changes are localized
- **Reusability:** Diagnostic logic could be reused elsewhere
- **Reduced cognitive load:** ~40 line main function vs 274 lines

---

## Files to Modify

- `query-index.c` - extract functions, refactor print_results_by_file

---

## Estimated Lines of Code

- **Before:** 274 lines in one function
- **After:**
  - Main function: ~40 lines
  - Helper functions: 5 functions × ~40 lines average = ~200 lines
  - **Total:** ~240 lines (slight reduction due to removed duplication)

---

## Notes

- All extracted functions should be `static` (file-local)
- Preserve existing comments
- No behavior changes - pure refactoring
- Maintain existing error handling patterns
