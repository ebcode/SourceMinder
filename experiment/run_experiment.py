#!/usr/bin/env python3
"""
Orchestrate the full qi context preservation experiment.

Reads an instance list file (or a single --instance-id), runs both arms
(control + treatment) for N repetitions each in randomized order, skipping
already-completed runs.

Usage:
    python3 experiment/run_experiment.py --instances-file experiment/verified_instance_ids.txt
    python3 experiment/run_experiment.py --instances-file experiment/verified_instance_ids.txt --runs 10 --workers 2
    python3 experiment/run_experiment.py --instance-id matplotlib__matplotlib-14623 --runs 5
    python3 experiment/run_experiment.py --instances-file experiment/verified_instance_ids.txt --dry-run

A run is identified by (arm, instance_id, rep). Completion is determined by the
per-run .manifest.json (the source of truth); runs predating manifests fall back
to .traj.json presence. Every attempt — including ones that crash without
producing a trajectory — is appended to logs/run_ledger.jsonl by this
orchestrator, so a "no result" run is always recorded somewhere. Restarting the
script safely resumes from where it left off.
"""

import argparse
import json
import os
import random
import subprocess
import sys
import threading
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[0]))  # -> experiment/
from lib import paths
from lib.instances import parse_instance_ids
from lib.model import MODEL, api_key_var, model_dir, normalize_model

REPO_ROOT = paths.REPO_ROOT
EXPERIMENT_DIR = paths.EXPERIMENT_DIR
LOGS_DIR = paths.LOGS_DIR
RUN_PILOT = EXPERIMENT_DIR / "run_pilot.py"
VENV_PYTHON = EXPERIMENT_DIR / ".venv" / "bin" / "python3"

# Append-only ledger of every run attempt (one JSON object per line). The
# orchestrator (parent) writes a row after each subprocess returns, so a child
# that is SIGKILLed or crashes without writing a trajectory is still recorded.
LEDGER_PATH = LOGS_DIR / "run_ledger.jsonl"
_ledger_lock = threading.Lock()

# Set by run_one() when a child exits with code 3 (DB corruption). Halts the
# main loop — continuing would waste API spend on a broken qi database.
_halt_event = threading.Event()

# API key comes from the environment: <PROVIDER>_API_KEY (e.g. DEEPSEEK_API_KEY),
# derived from MODEL's provider prefix in main(). Never hardcode a key here -- it
# would be committed to git. For convenience, set it in a git-ignored shell rc or
# `export DEEPSEEK_API_KEY=...` before running.
API_KEY = ""

# MODEL comes from lib.model; MODEL_DIR is its log-dir slug (provider--model-name).
MODEL_DIR = model_dir(MODEL)

# Named-batch identifier; when non-empty, trajectories route to a batch
# subdirectory (logs/<model>/<batch>/<arm>/<instance>/). Set once in main() so
# every path helper -- including resume/skip detection -- resolves to the same
# location without threading the value through is_done/run_outcome/summarize.
BATCH_ID = ""


def run_log_dir(arm: str, instance_id: str) -> Path:
    base = LOGS_DIR / MODEL_DIR
    if BATCH_ID:
        base = base / BATCH_ID
    return base / arm / instance_id


def traj_path(arm: str, instance_id: str, rep: int) -> Path:
    return run_log_dir(arm, instance_id) / f"{rep}.traj.json"


def manifest_path(arm: str, instance_id: str, rep: int) -> Path:
    return run_log_dir(arm, instance_id) / f"{rep}.manifest.json"


def read_manifest(arm: str, instance_id: str, rep: int) -> dict | None:
    mp = manifest_path(arm, instance_id, rep)
    if not mp.exists():
        return None
    try:
        return json.loads(mp.read_text())
    except (json.JSONDecodeError, OSError):
        return None


def traj_exit_status(arm: str, instance_id: str, rep: int) -> str | None:
    """exit_status recorded inside a written trajectory, or None if no traj."""
    p = traj_path(arm, instance_id, rep)
    if not p.exists():
        return None
    try:
        return json.loads(p.read_text()).get("info", {}).get("exit_status", "")
    except (json.JSONDecodeError, OSError):
        return None


