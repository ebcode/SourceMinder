# Line Range Search Implementation Plan

## Overview
Convert `--and` / `--same-line` from boolean (same line only) to accept optional range value for proximity searching.

**Goal:** `./qi malloc free --and 10` finds malloc/free within 10 lines of each other

## Current State
- `--and` / `--same-line`: Boolean flag (0 or 1)
- Line 1848: `int same_line = 0;`
- Line 2138-2139: Parsed as boolean flag
- Line 770-815: `build_query_filters()` uses INTERSECT to find exact same-line matches
- Used in 9 locations across query-index.c

## Required Changes

### 1. Rename Variable
**Change:** `same_line` → `line_range`

**Type:** Change from boolean `int` to range value `int`
- `0` = same line (backwards compatible)
- `N` = within N lines

**Occurrences to update:**
- Line 1848: Variable declaration
- Line 771: `build_query_filters()` parameter
- Line 827: `build_width_query()` parameter
- Line 915: `calculate_column_widths_from_query()` parameter
- Line 1229: `get_total_count()` parameter
- Line 1263: `get_context_summary()` parameter
- Line 1311: `get_total_file_count()` parameter
- Line 1344: `print_files_only()` parameter
- Line 1398: `print_results_by_file()` parameter
- Line 2138-2139: Flag parsing
- Line 2222: Validation check

### 2. Flag Parsing
**Location:** Lines 2138-2140

**Old:**
```c
if (strcmp(argv[i], "--and") == 0 || strcmp(argv[i], "--same-line") == 0) {
    same_line = 1;
}
```

**New:**
```c
if (strcmp(argv[i], "--and") == 0 || strcmp(argv[i], "--same-line") == 0) {
    /* Check if next arg is a number */
    if (i + 1 < argc && argv[i + 1][0] != '-') {
        char *endptr;
        errno = 0;
        long val = strtol(argv[i + 1], &endptr, 10);
        if (errno == 0 && *endptr == '\0' && val >= 0) {
            line_range = (int)val;
            i++;  /* Consume the number */
        } else {
            line_range = 0;  /* Default to same line */
        }
    } else {
        line_range = 0;  /* No number provided, default to same line */
    }
}
```

### 3. SQL Query Logic
**Location:** Lines 770-815 in `build_query_filters()`

**Strategy:**
- `line_range == 0`: Keep current INTERSECT approach (same line)
- `line_range > 0`: New two-step approach:
  1. Find all anchor pattern matches (first pattern)
  2. For each anchor, search secondary patterns within ±line_range lines
  3. Return symbols from both anchor and matches in range

**Prototype proven approach:**
```sql
-- Step 1: Find anchor matches
SELECT * FROM code_index WHERE symbol LIKE 'anchor_pattern'

-- Step 2: For each anchor, find secondaries within range
SELECT * FROM code_index
WHERE filename = ?
  AND directory = ?
  AND line_number BETWEEN (anchor_line - range) AND (anchor_line + range)
  AND (symbol LIKE 'pattern2' OR symbol LIKE 'pattern3' ...)
```

**Key Logic:**
- Ensure line numbers >= 1 (no negative ranges)
- Verify ALL secondary patterns found within range
- Return symbols from anchor + matches

### 4. Function Signature Updates

All functions accepting `same_line` parameter:

```c
// OLD signatures:
build_query_filters(..., int same_line)
print_results_by_file(..., int same_line, ...)
print_files_only(..., int same_line, ...)
get_total_count(..., int same_line, ...)
get_context_summary(..., int same_line, ...)
calculate_column_widths_from_query(..., int same_line, ...)

// NEW signatures:
build_query_filters(..., int line_range)
print_results_by_file(..., int line_range, ...)
print_files_only(..., int line_range, ...)
get_total_count(..., int line_range, ...)
get_context_summary(..., int line_range, ...)
calculate_column_widths_from_query(..., int line_range, ...)
```

### 5. Help Text Updates

**Pattern Selection section:**
```
--and [N], --same-line [N]  find patterns within N lines (default: 0 = same line)
```

**Find patterns section:**
```
Find patterns (multiple symbols together):
  qi fprintf stderr --and              # both on same line
  qi malloc free --and                 # both on same line (default)
  qi malloc free --and 10              # within 10 lines of each other
  qi lock unlock --and 20              # within 20 lines
```

**AND Refinement section update:**
```
AND Refinement (Proximity Search):

**Refine your search** by finding patterns within a specified line range:

qi malloc free --and           # same line (default: 0)
qi malloc free --and 10        # within 10 lines
qi open close --and 20         # within 20 lines
qi lock unlock --and 15        # within 15 lines

**How it works:**
- First pattern is the anchor
- Searches for other patterns within ±N lines of anchor
- Returns all matching symbols (anchor + patterns in range)

**Behavior:**
- line_range = 0: Same line matching (current INTERSECT behavior)
- line_range > 0: Proximity search (±N lines from anchor)
- ALL patterns must be found within range
- Works with all filters: context type, file, columns

**Examples:**
qi malloc free --and 10 -i call        # Calls within 10 lines
qi lock unlock --and 20 -f "%.c"       # In .c files, within 20 lines
qi fprintf stderr --and -x noise       # Same line, exclude comments/strings
```

