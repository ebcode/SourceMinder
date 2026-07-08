# query-index-web Development Guide

## Files

| File | Role |
|------|------|
| `query-index-web.c` | Web-safe extraction of query building logic. Compiles with no real sqlite3 linked. |
| `query-index-web.h` | Public types + function declarations for the web extraction surface. |
| `qi-web-entry.c` | WASM bridge entry point. Parses commands, builds SQL, formats output. Exports `qi_web_build`, `qi_web_format`, `qi_web_free_result`. |
| `html/qi-worker.js` | Web worker: loads `QiWebModule` (WASM) + `@sqlite.org/sqlite-wasm`, executes queries, marshals rows. |
| `html/app.js` | Main-thread xterm.js terminal UI. Posts commands to `qi-worker.js`. |
| `configure` | Regenerates Makefile including `web` and `web-smoke` targets. |

## Architecture

```
Browser (main thread)          Web Worker                     WASM (qi-web-entry.c)
xterm.js ──postMessage──>  qi-worker.js ──ccall──>  qi_web_build(command) → SQL string
  terminal UI                      │                qi_web_format(rows) → formatted text
                                   │
                           sqlite3.wasm (JS)
                           executes SQL, returns rows
```

Key design: **C builds SQL, JS owns the DB, C formats output.** No sqlite3 linked into the WASM module (34K binary).

## Testing & Parity

The fastest feedback loop is the parity harness, wrapped by `parity/check-parity.sh`:

```bash
parity/check-parity.sh                      # unit+integration tests AND the parity batch
parity/check-parity.sh --batch-only         # parity batch only
parity/check-parity.sh "qi % -f x.c -i com" # run ONE qi command; prints the diff if it diverges
parity/check-parity.sh -v                   # full harness output (no trimming)
parity/check-parity.sh -p negroni           # different project (default: sourceminder)
```

It defaults to the `sourceminder` project (the one with a source tree for `-e`/source flags),
trims the per-case noise to a summary, and returns 0 (all matched/passed) / 1 (divergence/failure)
/ 2 (harness error).

**The rebuild handshake:** the script has *no* build step on purpose — it tests whatever
`qi-web.js` is currently built. After any C/JS change, run `make web` yourself, then re-run the
script. (A bad build can emit many errors at once; keeping the build separate lets you act on them
before testing.)

Under the hood it drives two Node scripts in `test/web-harness/`: `parity.mjs` (native `qi` vs the
WASM bridge) and `run.mjs` (pure-helper unit tests + wasm/db integration). Add new parity cases to
the `BATCH` array in `test/web-harness/parity.mjs`.

## Build_info Count Round-Trip Protocol

Recurring pattern for anything that needs a DB count the C side can't compute (it has no sqlite3):

1. **C (`qi_web_build`)** emits a `*_SQL` query line in build_info (e.g. `COUNT_SQL|SELECT COUNT(*) ...`).
   For header lines whose *position* matters, C also emits a one-byte sentinel in the `HDR|` stream
   (e.g. `HDR|\x01` for the file-filter line) so `print_hdr_lines` knows where to inject it.
2. **JS worker (`qi-pipeline.js`)** runs the query against the DB and appends the result back into
   build_info as `KEY|N` (e.g. `buildResult += '\nFILE_FILTER_COUNT|' + n`). No ccall signature change.
3. **C (format function)** reads `KEY` back via `find_build_line`/`parse_int_value` and renders it.

Instances: `COUNT_SQL` (total matches), `BREAKDOWN_SQL` (context summary), `TOC_COUNT_SQL` (TOC
breakdown, with `IMP` counted as `COUNT(DISTINCT full_symbol)`), `FILE_FILTER_COUNT_SQL` +
the `HDR|\x01` sentinel + `FILE_FILTER_COUNT` (the "Filtering by file: N files matched" header,
counted with `build_common_filters_web` but *not* the search patterns — mirrors native
`count_distinct_files`), and `WITHIN_SQL` + the `HDR|\x02` sentinel + `WITHIN_COUNT` (the
"Within symbol(s): ... (N instances)" header; the worker already runs `WITHIN_SQL` to resolve
scope, so it just appends `withinRows.length` — `print_hdr_lines` pluralizes "symbol" from the
`WITHIN_SYMBOLS` token count and "instance" from `WITHIN_COUNT`).

## Known Divergences / Deferred

