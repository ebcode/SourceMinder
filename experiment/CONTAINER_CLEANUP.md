# Container Cleanup & Interrupt Handling — Plan

Status: **proposed** (do NOT implement mid-batch — see Timing).

## Problem

Neither `run_experiment.py` nor `run_one.py` handles SIGINT/SIGTERM. On Ctrl+C (or
the corruption halt killing workers), the signal hits the whole process group at
once and children die abruptly:

```
run_experiment.py   ThreadPoolExecutor, no KeyboardInterrupt handler
  └─ run_one.py      subprocess.run dies mid-flight → manifest stuck at "started"
       └─ mini-extra killed → its docker --rm cleanup never fires
            └─ minisweagent-<hash> container  ← orphaned, keeps running
```

### Evidence (observed during prompt_study3)
- **39 running containers** with only **~20 worker slots** (10 workers × 2
  orchestrators) → ~19 orphaned `minisweagent-*` containers leaked from the
  earlier halted attempt.
- Stale `"started"` manifests left by `run_one.py` killed between its two
  manifest writes (`run_one.py:214` writes "started" before mini-extra;
  `run_one.py:228` writes "completed"/"failed" only after it returns).

### Severity
- Orphaned containers = **real resource leak** (CPU/RAM/disk) — the priority.
- Stale manifests = **cosmetic**; resume already re-runs `started`-without-
  trajectory as "crashed", so no data is lost — just confusing status counts.

## Goals

- [ ] Ctrl+C performs a graceful shutdown (no traceback spew; clean summary).
- [ ] No orphaned `minisweagent-*` containers after an interrupt.
- [ ] Interrupted runs record an accurate manifest status (`"interrupted"`),
      not a lingering `"started"`.
- [ ] Behavior is identical whether the trigger is Ctrl+C or the corruption halt.

## Open questions (verify FIRST — they decide the approach)

- [ ] **Does mini-extra clean up its container on SIGTERM?** Send SIGTERM to a
      live `mini-extra swebench-single` and check whether the `minisweagent-*`
      container is removed.
  - If **yes** → a graceful terminate-and-wait in `run_one.py` is sufficient.
  - If **no** → `run_one.py` cannot easily `docker rm` it (the container name is
      generated inside mini-extra), so add a **reaper** (below).
- [ ] Confirm the container name pattern is always `minisweagent-*` (used by the
      reaper's match filter).
- [ ] Confirm child processes share the orchestrator's process group (they do by
      default), so the reaper can match container → live mini-extra PID.

## Implementation sketch

### Layer 1 — `run_one.py` (the child): write a final status + terminate cleanly

Wrap the mini-extra call so an interrupt is caught, the child is terminated
gracefully (giving mini-extra a chance to self-clean its container), and the
manifest is finalized.

```python
import signal

class _Interrupted(Exception):
    pass

def _on_term(signum, frame):
    raise _Interrupted()

# in run_instance(), around the subprocess.run(mini-extra ...):
signal.signal(signal.SIGTERM, _on_term)   # SIGINT already raises KeyboardInterrupt
proc = subprocess.Popen(cmd, ...)          # Popen, not run, so we can terminate it
try:
    proc.wait()
    status = "completed" if proc.returncode == 0 else "failed"
except (KeyboardInterrupt, _Interrupted):
    proc.terminate()                       # SIGTERM -> let mini-extra clean its container
    try:
        proc.wait(timeout=20)              # grace period for docker --rm
    except subprocess.TimeoutExpired:
        proc.kill()
    status = "interrupted"
finally:
    write_manifest(out_dir, run_id, arm, instance_id, model, status,
                   exit_code=getattr(proc, "returncode", None),
                   traj_written=out_path.exists(), started_at=started_at)
```

- [ ] Switch `subprocess.run` → `subprocess.Popen` so the child is terminable.
- [ ] Catch `KeyboardInterrupt` / SIGTERM; `terminate()` + bounded `wait()` +
      `kill()` fallback.
- [ ] Add `"interrupted"` as a manifest status (alongside started/completed/failed).
- [ ] Finalize the manifest in a `finally` so it never stays `"started"`.

### Layer 2 — `run_experiment.py` (the orchestrator): graceful executor shutdown

Reuse the existing `_halt_event` + `cancel_futures` machinery already present for
the corruption halt (`run_experiment.py:418-422`).

```python
try:
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = {...}
        for future in as_completed(futures):
            ...
except KeyboardInterrupt:
    print("\nInterrupted — finishing/cancelling in-flight runs...")
    pool.shutdown(wait=False, cancel_futures=True)   # cancel the queue
    # in-flight run_one children get SIGINT via the process group and
    # finalize their own manifests (Layer 1)
```

- [ ] Wrap the executor loop in `try/except KeyboardInterrupt`.
- [ ] On interrupt: `cancel_futures=True`, print a clean summary
      (`N done / M cancelled`), suppress the traceback.
- [ ] Make sure the per-arm/`--workers` sequential path (the non-pool branch,
      `run_experiment.py:~395-403`) is handled too.

### Layer 3 — Reaper (only if mini-extra does NOT self-clean on SIGTERM)

A standalone helper to remove orphaned containers — usable both as a post-run
safety net and as a manual one-off.

```bash
# experiment/scripts/reap_orphans.sh
# Remove minisweagent-* containers whose owning process is gone.
# (Match each container to a live mini-extra PID; force-remove the rest.)
```

- [ ] Write `scripts/reap_orphans.sh` (or a `--reap` flag on run_experiment).
- [ ] Match container → live mini-extra (via `docker inspect` start time / a
      label, or simply: any `minisweagent-*` not touched in N minutes when no
      orchestrator is running).
- [ ] Run it automatically at orchestrator exit (atexit) AND offer it standalone.
- [ ] SAFETY: never remove a container belonging to a live run — gate on "no
      orchestrator running" or an explicit `--force`.

## Testing

- [ ] Start a tiny batch (1 arm, 1 instance, 2 reps), Ctrl+C mid-run, assert:
      no `minisweagent-*` containers remain, manifests show `"interrupted"`,
      no traceback printed.
- [ ] Repeat with a double Ctrl+C (SIGKILL case) — reaper should still clean up.
- [ ] Confirm a normal completed run is unaffected (status still
      `"completed"`/`"failed"`, container removed by `--rm` as before).
- [ ] Re-run the same batch and confirm resume skips completed and re-runs
      interrupted ones.

## Timing

- [ ] **Apply only after the current `prompt_study3` batch finishes.** The
      orchestrator spawns a fresh `run_one.py` per task, so editing it mid-batch
      would change behavior for not-yet-started runs and risk a bug inside a live
      experiment.

## One-off cleanup available now

A careful command to reap the current orphans WITHOUT touching the ~20 live
containers (see Layer 3 safety) can be run on request — kept out of this plan so
it isn't applied to a running batch by accident.
```
