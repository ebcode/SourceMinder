#!/usr/bin/env python3
"""
Pre-index all SWE-bench instances for the qi context preservation experiment.

Reads an instance list (default experiment/verified_instance_ids.txt), optionally
pulls Docker images, then runs experiment/index_instance.sh for each instance.
Already-indexed instances (experiment/dbs/<instance_id>.db exists) are skipped.

Usage:
    python experiment/pre_index.py [OPTIONS]

Options:
    --pull              Pull Docker images before indexing (uses docker_images.txt)
    --workers N         Parallel workers for indexing (default: 1)
    --only INSTANCE_ID  Index a single instance and exit
    --dry-run           Print what would be done without doing it
"""

import argparse
import os
import subprocess
import sys
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

REPO_ROOT = Path(__file__).parent.parent.resolve()
EXPERIMENT_DIR = REPO_ROOT / "experiment"
DBS_DIR = EXPERIMENT_DIR / "dbs"
INDEX_SCRIPT = EXPERIMENT_DIR / "index_instance.sh"


def parse_instance_ids(path: Path):
    instances = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) >= 1:
                instances.append(parts[0])
    return instances


def instance_to_image(instance_id: str) -> str:
    return f"swebench/sweb.eval.x86_64.{instance_id.replace('__', '_1776_')}:latest"


def is_done(instance_id):
    return (DBS_DIR / f"{instance_id}.db").exists()


def pull_image(image, dry_run=False):
    print(f"  PULL {image}")
    if dry_run:
        return True
    result = subprocess.run(
        ["docker", "pull", image],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"  ERROR pulling {image}:\n{result.stderr.strip()}", file=sys.stderr)
        return False
    return True


_print_lock = threading.Lock()


def run_instance(instance_id, dry_run=False):
    if is_done(instance_id):
        with _print_lock:
            db = DBS_DIR / f"{instance_id}.db"
            size = db.stat().st_size // 1024
            print(f"  SKIP {instance_id} (already done, {size}K)")
        return True

    with _print_lock:
        print(f"  INDEX {instance_id} ...")

    if dry_run:
        return True

    result = subprocess.run(
        ["bash", str(INDEX_SCRIPT), instance_id],
        capture_output=True, text=True,
        cwd=str(REPO_ROOT)
    )

    with _print_lock:
        if result.returncode == 0:
            db = DBS_DIR / f"{instance_id}.db"
            size = db.stat().st_size // 1024 if db.exists() else 0
            print(f"  DONE  {instance_id} ({size}K)")
        else:
            print(f"  FAIL  {instance_id} (exit {result.returncode})", file=sys.stderr)
            if result.stdout.strip():
                print(result.stdout.strip(), file=sys.stderr)
            if result.stderr.strip():
                print(result.stderr.strip(), file=sys.stderr)

    return result.returncode == 0


def main():
    default_instances = EXPERIMENT_DIR / "verified_instance_ids.txt"
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--instances-file", type=Path, default=default_instances, metavar="FILE",
                        help=f"Instance list file (default: {default_instances.name})")
    parser.add_argument("--pull", action="store_true", help="Pull Docker images before indexing")
    parser.add_argument("--workers", type=int, default=1, metavar="N", help="Parallel workers (default: 1)")
    parser.add_argument("--only", metavar="INSTANCE_ID", help="Index a single instance and exit")
    parser.add_argument("--dry-run", action="store_true", help="Print plan without executing")
    args = parser.parse_args()

    if not args.instances_file.exists():
        print(f"ERROR: {args.instances_file} not found", file=sys.stderr)
        sys.exit(1)

    if not INDEX_SCRIPT.exists():
        print(f"ERROR: {INDEX_SCRIPT} not found", file=sys.stderr)
        sys.exit(1)

    instances = parse_instance_ids(args.instances_file)
    if args.only:
        if args.only not in instances:
            print(f"ERROR: {args.only!r} not in {args.instances_file.name}", file=sys.stderr)
            sys.exit(1)
        instances = [args.only]

    DBS_DIR.mkdir(exist_ok=True)

    done_count = sum(1 for i in instances if is_done(i))
    todo = [i for i in instances if not is_done(i)]
    print(f"Instances: {len(instances)} total, {done_count} already done, {len(todo)} to index")

    if args.pull:
        images = [instance_to_image(iid) for iid in instances]
        print(f"\nPulling {len(images)} Docker image(s)...")
        for image in images:
            pull_image(image, dry_run=args.dry_run)

    if not todo:
        print("Nothing to do.")
        return

    print(f"\nIndexing {len(todo)} instance(s) with {args.workers} worker(s)...")

    if args.workers == 1:
        successes = 0
        for i, instance_id in enumerate(todo, 1):
            print(f"[{i}/{len(todo)}]", end=" ")
            if run_instance(instance_id, dry_run=args.dry_run):
                successes += 1
    else:
        successes = 0
        completed = 0
        with ThreadPoolExecutor(max_workers=args.workers) as pool:
            futures = {pool.submit(run_instance, iid, args.dry_run): iid for iid in todo}
            for future in as_completed(futures):
                completed += 1
                if future.result():
                    successes += 1
                with _print_lock:
                    print(f"  [{completed}/{len(todo)} done]")

    print(f"\nFinished: {successes}/{len(todo)} succeeded")
    if successes < len(todo):
        sys.exit(1)


if __name__ == "__main__":
    main()
