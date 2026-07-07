# Query Instrumentation Plan

Goal: make `qi`'s SQLite behavior **observable** so we can measure — not guess —
where time goes on a ~10 GB `code-index.db`, broken down by the individual
sub-queries a single `qi` invocation fires and by the flag combinations that
trigger them.

This plan covers four composable mechanisms (referred to throughout by their
discussion numbers): **Idea 2** (trace backbone), **Idea 3** (why-it's-slow
capture), **Idea 4** (structured records for flag sweeps), and **Idea 5**
(realistic cold/warm harness). Idea 1 (manual per-call-site stopwatches) is
intentionally skipped — Idea 2 subsumes it with one hook and zero call-site
edits.

## Why this matters

- A `qi` call is not one query — `--debug` shows it fires up to ~5 (`[Main query]`,
  `[Calculate column widths]`, `[Get total count]`, `[Get context summary]`,
  `[Get total file count]`, plus `[Files query]` / `[Anchor]` / `[Range]` for
  certain flags). We need per-statement truth, not a single wall-clock number.
- The things that matter here are per-statement **latency**, whether the planner
  **scans or seeks**, and **rows examined vs. returned**.
- **Benchmarks lie (#19); understand your hardware (#14).** On a 10 GB DB the OS
  page cache dominates. Cold and warm runs can differ by 100×. Any number
  reported without a cold/warm distinction is misleading.
- Lean on documented, stable SQLite APIs (`sqlite3_trace_v2`, `sqlite3_stmt_status`,
  `EXPLAIN QUERY PLAN`) rather than reinventing timing.

## Shared infrastructure (build once, used by all four)

A single activation gate and output sink, so instrumentation is off by default
and adds nothing to normal runs.

- **Activation:** environment variable `SMQI_TIMING`.
  - `SMQI_TIMING=1` → human-readable timing lines to `stderr`.
  - `SMQI_TIMING=csv` → one machine-readable record per invocation (Idea 4).
  - `SMQI_TIMING=explain` → also emit EXPLAIN QUERY PLAN per statement (Idea 3).
  - unset / `0` → fully inert (no hook registered, no counters read).
  - Rationale for env var over a CLI flag: it must be settable across a sweep of
    many `qi` invocations (Ideas 4/5) without editing each command line, and it
    must be readable inside `db_init` before query flags are parsed. A `--timing`
    flag can be added later as sugar that sets the same internal state.
- **Output stream:** `stderr`, so timing never contaminates `--raw`/piped
  results on `stdout` (preserves the contract documented for `--raw`).
- **Invocation correlation id:** capture `argv` (the pattern + flags) once at
  startup so every emitted record can be tied back to the exact command. This is
  what makes "which combination of flags" answerable.

---

## Idea 2 — `sqlite3_trace_v2(SQLITE_TRACE_PROFILE)` backbone (do this first)

One hook, total coverage. SQLite invokes a profile callback **after every
statement finishes**, passing the prepared statement and the elapsed time in
**nanoseconds**. Register it once when the DB is opened and every statement —
including any we'd forget to wrap by hand — is timed automatically.

### Where it hooks
- `shared/database.c`, in `db_init()` (line ~78), immediately after the
  successful `sqlite3_open()` (line ~81). Register only when `SMQI_TIMING` is
  set.
- Teardown: nothing required, but mirror it in `db_close()` (line ~186) for
  symmetry if we ever want to detach mid-run.

### Sketch
```c
/* in db_init(), after sqlite3_open succeeds */
if (getenv("SMQI_TIMING")) {
    sqlite3_trace_v2(db->db, SQLITE_TRACE_PROFILE, profile_callback, NULL);
}

static int profile_callback(unsigned mask, void *ctx,
                            void *p_stmt, void *p_ns) {
    (void)mask; (void)ctx;
    sqlite3_stmt *stmt = (sqlite3_stmt *)p_stmt;
    sqlite3_int64 ns = *(sqlite3_int64 *)p_ns;
    const char *sql = sqlite3_sql(stmt);            /* static, no free */
    fprintf(stderr, "TIMING %8.2f ms  %s\n", ns / 1.0e6, sql);
    return 0;
}
```

### What it reveals
- Per-statement latency for **all** sub-queries, no per-call-site edits.
- Naturally separates, e.g., the `[Main query]` cost from the
  `[Calculate column widths]` pre-pass and the `[Get total count]` summary —
  letting us see which pass dominates for a given flag combo.

### Correlating to the friendly labels
`sqlite3_sql()` returns the SQL text, not our `[Main query]` label. Two options:
1. **Match by SQL prefix** against the known builders (cheap, no code churn).
2. **Tag statements explicitly** — keep a small map from prepared `stmt` pointer
   to label, set where each query is built (the existing `SQL: [label]` sites in
   `query-index.c`). Preferred if we want clean labels in the output.

### Cost / tradeoffs
- Effectively zero overhead when `SMQI_TIMING` is unset (hook not registered).
- Documented, stable API (precept #20).
- Limitation: times execution, not planning. Pair with Idea 3 for the "why."

---

## Idea 3 — Capture *why* it's slow: EXPLAIN QUERY PLAN + scan counters

Latency says a statement is slow; it does not say it's doing a full scan. This is
the class of bug we just fixed (the BINARY `idx_symbol` that forced
case-insensitive `LIKE` into `SCAN`). Two complementary, low-cost signals:

### 3a. EXPLAIN QUERY PLAN per statement (`SMQI_TIMING=explain`)
For each built SQL string, before executing, run `EXPLAIN QUERY PLAN <sql>` and
print the result alongside the statement.

- **Where it hooks:** at each query-build site in `query-index.c` (the functions
  that already `printf("SQL: [label] ...")`: `print_results_by_file`,
  `calculate_column_widths_from_query`, `get_total_count`, `get_context_summary`,
  `get_total_file_count`, `print_files_only`, `execute_proximity_to_temp_table`,
  `count_pattern_matches`). A single helper `explain_and_log(db, sql)` keeps it
  DRY.
- **What it reveals:** `SCAN code_index` vs `SEARCH ... USING INDEX idx_symbol`,
  temp B-tree sorts for `ORDER BY directory, filename, line`, and which index (if
  any) each flag combination actually engages.
- **Cost:** near-zero — EQP plans but does not execute.
- **Precept fit:** #1 (the planner's real choice is reality), #4 (make scans
  loud).

### 3b. Per-statement scan/sort counters via `sqlite3_stmt_status()`
After a statement's step loop completes, read:
- `SQLITE_STMTSTATUS_FULLSCAN_STEP` — how many steps hit a full scan.
- `SQLITE_STMTSTATUS_SORT` — whether a sort (temp B-tree) occurred.
- `SQLITE_STMTSTATUS_AUTOINDEX` — whether SQLite built a throwaway index.

- **What it reveals:** a *quantitative* "did this scan / sort" number — ideal for
  automated flagging in the harness ("warn if FULLSCAN_STEP > 0"), without
  parsing EQP text.
- **Where it hooks:** same step-loop sites; or fold into the Idea 2 callback if
  we tag statements (counters are read from the `stmt` the callback receives).
- **Precept fit:** #18 (instrument the things that matter, well).

---

## Idea 4 — Structured records for flag-combination sweeps (`SMQI_TIMING=csv`)

To compare *combinations of flags* rather than eyeball one run, emit one
machine-readable record per `qi` invocation.

### Record shape (CSV header)
```
timestamp,argv,subquery_label,elapsed_ms,rows_returned,fullscan_steps,sort,used_index
```
- One row per sub-query, sharing the same `argv` so the whole invocation
  reconstructs.
- `argv` is the full command (pattern + flags) — the join key for analysis.
- `used_index` derived from EQP (3a) or counters (3b).

### Where it hooks
- The Idea 2 callback (timing + counters) plus the captured `argv` and label map.
- Append to a file when `SMQI_TIMING=csv` (path from `SMQI_TIMING_FILE`, default
  `./qi-timing.csv`), so a sweep accumulates into one analyzable dataset.

### What it enables
- A driver script runs a matrix of representative queries and flag combos:
  - pattern shapes: `exact`, `prefix*`, `*contains*`, `.ingleChar`
  - context filters: `-i func`, `-i var`, `-x noise`
  - structural: `--and`, `--and 10`, `--within <sym>`, `-e`, `-C 3`, `--toc`
  - file filters: `-f .c`, `-f shared/`
  - limits: none, `--limit 20`, `--limit-per-file 3`
- Aggregate (e.g. `csvstat`, a tiny Python/awk reducer, or even `qi` itself on
  the source) to a comparison table: median ms per (flag-combo × sub-query),
  and which combos trigger a full scan.

### Precept fit
- #18 (measured well = analyzable later), #16 (smallest change that tests an
  assumption; iterate). #6/#5 — the CSV is a small, stable encoding contract, so
  keep the columns append-only if we extend it.

