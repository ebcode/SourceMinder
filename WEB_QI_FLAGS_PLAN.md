# Web qi Flag Implementation Plan

## Current State

The WASM bridge supports a superset of qi flags:

| Flag | Status | Notes |
|------|--------|-------|
| positional patterns | done | |
| `-i` / `--include-context` | done | |
| `-x` / `--exclude-context` | done | includes `-x noise` |
| `-f` / `--file` | done | |
| `--def` | done | |
| `--usage` | done | |
| `--limit` | done | |
| `-v` / `--verbose` | done | full CLI-style column registry |
| `--columns` | done | alias support, comma/space syntax |
| `--compact` | done | show raw DB context codes |
| `-p` / `--parent` | done | show column + AND filter |
| `-s` / `--scope` | done | show column + AND filter |
| `-ns` / `--namespace` | done | show column + AND filter |
| `-m` / `--modifier` | done | show column + AND filter |
| `-c` / `--clue` | done | show column + AND filter |
| `-t` / `--type` | done | show column + AND filter |
| `-d` / `--definition` | done | show column + AND filter |
| `--debug` | done | emits SQL in output |
| `--and [N]` | done | EXISTS self-join (single SQL, no temp tables) |
| `--within SYMBOL` | done | JS-side lookup + WHERE injection |
| `--toc` | done | no file access needed, pure DB + formatting |
| arrow-key prompt editing | done | history ring, cursor movement, VT redraw |

Remaining:
| `--files` | not done | |
| `-e` | not done | blocked on source-fetch host bridge |
| `-C` / `-A` / `-B` | not done | blocked on source-fetch host bridge |
| context breakdown | not done | JS GROUP BY + format extension |
| syntax highlight marker | not done | `^^^ N ^^^` in output |

## Architecture: Where Each Flag Lives

```
Main thread (html/app.js)    Worker (html/qi-worker.js)
─────────────────────        ──────────────────────────────
- xterm.js terminal UI       - qi.wasm: build SQL, format output
- command history/editing    - sqlite3: execute SQL, within lookups
- worker messaging           - marshal rows (14 canonical columns)
```

The C/WASM module builds SQL and formats output; the JS worker owns the
sqlite3 connection via `@sqlite.org/sqlite-wasm`. No sqlite3 is linked
into the WASM module — this keeps the binary small (34K) and avoids
dual sqlite3 runtimes.

Flags that affect SQL building go through C. Flags that affect output
formatting go through C. Flags that require source-file access
(HOST_BRIDGED) need a JS callback or a separate fetch path. Flags that
need definition lookups (`--within`) are split: C emits the lookup SQL
as metadata, JS executes it and injects WHERE clauses.

**Architectural decisions made:**
- WASIX was rejected as too heavy for the page download budget.
- Temp-table-based `--and` was replaced with EXISTS self-joins (single SQL).
- `--within` uses JS-side DB queries + string injection rather than
  multi-statement bridge protocol or in-WASM sqlite3.
- `--toc` was initially flagged as HOST_BRIDGED but proved to be fully
  database-only: `build_toc_web_sql()` in `shared-web/toc-web.c` builds
  the SQL, JS executes it, and `format_toc_web()` renders the output.
  No file access required.

## Phase 1: Verbose Mode (`-v` / `--verbose`) ✅ DONE

**What it does:** Shows all extensible columns (parent_symbol, modifier, clue,
scope, namespace, definition_type) in the output table.

**Implementation (as built):**
1. `WebCommand.verbose` flag set by parser.
2. SQL unchanged — `SELECT *` already fetches all columns.
3. `qi_web_format` uses a `WebColSpec` table (name, header, header_compact,
   tsv_index, width, is_int) to control column display. `SHOW_IF` macro
   checks `cmd.verbose` (show all) or per-flag `.show` toggles.
4. Build info emits `VERBOSE|1`.
5. TSV extended from 5 fields to all 14 canonical columns.

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

## Phase 2: `--columns` (Custom Column Selection) ✅ DONE

**What it does:** Lets the user pick which columns to show.

**Implementation (as built):**
1. Parser detects `--columns`, collects column name arguments.
2. Alias support: sym→symbol, ctx→context, par→parent, mod→modifier,
   ns→namespace, d→definition. Comma-separated and space-separated syntax.
3. Build info emits `COLUMNS|col1 col2 ...`.
4. `--columns` overrides both default selection and flag-based toggles.

## Phase 3: `--and` / Proximity Search ✅ DONE

**What it does:** Finds patterns within N lines of each other (default: same line).

**Implementation (as built — EXISTS self-join, NOT temp tables):**
1. Parser detects `--and [N]`, validates N is a positive integer.
2. `build_query_sql_proximity_web()` generates a single EXISTS self-join
   query — no temp tables, no multi-statement bridge, no sqlite3 in WASM.
3. For N patterns, generates N EXISTS clauses checking
   `ABS(line - ci.line) <= R`.
4. Same-line case (range=0) uses the existing INTERSECT-based path.
5. Naturally deduplicates (temp tables can insert duplicates when anchor
   ranges overlap).