- **`--debug` SQL — intentional divergence (the web's is *better*).** Native prints the
  prepared-statement form with `?` placeholders (`symbol LIKE ? ESCAPE '\'`) and a
  `[Calculate column widths]` line. The web instead prints the **actual, inlined, runnable**
  SQL it executes (`symbol LIKE 'malloc' ...`), labeled and positioned at each execution point:
  `[Within lookup]`, `[File filter count]`, `[Main query]` (with `LIMIT`), `[Get total count]`,
  `[Get context summary]`. Goal: a user who downloads the project `.db` can paste any line into
  `sqlite3` and reproduce exactly what's shown above it (verified in the `run.mjs` debug case).
  The web omits `[Calculate column widths]` because it doesn't run that query — column widths are
  derived in C from the already-fetched rows. Mechanism: `qi-pipeline.js` appends the executed
  strings as `DEBUG_MAIN_SQL` / `DEBUG_COUNT_SQL` / `DEBUG_WITHIN_SQL` / `DEBUG_FILE_FILTER_SQL`
  (gated on `buildLines.DEBUG`, not the logger toggle); `print_debug_sql` renders them, and the
  breakdown SQL is passed as the new `debug_sql` arg to `qi_web_format_breakdown`. Because this
  diverges by design it is **not** in the parity batch — it has a dedicated `run.mjs` case instead.

- **`--full --toc` conflict not enforced** — native rejects `--full` with `--toc` (TOC conflict
  error); the web silently proceeds. Rare combo, not in the parity suite. (The `--full` table
  output itself is at full parity, including the context-column overflow when a full name like
  `FUNCTION` is wider than the compact-measured column.)

- **`--within` + `-f` file count** — the within scope is not injected into `FILE_FILTER_COUNT_SQL`
  (the subquery form makes `injectWhereClause` unsafe), so the "Filtering by file: N" count
  ignores `--within`. Rare combo, not in the parity suite.

## SQL Quoting: Three Layers of Escaping

This is the most error-prone area. Understand these three layers:

### Layer 1: C string literals → memory
```c
"ESCAPE '\\'"    // C source: \\ = 1 backslash in memory → SQL: ESCAPE '\'
"ESCAPE '\\\\'"  // C source: \\\\ = 2 backslashes in memory → SQL: ESCAPE '\\'
```
Rule: each `\\` in C source produces one literal `\` in the string.

### Layer 2: vsnprintf format string → SQL text
`vsnprintf` outputs the format string literally except for `%` sequences. There is **no** additional escape processing. Whatever backslashes are in the format string appear verbatim in the output.

### Layer 3: SQLite SQL parser → escape character
SQLite's string literal rules: `'\\'` is the two-character string `\ \ ` (backslash is not an escape in SQLite string literals). The `ESCAPE` clause requires exactly **one** character.

### Correct Pattern

For LIKE patterns with ESCAPE:
```c
// Native CLI (query-index.c line 970) — uses '%s' in format, value is NOT pre-quoted:
sql_append(builder, "%sparent_symbol LIKE '%s' ESCAPE '\\'",
           i > 0 ? " OR " : "", value);
// SQL output: ... parent_symbol LIKE 'wo' ESCAPE '\'
// C source: '\\' = 1 backslash → SQL: 1 backslash ✓

// Web (query-index-web.c line 411) — uses %s, value IS pre-quoted by %q:
char *escaped = sqlite3_mprintf("%q", value);
sql_append(builder, "%sparent_symbol LIKE %s ESCAPE '\\'",
           i > 0 ? " OR " : "", escaped);
// SQL output: ... parent_symbol LIKE 'wo' ESCAPE '\'
// C source: '\\' = 1 backslash → SQL: 1 backslash ✓
```

**Note the difference**: native uses `'%s'` (quotes in format string) because values are raw. Web uses `%s` (no quotes in format) because `sqlite3_mprintf("%q", ...)` already wraps values in `'...'`.

### Documented Bug: Backslash Doubling (2026-05-27)

**Symptom:** `SQLITE_ERROR: ESCAPE expression must be a single character`

**Root cause:** The web extraction originally had `ESCAPE '\\\\'` (4 backslashes in C source → 2 backslashes in the SQL string). SQLite saw `'\\'` as a two-character string, violating the single-character ESCAPE requirement.

**Why it happened:** The native CLI uses `'\\'` in C source → 1 backslash. When extracting to `query-index-web.c`, the author saw single backslashes elsewhere and doubled them, thinking they needed escaping for the `%s` (no-quotes) pattern. But `vsnprintf` does not interpret backslashes — only `%` format specifiers.

**Fix:** Changed `ESCAPE '\\\\'` → `ESCAPE '\\'` at `query-index-web.c:411` and `:423`.

**Detection tip:** Run `qi <query> --debug` on the native CLI to see the exact SQL. Compare with the web-generated SQL by logging `buildLines.SQL` in the worker console.

## sqlite3_mprintf Shim Gotchas

The web build uses a custom `sqlite3_mprintf` / `sqlite3_free` shim (`query-index-web.c:61-156`) because real sqlite3 is not linked into the WASM module.

### Measuring mode (size == 0)

`mprintf` works by calling `vsnprintf(NULL, 0, ...)` to measure output length, then allocating and writing. The shim's `%q` handler **must** support this:

```c
static int qi_sql_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    int measuring = (size == 0);
    // When measuring: count characters but don't write to buf.
    ...
}
```

**Documented Bug (2026-05-26):** The initial shim had `while (*fmt && pos < size)` which returned 0 when `size==0` (measuring mode). This caused `mprintf` to allocate 1 byte, truncating all `%q` output. Pattern strings vanished, producing `LIKE  ESCAPE` with no pattern → "near ESCAPE: syntax error".

### %q format specifier

The shim must handle SQLite's `%q` (quote-string). Behavior:
- NULL → outputs `NULL` (no quotes)
- Non-NULL → wraps in single quotes, doubles embedded single quotes

### Double-Quoting Bug (2026-05-26)

**Symptom:** Every query returned the same 24 empty-symbol rows regardless of search pattern.

**Root cause:** SQL builder format strings had `'%s'` while `sqlite3_mprintf("%q", ...)` was also wrapping values in `'...'`. Result: `''value''` → SQLite parsed `''` as empty-string literal, matching only rows with empty symbol columns.

**Fix:** Removed redundant `'...'` from all 13 format strings: `'%s'` → `%s`.

**Rule:** If using `sqlite3_mprintf("%q", ...)`, do NOT add quotes around `%s` in the format string.

## Building

```bash
./configure --enable-all    # regenerate Makefile (needed after configure changes)
make qi                     # native CLI (sanity check)
make web                    # WASM module → html/qi-web.js + html/qi-web.wasm
make web-smoke              # compile-only check of query-index-web.c
```

The `web` target copies output to `html/` automatically.

### Post-Reindex Workflow

Whenever the **indexer output changes** (e.g. the macro work that added `CONTEXT_MACRO` rows), the
`sourceminder` demo DB and its browser snapshot must be regenerated or parity will compare against
stale data:

```bash
# 1. Re-index the repo so code-index.db reflects the new indexer output, then:
bash html/sources/refresh-sourceminder.sh            # resync html/sources/sourceminder/ source tree
bash html/sources/browser-snapshot.sh --project sourceminder   # regenerate code-index-sourceminder.browser.db
# 2. Bump the sourceminder entry in html/projects.json: "version" (cache-busts the browser) and "sizeBytes".
```

Skipping step 2's `version` bump means returning browsers keep the cached old snapshot. The snapshot
must be non-WAL (`browser-snapshot.sh` uses `VACUUM INTO`, which handles this).

## Testing in Browser

```bash
cd html && npm run serve    # starts static file server
```
Then open the browser and type qi commands at the `qi>` prompt.

Worker console logging is still active — check browser DevTools → Console for SQL strings, row counts, and format output previews.

## TSV Row Marshaling

The worker marshals all 14 DB columns in `SELECT *` order:

| Index | DB Column      | Use |
|-------|---------------|-----|
| 0 | symbol (lowercase) | pattern matching (not displayed) |
| 1 | directory      | file grouping |
| 2 | filename       | file grouping |
| 3 | line           | LINE column |
| 4 | context        | CTX column (incl. `MACRO` — object/function-like C macros and Rust `macro_rules!`) |
| 5 | full_symbol    | SYM column |
| 6 | source_location| (not displayed) |
| 7 | parent_symbol  | PAR column |
| 8 | scope          | SCOPE column |
| 9 | namespace      | NS column |
| 10| modifier       | MOD column |
| 11| clue           | CLUE column |
| 12| type           | TYPE column |
| 13| is_definition  | D column |

## Conditional Column Compilation

`scope` and `namespace` are gated by `ENABLED(TYPESCRIPT) || ENABLED(PHP) || ...` in `shared/column_schema.def`. The web build defines `-DENABLE_TYPESCRIPT=1` (in `configure`) so these columns are always available in the WASM module. Without this, the `QueryFilters` struct lacks `.scope` and `.namespace` fields, causing compile errors if they're referenced.

## Column Flag Implementation Pattern

Each extensible column flag (`-p`, `-s`, `-ns`, `-m`, `-c`, `-t`, `-d`) has three parts:

1. **Parser** (`qi-web-entry.c:parse_command`): `parse_col_flag_values()` sets `show=1` and collects non-flag tokens as filter values.
2. **SQL builder** (`qi-web-entry.c:qi_web_build`): `POPULATE_COL_FILTER` macro copies filter values into `QueryFilters` fields (with wildcard conversion), which `build_common_filters_web` turns into `AND (column LIKE ...)` clauses.
3. **Format output** (`qi-web-entry.c:qi_web_build` COLUMNS| line): `SHOW_IF` macro adds the column name when `show` is set (or verbose mode is on).

Dual behavior: bare flag shows the column; flag with arguments shows AND filters.