---

## Idea 5 — Realistic cold/warm benchmark harness

The numbers from Ideas 2–4 are only trustworthy if measured honestly. On a 10 GB
DB the OS page cache is the dominant variable.

### What it does
A script (`benchmark/qi-bench.sh` or similar) that:
1. Reads the query/flag matrix (same matrix Idea 4 sweeps).
2. For each query, runs it **cold** and **warm**:
   - **Cold:** drop the page cache first (`echo 3 | sudo tee
     /proc/sys/vm/drop_caches`, or document the sudo requirement and fall back to
     "first run after a fresh boot" when unavailable). Record the first-run time.
   - **Warm:** run N times (e.g. 5), report the **median** (robust to outliers).
3. Records, per query: cold ms, warm median ms, rows returned, and whether the DB
   fit in RAM (compare DB size to `MemAvailable`).
4. Emits a table separating cold vs warm — never a single blended number.

### Why both numbers
- **Warm** reflects repeated interactive use (what a developer feels in a hot
  loop).
- **Cold** reflects first-touch / CI / freshly-rebuilt-index cost.
- Latency vs throughput (#15): for interactive `qi`, **latency** of a single
  query is what matters; the harness should optimize-measure for that, not
  aggregate throughput.

### Precept fit
- #19 (test under realistic conditions, real 10 GB data), #14 (disk vs cache
  latency are different worlds), #21 (operability — a repeatable harness makes
  regressions catchable).

