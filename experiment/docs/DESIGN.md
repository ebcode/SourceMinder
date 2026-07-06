# Patch Evaluation — Design Notes

The `task_success` metric is produced by two scripts:

- **`evaluate_patches.py`** — runs the SWE-bench harness over the submitted
  patches and writes per-run pass/fail to a shared SQLite DB (`eval_db.py`),
  re-exporting `eval_results.csv` after every group.
- **`merge_results.py`** — left-joins `eval_results.csv` onto `runs.csv` (the
  token metrics from `analyze_trajectories.py`) into `runs_with_success.csv`.

The two source CSVs are kept separate on purpose: evaluation is a heavyweight
Docker pass, and re-running the analyzer must never clobber harness results. The
merge regenerates the combined view on demand.

## Results store — why SQLite/WAL, not a plaintext CSV

`evaluate_patches.py` originally appended results to a single `"w"`-mode CSV
handle. That is safe for one sequential process but corrupts silently if **two
eval runs target the same output** (e.g. a stale run that wasn't fully killed
plus a fresh one): both truncate and interleave the one file, so the row count
swings unpredictably.

Results now go to a shared WAL SQLite DB (`analysis/eval_results.db`, see
`eval_db.py`):

- **Concurrency-safe.** WAL + `busy_timeout` lets multiple writers coexist —
  required if the eval is ever sharded across `(arm, rep)` groups for speed.
  Per-result writes are trivially small next to the minutes-long Docker runs, so
  write contention is negligible.
- **Idempotent.** Primary key `(run_tag, arm, instance_id, rep)` with
  `INSERT OR REPLACE` means a re-run or `--retry-failed` overwrites the prior row
  in place — no duplicates, unlike CSV append.
- **Namespaced.** `run_tag` (default: the `--dir` name) separates independent
  batches in the one shared DB, so a new pilot doesn't overwrite an old one's
  reps.

`eval_results.csv` is still re-exported from the DB (filtered to `run_tag`) after
every group, so it stays the crash-recoverable interchange format and
`merge_results.py` is unchanged. The DB is the source of truth; the CSV is a
view. (`eval_results.db` is covered by the root `*.db` gitignore; results are
committed via the exported CSV.)

## Input

**Trajectory files** `logs/<arm>/<instance>/<run_id>.traj.json` are the patch
source. The submitted diff is at `info.submission` (a `diff --git ...` string);
it is empty/whitespace when the agent never submitted (budget/turn exhaustion,
crash). `evaluate_patches.py` walks the trajectories directly — it does **not**
read `runs.csv`. The two are joined afterward by `merge_results.py` on the run
identity `(arm, instance_id, run_id/rep)`.

| Scenario | `.traj.json` | `info.submission` | task_success |
|----------|--------------|-------------------|--------------|
| Agent submitted | yes | diff string | `resolved` from harness |
| Budget/turn exhausted | yes | `""` | `0` (scored `empty_patch`) |
| Crash before serialization | **no** | N/A | `0` (`not_evaluated` after merge) |

A run present in `runs.csv` but with no harness outcome (eval not run, or no
trajectory) is mapped by `merge_results.py` to `task_success=0`,
`outcome=not_evaluated` — never silently dropped.

## Why batch per (arm, rep) — the dedup trap

The harness collapses predictions with:

```python
predictions = {pred[KEY_INSTANCE_ID]: pred for pred in predictions}
```

i.e. **purely by `instance_id`** — *not* by `instance_id + model_name_or_path`.
control/treatment and every rep reuse the same instance_ids, so putting them all
in one predictions file would silently keep only the last prediction per
instance (1 of every 20 runs in a 2-arm × 10-rep study).

`evaluate_patches.py` therefore makes **one harness call per `(arm, rep)`
group**, where instance_ids are unique. Each group gets its own
`run_id = qiexp_<arm>_rep<rep>` and produces its own report file.

## Prediction format

A `.json` file (the harness also accepts a dict or `.jsonl`), one entry per run
in the group:

