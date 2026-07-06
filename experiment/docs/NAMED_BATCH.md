# Named Batch Plan

Give each experiment run a `batch_id` that travels end-to-end through the
pipeline: orchestrator → manifest → ledger → DB → CSV → stats → charts. A batch
is "one run of the experiment" — a specific sample of instances, a specific
model, a specific number of reps. It replaces the current pattern where every
step resolves "the newest timestamped directory" or concatenates CSVs by hand.

---

## 1. Design Decisions

### 1.1 `batch_id` is a user-supplied string, defaulting to a timestamp

- `--batch-id pilot_n5` on the orchestrator.
- When omitted, `batch_id` defaults to `YYYYMMDD_HHMMSS` — current behavior
  preserved as the degenerate case.
- The batch id must be filesystem- and DB-safe: alphanumeric + `_` + `-` only
  (validated at parse time).

### 1.2 The run directory becomes `results/runs/<batch_id>/`

Currently `results/runs/<timestamp>/`; the timestamp is a de-facto unnamed
batch. After the change, `results/runs/pilot_n5/` holds everything for that
batch: `runs.csv`, `eval_results.csv`, `runs_with_success.csv`, `stats/`,
`charts/`, etc.

Every pipeline script that today accepts `--dir <path>` continues to accept
`--dir <path>` — only the *default* resolution changes when a `--batch` flag is
given.

### 1.3 `batch_id` is a column, not part of the row identity

The join key remains `(model, arm, instance_id, rep)`. A `batch_id` column tags
every row so you can query or export one batch. The DB primary key does **not**
include `batch_id` — two batches re-evaluating the same trajectories would
collide, and that's intentional (the evaluation is deterministic given the same
trajectory, so the second write overwrites the first idempotently).

### 1.4 The pipeline is chainable by batch name

A new `scripts/run_pipeline.sh` (or Makefile target) reduces the current
5-invocation hand-threading to one command:

```
make analyze BATCH=pilot_n5
# or:
python3 experiment/run_pipeline.py --batch pilot_n5
```

This runs analyze_trajectories → evaluate_patches → merge_results →
analyze_stats → compare_models against the named batch.

### 1.5 `n_files` and `patch_files` are first-class pipeline columns

Two related-but-distinct file-count columns travel through every pipeline stage:

| Column | Source | Meaning | When |
|--------|--------|---------|------|
| `n_files` | Gold patch (`pool.csv` / instance list) | Problem complexity — how many files the ground-truth fix spans | Pre-experiment covariate |
| `patch_files` | Agent's submitted diff | Solution scope — how many unique files the agent's patch touches | Outcome variable |

`n_files` is known before any run starts: it's in the instance list the
orchestrator reads. It goes into the manifest so trajectory-analysis scripts can
read it without joining `pool.csv`. `patch_files` is computed from
`info.submission` by counting unique file paths in the unified diff.

Both appear in `runs.csv`, `eval_results.db`, `eval_results.csv`, and
`runs_with_success.csv`. `analyze_stats.py` currently joins `n_files` from
`pool.csv` at analysis time — after this change it reads the column directly,
simplifying the analysis and making the size moderator visible in every
intermediate CSV and chart label.

Why keep both: an instance with `n_files=5` may be solved by an agent touching
only 2 files. Reporting only `n_files` would overstate the patch scope; reporting
only `patch_files` would lose the pre-experiment covariate. Having both lets you
ask "do agents that use qi produce more focused patches (lower patch_files /
n_files)?"

### 1.6 Backward compatibility

Every change is additive. Existing timestamp-named directories still work — they
simply have `batch_id` equal to the timestamp. Scripts with no `--batch` flag
fall back to the current "newest directory" behavior. Legacy DB rows keep
`batch_id = ''`. CSV files predating the `n_files`/`patch_files` columns are read
with those fields defaulting to `""` (unknown/not computed).

---

## 2. File-by-File Changes

### 2.1 `experiment/lib/paths.py`

**Add:**

```python
def batch_run_dir(batch_id: str) -> Path:
    """results/runs/<batch_id>/ — does not create."""
    return RUNS_DIR / batch_id
```

**Modify `new_run_dir`:** Accept an optional `batch_id` parameter; when given,
returns `batch_run_dir(batch_id)` instead of the timestamped path.

---

### 2.2 `experiment/run_one.py`

**New CLI args:** `--batch-id` (default: `""`) and `--n-files` (default: `""` —
unknown when invoked standalone without the orchestrator).

**`write_manifest()`:** Add `batch_id` and `n_files` to the manifest record:

