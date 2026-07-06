# Logs Batch-Folder Refactor Plan

## Goal

When `--batch-id` is passed (non-empty), log files route to a batch subdirectory:

```
logs/<model>/<batch>/<arm>/<instance>/<rep>.{traj,manifest}.json
```

When `--batch-id` is empty (not passed or `""`), the old layout is preserved:

```
logs/<model>/<arm>/<instance>/<rep>.{traj,manifest}.json
```

This prevents re-runs of the same model+instances with different batch IDs from
overwriting each other's trajectories and manifests.

---

## Files to change (7)

### 1. `experiment/lib/trajmeta.py` — path-parsing contract

`infer_path_meta(path)` currently returns `(model, arm, instance)` (3-tuple).
It must detect whether a batch subdirectory exists and return a 4-tuple:

```python
def infer_path_meta(path: Path) -> tuple[str, str, str, str]:
    """Derive (model, batch_id, arm, instance_id) from the path.

    Layouts supported:
      Nested with batch:  logs/<model>/<batch>/<arm>/<instance>/<rep>.traj.json
      Nested, no batch:   logs/<model>/<arm>/<instance>/<rep>.traj.json
      Legacy flat:        <instance>_<arm>.traj.json
    """
    # ... detection logic ...
    return model, batch_id, arm, instance
```

**Detection logic** (line 18-39):

1. Find `arm` in path parts (as before).
2. If `path.parent.parent.name == arm` (nested layout):
   - `arm_parent = path.parent.parent.parent` (either `<model>` or `<batch>`)
   - Try one level up: `grandparent = path.parent.parent.parent.parent`
   - If `grandparent` exists and is not `"logs"`, then `arm_parent` is the
     batch directory and `grandparent` is the model.
   - Otherwise, `arm_parent` is the model (no batch).
3. Fallback flat layout: batch_id = "".

`batch_of(path)` (line 57-59) is updated to read batch_id from
`infer_path_meta` first (the directory name is authoritative), falling back to
the manifest's `batch_id` field.

---

### 2. `experiment/run_experiment.py` — orchestrator paths

**`traj_path()` (line 67) and `manifest_path()` (line 71):**

Add `batch_id=""` parameter. When non-empty, insert `batch_id` between
`MODEL_DIR` and `arm`:

```python
def traj_path(arm, instance_id, rep, batch_id=""):
    if batch_id:
        return LOGS_DIR / MODEL_DIR / batch_id / arm / instance_id / f"{rep}.traj.json"
    return LOGS_DIR / MODEL_DIR / arm / instance_id / f"{rep}.traj.json"
```

Same pattern for `manifest_path()`.

**Callers of these functions:**
- `traj_exit_status()` (line 86) — pass through `batch_id`
- `run_one()` lines 170, 216 — pass `batch_id` from args
- `read_manifest()` line 75 — pass through `batch_id`
- `summarize()` lines 253-255 — pass `batch_id=""` (inventory only)

---

### 3. `experiment/run_one.py` — child process paths

**`run_instance()` (line 150-152):**

Insert `batch_id` between model_dir and arm in `out_dir` when `--batch-id`
is given:

```python
if batch_id:
    out_dir = LOGS_DIR / model_dir(model) / batch_id / arm / instance_id
else:
    out_dir = LOGS_DIR / model_dir(model) / arm / instance_id
```

The `write_manifest()` call (line 123) writes to `out_dir / f"{run_id}.manifest.json"`
and does not need to change — it inherits `out_dir`.

---

### 4. `experiment/analysis/analyze_trajectories.py`

Line 50: change `model, arm, instance = infer_path_meta(path)` to
`model, batch, arm, instance = infer_path_meta(path)`.

Use `batch` in the output CSV row instead of calling `batch_of(path)` (line 51
probably uses batch_of already — verify and deduplicate).

---

### 5. `experiment/analysis/evaluate_patches.py`

Line 88: change `model, arm, instance = infer_path_meta(path)` to
`model, batch, arm, instance = infer_path_meta(path)`.

---

### 6. `experiment/analysis/correct_submissions.py`

Line 96: change `model, arm, instance = infer_path_meta(path)` to
`model, batch, arm, instance = infer_path_meta(path)`.

---

### 7. `estimate_cost.py` — `measured_costs_from_logs()`

Line 218: the function constructs `base = LOGS_DIR / model_dir(model)` and
then globs `base / arm / */*.traj.json`. With batches, the glob pattern
`(base / arm).glob("*/*.traj.json")` won't find files inside batch
subdirectories. Change to recursive glob:

```python
for traj in sorted(base.rglob(f"{arm}/*/*.traj.json")):
```

Or pre-compute: `for traj in sorted(base.glob(f"**/{arm}/*/*.traj.json"))`.

Line 232: `traj.parent.name` extracts instance_id — unchanged (it's still
two levels above the trajectory file).

---

## Files NOT changed

- `experiment/lib/paths.py` — `LOGS_DIR` definition unchanged
- `experiment/lib/model.py` — `model_dir()` unchanged
- `experiment/analysis/traj_diff.py` — uses `parts[-3]` for display; batch adds
  one extra layer but the display is cosmetic (tolerates either depth)
- `experiment/compare.sh` — legacy flat layout, unrelated
- `experiment/analysis/merge_results.py` — reads CSVs, not logs
- `experiment/analysis/analyze_stats.py` — reads CSVs, not logs
- `experiment/analysis/compare_models.py` — reads CSVs, not logs

---

## Migration (pilot data)

The existing pilot trajectories under `logs/deepseek--deepseek-v4-flash/` have
no batch subdirectory. The updated `infer_path_meta` detects the old layout
and returns `batch_id=""` for them — no file moves required for the pilot data.

The new N=18 run will use `--batch-id n18_v4_flash`, which places files under
`logs/deepseek--deepseek-v4-flash/n18_v4_flash/...`. No conflict.

The 5 overlapping pilot instances are safe from overwrite because the new
batch writes to a different subdirectory.

---

## Verification checklist

- [ ] `infer_path_meta` correctly parses both 3-level and 4-level layouts
- [ ] `batch_of(path)` returns `"n18_v4_flash"` for new trajectories, `""` for pilot
- [ ] `run_experiment.py --dry-run --batch-id n18_v4_flash` shows correct paths
- [ ] All 3 analysis scripts compile and accept the 4-tuple
- [ ] `estimate_cost.py --from-logs` finds trajectories in batch subdirectories
