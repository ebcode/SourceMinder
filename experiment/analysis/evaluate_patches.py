#!/usr/bin/env python3
"""Evaluate submitted patches with the SWE-bench harness -> task_success.

Walks a logs directory for ``*.traj.json`` files, extracts each run's submitted
patch from ``info.submission`` (the unified diff mini-swe-agent stores on a
``Submitted`` exit; empty string when the agent never submitted), and feeds the
patches through ``swebench.harness.run_evaluation`` to determine whether each
patch actually resolves its instance. Writes one row per run to a shared WAL
SQLite DB (see ``eval_db.py``) and re-exports ``eval_results.csv`` -- with a
``resolved`` column that can be joined onto ``runs.csv`` from
``analyze_trajectories.py`` -- after every group.

Why batch per (arm, rep): SWE-bench predictions are keyed by ``instance_id`` and
the harness evaluates one prediction per instance. control/treatment and each rep
all reuse the same instance_ids, so they must be evaluated in separate groups.
Each (arm, rep) group has unique instance_ids and gets its own ``run_id`` and
report file.

This step is Docker-only (no API spend): the harness pulls the prebuilt
``swebench/sweb.eval.*`` images and runs the instance's test suite against the
patched ``/testbed``.

Usage:
  # preview the prediction plan without invoking Docker
  python3 experiment/analysis/evaluate_patches.py --logs experiment/logs --dry-run

  # run the harness and write results to a timestamped directory
  python3 experiment/analysis/evaluate_patches.py \
      --logs experiment/logs --dir experiment/analysis/20260616_223000

  # re-evaluate only retry-failed instances (scope + force reports rebuild)
  python3 experiment/analysis/evaluate_patches.py \
      --logs experiment/logs --dir experiment/analysis/retry-01 \
      --instance-id matplotlib__matplotlib-14623 --rewrite-reports
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
import time
from pathlib import Path

import eval_db

ARMS = ("control", "treatment")

# subset -> HuggingFace dataset name the swebench harness loads.
DATASETS = {
    "verified": "princeton-nlp/SWE-bench_Verified",
    "lite": "princeton-nlp/SWE-bench_Lite",
    "test": "princeton-nlp/SWE-bench",
}


def infer_arm_instance(path: Path) -> tuple[str, str]:
    """Derive (arm, instance_id) from the path, preferring directory layout.

    Mirrors analyze_trajectories.infer_arm_instance so both analysis scripts
    agree on how a trajectory path maps to (arm, instance).
    """
    parts = path.parts
    arm = next((p for p in parts if p in ARMS), "")
    if arm and path.parent.parent.name == arm:
        return arm, path.parent.name
    stem = path.name.replace(".traj.json", "")
    for a in ARMS:
        if stem.endswith("_" + a):
            return a, stem[: -(len(a) + 1)]
    return arm, stem


def rep_of(path: Path) -> str:
    """Run id (rep) for a trajectory: the filename stem, e.g. ``1`` or ``3``."""
    return path.name.replace(".traj.json", "")


def collect_runs(logs: Path, instance_ids: set[str] | None = None) -> list[dict]:
    """One record per trajectory: arm, instance, rep, patch, exit_status.
    
    When ``instance_ids`` is given, only trajectories belonging to those
    instances are collected.
    """
    runs: list[dict] = []
    for path in sorted(logs.rglob("*.traj.json")):
        try:
            data = json.loads(path.read_text())
        except (json.JSONDecodeError, OSError) as exc:
            print(f"WARNING: skipping {path}: {exc}", file=sys.stderr)
            continue
        info = data.get("info", {})
        arm, instance = infer_arm_instance(path)
        if instance_ids is not None and instance not in instance_ids:
            continue
        patch = (info.get("submission") or "").strip()
        runs.append(
            {
                "arm": arm,
                "instance_id": instance,
                "rep": rep_of(path),
                "exit_status": info.get("exit_status", ""),
                "patch": patch,
                "has_patch": bool(patch),
            }
        )
    return runs


def evaluate_group(
    runs: list[dict],
    arm: str,
    rep: str,
    dataset_name: str,
    split: str,
    max_workers: int,
    timeout: int,
    namespace: str | None,
    rewrite_reports: bool = False,
) -> dict[str, str]:
    """Run the harness on one (arm, rep) group; return instance_id -> outcome.

    Outcome is one of: resolved, unresolved, error, empty_patch, incomplete.
    """
    from swebench.harness import run_evaluation as re

    predictions = [
        {
            "instance_id": r["instance_id"],
            "model_name_or_path": f"sourceminder-{arm}",
            "model_patch": r["patch"],
        }
        for r in runs
    ]
    instance_ids = [r["instance_id"] for r in runs]
    run_id = f"qiexp_{arm}_rep{rep}"

    with tempfile.TemporaryDirectory() as tmp:
        preds_path = os.path.join(tmp, "predictions.json")
        Path(preds_path).write_text(json.dumps(predictions))

        report_file = re.main(
            dataset_name=dataset_name,
            split=split,
            instance_ids=instance_ids,
            predictions_path=preds_path,
            max_workers=max_workers,
            force_rebuild=False,
            cache_level="env",
            clean=False,
            open_file_limit=4096,
            run_id=run_id,
            timeout=timeout,
            namespace=namespace,
            rewrite_reports=rewrite_reports,
            modal=False,
        )

    report = json.loads(Path(report_file).read_text())
    outcome: dict[str, str] = {}
    for iid in report.get("resolved_ids", []):
        outcome[iid] = "resolved"
    for iid in report.get("unresolved_ids", []):
        outcome[iid] = "unresolved"
    for iid in report.get("error_ids", []):
        outcome[iid] = "error"
    for iid in report.get("empty_patch_ids", []):
        outcome[iid] = "empty_patch"
    for iid in instance_ids:
        outcome.setdefault(iid, "incomplete")
    return outcome


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--logs", type=Path, default=Path("experiment/logs"),
                    help="Logs directory (default: experiment/logs)")
    ap.add_argument("--dir", type=Path, default=None,
                    help="Analysis output directory (default: analysis/<timestamp>/)")
    ap.add_argument("--subset", default="verified", choices=list(DATASETS),
                    help="SWE-bench subset (default: verified)")
    ap.add_argument("--split", default="test", help="Dataset split (default: test)")
    ap.add_argument("--namespace", default="swebench",
                    help="Docker namespace for prebuilt images; 'none' to build "
                         "locally (default: swebench)")
    ap.add_argument("--max-workers", type=int, default=4,
                    help="Parallel Docker evaluations per group (default: 4)")
    ap.add_argument("--timeout", type=int, default=1800,
                    help="Per-instance test timeout in seconds (default: 1800)")
    ap.add_argument("--dry-run", action="store_true",
                    help="Print the prediction plan without invoking Docker")
    ap.add_argument("--instance-id", type=str, default=None,
                    help="Evaluate only this instance (e.g. matplotlib__matplotlib-14623)")
    ap.add_argument("--instances-file", type=Path, default=None,
                    help="Evaluate only instances listed in this file")
    ap.add_argument("--rewrite-reports", action="store_true",
                    help="Force harness to re-evaluate even when cached reports exist")
    ap.add_argument("--db", type=Path, default=eval_db.DEFAULT_DB,
                    help="SQLite results DB (WAL, shared across runs); "
                         f"default: {eval_db.DEFAULT_DB}")
    ap.add_argument("--run-tag", default=None,
                    help="Namespaces rows in the DB so reruns are idempotent and "
                         "concurrent batches don't collide (default: the --dir name)")
    args = ap.parse_args()

    if not args.logs.is_dir():
        print(f"ERROR: logs directory not found: {args.logs}", file=sys.stderr)
        return 1

    instance_ids: set[str] | None = None
    if args.instance_id:
        instance_ids = {args.instance_id}
    elif args.instances_file:
        if not args.instances_file.exists():
            print(f"ERROR: {args.instances_file} not found", file=sys.stderr)
            return 1
        instance_ids = set()
        for line in args.instances_file.read_text().splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            instance_ids.add(line.split()[0])

    runs = collect_runs(args.logs, instance_ids)
    if not runs:
        print(f"ERROR: no trajectories under {args.logs}", file=sys.stderr)
        return 1

    # group by (arm, rep) so every group has unique instance_ids
    groups: dict[tuple[str, str], list[dict]] = {}
    for r in runs:
        groups.setdefault((r["arm"], r["rep"]), []).append(r)

    print(f"Found {len(runs)} runs in {len(groups)} (arm, rep) groups:")
    for (arm, rep), grp in sorted(groups.items()):
        with_patch = sum(1 for r in grp if r["has_patch"])
        print(f"  {arm:9s} rep {rep}: {len(grp):3d} instances, {with_patch} with a patch")

    if args.dry_run:
        print("\n[dry-run] no Docker evaluation performed.")
        return 0

    out_dir = args.dir or Path(f"experiment/analysis/{time.strftime('%Y%m%d_%H%M%S')}")
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / "eval_results.csv"
    run_tag = args.run_tag or out_dir.name

    dataset_name = DATASETS[args.subset]
    namespace = None if args.namespace.lower() == "none" else args.namespace

    # Results go to a shared WAL SQLite DB (concurrent-write safe, idempotent on
    # the (run_tag, arm, instance_id, rep) primary key). eval_results.csv is
    # re-exported from the DB after every group so a crashed eval still leaves a
    # usable CSV and merge_results.py keeps working unchanged.
    conn = eval_db.connect(args.db)
    try:
        for (arm, rep), grp in sorted(groups.items()):
            print(f"\n=== Evaluating {arm} rep {rep} "
                  f"({len(grp)} instances) ===", flush=True)
            outcome = evaluate_group(
                grp, arm, rep, dataset_name, args.split,
                args.max_workers, args.timeout, namespace,
                rewrite_reports=args.rewrite_reports,
            )
            for r in sorted(grp, key=lambda x: x["instance_id"]):
                eval_db.upsert(conn, run_tag, eval_db.EvalResult(
                    arm=r["arm"], instance_id=r["instance_id"], rep=r["rep"],
                    exit_status=r["exit_status"], has_patch=r["has_patch"],
                    outcome=outcome.get(r["instance_id"], "incomplete"),
                    dataset=dataset_name))
            eval_db.export_csv(conn, out_path, run_tag)

        n_total = eval_db.export_csv(conn, out_path, run_tag)
        n_res = eval_db.count_resolved(conn, run_tag)
    finally:
        conn.close()

    print(f"\nWrote {n_total} rows to {out_path} "
          f"({n_res} resolved / {n_total} runs).")
    print(f"Results stored in {args.db} [run_tag={run_tag}].")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