# Exit statuses that represent a legitimate, complete termination. Anything else
# recorded in a trajectory (e.g. "ValueError" from the interactive turn-limit
# prompt, "EOFError", or "") is a framework crash mid-run — contaminated data
# that should be re-runnable, not silently counted as done.
CLEAN_EXIT_STATUSES = {"Submitted", "LimitsExceeded"}


def run_outcome(arm: str, instance_id: str, rep: int) -> str:
    """Classify a run's recorded state.

    Returns one of:
      "clean"   — terminated normally (Submitted / LimitsExceeded)
      "crashed" — a trajectory with a non-clean exit_status, or a manifest
                  "failed"/"completed"-without-trajectory (framework error)
      "started" — manifest "started" with no finish (in-flight or orphaned)
      "none"    — never attempted (no trajectory, no manifest)

    An in-flight "started" manifest takes precedence over a partially-written
    trajectory, so a running job is not misread as crashed.
    """
    m = read_manifest(arm, instance_id, rep)
    if m is not None and m.get("status") == "started" and not m.get("finished_at"):
        return "started"
    es = traj_exit_status(arm, instance_id, rep)
    if es is not None:
        return "clean" if es in CLEAN_EXIT_STATUSES else "crashed"
    if m is None:
        return "none"
    if m.get("status") in ("failed", "completed"):
        return "crashed"  # "completed" but no trajectory on disk is itself a fault
    return "none"


def is_done(arm: str, instance_id: str, rep: int, retry_failed: bool = False) -> bool:
    """Whether a run is terminally accounted for (and should be skipped).

    - "clean"   → done (skip).
    - "crashed" → done unless --retry-failed (then re-run). Covers manifest
                  "failed" *and* trajectories with a crash exit_status such as
                  the interactive-prompt ValueError.
    - "started" → not done (orphaned/in-flight; re-run on resume).
    - "none"    → not done (never attempted).
    """
    outcome = run_outcome(arm, instance_id, rep)
    if outcome == "clean":
        return True
    if outcome == "crashed":
        return not retry_failed
    return False  # "started" or "none"


def append_ledger(record: dict) -> None:
    """Append one attempt record to the ledger (atomic small-write under O_APPEND)."""
    LEDGER_PATH.parent.mkdir(parents=True, exist_ok=True)
    line = json.dumps(record) + "\n"
    with _ledger_lock:
        with open(LEDGER_PATH, "a") as f:
            f.write(line)


def build_runs(instances: list[str], arms: list[str], reps: int) -> list[tuple[str, str, int]]:
    """Return all (arm, instance_id, rep) triples, sorted for deterministic ordering."""
    runs = []
    for arm in arms:
        for iid in instances:
            for rep in range(1, reps + 1):
                runs.append((arm, iid, rep))
    return runs


