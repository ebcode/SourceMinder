#!/usr/bin/env python3
"""
Run one arm of the qi context preservation experiment on a single SWE-bench instance.

Usage:
    python3 experiment/run_pilot.py --arm control   --instance django__django-11099
    python3 experiment/run_pilot.py --arm treatment --instance django__django-11099
    python3 experiment/run_pilot.py --arm treatment --instance django__django-11099 --run-id 7
    python3 experiment/run_pilot.py --arm control   --instance django__django-11099 --model anthropic/claude-haiku-4-5-20251001

The script handles:
  - treatment arm: injects the correct per-instance DB path into run_args (since
    env_startup_command is broken in mini-swe-agent v2.4.1 with DockerEnvironment)
  - output path: experiment/logs/<model>/<arm>/<instance_id>/<run_id>.traj.json
  - API key: read from <PROVIDER>_API_KEY env var (derived from --model prefix)

Prerequisites:
    ./configure --enable-all && make
    bash experiment/build_qi_static.sh
    # DB for the instance must already exist:
    bash experiment/index_instance.sh <instance_id>
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import textwrap
import time
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[0]))  # -> experiment/
from lib import paths
from lib.dbcheck import integrity_ok
from lib.model import MODEL as DEFAULT_MODEL, api_key_var, model_dir, normalize_model

REPO_ROOT = paths.REPO_ROOT
EXPERIMENT_DIR = paths.EXPERIMENT_DIR
VENV_BIN = EXPERIMENT_DIR / ".venv" / "bin"
MINI_EXTRA = VENV_BIN / "mini-extra"
DBS_DIR = paths.DBS_DIR
LOGS_DIR = paths.LOGS_DIR
QI_STATIC = REPO_ROOT / "build" / "qi-static"
CONTROL_CONFIG = paths.CONFIG_DIR / "control.yaml"
TREATMENT_CONFIG = paths.CONFIG_DIR / "treatment.yaml"
SHARED_CONFIG = paths.CONFIG_DIR / "shared.yaml"

COST_LIMIT = 5.0   # per-run safety ceiling in dollars


def check_prerequisites(arm: str, instance_id: str, model: str) -> None:
    errors = []
    if not MINI_EXTRA.exists():
        errors.append(f"mini-extra not found at {MINI_EXTRA} — activate or install the venv")
    if arm == "treatment":
        if not QI_STATIC.exists():
            errors.append(f"qi-static not found at {QI_STATIC} — run: bash experiment/build_qi_static.sh")
        db = DBS_DIR / f"{instance_id}.db"
        if not db.exists():
            errors.append(f"DB not found: {db} — run: bash experiment/index_instance.sh {instance_id}")
        else:
            # Verify the source DB is well-formed before we trust it.
            ok, detail = integrity_ok(db)
            if not ok:
                errors.append(
                    f"DB integrity check FAILED for {db}: {detail!r}\n"
                    f"  Regenerate: rm {db} && bash experiment/index_instance.sh {instance_id}"
                )
    key_var = api_key_var(model)
    if not os.environ.get(key_var):
        errors.append(f"{key_var} is not set")
    if errors:
        for e in errors:
            print(f"ERROR: {e}", file=sys.stderr)
        # Exit code 3 = DB corruption; signals the orchestrator to halt the batch.
        db_errors = any("DB integrity check FAILED" in e for e in errors)
        sys.exit(3 if db_errors else 1)


def prepare_db_copy(instance_id: str, tmp_dir: str) -> str:
    """Copy the source DB into tmp_dir so the container gets an isolated copy.

    Returns the path to the copied DB. The copy lives inside tmp_dir and is
    automatically cleaned up when the temp dir is torn down.
    """
    src = DBS_DIR / f"{instance_id}.db"
    dst = os.path.join(tmp_dir, "code-index.db")
    shutil.copy2(src, dst)
    return dst


def make_treatment_config(instance_id: str, tmp_dir: str,
                          db_path: str | None = None) -> str:
    """Write a per-instance treatment config that mounts the correct DB."""
    if db_path is None:
        db_path = str(DBS_DIR / f"{instance_id}.db")
    config_text = textwrap.dedent(f"""\
        # Auto-generated per-instance treatment config — do not commit.
        # Extends experiment/config/treatment.yaml with the correct DB path.
        environment:
          run_args:
            - "--rm"
            - "-v"
            - "{QI_STATIC}:/usr/local/bin/qi:ro"
            - "-v"
            - "{db_path}:/testbed/code-index.db"
    """)
    path = os.path.join(tmp_dir, f"treatment_{instance_id}.yaml")
    with open(path, "w") as f:
        f.write(config_text)
    return path


def write_manifest(out_dir: Path, run_id: str, arm: str, instance_id: str,
                   model_name: str, status: str, exit_code: int | None = None,
                   traj_written: bool | None = None,
                   started_at: str | None = None,
                   batch_id: str = "", n_files: str = "") -> Path:
    manifest_path = out_dir / f"{run_id}.manifest.json"
    existing = {}
    if manifest_path.exists():
        try:
            existing = json.loads(manifest_path.read_text())
        except json.JSONDecodeError:
            pass
    record = {
        "arm": arm,
        "instance_id": instance_id,
        "run_id": run_id,
        "model_name": model_name,
        "batch_id": batch_id or existing.get("batch_id", ""),
        "n_files": n_files or existing.get("n_files", ""),
        "started_at": started_at or existing.get("started_at") or datetime.now(timezone.utc).isoformat(),
        "finished_at": datetime.now(timezone.utc).isoformat() if status != "started" else None,
        "exit_code": exit_code if exit_code is not None else existing.get("exit_code"),
        "status": status,
        "traj_written": traj_written if traj_written is not None else existing.get("traj_written"),
    }
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(record, indent=2) + "\n")
    return manifest_path


def run_instance(arm: str, instance_id: str, run_id: str, subset: str,
                 model: str, quiet: bool = False, dry_run: bool = False,
                 batch_id: str = "", n_files: str = "") -> int:
    # A named batch routes trajectories to logs/<model>/<batch>/<arm>/<instance>/
    # so re-runs of the same model+instances under different batch ids don't
    # overwrite each other; without a batch the legacy layout is preserved.
    out_dir = LOGS_DIR / model_dir(model)
    if batch_id:
        out_dir = out_dir / batch_id
    out_dir = out_dir / arm / instance_id
    out_path = out_dir / f"{run_id}.traj.json"

    env = os.environ.copy()
    env["MSWEA_COST_TRACKING"] = "ignore_errors"
    # Unbuffer the child's stdout so `tail -f` on the --quiet log file stays
    # responsive instead of lagging behind block buffering.
    env["PYTHONUNBUFFERED"] = "1"

    with tempfile.TemporaryDirectory() as tmp_dir:
        if arm == "control":
            configs = ["-c", "swebench.yaml", "-c", str(SHARED_CONFIG), "-c", str(CONTROL_CONFIG)]
        else:
            # In dry-run, point at the source DB instead of copying it (no I/O,
            # no API): the printed command still shows the real mount target.
            db_copy = (str(DBS_DIR / f"{instance_id}.db") if dry_run
                       else prepare_db_copy(instance_id, tmp_dir))
            per_instance_cfg = make_treatment_config(instance_id, tmp_dir, db_path=db_copy)
            configs = ["-c", "swebench.yaml", "-c", str(SHARED_CONFIG),
                       "-c", str(TREATMENT_CONFIG), "-c", per_instance_cfg]

        cmd = [
            str(MINI_EXTRA), "swebench-single",
            "--subset", subset,
            "--split", "test",
            "--instance", instance_id,
            "--model", model,
            "--cost-limit", str(COST_LIMIT),
            "--output", str(out_path),
            "--exit-immediately",
            "--yolo",
            *configs,
        ]

        log_path = out_dir / f"{run_id}.log" if quiet else None

        print(f"[{arm}] {instance_id} run={run_id}")
        print(f"  trajectory: {out_path}")
        if quiet:
            print(f"  log:        {log_path}")
            print(f"  follow:     tail -f {log_path}")
        print(f"  cmd: {' '.join(cmd)}")
        print(flush=True)

        if dry_run:
            print("  [dry-run] prerequisites OK; not invoking mini-extra "
                  "(no API spend).")
            return 0

        out_dir.mkdir(parents=True, exist_ok=True)
        # Record that this run was attempted — before subprocess so a crash
        # doesn't look like "never tried" to the orchestrator.
        started_at = datetime.now(timezone.utc).isoformat()
        write_manifest(out_dir, run_id, arm, instance_id, model, "started",
                       started_at=started_at, batch_id=batch_id, n_files=n_files)

        if quiet:
            with log_path.open("w") as log_f:
                result = subprocess.run(cmd, env=env, cwd=str(REPO_ROOT),
                                        stdout=log_f, stderr=subprocess.STDOUT,
                                        stdin=subprocess.DEVNULL)
        else:
            result = subprocess.run(cmd, env=env, cwd=str(REPO_ROOT),
                                    stdin=subprocess.DEVNULL)

    traj_written = out_path.exists()
    status = "completed" if result.returncode == 0 else "failed"
    write_manifest(out_dir, run_id, arm, instance_id, model, status,
                   exit_code=result.returncode, traj_written=traj_written,
                   started_at=started_at)

    if traj_written:
        size_kb = out_path.stat().st_size // 1024
        print(f"\n  trajectory saved ({size_kb}K): {out_path}")
    else:
        print(f"\n  WARNING: trajectory file not written to {out_path}", file=sys.stderr)

    return result.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--arm", required=True, choices=["control", "treatment"],
                        help="Experimental arm to run")
    parser.add_argument("--instance", required=True, metavar="INSTANCE_ID",
                        help="SWE-bench instance ID (e.g. django__django-11099)")
    parser.add_argument("--model", default=DEFAULT_MODEL, metavar="MODEL",
                        help=f"litellm model identifier (default: {DEFAULT_MODEL})")
    parser.add_argument("--subset", default="verified", choices=["lite", "verified", "test"],
                        help="SWE-bench subset (default: verified)")
    parser.add_argument("--run-id", default=None, metavar="ID",
                        help="Run identifier for the output filename (default: timestamp)")
    parser.add_argument("--quiet", action="store_true",
                        help="Capture output; only show final result line")
    parser.add_argument("--dry-run", action="store_true",
                        help="Check prerequisites and print the command without "
                             "invoking mini-extra (no API spend)")
    parser.add_argument("--batch-id", default="", metavar="BATCH",
                        help="Batch identifier written to the manifest (default: '')")
    parser.add_argument("--n-files", default="", metavar="N",
                        help="Gold-patch file count written to the manifest (default: '')")
    args = parser.parse_args()

    # Accept either the litellm id or its dir-slug for --model; normalize to the
    # canonical provider/model form so the key lookup and litellm both get a
    # valid id.
    model = normalize_model(args.model)

    run_id = args.run_id or time.strftime("%Y%m%d_%H%M%S")
    check_prerequisites(args.arm, args.instance, model)
    return run_instance(args.arm, args.instance, run_id, args.subset,
                        model, args.quiet, args.dry_run,
                        batch_id=args.batch_id, n_files=args.n_files)


if __name__ == "__main__":
    raise SystemExit(main())