```json
{
  "arm": "treatment",
  "instance_id": "django__django-11532",
  "run_id": "1",
  "model_name": "deepseek/deepseek-v4-pro",
  "batch_id": "pilot_n5",
  "n_files": "5",
  "started_at": "...",
  "finished_at": "...",
  "exit_code": 0,
  "status": "completed",
  "traj_written": true
}
```

`n_files` is stored as a string in the JSON (for manifest simplicity) but
coerced to int by downstream readers. An empty string means "unknown" — the
orchestrator didn't supply it (or this is a legacy run).

**`main()`:** Accept and forward `--batch-id` and `--n-files` through to
`run_instance()` and `write_manifest()`.

**Lines affected:** ~118–142 (`write_manifest` signature + record dict),
~227–258 (`main()` argument parsing), ~145 (`run_instance` signature).

---

### 2.3 `experiment/run_experiment.py`

**New CLI arg:** `--batch-id` (required unless `--dry-run`; default:
`time.strftime("%Y%m%d_%H%M%S")` — preserves current behavior exactly).

**Plan header:** Print `Batch:` alongside `Model:`, `Instances:`, etc.

**`build_runs()`:** Extend to carry `n_files` alongside each `(arm, instance_id,
rep)` triple. The instance list file (`verified_instance_ids.txt`) already has
`instance_id repo n_files` columns — parse the third column and thread it through
so the orchestrator can pass it to the child.

**Child command:** Pass `--batch-id <batch_id>` and `--n-files <n>` to
`run_one.py` in `run_one()`.

**Ledger:** Include `batch_id` and `n_files` in each `append_ledger()` record.

**Default `--dir` for downstream steps:** The orchestrator itself doesn't run the
analysis pipeline, but the plan header should print the batch id so the user
knows what to pass to the analysis scripts.

**Lines affected:** ~155–162 (`build_runs` signature), ~165–233 (`run_one` —
child command construction and ledger), ~257–306 (`main` — arg parsing, header
printing).

---

### 2.4 `experiment/lib/trajmeta.py`

**New function — batch lookup:**

```python
def batch_of(path: Path) -> str:
    """Read batch_id from the corresponding manifest, or '' if missing."""
    manifest = path.with_name(path.name.replace(".traj.json", ".manifest.json"))
    if manifest.exists():
        try:
            return json.loads(manifest.read_text()).get("batch_id", "")
        except (json.JSONDecodeError, OSError):
            pass
    return ""
```

**New function — n_files lookup from manifest:**

```python
def n_files_of(path: Path) -> str:
    """Read n_files from the corresponding manifest, or '' if missing."""
    manifest = path.with_name(path.name.replace(".traj.json", ".manifest.json"))
    if manifest.exists():
        try:
            return json.loads(manifest.read_text()).get("n_files", "")
        except (json.JSONDecodeError, OSError):
            pass
    return ""
```

**New function — patch file count from a diff string:**

```python
def patch_files_of(diff: str) -> int:
    """Count unique file paths touched by a unified diff. Returns 0 for empty."""
    if not diff.strip():
        return 0
    files: set[str] = set()
    for line in diff.splitlines():
        if line.startswith("--- a/") or line.startswith("+++ b/"):
            path = line.split("/", 1)[1] if "/" in line else line[6:]
            files.add(path.rstrip("\t"))
    return len(files)
```

All trajectory-identity and trajectory-metric helpers now reside in one module.
`analyze_trajectories.py` and `evaluate_patches.py` call these instead of
duplicating the logic.

**Lines affected:** ~1–43 (new functions; add `import json`).

---

### 2.5 `experiment/analysis/analyze_trajectories.py`

**`analyze_one()`:** Add `batch_id`, `n_files`, and `patch_files` to the returned
dict:

```python
submission = info.get("submission", "")

return {
    "batch_id": trajmeta.batch_of(path),       # new
    "run_id": ...,
    "model": ...,
    "instance_id": instance,
    "arm": arm,
    "n_files": trajmeta.n_files_of(path),       # new — from manifest
    "patch_files": trajmeta.patch_files_of(submission),  # new — from diff
    "exit_status": info.get("exit_status", ""),
    ...
}
```

**`runs.csv` output:** `batch_id`, `n_files`, and `patch_files` appear as columns
alongside the existing metrics.

**`main()`:** Accept `--batch` flag to filter trajectories: only process traj
files whose manifest `batch_id` matches. When not given, process all (current
behavior).

**Lines affected:** ~41–103 (`analyze_one` return dict — add 3 fields, read
submission), ~135–170 (`main` args).

---