```json
{"instance_id": "matplotlib__matplotlib-14623",
 "model_name_or_path": "sourceminder-treatment",
 "model_patch": "diff --git ..."}
```

`model_patch` is the raw `info.submission` diff. Empty-patch runs are still
submitted to the harness, which scores them `empty_patch` (unresolved).

## Harness invocation

`swebench.harness.run_evaluation.main(...)` is called directly (not via CLI) and
**returns the path to a run-level report** `<model>.<run_id>.json` written to the
cwd. Parameters used:

| Parameter | Value | Notes |
|-----------|-------|-------|
| `dataset_name` | `princeton-nlp/SWE-bench_Verified` | by `--subset` (verified/lite/test) |
| `split` | `test` | |
| `instance_ids` | the group's instance_ids | |
| `predictions_path` | temp `.json` (auto-cleaned) | |
| `max_workers` | `4` (CLI `--max-workers`) | Docker parallelism |
| `force_rebuild` | `False` | images pulled in the pre_index phase |
| `cache_level` | `env` | reuse env images |
| `namespace` | `swebench` (CLI `--namespace`) | pull prebuilt `swebench/sweb.eval.*`; `none` builds locally |
| `timeout` | `1800` (CLI `--timeout`) | per-instance test timeout (s) |

> The `princeton-nlp/SWE-bench_Verified` dataset is **not** HF-gated — no
> `HF_TOKEN` is required (confirmed on swebench 4.1.0).

The returned report is parsed for `resolved_ids` / `unresolved_ids` /
`empty_patch_ids` / `error_ids`; any instance in the group not in one of those is
`incomplete`. The harness also writes per-instance logs under
`logs/run_evaluation/<run_id>/...` — harmless to the analysis scripts, which
only glob `*.traj.json`.

## Output

Each result is upserted into `analysis/eval_results.db` (table `eval_results`,
PK `(run_tag, arm, instance_id, rep)`) and exported to `eval_results.csv`, one
row per run:

```
arm, instance_id, rep, exit_status, has_patch, outcome, resolved
```

`outcome` ∈ {resolved, unresolved, error, empty_patch, incomplete}; `resolved` is
`1`/`0`. The CSV is re-exported from the DB after every group so a long eval is
crash-recoverable.

`merge_results.py` then joins it onto `runs.csv` → `runs_with_success.csv`,
appending `outcome` and `task_success` columns to the full token-metrics table.

## Workflow

```
analyze_trajectories.py ──→ runs.csv ──────────────┐
                                                    │
trajectories (info.submission)                      │
        │                                           ▼
        ▼                                    merge_results.py ──→ runs_with_success.csv
evaluate_patches.py ──→ run_evaluation ──→ eval_results.csv ─────┘
   (batched per arm,rep)     (Docker)
```

## Edge cases / notes

- **Empty patch:** `info.submission` empty → submitted to harness, scored
  `empty_patch` → `resolved=0`.
- **Duplicate instance_ids:** *not* handled by the harness via model name (see
  "dedup trap"); handled here by per-(arm,rep) batching.
- **Docker not running:** the harness fails fast; `docker info` is a sensible
  pre-check before a long run.
- **Idempotency:** results upsert on `(run_tag, arm, instance_id, rep)`, so
  re-running an interrupted eval overwrites rows in place rather than
  duplicating. The harness also skips instances that already have a report under
  `logs/run_evaluation/<run_id>/`, so a resumed eval is cheap when images and
  reports are cached.
- **Concurrent runs:** two evals against the **same** `run_tag` no longer
  corrupt results (idempotent upserts converge); against different `run_tag`s
  they're fully isolated. The CSV export still opens `"w"`, so prefer distinct
  `--dir`s for concurrent batches — the DB, not the CSV, is the source of truth.
- **Report files in cwd:** `main()` writes `<model>.<run_id>.json` to the current
  directory — e.g. `sourceminder-treatment.qiexp_treatment_rep1.json`. Worth
  `.gitignore`-ing.
