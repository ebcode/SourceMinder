#!/usr/bin/env python3
"""
Run N reps of a single SWE-bench Pro instance across both arms in PARALLEL.

Pro analog of run_experiment.py's worker pool: a ThreadPoolExecutor where each
task is a blocking subprocess.run() of run_pro_one.py with its own --run-id.
Threads (not processes) are fine because each task spends its whole life blocked
in a subprocess that owns a separate Docker container.

Each (arm, rep) writes:
  - its full live model stream to its own log file (reviewable after the fact,
    never interleaved with other reps), and
  - the normal <instance>.<rep>.traj.json / .pred under the output root
    (logs/pro_pilot/ or logs/<model>/<batch-id>/).

Resume-safe: a rep whose trajectory already exists with a clean exit_status is
skipped, so re-running (or running alongside an in-flight run_reps.sh) won't
clobber finished reps. Pass --force to redo them.

Temperature stays at the upstream-faithful 0.0 (set in config/swebp_*.yaml); this
script does not touch it. Run with the Pro venv interpreter:

    experiment/.venv_pro/bin/python experiment/run_pro_reps.py --reps 5 --workers 5
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
from pathlib import Path

EXPERIMENT_DIR = Path(__file__).resolve().parent
RUN_ONE = EXPERIMENT_DIR / "run_pro_one.py"
VENV_PYTHON = EXPERIMENT_DIR / ".venv_pro" / "bin" / "python"
DEFAULT_INSTANCE = ("instance_qutebrowser__qutebrowser-"
                    "f91ace96223cac8161c16dd061907e138fe85111-"
                    "v059c6fdc75567943479b23ebca7c07b5e9a7f34c")
DEFAULT_MODEL = "deepseek/deepseek-v4-flash"

sys.path.insert(0, str(EXPERIMENT_DIR))
from lib.model import model_dir


def default_output(batch_id: str = "", model: str = DEFAULT_MODEL) -> Path:
    """Output root: logs/<model_dir>/<batch_id>/ if batch_id is set,
    otherwise logs/pro_pilot/."""
    if batch_id:
        return EXPERIMENT_DIR / "logs" / model_dir(model) / batch_id
    return EXPERIMENT_DIR / "logs" / "pro_pilot"

_print_lock = threading.Lock()

# Append-only ledger of every completed attempt (one JSON object per line). The
# orchestrator (this parent) writes a row after each subprocess returns, so a rep
# that is killed or crashes without writing a trajectory is still recorded. Pro
# analog of run_experiment.py's logs/run_ledger.jsonl. Lives under logs/ (ignored),
# so it is a local, regenerable artifact -- never committed.
LEDGER_PATH = EXPERIMENT_DIR / "logs" / "run_pro_ledger.jsonl"
_ledger_lock = threading.Lock()


def board(msg: str) -> None:
    with _print_lock:
        print(msg, flush=True)


def append_ledger(record: dict) -> None:
    """Append one attempt record to the ledger (atomic small-write under O_APPEND)."""
    LEDGER_PATH.parent.mkdir(parents=True, exist_ok=True)
    line = json.dumps(record) + "\n"
    with _ledger_lock:
        with LEDGER_PATH.open("a") as f:
            f.write(line)


def traj_path(output: Path, arm: str, instance: str, rid: str) -> Path:
    return output / arm / instance / f"{instance}.{rid}.traj.json"


def is_done(output: Path, arm: str, instance: str, rid: str) -> bool:
    """A rep counts as done if its trajectory exists with a clean exit_status.

    Mirrors run_experiment.is_done: a crashed run (e.g. exit_status=ValueError)
    is NOT done and will be redone.
    """
    p = traj_path(output, arm, instance, rid)
    if not p.exists():
        return False
    try:
        import json
        es = json.loads(p.read_text()).get("info", {}).get("exit_status", "")
    except Exception:
        return False
    # Treat anything that isn't an exception-class name as a clean terminal state.
    return bool(es) and es in {"Submitted", "LimitsExceeded", "Completed"}


def run_one(arm: str, rid: str, instance: str, model: str, subset: str | None,
            logdir: Path, output: Path, force: bool,
            batch_id: str = "") -> tuple[str, str, int, str]:
    """Run a single (arm, rep). Returns (arm, rid, returncode, summary_line)."""
    if not force and is_done(output, arm, instance, rid):
        return arm, rid, 0, "SKIP (already done)"

    log = logdir / f"{arm}_{rid}.log"
    cmd = [str(VENV_PYTHON if VENV_PYTHON.exists() else sys.executable),
           str(RUN_ONE),
           "--instance", instance,
           "--arm", arm,
           "--run-id", rid,
           "--model", model,
           "--output", str(output)]
    if subset:
        cmd += ["--subset", subset]

    board(f"{arm:<16} {rid}  {datetime.now():%H:%M:%S}  starting  -> {log}")
    started_at = datetime.now(timezone.utc).isoformat()
    with log.open("w") as fh:
        rc = subprocess.run(cmd, stdout=fh, stderr=subprocess.STDOUT).returncode
    finished_at = datetime.now(timezone.utc).isoformat()

    summary = ""
    try:
        for line in log.read_text().splitlines():
            if line.startswith("exit_status="):
                summary = line
    except Exception:
        pass

    # Record the attempt: subprocess.run() returns even if the child was killed,
    # so this captures crashes that leave no trajectory. exit_status is read from
    # the written trajectory (same source is_done uses), or None if none was written.
    tp = traj_path(output, arm, instance, rid)
    traj_written = tp.exists()
    exit_status = None
    if traj_written:
        try:
            exit_status = json.loads(tp.read_text()).get("info", {}).get("exit_status", "")
        except Exception:
            exit_status = None
    append_ledger({
        "arm": arm,
        "instance_id": instance,
        "rep": rid,
        "batch_id": batch_id,
        "model": model,
        "started_at": started_at,
        "finished_at": finished_at,
        "returncode": rc,
        "traj_written": traj_written,
        "exit_status": exit_status,
        "ok": rc == 0 and traj_written,
    })

    return arm, rid, rc, summary or "(no summary line)"


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--reps", type=int, default=5, help="Reps per arm (default: 5)")
    ap.add_argument("--run-id-prefix", default="",
                    help="Prepended to every run-id so distinct rep batches stay "
                         "separable in logs/pro_pilot (e.g. 'oldprompt_' -> run-id "
                         "'oldprompt_rep01'). Default '' -> 'rep01'.")
    ap.add_argument("--workers", type=int, default=5,
                    help="Parallel workers across all (arm,rep) tasks (default: 5)")
    ap.add_argument("--arms", nargs="+", default=["swebp_control", "swebp_treatment"],
                    help="Arms to run (default: swebp_control swebp_treatment)")
    ap.add_argument("--instance", default=DEFAULT_INSTANCE, help="Instance id")
    ap.add_argument("--model", default=DEFAULT_MODEL, help="litellm model id")
    ap.add_argument("--subset", default=None, help="Dataset path/subset (default: runner's)")
    ap.add_argument("--output", default=None, help="Output root dir (default: logs/pro_pilot/ or logs/<model>/<batch-id>/)")
    ap.add_argument("--batch-id", default="", metavar="BATCH",
                    help="Named batch identifier; routes output to "
                         "logs/<model>/<batch-id>/ instead of logs/pro_pilot/")
    ap.add_argument("--force", action="store_true", help="Redo reps even if already done")
    args = ap.parse_args()

    output = Path(args.output) if args.output else default_output(args.batch_id, args.model)
    # Per-run logs live directly in the batch folder: each filename is already
    # unique per (arm, rep) -- e.g. swebp_treatment_rep05.log -- so no
    # reps_<timestamp> subfolder is needed. Re-running a batch overwrites same-
    # named logs (the trajectory is the source of truth; runs are resume-safe).
    logdir = output
    logdir.mkdir(parents=True, exist_ok=True)

    tasks = [(arm, f"{args.run_id_prefix}rep{i:02d}") for arm in args.arms
             for i in range(1, args.reps + 1)]

    print(f"instance: {args.instance}")
    print(f"arms:     {', '.join(args.arms)}")
    print(f"reps:     {args.reps}  x  {len(args.arms)} arms  =  {len(tasks)} runs"
          + (f"  (run-id prefix '{args.run_id_prefix}')" if args.run_id_prefix else ""))
    print(f"workers:  {args.workers}   (temp 0.0, upstream-faithful)")
    print(f"logs:     {logdir}/<arm>_repNN.log")
    print(f"ledger:   {LEDGER_PATH}")
    print("=" * 72)

    failures = 0
    completed = 0
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = {
            pool.submit(run_one, arm, rid, args.instance, args.model,
                        args.subset, logdir, output, args.force,
                        args.batch_id): (arm, rid)
            for arm, rid in tasks
        }
        for fut in as_completed(futures):
            arm, rid, rc, summary = fut.result()
            completed += 1
            if rc != 0:
                failures += 1
            board(f"{arm:<16} {rid}  {datetime.now():%H:%M:%S}  "
                  f"done rc={rc}  [{completed}/{len(tasks)}]  {summary}")

    print("=" * 72)
    print(f"ALL REPS DONE ({datetime.now():%H:%M:%S}) — "
          f"{failures} failure(s) / {len(tasks)} runs")
    print(f"per-run logs: {logdir}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