6. Performance: 3-pattern `--and 10` on 62K-row index ~850ms/query.
   A composite index on `(directory, filename, symbol, line)` would help.

**Note:** The dead `execute_proximity_to_temp_table_web()` and
`lookup_within_definitions_web()` functions have been removed from
`query-index-web.c` (~620 lines deleted in total across both).

## Phase 4: `--within` (Definition-Local Scoping) ✅ DONE

**What it does:** Restricts search to the definition body of a named symbol.

**Implementation (as built — JS-side lookup + WHERE injection):**
1. Parser detects `--within SYMBOL...`, collects symbol names.
2. C emits `WITHIN_SQL|SELECT directory, filename, source_location, symbol ...`
   in build_info plus `WITHIN_SYMBOLS|sym1 sym2 ...`.
3. JS worker executes the lookup SQL, parses `source_location` → line ranges
   (format: `line_start|line_end`), injects
   `AND ((directory='d' AND filename='f' AND line BETWEEN s AND e))`
   before `ORDER BY`.
4. Automatically detects self-join alias (`ci.`) so `--within` works
   correctly with `--and`.
5. Validates every requested symbol has a definition (not just that
   SOME symbol matched). Reports "No definition found for..." on missing.

## Phase 5: `--compact` (Compact Context Display) ✅ DONE

**What it does:** Shows abbreviated context names (FUNC not FUNCTION).

**Implementation (as built):**
1. Parser detects `--compact` flag.
2. Build info emits `COMPACT|1`.
3. Format uses the `header_compact` field from the web column registry
   instead of the full `header`.

## Column Filter Flags ✅ DONE (Not in Original Plan)

**Flags:** `-p`/`--parent`, `-s`/`--scope`, `-ns`/`--namespace`,
`-m`/`--modifier`, `-c`/`--clue`, `-t`/`--type`, `-d`/`--definition`

**Dual behavior:** Bare flag shows the column. Flag with values shows
AND filters (OR within column, AND across columns).

**Implementation:**
1. `parse_col_flag_values()`: always sets `show=1`, then collects
   following non-flag tokens as filter values.
2. SHOW_IF macro checks both verbose and per-flag show toggles.
3. `build_common_filters_web` now accepts a `col_prefix` parameter for
   table alias support (e.g., `"ci."` for self-join queries).

**Note:** Required `-DENABLE_TYPESCRIPT=1` in the emcc configure flags
so scope/namespace columns compile into QueryFilters.

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

**Flags:** `-e`, `-C N`, `-A N`, `-B N`

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

### ✅ Completed

1. **Phase 1-2: Verbose + Columns** — biggest visual improvement, no arch changes
2. **Phase 5: --compact** — trivial
3. **Column Filter Flags** — `-p`/`-s`/`-ns`/`-m`/`-c`/`-t`/`-d`
4. **Phase 3-4: --and + --within** — solved with EXISTS self-join + JS-side lookup
5. **--debug flag** — useful for development
6. **--toc** — pure DB + formatting via `shared-web/toc-web.c`, no file access needed

### Remaining (in recommended order)

6. **Phase 6: Context Breakdown** — small change, big UX improvement
7. **Phase 7: --files** — small, useful standalone flag
8. **Strip/gate debug console.log statements** in worker and app.js
9. **Upgrade xterm.js** from v5.3.0 (deprecated UMD) to `@xterm/xterm`
10. **Persist command history to localStorage**
11. **Phase 8: Host Bridge** — blocked on source-fetch architecture
12. **Phase 9: Parity Testing** — final polish
13. **Apply NATIVE_CLI_FIXES_NEEDED.md** — 6 bugs in native CLI (separate branch)

## Open Questions

- ~~Should verbose columns be computed in JS (GROUP BY) or passed from C?~~
  → Resolved: JS provides width data via TSV; C format function uses a
    `WebColSpec` registry (tsv_index-based, not function-pointer-based).

- ~~Should `--and` same-line mode (range=0) work now?~~
  → Resolved: Yes, INTERSECT path handles range=0. Full `--and` with range
    uses EXISTS self-join — single SQL, no temp tables.

- ~~Should the context breakdown use `get_context_summary_web` (which needs
  sqlite3 in WASM) or a separate JS GROUP BY query?~~
  → Resolved: JS GROUP BY is simpler and avoids adding sqlite3 to WASM.
    The dead `get_context_summary_web` and other counting functions have
    been removed from query-index-web.c.

- ~~Should we pivot to WASIX for `--and`/`--within`?~~
  → Resolved: No, WASIX is too heavy for page download budget. Server-side
    source-file API is the right approach for HOST_BRIDGED flags.

- ~~Should `--and` use temp tables (like the native CLI) or a single SQL approach?~~
  → Resolved: EXISTS self-join produces a single SQL statement, naturally
    deduplicates, handles wildcards correctly, and requires no multi-statement
    bridge protocol or in-WASM sqlite3. Performance is acceptable for
    interactive use on the current 62K-row index.
