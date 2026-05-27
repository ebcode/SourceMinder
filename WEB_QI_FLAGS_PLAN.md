# Web qi Flag Implementation Plan

## Current State

The WASM bridge supports a subset of qi flags:

| Flag | Status | Notes |
|------|--------|-------|
| positional patterns | done | |
| `-i` / `--include-context` | done | |
| `-x` / `--exclude-context` | done | includes `-x noise` |
| `-f` / `--file` | done | |
| `--def` | done | |
| `--usage` | done | |
| `--limit` | done | |

Output is a basic formatted table (LINE | SYM | CTX) without verbose columns,
context breakdowns, or the full CLI result summary.

## Architecture: Where Each Flag Lives

```
JS (html/app.js)          WASM (qi-web-entry.c + query-index-web.c)
─────────────────         ─────────────────────────────────────────
- parse command           - build SQL (build_query_sql_web)
- execute SQL             - format output (qi_web_format)
- marshal rows
```

Flags that affect SQL building go through C. Flags that affect output formatting
go through C. Flags that require source-file access (HOST_BRIDGED) need a JS callback
or a separate fetch path.

## Phase 1: Verbose Mode (`-v` / `--verbose`)

**What it does:** Shows all extensible columns (parent_symbol, modifier, clue,
scope, namespace, definition_type) in the output table.

**Implementation:**
1. `qi_web_build` parser: detect `-v` / `--verbose` flag, include in build info.
2. `qi_web_build` SQL: no change — `SELECT *` already fetches all columns.
3. `qi_web_format`: when verbose, emit additional column headers and data.
   Need to know column names from `shared/column_schema.def`.

**Column schema (from shared/column_schema.def):**
```
COLUMN  parent_symbol  TEXT    8  PARENT  PAR
COLUMN  modifier       TEXT    8  MODIFIER MOD
COLUMN  clue           TEXT    8  CLUE    CLUE
COLUMN  scope          TEXT    8  SCOPE   SCO
COLUMN  namespace      TEXT    8  NS      NS
COLUMN  definition_type TEXT   7  TYPE    TYPE
INT_COLUMN is_definition INT   1  D       D
```

**Build info extension:** Add a `VERBOSE|1` line to the build result.
**TSV format extension:** Add extra columns after the core 5 (line, ctx, sym, dir, file).

**Work:** medium — needs column-schema awareness in the format function.

## Phase 2: `--columns` (Custom Column Selection)

**What it does:** Lets the user pick which columns to show.

**Implementation:**
1. Parser: detect `--columns` flag, collect column name arguments.
2. Build info: add `COLUMNS|col1 col2 ...` line.
3. Format: use the column list to control which columns appear/their order.

**Dependency:** Phase 1 (column infrastructure).

**Work:** small once verbose mode exists.

## Phase 3: `--and` / Proximity Search

**What it does:** Finds patterns within N lines of each other (default: same line).

**Implementation:**
1. Parser: detect `--and [N]` flag.
2. `qi_web_build`: call `execute_proximity_to_temp_table_web()` before building
   the main query. Problem: this function needs a real sqlite3 connection to
   create temp tables and run subqueries.
3. **Architecture decision needed:** proximity search requires multi-step SQL
   execution (CREATE TEMP TABLE, INSERT, SELECT). The current bridge only
   returns a single SQL string. Options:
   - (a) Return multiple SQL statements, JS executes them sequentially
   - (b) Move the entire query execution + temp table logic into JS
   - (c) Wait until we have sqlite3 in WASM

**Recommendation:** Defer full `--and` support until we have a path for
multi-statement execution or in-WASM sqlite3. For now, the same-line
INTERSECT-based query works for the special case of range=0.

**Work:** large — requires architectural change.

## Phase 4: `--within` (Definition-Local Scoping)

**What it does:** Restricts search to the definition body of a named symbol.

**Implementation:**
1. Parser: detect `--within SYMBOL` flag.
2. `qi_web_build`: call `lookup_within_definitions_web()` to find definition
   ranges, then pass them to `build_query_sql_web()` as within_ranges.
3. `lookup_within_definitions_web` needs sqlite3 access to query for definitions.
   Same problem as Phase 3.

**Recommendation:** Group with `--and` for the multi-statement execution
solution.

**Work:** small code-wise, blocked by same architectural constraint.

## Phase 5: `--compact` (Compact Context Display)

**What it does:** Shows abbreviated context names (FUNC not FUNCTION).