### 6. Validation Updates
**Location:** Lines 2222-2232

**Old:**
```c
if (same_line && patterns.count < 2) {
    fprintf(stderr, "Error: --and (--same-line) requires at least 2 search patterns for AND refinement.\n");
    fprintf(stderr, "Example: ./query-index fprintf err_msg --and\n");
```

**New:**
```c
if (line_range >= 0 && patterns.count < 2) {
    fprintf(stderr, "Error: --and requires at least 2 search patterns.\n");
    fprintf(stderr, "Example: qi malloc free --and 10\n");
```

## Implementation Phases

### Phase 1: Preparation & Renaming ✓
1. Create this plan document
2. Rename all `same_line` → `line_range` throughout codebase
3. Update all function signatures
4. Update variable declarations
5. Verify compilation

### Phase 2: Flag Parsing
1. Update parsing logic to accept optional number (lines 2138-2140)
2. Add validation for range value (must be >= 0)
3. Update error messages
4. Test flag parsing with various inputs

### Phase 3: SQL Query Logic (Most Complex)
1. Keep `line_range == 0` using current INTERSECT approach
2. Implement `line_range > 0` using two-step approach:
   - Step 1: Find anchor pattern (first pattern in list)
   - Step 2: For each anchor, query secondary patterns within range
   - Verify ALL secondary patterns found
   - Collect and return matching symbols
3. Handle edge cases:
   - Line number bounds (ensure >= 1)
   - Empty results
   - Single file constraint (range doesn't cross files)
4. Test queries manually with sqlite3

### Phase 4: Integration & Testing
1. Test with various patterns and ranges:
   - `qi malloc free --and` (should work as before)
   - `qi malloc free --and 10` (new behavior)
   - `qi malloc free realloc --and 20` (3 patterns)
2. Verify backwards compatibility
3. Test with filters (-i, -x, -f)
4. Test with other flags (--limit, --limit-per-file, -e, -C)
5. Update help text
6. Test edge cases

## Key Design Decisions

### Q1: Modify build_query_filters() or create separate path?
**Decision:** Create separate query path for `line_range > 0`
**Rationale:**
- Cleaner separation of concerns
- Less complex conditionals
- Easier to maintain and debug
- Can optimize each path independently

### Q2: Single SQL query or two-step approach?
**Decision:** Two-step approach (proven in prototype)
**Rationale:**
- Prototype validates this works
- Simpler to implement initially
- Can optimize later with CTEs if needed
- More flexible for future enhancements

### Q3: How to handle anchor selection?
**Decision:** First pattern is always the anchor
**Rationale:**
- Simple and predictable
- User controls which symbol is anchor
- Matches user mental model: "find malloc, then look for free nearby"

## Testing Checklist

- [ ] Backwards compatibility: `--and` without number works as before
- [ ] Range search: `--and 10` finds patterns within 10 lines
- [ ] Multiple patterns: Works with 3+ patterns (ALL must be found)
- [ ] Edge cases:
  - [ ] Range at start of file (line 1-10)
  - [ ] Range with negative calculation (clamped to 1)
  - [ ] No matches found
  - [ ] Partial matches (anchor found, but not all secondaries)
- [ ] Integration with filters:
  - [ ] Works with `-i` include filters
  - [ ] Works with `-x` exclude filters
  - [ ] Works with `-f` file filters
  - [ ] Works with `--limit`
  - [ ] Works with `--limit-per-file`
- [ ] Output correctness:
  - [ ] Shows anchor symbol
  - [ ] Shows matching symbols in range
  - [ ] Line numbers are correct
  - [ ] File grouping works correctly

## Example Usage

```bash
# Same line (backwards compatible)
qi malloc free --and
qi fprintf stderr --and

# Within N lines
qi malloc free --and 10
qi open close --and 20
qi lock unlock --and 15

# With filters
qi malloc free --and 10 -i call -f %.c
qi fprintf stderr --and 5 -x noise --limit 20

# Multiple patterns (ALL must be found)
qi malloc free realloc --and 20
```

## Success Criteria

1. ✓ All `same_line` renamed to `line_range`
2. ✓ Compilation succeeds without errors
3. `--and` without number works exactly as before (line_range=0)
4. `--and N` performs proximity search within ±N lines
5. Help text updated and clear
6. All existing tests pass
7. New tests for range behavior pass
8. Performance is acceptable (< 2x slower than current for range searches)
