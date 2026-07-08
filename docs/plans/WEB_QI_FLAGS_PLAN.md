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

| `--files` | done | `SELECT DISTINCT directory, filename` + file-list format |
| `--raw` | done | suppresses all framing; bare source for copy-paste |
| `-e` / `--expand` | done | source host bridge (worker fetch + render twin) |
| `-C` / `-A` / `-B` | done | source host bridge (worker fetch + render twin) |
| context breakdown | done | JS GROUP BY → `qi_web_format_breakdown` |
| match highlighting | done | ANSI dark-green background (matches CLI), not a `^^^` marker |

Remaining:
| Phase 9 parity testing | ongoing | column-width + Tip-line disparities fixed; more queries to diff |
| `NATIVE_CLI_FIXES_NEEDED.md` | not done | 13 logged native-CLI bugs, separate branch |

**Supporting infrastructure added since this plan was first written:**
- Multi-project dropdown + per-project `html/sources/<project>/` convention,
  DB load via Cache Storage with streamed download progress.
- Headless Node test harness (`make web-test`) sharing the real
  `html/qi-pipeline.js` so the browser path can't drift from what's tested.
- Asset cache-busting in `make web`: content-hashed `qi-web.<hash>.{js,wasm}`
  resolved via `html/asset-manifest.json`, plus per-load `?t` timestamps on
  `app.js` / `qi-worker.js` / `qi-pipeline.js`.

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

## Phase 6: Context Breakdown (Result Breakdown) ✅ DONE

**What the CLI shows after `Found N matches`, only when results are truncated
(`limit > 0 && total >= limit`):**
```
Result breakdown: COM (16702), ARG (14812), VAR (11370), ...
Tip: Use -i <context> to narrow results
```