**Implementation:**
1. Parser: detect `--compact` flag.
2. Build info: add `COMPACT|1` line.
3. Format: skip the `display_context` expansion, show raw DB context codes.

**Already partially working:** DB stores compact form. Just need to pass the
flag through.

**Work:** trivial.

## Phase 6: Context Breakdown (Result Breakdown)

**What the CLI shows after `Found N matches`:**
```
Result breakdown: COM (16702), ARG (14812), VAR (11370), ...
Tip: Use -i <context> to narrow results
```

**Implementation:**
1. JS runs a GROUP BY context query against the browser DB.
2. JS passes the breakdown as a string to `qi_web_format`.
3. Format appends it after the match count.

**New format API:** Extend `qi_web_format` to accept an optional context
breakdown string:
```
qi_web_format(build_info, rows_tsv, total, shown, context_breakdown)
```

**Work:** small — one extra DB query in JS, one extra parameter to format.

## Phase 7: `--files` (Files-Only Output)

**What it does:** Shows only distinct files matching filters, not individual symbols.

**Implementation:**
1. Parser: detect `--files` flag.
2. `qi_web_build`: generate `SELECT DISTINCT directory, filename FROM code_index WHERE (...) ORDER BY directory, filename` instead of the full row query.
3. `qi_web_format`: when files-only, format as a simple file list instead of a table.

**Work:** small.

## Phase 8: HOST_BRIDGED Features (Source-Dependent)

**Flags:** `-e`, `-C N`, `-A N`, `-B N`, `--toc`

**These require reading source files from disk**, which the browser can't do.
They need a host bridge:

### Source Fetch Bridge Design

1. **C side:** Instead of reading files from disk, the C code identifies what
   file content it needs (path, line range) and places a request.
2. **JS side:** Fetches the file content (from a static file server or an API)
   and passes it back to C.
3. **Format function** uses the fetched content to render context lines or
   expanded definitions.

### Bridge API Sketch

```c
// C side: places a source request
typedef struct {
    char path[PATH_MAX_LENGTH];
    int start_line;
    int end_line;
} SourceRequest;

// Called by format function when it encounters -e/-C/-A/-B
// In CLI: reads from filesystem. In browser: calls back to JS.
char *qi_web_fetch_source(const char *path, int start, int end);
```

In the JS bridge, `qi_web_fetch_source` would be implemented via a JS callback
that does `fetch()` against a static file server or source-content API.

### Order of Implementation for Host-Bridged Features

1. `-C N` (context lines) — simplest: fetch N lines before/after each match.
2. `-A N` / `-B N` — same as `-C` but asymmetric.
3. `-e` (expand definitions) — fetches the full definition span using
   `parse_source_location()` to compute start/end.
4. `--toc` (table of contents) — most complex, assembles file-level structure
   from indexed metadata.

**Work:** large for the full bridge. Start with `-C N` as proof of concept.

## Phase 9: Exact Output Parity

Once all flags are wired, do a side-by-side comparison of `qi` CLI output
vs browser output for representative queries:

| Test Query | CLI | Browser |
|---|---|---|
| `qi user` | single pattern | |
| `qi user -i func var` | multi-include | |
| `qi user -x noise -f *.c` | exclude + file filter | |
| `qi user --def -e` | definitions + expansion | |
| `qi user -v` | verbose columns | |
| `qi user -C 3` | context lines | |
| `qi % -i call -x noise --limit 20` | wildcard baseline | |

## Recommended Implementation Order

1. **Phase 1-2: Verbose + Columns** — biggest visual improvement, no arch changes
2. **Phase 6: Context Breakdown** — small change, big UX improvement
3. **Phase 5: --compact** — trivial
4. **Phase 7: --files** — small, useful standalone flag
5. **Phase 3-4: --and + --within** — blocked on multi-statement execution
6. **Phase 8: Host Bridge** — blocked on source-fetch architecture
7. **Phase 9: Parity Testing** — final polish

## Open Questions

- Should verbose columns be computed in JS (GROUP BY) or passed from C?
  - JS side is simpler since column widths need the actual data.
- Should `--and` same-line mode (range=0) work now? The INTERSECT-based
  SQL builder already handles this case without temp tables.
- Should the context breakdown use `get_context_summary_web` (which needs
  sqlite3 in WASM) or a separate JS GROUP BY query?
  - JS GROUP BY is simpler and avoids adding sqlite3 to WASM.