def run_one(arm: str, instance_id: str, rep: int, dry_run: bool, subset: str,
            quiet: bool, retry_failed: bool = False,
            batch_id: str = "", n_files_map: dict | None = None) -> bool:
    if _halt_event.is_set():
        return False
    if is_done(arm, instance_id, rep, retry_failed):
        print(f"  SKIP  [{arm}] {instance_id} rep={rep}")
        return True

    print(f"  RUN   [{arm}] {instance_id} rep={rep}")
    if dry_run:
        return True

    n_files = (n_files_map or {}).get(instance_id, "")
    python = str(VENV_PYTHON) if VENV_PYTHON.exists() else sys.executable
    cmd = [python, str(RUN_PILOT),
           "--arm", arm,
           "--instance", instance_id,
           "--model", MODEL,
           "--subset", subset,
           "--run-id", str(rep)]
    if batch_id:
        cmd += ["--batch-id", batch_id]
    if n_files:
        cmd += ["--n-files", n_files]
    if quiet:
        cmd.append("--quiet")

    started_at = datetime.now(timezone.utc).isoformat()
    result = subprocess.run(
        cmd,
        env=os.environ.copy(),
        cwd=str(REPO_ROOT),
    )
    finished_at = datetime.now(timezone.utc).isoformat()

    if result.returncode == 3:
        _halt_event.set()
        print("\n" + "=" * 68, flush=True)
        print("DB CORRUPTION DETECTED — halting the batch.", flush=True)
        print(f"  Instance: {instance_id}", flush=True)
        print(f"  Database: experiment/dbs/{instance_id}.db", flush=True)
        print("  Investigate before regenerating. To regenerate:", flush=True)
        print(f"    rm experiment/dbs/{instance_id}.db", flush=True)
        print(f"    bash experiment/index_instance.sh {instance_id}", flush=True)
        print("=" * 68 + "\n", flush=True)
        return False

    # The child writes its own manifest; the parent additionally records the
    # attempt here. subprocess.run() returns even if the child was killed, so
    # this captures crashes that leave no trajectory.
    traj_written = traj_path(arm, instance_id, rep).exists()
    exit_status = traj_exit_status(arm, instance_id, rep)
    ok = result.returncode == 0 and traj_written
    m = read_manifest(arm, instance_id, rep)
    model_name = m.get("model_name", "") if m else ""

    append_ledger({
        "arm": arm,
        "instance_id": instance_id,
        "rep": rep,
        "batch_id": batch_id,
        "n_files": n_files,
        "model_name": model_name,
        "started_at": started_at,
        "finished_at": finished_at,
        "returncode": result.returncode,
        "traj_written": traj_written,
        "exit_status": exit_status,
        "ok": ok,
    })

    status = "DONE" if ok else "FAIL"
    print(f"  {status}  [{arm}] {instance_id} rep={rep} "
          f"(rc={result.returncode}, traj={'yes' if traj_written else 'NO'}, "
          f"exit={exit_status!r})")
    return ok


def summarize(all_runs: list[tuple[str, str, int]]) -> None:
    """Print current terminal-state counts across all runs, by exit_status.

    Lets completion rate (preregistration §9.3) and failure-by-cause (§11) be
    read at a glance. A run with no trajectory is bucketed by its manifest
    status, or (not-run) if it was never attempted.
    """
    counts: Counter[str] = Counter()
    for arm, iid, rep in all_runs:
        es = traj_exit_status(arm, iid, rep)
        if es is None:
            m = read_manifest(arm, iid, rep)
            es = f"(no-traj: {m.get('status')})" if m else "(not-run)"
        elif es == "":
            es = "(empty/incomplete)"
        counts[es] += 1
    print("\nRun outcomes (by exit_status):")
    for status, n in sorted(counts.items(), key=lambda kv: -kv[1]):
        print(f"  {n:4d}  {status}")