### 2.6 `experiment/analysis/eval_db.py`

**Schema change:** Add `batch_id`, `n_files`, and `patch_files` columns to
`eval_results`:

```sql
CREATE TABLE IF NOT EXISTS eval_results (
    run_tag     TEXT NOT NULL,
    batch_id    TEXT NOT NULL DEFAULT '',
    model       TEXT NOT NULL,
    arm         TEXT NOT NULL,
    instance_id TEXT NOT NULL,
    rep         TEXT NOT NULL,
    n_files     INTEGER,                  -- new: gold-patch file span (from manifest)
    patch_files INTEGER,                  -- new: unique files in agent's diff
    exit_status TEXT,
    has_patch   INTEGER,
    outcome     TEXT,
    resolved    INTEGER,
    dataset     TEXT,
    updated_at  TEXT,
    PRIMARY KEY (run_tag, model, arm, instance_id, rep)
);
```

**Migration:** On connect, for each column not present, `ALTER TABLE ADD COLUMN`
with a `DEFAULT` — `''` for `batch_id`, `NULL` for `n_files`/`patch_files`
(unknown / pre-dating this change).

**`EvalResult` dataclass:** Add `batch_id: str`, `n_files: int | None`,
`patch_files: int | None` fields.

**`CSV_COLUMNS`:** Add `"batch_id"`, `"n_files"`, `"patch_files"` (in that
order, before `model`).

**`upsert()`:** Include the three new columns in the INSERT.

**`export_csv()`:** Include the three new columns in the SELECT. Add optional
`batch_id` filter parameter so you can export a single batch's rows.

**Lines affected:** ~36–37 (`CSV_COLUMNS`), ~41–58 (`EvalResult` dataclass),
~78–124 (`init_schema` + migration), ~128–140 (`upsert`), ~143–178
(`export_csv`), ~180–184 (`count_resolved`).

---

### 2.7 `experiment/analysis/evaluate_patches.py`

**`collect_runs()`:** After extracting `(model, arm, instance)` from the
trajectory, read `batch_id` and `n_files` from the corresponding manifest (using
`trajmeta.batch_of(path)` and `trajmeta.n_files_of(path)`). Compute
`patch_files` from the submission diff (`trajmeta.patch_files_of(patch)`).
Include all three in each run dict:

```python
runs.append({
    "model": model,
    "arm": arm,
    "instance_id": instance,
    "rep": rep_of(path),
    "batch_id": trajmeta.batch_of(path),
    "n_files": trajmeta.n_files_of(path),
    "patch_files": trajmeta.patch_files_of(patch),
    ...
})
```

**`run_group()`:** Pass `batch_id`, `n_files`, and `patch_files` through to
`EvalResult(...)`.

**`main()`:** Accept `--batch` flag to filter to one batch's trajectories.

**`run_tag` default:** Currently `out_dir.name` (a timestamp or directory name).
With named batches, the `run_tag` should incorporate the batch_id so different
batches' rows don't collide in the DB: `run_tag = batch_id` when `--batch` is
given, or `out_dir.name` otherwise. This also means retries within the same
batch are idempotent (same run_tag, same PK → upsert overwrites).

**`--run-tag`:** Still works as an explicit override.

**Lines affected:** ~74–103 (`collect_runs` — read 3 new fields), ~238–256
(`main` groups + run_tag resolution), ~273–293 (`run_group` — pass through to
EvalResult).

---

### 2.8 `experiment/analysis/merge_results.py`

**`key_runs()` / `key_eval()`:** Unchanged — `batch_id`, `n_files`, and
`patch_files` are tags/outcomes, not part of the join identity.

**Output CSV:** Include `batch_id`, `n_files`, and `patch_files` columns in
`runs_with_success.csv`. These flow through from `runs.csv` — they are not taken
from the eval side (the eval side holds the same values, so either source is
equivalent).

**`main()`:** Accept `--batch` flag to merge only rows for one batch. Default:
merge all (current behavior).

**Lines affected:** ~36–41 (key functions — unchanged), ~49–104 (`main` — add
`--batch` filter, include new columns in output fields).

---

### 2.9 `experiment/analysis/analyze_stats.py`

**Input resolution:** Accept `--batch <batch_id>` as an alternative to
`--dir`/`--csv`. When `--batch pilot_n5` is given:
- Find `results/runs/pilot_n5/runs_with_success.csv` (or error if missing).
- Default output goes to `results/runs/pilot_n5/stats/`, `charts/`.

**Row filtering:** When `--batch` is given, filter rows to that `batch_id`
column value before analysis. When not given, analyze all rows (current
behavior).