**Implementation (as built — separate export, not a `qi_web_format` parameter):**
1. C emits `BREAKDOWN_SQL` alongside `COUNT_SQL` in `qi_web_build`.
2. The worker runs the breakdown GROUP BY **only** when `total > rows.length`
   (mirrors the CLI's truncated-result condition).
3. `qi_web_format_breakdown` formats the `Result breakdown:` line plus the
   `Tip: Use -i <context> to narrow results` line. `map_context_web()` (canonical
   in `query-index-web.c`) maps full context names to compact codes.

**Note:** `qi_web_format` itself does **not** print the Tip — that lives with the
breakdown so it appears only on truncation, matching the CLI (the unconditional
Tip in the format footer was removed during parity work).

## Phase 7: `--files` (Files-Only Output) ✅ DONE

**What it does:** Shows only distinct files matching filters, not individual symbols.

**Implementation (as built):**
1. Parser sets `files_mode`; branch placed after `POPULATE_COL_FILTER` to reuse
   the built filter structures.
2. `qi_web_build` emits `MODE|files` + `FILES_SQL|SELECT DISTINCT directory,
   filename ... ORDER BY directory, filename`.
3. `qi_web_format_files` formats one filepath per line with a footer; the worker
   `files` branch mirrors the `toc` branch.

## Phase 8: HOST_BRIDGED Features (Source-Dependent) ✅ DONE

**Flags:** `-e`/`--expand`, `-C N`, `-A N`, `-B N`, `--raw`

**These require source-file content**, which the browser can't read from disk.
The host bridge was built as a **superset-of-files fetch + render twins** rather
than the per-request callback originally sketched.

### Source Fetch Bridge (as built)

1. **C build:** `qi_web_build` emits `NEEDS_SOURCE` / `EXPAND` / `CONTEXT_BEFORE`
   / `CONTEXT_AFTER` / `RAW` in build_info when any source flag is present.
2. **Worker fetch (`SourceCache` in `qi-worker.js`):** for every displayed row's
   file, fetches the whole file from `html/sources/<project>/` (deduped, session
   cached, bounded-concurrency pool, 404 → omit). The result is marshaled into
   the WASM heap as a **NUL-framed blob** (`path\0content\0...`) via `_malloc` +
   `stringToUTF8` — zero per-file copies on the C side.
3. **C render (`shared-web/source-render-web.c`):** render *twins* of the CLI
   functions — `print_lines_range_web` (`-e`), `print_context_lines_web`
   (`-C/-A/-B`), orchestrated by `print_expansion_or_context_web`. They write to
   a `WebOutput` buffer instead of `FILE*`; `source_map_parse` stores in-place
   pointers into the blob. C alone decides per row what to render (`-e` only for
   definitions); JS just supplies a superset of files, so no parity logic is
   duplicated in JS.

**Key invariant:** the C `build_row_filepath` formula and the worker's
`buildRowFilepath` must stay identical — that string is the blob lookup key.

**`--raw`:** suppresses all non-source framing (header, table, line numbers,
stats) so `-e`/`-B`/`-A` output is bare source suitable for copy-paste into an
Edit `old_string`.

**Snapshot-consistency caveat:** the indexed line numbers must match the served
source. Regenerate the browser DB (`html/sources/browser-snapshot.sh`, which uses
`VACUUM INTO`) after editing indexed files, or expansions will be off by the drift.

## Phase 9: Exact Output Parity (ongoing)

All flags are wired; this is now the active polish phase — side-by-side diffs of
`qi` CLI output vs browser output, fixing divergences as found.

**Disparities found and fixed:**
- **Empty-column widths:** the web column registry seeded widths from a fixed
  `default_width` floor, so empty columns stayed wide. Now seeded from header
  width only, matching the CLI's `max(header, data)` (`update_column_widths`).
- **Stray Tip line:** `qi_web_format` printed `Tip: Use -i <context> to narrow
  results` unconditionally; the CLI prints it only with the truncation breakdown.
  Removed from the format footer (the breakdown path already emits it).

### Methodology: differential harness (`make web-parity`)

A new `test/web-harness/parity.mjs` runs a corpus of qi commands through **both**
paths against the **same DB file** and diffs them. CLI output is source-of-truth.

- **Same-index guarantee:** both sides target the identical `.browser.db`
  snapshot — CLI via `qi <cmd> --db-file html/code-index-<project>.browser.db`,
  web via `pipeline.runQuery` against the same file. Same bytes ⇒ identical
  paths/columns/ordering as inputs, so any output diff is a real divergence.
- **Reference project:** flask (`code-index-flask.browser.db`) to start; expand
  to other languages later.
- **Comparison:** ANSI-stripped structural diff (strip color, trailing ws, CRLF).
  Color parity is a possible later pass.
- **`--debug` excluded** from the corpus — its `SQL:` text differs by design
  (web embeds patterns / EXISTS self-joins vs CLI bound params / temp tables).
- **Gating:** standalone `make web-parity` (skips with a clear message if the
  `qi` binary isn't built); not folded into the fast `make web-test` smoke test.
- **Known-divergence allowlist** (`parity-known-diffs.json`): only for genuinely
  intended differences, each documented. Row-order divergences are bugs to fix,
  not to allowlist.
- Shared `normalize.mjs` is reused by `run.mjs` so both harnesses agree on rules.

Representative queries to diff (full corpus organized by flag category in
`parity-cases.mjs`):

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
5. **--debug flag** + gated worker/app.js logging behind a `debug` flag
6. **--toc** — pure DB + formatting via `shared-web/toc-web.c`, no file access needed
7. **Phase 6: Context Breakdown** — `qi_web_format_breakdown`, worker-gated on truncation
8. **Phase 7: --files** — `qi_web_format_files`
9. **Phase 8: Host Bridge** — `-e`/`-C`/`-A`/`-B`/`--raw` via superset fetch + render twins
10. **Persist command history to localStorage** — 500-entry cap
11. **Multi-project dropdown + Node test harness (`make web-test`) + asset cache-busting**

### Remaining (in recommended order)

12. **Phase 9: Parity Testing** — ongoing; column-width + Tip-line fixed, more to diff
13. **Apply NATIVE_CLI_FIXES_NEEDED.md** — 13 bugs in native CLI (separate branch)

(xterm.js upgrade intentionally deferred: ESM-only v6 brings no major win for
this use, and v7 is imminent — revisit after it lands.)

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