def main() -> int:
    global MODEL, MODEL_DIR, BATCH_ID
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--instances-file", type=Path, metavar="FILE",
                        help="Instance list file (e.g. experiment/verified_instance_ids.txt)")
    parser.add_argument("--instance-id", type=str, metavar="ID",
                        help="Single instance ID (e.g. matplotlib__matplotlib-14623)")
    parser.add_argument("--model", type=str, default=MODEL, metavar="MODEL",
                        help=f"Model id (slash or dir-slug form); default: {MODEL}")
    parser.add_argument("--runs", "--reps", type=int, default=10, dest="runs", metavar="N",
                        help="Repetitions per instance per arm (default: 10)")
    parser.add_argument("--arms", nargs="+", choices=["control", "treatment"],
                        default=["control", "treatment"],
                        help="Arms to run (default: both)")
    parser.add_argument("--workers", type=int, default=1, metavar="N",
                        help="Parallel workers (default: 1)")
    parser.add_argument("--seed", type=int, default=42,
                        help="Random seed for run ordering (default: 42)")
    parser.add_argument("--subset", default="verified", choices=["lite", "verified", "test"],
                        help="SWE-bench subset (default: verified)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print plan without executing")
    parser.add_argument("--quiet", action="store_true",
                        help="Capture output; only show progress lines")
    parser.add_argument("--retry-failed", action="store_true",
                        help="Re-run crashed attempts: manifest 'failed' or a "
                             "trajectory with a non-clean exit_status "
                             "(e.g. ValueError), instead of treating them as done")
    parser.add_argument("--batch-id", default="", metavar="BATCH",
                        help="Named batch identifier forwarded to each run's manifest "
                             "(default: '' — no batch tag)")
    args = parser.parse_args()

    # Override the module-level model (and its log-dir slug) so every path,
    # child --model, and API-key lookup below uses the requested model.
    MODEL = normalize_model(args.model)
    MODEL_DIR = model_dir(MODEL)
    BATCH_ID = args.batch_id

    if not args.instances_file and not args.instance_id:
        print("ERROR: one of --instances-file or --instance-id is required", file=sys.stderr)
        return 1

    n_files_map: dict[str, str] = {}
    if args.instances_file:
        if not args.instances_file.exists():
            print(f"ERROR: {args.instances_file} not found", file=sys.stderr)
            return 1
        instances = parse_instance_ids(args.instances_file)
        # Parse n_files from the third column of the instances file (when present).
        for line in args.instances_file.read_text().splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            toks = line.split()
            if len(toks) >= 3 and toks[2].isdigit():
                n_files_map[toks[0]] = toks[2]
    else:
        instances = [args.instance_id]

    all_runs = build_runs(instances, args.arms, args.runs)

    done = [(a, i, r) for a, i, r in all_runs if is_done(a, i, r, args.retry_failed)]
    todo = [(a, i, r) for a, i, r in all_runs if not is_done(a, i, r, args.retry_failed)]

    crashed = [(a, i, r) for a, i, r in all_runs if run_outcome(a, i, r) == "crashed"]

    print(f"Batch:      {args.batch_id or '(none)'}")
    print(f"Model:      {MODEL}")
    print(f"Instances:  {len(instances)}")
    print(f"Arms:       {', '.join(args.arms)}")
    print(f"Runs/arm:   {args.runs}")
    print(f"Total runs: {len(all_runs)}  ({len(done)} done, {len(todo)} remaining)")
    if crashed:
        shown = "queued for re-run" if args.retry_failed else "counted done; pass --retry-failed to re-run"
        print(f"Crashed:    {len(crashed)} run(s) with a non-clean exit_status ({shown})")

    if not todo:
        print("Nothing to do.")
        summarize(all_runs)
        return 0

    if args.dry_run:
        print("\n--dry-run: exiting without executing.")
        return 0

    # Resolve API key: inline constant first, then env var.
    key_var = api_key_var(MODEL)
    key = API_KEY.strip() if API_KEY else os.environ.get(key_var, "")
    os.environ[key_var] = key
    if not key:
        print(f"ERROR: {key_var} is not set and API_KEY is empty", file=sys.stderr)
        return 1

    # Randomize remaining run order (per preregistration)
    rng = random.Random(args.seed)
    rng.shuffle(todo)

    print(f"\nRunning {len(todo)} run(s) with {args.workers} worker(s)...\n")

    failures = 0

    if args.workers == 1:
        for i, (arm, iid, rep) in enumerate(todo, 1):
            print(f"[{i}/{len(todo)}]", end=" ")
            if not run_one(arm, iid, rep, args.dry_run, args.subset, args.quiet, args.retry_failed,
                           batch_id=args.batch_id, n_files_map=n_files_map):
                failures += 1
            if _halt_event.is_set():
                print(f"\nBatch halted: DB corruption. {len(todo) - i} run(s) skipped.")
                break
    else:
        completed = 0
        with ThreadPoolExecutor(max_workers=args.workers) as pool:
            futures = {
                pool.submit(run_one, arm, iid, rep, args.dry_run, args.subset, args.quiet,
                            args.retry_failed, args.batch_id, n_files_map): (arm, iid, rep)
                for arm, iid, rep in todo
            }
            for future in as_completed(futures):
                completed += 1
                if not future.result():
                    failures += 1
                arm, iid, rep = futures[future]
                print(f"  [{completed}/{len(todo)} done]")
                if _halt_event.is_set():
                    remaining = len(todo) - completed
                    print(f"\nBatch halted: DB corruption. {remaining} run(s) skipped.")
                    pool.shutdown(wait=False, cancel_futures=True)
                    break

    print(f"\nFinished. Failures this session: {failures}/{len(todo)}")
    print(f"Attempt ledger: {LEDGER_PATH}")
    summarize(all_runs)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