**`n_files` resolution — simplified.** Currently `analyze_stats.py` joins
`n_files` from `data/pool.csv` at analysis time (the `_nfiles_map()` helper).
After this change, `n_files` is already a column in `runs_with_success.csv`.
Remove the `pool.csv` join entirely — read the column directly, coercing to int
(falling back to `None` for legacy CSVs where the column is missing or empty).

**`patch_files` reporting.** The new column is available for analysis as an
outcome: per-arm descriptives of how many unique files the agent patched,
alongside the pre-experiment `n_files`. Add a `patch_files` row to the per-arm
descriptive table and a paired comparison (do treatment patches touch fewer
files?).

**`_default_csv()`:** Updated to either resolve by `--batch` or fall back to the
current "newest directory" behavior.

**Lines affected:** ~51–60 (docstring + imports), ~120–180 (input resolution
logic), ~200–250 (remove `_nfiles_map` / `pool.csv` join; read column directly),
~300+ (add `patch_files` to descriptive table), ~1000+ (any place that
references `--dir` semantics).

---

### 2.10 `experiment/analysis/compare_models.py`

**Input resolution:** Same `--batch` flag as `analyze_stats.py`. Since
cross-model comparison needs a combined CSV, `--batch pilot_n5` means "read
`results/runs/pilot_n5/runs_with_success.csv`" (already contains both models'
rows from the one batch).

**Lines affected:** ~57–90 (input resolution), ~400–600 (main + arg parsing).

---

### 2.11 `experiment/analysis/estimate_cost.py`

**`--from-logs` mode:** Accept an optional `--batch` flag to filter which
trajectory manifests to read for measured costs. Without it, read all (current
behavior).

**Lines affected:** ~41–80 (args), ~250–350 (`--from-logs` implementation).

---

### 2.12 `experiment/scripts/run_pipeline.sh` (NEW)

A single command that chains the full analysis for one batch:

```bash
#!/usr/bin/env bash
# Usage: bash experiment/scripts/run_pipeline.sh <batch_id>
# Runs: analyze_trajectories → evaluate_patches → merge_results →
#       analyze_stats → compare_models
set -euo pipefail
BATCH="${1:?Usage: $0 <batch_id>}"
PYTHON="experiment/.venv/bin/python3"

echo "=== analyze_trajectories ==="
$PYTHON experiment/analysis/analyze_trajectories.py \
    --logs experiment/logs --batch "$BATCH"

echo "=== evaluate_patches ==="
$PYTHON experiment/analysis/evaluate_patches.py \
    --logs experiment/logs --batch "$BATCH"

echo "=== merge_results ==="
$PYTHON experiment/analysis/merge_results.py \
    --batch "$BATCH"

echo "=== analyze_stats ==="
$PYTHON experiment/analysis/analyze_stats.py \
    --batch "$BATCH" --no-charts

echo "=== compare_models ==="
$PYTHON experiment/analysis/compare_models.py \
    --batch "$BATCH"

echo "Done: results/runs/$BATCH/"
```

Alternatively, a minimal Makefile target at the repo root:

```makefile
.PHONY: analyze
analyze:
	bash experiment/scripts/run_pipeline.sh $(BATCH)
```

---

### 2.13 `experiment/check_dbs.py`

No changes needed — it validates DB integrity, which is orthogonal to batch
identity.

---

### 2.14 `experiment/pre_index.py`

No changes needed — indexing happens before any batch runs.

---

## 3. Documentation Files to Update

### 3.1 `experiment/docs/RUN_BOOKKEEPING.md`

- Add `batch_id` and `n_files` to the manifest JSON example (~line 20–30).
- Document that the ledger record includes `batch_id`.
- Add a section "Batch identity" explaining that `batch_id` links every record
  in the manifest, ledger, DB, and CSV.

### 3.2 `experiment/docs/DESIGN.md`

- Update "Results store" section (~line 33): `run_tag` is now the batch id, not
  a bare timestamp.
