# qi Over-Filtering Diagnostic Plan

## Problem

When an agent's qi query returns zero results due to extensible column filters
(`-p`, `-t`, `-m`, `-c`, `-s`, `-ns`, `-d`), the diagnostic message blames only
`-i` (context) or `-f` (file). The agent drops `-i func` when the real culprit
is `-p Server`, wastes turns on the wrong fix, and abandons qi.

### Concrete example (flipt Haiku treatment, rep02)

```
T16: qi 'Evaluate' -i func -p Server -l 20 -v
      → "'Evaluate': 1 match excluded by -i. Re-run without -i to see them."

T18: qi 'Evaluate' -l 20 -v                        (drops -i, keeps -p? no, drops both)
      → 3 rows: COM, FUNC(*Server), STR

T20: qi 'Evaluate' -i func -p Server -e --raw       (re-adds -i, keeps -p)
      → "'Evaluate': 1 match excluded by -i."
```

The agent keeps `-p Server` across all attempts because the diagnostic never
names `-p` as a suspect. Before the substring-matching fix (`query-index.c:1052`),
`-p Server` failed to match Go's `*Server` pointer receiver. After the fix,
`-p Server` does substring matching and this specific case is resolved — but
any future over-filter with an extensible column will repeat the pattern.

## Root Cause

`diagnose_filter_exclusion()` (`query-index.c:2464`) only tests clearing three
filter categories:

1. `-i` (include context) — counts without the context filter
2. `-f` (file filter) — counts without the file filter
3. `-x` (exclude context) — counts without the exclude filter

Extensible column filters (`-p`, `-t`, `-m`, `-c`, `-s`, `-ns`, `-d`) are
never individually tested. When one of them is the sole cause of zero results,
the function falls through to the generic "all excluded by filters" message,
or worse, blames `-i`/`-f` if those happen to also be set (even if harmless).

## Proposed Fix

Extend `diagnose_filter_exclusion()` to test clearing each active extensible
column filter individually, and include the guilty flag(s) in the diagnostic
message alongside `-i` and `-f`.

### Design

For each extensible column that has `count > 0` in the current `QueryFilters`:

1. Create a copy of `filters` with that one column's count zeroed.
2. Call `get_total_count()` with the modified copy (all other filters — including
   `-i`, `-f`, `-x`, and other extensible columns — stay active).
3. If the count recovers from zero, add that flag to the `culprits` string.

The existing `culprits` buffer (32 bytes) is too small once extensible columns
join the list. Increase it to 128 bytes.

### Implementation

Use an X-Macro expansion over `column_schema.def` to generate the per-column
check blocks. Both `COLUMN` (TEXT filters) and `INT_COLUMN` (is_definition)
need checking — a `-d 1` filter that excludes all definitions is just as
misleading when unnamed.

```c
#define CHECK_EXTENSIBLE_FILTER(name, sql_type, c_type, width, full, compact, long_flag, short_flag, ...) \
    if (filters && filters->name.count > 0 && !has_within) { \
        QueryFilters no_col = *filters; \
        no_col.name.count = 0; \
        int n = get_total_count(db, patterns, include, exclude, &no_col, \
                                file_filter, within_ranges, line_range, debug); \
        if (n > 0) { \
            if (culprits[0] != '\0') { \
                strncat(culprits, " and ", sizeof(culprits) - strlen(culprits) - 1); \
            } \
            strncat(culprits, "-" #short_flag, sizeof(culprits) - strlen(culprits) - 1); \
            if (n > recovered) recovered = n; \
        } \
    }

#define CHECK_INT_FILTER(...) CHECK_EXTENSIBLE_FILTER(__VA_ARGS__)
```

Inserted into `diagnose_filter_exclusion()` after the `-x` check (line 2478),
before the culprit-build logic (line 2481).

`#short_flag` stringification produces `"p"`, `"t"`, `"m"`, `"c"`, `"s"`,
`"ns"`, `"d"` — which concatenate with `"-"` to form `"-p"`, `"-t"`, etc.

### Per-column cost

Each active extensible column adds one `get_total_count()` call (a `SELECT
COUNT(*)` with the modified filter set). This is cheap — the symbol-index
query is the same, only the WHERE clause changes. The function already runs
2–3 such queries today.

### Message format

Before: `'Evaluate': 1 match excluded by -i. Re-run without -i to see them.`
After:  `'Evaluate': 1 match excluded by -i and -p. Re-run without -i and -p to see them.`

The "Re-run without …" suffix stays the same; it just names all culprits.

## Files Changed

| File | Change |
|------|--------|
| `query-index.c` | Add X-Macro block in `diagnose_filter_exclusion()` to check extensible column filters; bump `culprits` buffer to 128 bytes |

## Verification

1. **Single extensible culprit.** Index a Go file with `func (s *Server) Evaluate()`.
   Run `qi Evaluate -i func -p Server -v` against the old binary (exact parent match)
   and confirm the diagnostic says `-p` not `-i`.

2. **Multiple culprits.** Run `qi Evaluate -i func -p Server -f nonexistent/` and
   confirm the diagnostic lists both `-i` and `-p` and `-f`.

3. **INT_COLUMN.** Run `qi Evaluate -d 0` (definitions only... no, `-d 0` means
   non-definitions). Verify `-d` appears when `is_definition` filter is the
   sole cause of zero results.

4. **No false positives.** Run `qi Evaluate -i func` on a file where Evaluate
   appears only as a COM (comment), not FUNC. Confirm the diagnostic still says
   `-i` and does NOT name extensible filters that aren't set.

5. **Within-range bailout.** With `--within` active (`has_within=1`), extensible
   checks should skip (matching the existing `!has_within` guard on `-i`/`-f`/`-x`).

## Notes

- After the substring-matching fix for TEXT column filters (session
  20260628), the specific `-p Server` vs `*Server` case no longer causes zero
  results. The diagnostic fix here covers any future over-filter — a new
  developer adding a column, a model using too many `-m`/`-c`/`-s` flags, etc.
- The existing `-i`/`-f`/`-x` checks are preserved unchanged; extensible
  checks are additive.
- `culprits` growing from 32 to 128 bytes stays on the stack (no malloc needed).