---

## How the four compose

```
Idea 2 (trace backbone)  ── times every statement, always-on when gated
        │
        ├── Idea 3a EQP ........ explains the slow ones (scan vs seek)
        ├── Idea 3b counters ... quantifies scans/sorts for auto-flagging
        │
        └── Idea 4 (csv) ....... persists timing+counters+argv per run
                    │
                    └── Idea 5 (harness) ... drives the matrix cold & warm,
                                             consumes the csv, reports honestly
```

- Idea 2 is the foundation: nothing else needs manual per-call-site timing.
- Idea 3 turns "slow" into "scanning because of X."
- Idea 4 turns single runs into a dataset keyed by flag combination.
- Idea 5 makes the dataset trustworthy.

## Suggested rollout order

1. **Shared gate + Idea 2.** Smallest change that tests the core assumption
   (precept #16): which sub-query is slow? One hook in `db_init`, one callback.
2. **Idea 3b counters**, folded into the same callback (cheap, quantitative).
3. **Idea 4 CSV mode**, reusing the callback's data + captured `argv`.
4. **Idea 3a EQP mode**, at the build sites (slightly more surface area).
5. **Idea 5 harness**, once there's structured output to consume.

Each step is independently revertible and adds nothing when `SMQI_TIMING` is
unset (precept #16: easy to revert; #4: off by default, loud when on).

## Open decisions (need a call before implementing)

1. **Label correlation (Idea 2):** match-by-SQL-prefix (no code churn) vs.
   explicit stmt→label tagging (clean labels, touches the build sites). Tagging
   is nicer output; prefix-match is faster to land.
2. **CSV destination:** stderr vs. an appendable file (`SMQI_TIMING_FILE`).
   File is better for sweeps; stderr is better for one-offs. Support both?
3. **Cold-cache method (Idea 5):** rely on `drop_caches` (needs sudo) vs.
   document "measure after reboot." Affects how portable the harness is.
4. **Web build:** `query-index-web.c` shares query logic. Do we gate the
   instrumentation out of the WEB_SAFE build entirely, or keep it env-gated
   there too?