- Add `batch_id`, `n_files`, and `patch_files` to the table of CSV/DB columns.
- Document the distinction: `n_files` (gold-patch span, pre-experiment covariate)
  vs `patch_files` (agent's diff span, outcome variable).
- Mention that `eval_results.csv` now carries `batch_id`, `n_files`, and
  `patch_files` columns.

### 3.3 `experiment/README.md`

- Update directory structure diagram (~line 15–48): `results/runs/<batch_id>/`
  instead of `<timestamp>/`; add `batch_id` column docs.
- Update usage examples: show `--batch-id` on the orchestrator and `--batch` on
  analysis scripts.
- Add a "Workflow" section: orchestrator → pipeline scripts → results, all
  keyed by batch id.

### 3.4 `experiment/docs/PREREGISTRATION.md`

- In the data model section: add `batch_id`, `n_files`, and `patch_files` to the
  list of fields in `runs.csv`, `eval_results.csv`, and `runs_with_success.csv`.
- Document `n_files` as a pre-experiment covariate (gold-patch file span from
  `pool.csv`) and `patch_files` as an outcome variable (unique files in the
  agent's submitted diff).
- Note that the batch is the unit of analysis — pilot and confirmatory batches
  are analyzed independently, never pooled across batches unless explicitly
  combined.

### 3.5 `experiment/docs/ANALYZE_STATS_PLAN.md`

- Update usage examples to show `--batch` flag.

### 3.6 `experiment/docs/COMPARE_MODELS_SCRIPT_DESIGN.md`

- Update usage examples to show `--batch` flag.

### 3.7 `experiment/docs/CROSS_MODEL_PLAN.md`

- Superseded — already has a banner. No update needed.

---

## 4. Migration Path

### 4.1 Existing data

- Existing `runs_with_success.csv` files in timestamped directories: these
  predate the `batch_id`, `n_files`, and `patch_files` columns. Scripts that
  read these CSVs treat missing columns as `batch_id=""`, `n_files=None`,
  `patch_files=None` (legacy / unknown).
- Existing `eval_results.db` rows: `ALTER TABLE ADD COLUMN` migrations set
  `batch_id=""` and `n_files`/`patch_files` to `NULL` for all legacy rows. They
  continue to work — you just can't filter or group by the new columns for
  pre-batch data.
- Existing manifests: lack `batch_id` and `n_files`. `trajmeta.batch_of()` and
  `trajmeta.n_files_of()` return `""` for them. `patch_files_of()` works on any
  diff string regardless.
- Existing `analyze_stats.py` `pool.csv` join: removed as part of the refactor.
  Legacy CSVs with no `n_files` column will see `n_files=None`; the size
  interaction code should handle this gracefully (exclude rows with unknown
  `n_files` from that analysis, same as the current `_nfiles_map` behavior when
  an instance isn't in `pool.csv`).

### 4.2 Timeline

1. Implement the changes in the order above (paths → run_one → run_experiment →
   trajmeta → eval_db → evaluate_patches → merge_results → analyze_stats →
   compare_models → estimate_cost → pipeline script).
2. Run the orchestrator with `--batch-id pilot_n7` (7 reps: existing 5 + 2 new).
3. Run the pipeline: `bash experiment/scripts/run_pipeline.sh pilot_n7`.
4. Verify: `results/runs/pilot_n7/` contains everything; `eval_results.db` has
   `batch_id = 'pilot_n7'` rows.
5. Future N=20 confirmatory run: `--batch-id confirm_n20`.

### 4.3 Backward compatibility

All scripts degrade gracefully:
- No `--batch` / `--batch-id`? Fall back to current timestamp behavior.
- Manifest missing `batch_id` or `n_files`? Returns `""` — rows tagged with empty
  string or unknown file count.
- DB missing `batch_id`, `n_files`, or `patch_files` columns? Migration adds them
  on next connect.
- CSV missing `n_files` or `patch_files` columns? Read as `None`; analysis skips
  rows with unknown values for size-interaction computations (same as current
  `_nfiles_map` behavior).

---

## 5. What Does Not Change

- The log directory layout: `logs/<model>/<arm>/<instance>/<rep>.traj.json`
  remains the same. `batch_id` and `n_files` are in the manifest, not the path.
- The join key: `(model, arm, instance_id, rep)` is unchanged in
  `merge_results.py`, `eval_db.py`, and `compare_models.py`.
- The DB primary key: `batch_id` is not part of the PK — runs are uniquely
  identified by `(run_tag, model, arm, instance_id, rep)` as before.
- The manifest is still the source of truth for per-run state; the ledger is
  still append-only.
- `data/pool.csv` is unchanged — it remains the canonical source of `n_files`
  (the manifest gets its value from the instance list, which is derived from
  `pool.csv`). The `analyze_stats.py` pool.csv join is *removed* as redundant,
  but `pool.csv` itself stays.
- `sample_pool.py`, `build_pool.py`, `compare.sh`, `traj_diff.py`, and
  `compare_runs.py` are unaffected.
- `lib/cmds.py`, `lib/model.py`, `lib/naming.py`, `lib/instances.py`,
  `lib/dbcheck.py` are unaffected.
