# Run Bookkeeping — Manifest, Ledger, and Resume

How the experiment pipeline tracks every run attempt, classifies outcomes,
recovers from crashes, and never silently loses a "no-result" run.

## Two-Layer Record

Every run attempt is recorded in two places by separate processes:

| Layer | Written by | File | Survives child crash? |
|-------|-----------|------|-----------------------|
| **Manifest** | `run_pilot.py` (child) | `logs/<arm>/<instance>/<rep>.manifest.json` | Yes, once `started` is written |
| **Ledger** | `run_experiment.py` (parent) | `logs/run_ledger.jsonl` | Yes — parent lives longer than child |

### Manifest (child-side)

`run_pilot.py` writes the manifest *before* the subprocess starts (`status:
"started"`) and again after it returns (`status: "completed"` or `"failed"`).

```json
{
  "arm": "treatment",
  "instance_id": "django__django-10554",
  "run_id": "1",
  "started_at": "2026-06-17T00:00:00+00:00",
  "finished_at": "2026-06-17T00:12:34+00:00",
  "exit_code": 0,
  "status": "completed",
  "traj_written": true
}
```

The manifest is the **resume source of truth** because:
- `status: "started"` with no `finished_at` = crashed/orphaned (re-run on resume)
- `status: "completed"` = clean finish (skip on resume)
- `status: "failed"` = subprocess returned non-zero (skip on resume unless `--retry-failed`)

### Ledger (parent-side)

`run_experiment.py` appends one row to `logs/run_ledger.jsonl` after *every*
`subprocess.run()` returns — even if the child was SIGKILLed or crashed before
writing a trajectory. The ledger is append-only (small atomic writes under
`O_APPEND`), so it's robust against orchestrator crashes.

```json
{"arm": "control", "instance_id": "django__django-10554", "rep": 1,
 "started_at": "...", "finished_at": "...", "returncode": 0,
 "traj_written": true, "exit_status": "Submitted", "ok": true}
```

The ledger is the **durable record** for completion-rate (§9.3) and
failure-by-cause (§11) reporting. It covers runs that produced no trajectory
at all (crash before serialization).

## Run Outcome Classification

`run_experiment.py` classifies each run's recorded state via `run_outcome()`:

```
  manifest "started" + no finished_at?  →  "started"   (in-flight/orphaned)
  trajectory exit_status in {Submitted, LimitsExceeded}?  →  "clean"
  trajectory with other exit_status (e.g., ValueError)?   →  "crashed"
  manifest "failed"/"completed" but no trajectory?         →  "crashed"
  no manifest, no trajectory?                              →  "none"
```

### Clean exit statuses

Only these two represent a legitimate, complete termination:
- **`Submitted`** — agent declared `COMPLETE_TASK_AND_SUBMIT_FINAL_OUTPUT`
- **`LimitsExceeded`** — agent hit the turn budget (100 turns)

Anything else (`ValueError`, `EOFError`, empty string) is a framework crash
mid-run — contaminated data that should be re-run, not silently counted as done.

## Resume Logic

```
is_done() return:
  "clean"   → True  (skip — terminally complete)
  "crashed" → True unless --retry-failed (skip by default; re-run with flag)
  "started" → False (orphaned/in-flight; re-run on resume)
  "none"    → False (never attempted)
```

### Default resume (no flags)

On restart, `run_experiment.py` skips `clean` and `crashed` runs, re-runs only
`started` and `none`. This is the normal "pick up where you left off" behavior.

### Re-running crashed runs

```bash
python3 experiment/run_experiment.py \
    --instances-file experiment/verified_instance_ids.txt \
    --reps 10 --retry-failed
```

This makes `is_done("crashed")` return `False`, re-queuing runs that:
- Have a trajectory with a crash `exit_status` (e.g., `ValueError` from the
  interactive turn-limit prompt)
- Have a `failed` manifest but no trajectory
- Have a `completed` manifest but somehow no `.traj.json` on disk (a fault)

### Backward compatibility with old runs

Runs that predate the manifest system (early matplotlib-14623 batch) have no
`.manifest.json`. The orchestrator falls back to `.traj.json` presence:
- `.traj.json` exists → checks `exit_status` → clean or crashed
- No `.traj.json`, no manifest → `none` (never attempted)

These old runs can't distinguish "completed" from "failed" from "never tried"
as precisely, but the fallback is safe: all three states are handled.

## Exit Status Reference

| exit_status | Meaning | `task_success` |
|-------------|---------|----------------|
| `Submitted` | Agent submitted a patch | From harness evaluation |
| `LimitsExceeded` | Hit the turn budget without submitting | `0` (empty_patch) |
| `ValueError` | Interactive prompt crash (see TROUBLESHOOTING.md) | `0` (re-run) |
| *(empty string)* | Trajectory incomplete (crash mid-serialization) | `0` (re-run) |
| `EOFError` | Interrupted by signal | `0` (re-run) |

## Harness Report Files

The SWE-bench evaluation harness writes report files to the current directory
when `evaluate_patches.py` runs:

```
sourceminder-control.qiexp_control_rep1.json
sourceminder-control.qiexp_control_rep2.json
...
sourceminder-treatment.qiexp_treatment_rep1.json
...
```

These are `.gitignore`d (`experiment/.gitignore` includes `*qiexp_*.json`).
Per-instance evaluation logs go to `experiment/logs/run_evaluation/<run_id>/...`
and don't interfere with `.traj.json` globbing.

## Inspecting State

```bash
# All manifest statuses
find experiment/logs -name '*.manifest.json' \
  -exec sh -c 'echo "$(jq -r ".status" "$1") $(dirname "$1")/$(basename "$1" .manifest.json)"' _ {} \; \
  | sort

# Count by outcome class (from the orchestrator's summary at end of session)
# Or, for a running orchestrator, check the latest session output
# The orchestrator prints a summary:
#   Run outcomes (by exit_status):
#     <N>  Submitted
#     <N>  LimitsExceeded
#     <N>  (no-traj: failed)
#     ...

# Latest ledger entries
tail -10 experiment/logs/run_ledger.jsonl | jq '.'
```
