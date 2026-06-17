#!/usr/bin/env python3
"""
Run one arm of the qi context preservation experiment on a single SWE-bench instance.

Usage:
    python3 experiment/run_pilot.py --arm control   --instance django__django-11099
    python3 experiment/run_pilot.py --arm treatment --instance django__django-11099
    python3 experiment/run_pilot.py --arm treatment --instance django__django-11099 --run-id 7

The script handles:
  - treatment arm: injects the correct per-instance DB path into run_args (since
    env_startup_command is broken in mini-swe-agent v2.4.1 with DockerEnvironment)
  - output path: experiment/logs/<arm>/<instance_id>/<run_id>.traj.json
  - API key: read from DEEPSEEK_API_KEY env var

Prerequisites:
    ./configure --enable-all && make
    bash experiment/build_qi_static.sh
    # DB for the instance must already exist:
    bash experiment/index_instance.sh <instance_id>
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
import textwrap
import time
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).parent.parent.resolve()
EXPERIMENT_DIR = REPO_ROOT / "experiment"
VENV_BIN = EXPERIMENT_DIR / ".venv" / "bin"
MINI_EXTRA = VENV_BIN / "mini-extra"
DBS_DIR = EXPERIMENT_DIR / "dbs"
LOGS_DIR = EXPERIMENT_DIR / "logs"
QI_STATIC = REPO_ROOT / "build" / "qi-static"
CONTROL_CONFIG = EXPERIMENT_DIR / "config" / "control.yaml"
TREATMENT_CONFIG = EXPERIMENT_DIR / "config" / "treatment.yaml"
SHARED_CONFIG = EXPERIMENT_DIR / "config" / "shared.yaml"

MODEL = "deepseek/deepseek-v4-flash"
COST_LIMIT = 0.5   # per-run safety ceiling in dollars


def check_prerequisites(arm: str, instance_id: str) -> None:
    errors = []
    if not MINI_EXTRA.exists():
        errors.append(f"mini-extra not found at {MINI_EXTRA} — activate or install the venv")
    if arm == "treatment":
        if not QI_STATIC.exists():
            errors.append(f"qi-static not found at {QI_STATIC} — run: bash experiment/build_qi_static.sh")
        db = DBS_DIR / f"{instance_id}.db"
        if not db.exists():
            errors.append(f"DB not found: {db} — run: bash experiment/index_instance.sh {instance_id}")
    if not os.environ.get("DEEPSEEK_API_KEY"):
        errors.append("DEEPSEEK_API_KEY is not set")
    if errors:
        for e in errors:
            print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)


def make_treatment_config(instance_id: str, tmp_dir: str) -> str:
    """Write a per-instance treatment config that mounts the correct DB in run_args.

    env_startup_command is broken in mini-swe-agent v2.4.1 with DockerEnvironment,
    so we mount the DB file directly instead of copying it at startup.
    """
    db_path = DBS_DIR / f"{instance_id}.db"
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
                   status: str, exit_code: int | None = None,
                   traj_written: bool | None = None,
                   started_at: str | None = None) -> Path:
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
        "started_at": started_at or existing.get("started_at") or datetime.now(timezone.utc).isoformat(),
        "finished_at": datetime.now(timezone.utc).isoformat() if status != "started" else None,
        "exit_code": exit_code if exit_code is not None else existing.get("exit_code"),
        "status": status,
        "traj_written": traj_written if traj_written is not None else existing.get("traj_written"),
    }
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(record, indent=2) + "\n")
    return manifest_path


def run_instance(arm: str, instance_id: str, run_id: str, subset: str, quiet: bool = False) -> int:
    out_dir = LOGS_DIR / arm / instance_id
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / f"{run_id}.traj.json"

    # Record that this run was attempted — before subprocess so a crash
    # doesn't look like "never tried" to the orchestrator.
    started_at = datetime.now(timezone.utc).isoformat()
    write_manifest(out_dir, run_id, arm, instance_id, "started",
                   started_at=started_at)

    env = os.environ.copy()
    env["MSWEA_COST_TRACKING"] = "ignore_errors"
    # Unbuffer the child's stdout so `tail -f` on the --quiet log file stays
    # responsive instead of lagging behind block buffering.
    env["PYTHONUNBUFFERED"] = "1"

    with tempfile.TemporaryDirectory() as tmp_dir:
        if arm == "control":
            configs = ["-c", "swebench.yaml", "-c", str(SHARED_CONFIG), "-c", str(CONTROL_CONFIG)]
        else:
            per_instance_cfg = make_treatment_config(instance_id, tmp_dir)
            configs = ["-c", "swebench.yaml", "-c", str(SHARED_CONFIG),
                       "-c", str(TREATMENT_CONFIG), "-c", per_instance_cfg]

        cmd = [
            str(MINI_EXTRA), "swebench-single",
            "--subset", subset,
            "--split", "test",
            "--instance", instance_id,
            "--model", MODEL,
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
    write_manifest(out_dir, run_id, arm, instance_id, status,
                   exit_code=result.returncode, traj_written=traj_written,
                   started_at=started_at)

    if traj_written:
        size_kb = out_path.stat().st_size // 1024
        print(f"\n  trajectory saved ({size_kb}K): {out_path}")
    else:
        print(f"\n  WARNING: trajectory file not written to {out_path}", file=sys.stderr)

    return result.returncode


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--arm", required=True, choices=["control", "treatment"],
                        help="Experimental arm to run")
    parser.add_argument("--instance", required=True, metavar="INSTANCE_ID",
                        help="SWE-bench instance ID (e.g. django__django-11099)")
    parser.add_argument("--subset", default="verified", choices=["lite", "verified", "test"],
                        help="SWE-bench subset (default: verified)")
    parser.add_argument("--run-id", default=None, metavar="ID",
                        help="Run identifier for the output filename (default: timestamp)")
    parser.add_argument("--quiet", action="store_true",
                        help="Capture output; only show final result line")
    args = parser.parse_args()

    run_id = args.run_id or time.strftime("%Y%m%d_%H%M%S")
    check_prerequisites(args.arm, args.instance)
    sys.exit(run_instance(args.arm, args.instance, run_id, args.subset, args.quiet))


if __name__ == "__main__":
    main()
