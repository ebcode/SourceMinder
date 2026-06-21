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

Defaults resolve under experiment/ regardless of cwd: logs from
experiment/logs/, output to experiment/results/runs/<timestamp>/, the WAL DB at
experiment/results/eval_results.db, and harness reports to
experiment/results/reports/.

Usage:
  # preview the prediction plan without invoking Docker
  python3 experiment/analysis/evaluate_patches.py --dry-run

  # run the harness and write results to a timestamped run directory
  python3 experiment/analysis/evaluate_patches.py

  # re-evaluate only retry-failed instances (scope + force reports rebuild)
  python3 experiment/analysis/evaluate_patches.py \
      --dir experiment/results/runs/retry-01 \
      --instance-id matplotlib__matplotlib-14623 --rewrite-reports

  # run 4 (arm, rep) groups concurrently, 1 container each (~4 containers total)
  python3 experiment/analysis/evaluate_patches.py --workers 4 --max-workers 1

Parallelism has two independent knobs: ``--max-workers`` parallelizes Docker
containers *within* one (arm, rep) group (passed through to the harness), while
``--workers`` parallelizes the (arm, rep) groups themselves. Each group runs
under a distinct ``run_id`` so containers/reports never collide, and results
land in the shared WAL DB. Total concurrent containers ~= workers * max-workers;
size both to your CPU/RAM budget.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # -> experiment/
from lib import instances, paths
from lib.trajmeta import infer_path_meta, rep_of, batch_of, n_files_of, patch_files_of

from analysis import eval_db

# subset -> HuggingFace dataset name the swebench harness loads.
DATASETS = {
    "verified": "princeton-nlp/SWE-bench_Verified",
    "lite": "princeton-nlp/SWE-bench_Lite",
    "test": "princeton-nlp/SWE-bench",
}


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
        model, batch, arm, instance = infer_path_meta(path)
        if instance_ids is not None and instance not in instance_ids:
            continue
        patch = (info.get("submission") or "").strip()
        # Unified diffs must end in a newline; .strip() removes the trailing
        # one, which makes git apply / GNU patch reject any patch whose final
        # line is the last line of the target file ("malformed patch").
        if patch:
            patch += "\n"
        runs.append(
            {
                "model": model,
                "arm": arm,
                "instance_id": instance,
                "rep": rep_of(path),
                "batch_id": batch or batch_of(path),
                "n_files": n_files_of(path),
                "patch_files": patch_files_of(patch),
                "exit_status": info.get("exit_status", ""),
                "patch": patch,
                "has_patch": bool(patch),
            }
        )
    return runs


def _slug(text: str) -> str:
    """Filesystem/Docker-safe token: keep alnum/dash/underscore, fold the rest."""
    return "".join(c if c.isalnum() or c in "-_" else "-" for c in text)


def evaluate_group(
    runs: list[dict],
    model: str,
    arm: str,
    rep: str,
    dataset_name: str,
    split: str,
    max_workers: int,
    timeout: int,
    namespace: str | None,
    rewrite_reports: bool = False,
    batch_id: str = "",
) -> dict[str, str]:
    """Run the harness on one (model, arm, rep) group; return instance_id -> outcome.

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
    # Model is part of the run_id so concurrent groups for different models never
    # collide on container/report names.
    batch_slug = f"{_slug(batch_id)}_" if batch_id else ""
    run_id = f"qiexp_{batch_slug}{_slug(model)}_{arm}_rep{rep}" if model else f"qiexp_{batch_slug}{arm}_rep{rep}"

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
    ap.add_argument("--logs", type=Path, default=paths.LOGS_DIR,
                    help=f"Logs directory (default: {paths.LOGS_DIR})")
    ap.add_argument("--dir", type=Path, default=None,
                    help="Output run directory (default: results/runs/<timestamp>/)")
    ap.add_argument("--subset", default="verified", choices=list(DATASETS),
                    help="SWE-bench subset (default: verified)")
    ap.add_argument("--split", default="test", help="Dataset split (default: test)")
    ap.add_argument("--namespace", default="swebench",
                    help="Docker namespace for prebuilt images; 'none' to build "
                         "locally (default: swebench)")
    ap.add_argument("--max-workers", type=int, default=4,
                    help="Parallel Docker containers WITHIN one (arm, rep) group "
                         "(passed to the harness; default: 4)")
    ap.add_argument("--workers", type=int, default=1,
                    help="Parallel (arm, rep) GROUPS evaluated at once. Each group "
                         "runs its own harness with a distinct run_id, so total "
                         "concurrent containers ~= workers * max-workers. Writes go "
                         "to the WAL DB so concurrent groups don't clobber "
                         "(default: 1, fully sequential)")
    ap.add_argument("--timeout", type=int, default=300,
                    help="Per-instance test timeout in seconds (default: 300)")
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
                         "concurrent batches don't collide (default: the --batch id "
                         "or the --dir name)")
    ap.add_argument("--batch", default=None, metavar="BATCH_ID",
                    help="Filter to trajectories whose manifest batch_id matches; "
                         "also sets the output dir to results/runs/<batch>/ and "
                         "the DB run_tag to <batch>")
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
        instance_ids = set(instances.parse_instance_ids(args.instances_file))

    runs = collect_runs(args.logs, instance_ids)
    if args.batch:
        runs = [r for r in runs if r.get("batch_id") == args.batch]
    if not runs:
        print(f"ERROR: no trajectories under {args.logs}", file=sys.stderr)
        return 1

    # group by (model, arm, rep) so every group has unique instance_ids and two
    # models never share a predictions set / run_id
    groups: dict[tuple[str, str, str], list[dict]] = {}
    for r in runs:
        groups.setdefault((r["model"], r["arm"], r["rep"]), []).append(r)

    print(f"Found {len(runs)} runs in {len(groups)} (model, arm, rep) groups:")
    for (model, arm, rep), grp in sorted(groups.items()):
        with_patch = sum(1 for r in grp if r["has_patch"])
        print(f"  {model or '(none)':24s} {arm:9s} rep {rep}: "
              f"{len(grp):3d} instances, {with_patch} with a patch")

    if args.dry_run:
        print("\n[dry-run] no Docker evaluation performed.")
        return 0

    # Resolve to absolute *before* the cwd redirect below, so these still point
    # at the right place once the harness call runs from results/reports/.
    out_dir = (args.dir or paths.new_run_dir(batch_id=args.batch or "")).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / "eval_results.csv"
    run_tag = args.run_tag or args.batch or out_dir.name
    db_path = args.db.resolve()

    dataset_name = DATASETS[args.subset]
    namespace = None if args.namespace.lower() == "none" else args.namespace

    # Results go to a shared WAL SQLite DB (concurrent-write safe, idempotent on
    # the (run_tag, arm, instance_id, rep) primary key). eval_results.csv is
    # re-exported from the DB after every group so a crashed eval still leaves a
    # usable CSV and merge_results.py keeps working unchanged.
    #
    # Each (arm, rep) group runs its own harness invocation (distinct run_id ->
    # distinct container names) and opens its own SQLite connection (connections
    # aren't shareable across threads). export_csv is atomic, so concurrent
    # --workers need no external lock. With --workers 1 the pool runs one group
    # at a time -- the same code path, just no concurrency.
    def run_group(key_grp: tuple[tuple[str, str, str], list[dict]]) -> None:
        (model, arm, rep), grp = key_grp
        print(f"\n=== Evaluating {model or '(none)'} {arm} rep {rep} "
              f"({len(grp)} instances) ===", flush=True)
        outcome = evaluate_group(
            grp, model, arm, rep, dataset_name, args.split,
            args.max_workers, args.timeout, namespace,
            rewrite_reports=args.rewrite_reports,
            batch_id=args.batch or "",
        )
        conn = eval_db.connect(db_path)
        try:
            for r in sorted(grp, key=lambda x: x["instance_id"]):
                nf_raw = r.get("n_files", "")
                try:
                    nf = int(nf_raw) if nf_raw else None
                except (TypeError, ValueError):
                    nf = None
                eval_db.upsert(conn, run_tag, eval_db.EvalResult(
                    model=r["model"], arm=r["arm"], instance_id=r["instance_id"],
                    rep=r["rep"], exit_status=r["exit_status"],
                    has_patch=r["has_patch"],
                    outcome=outcome.get(r["instance_id"], "incomplete"),
                    dataset=dataset_name,
                    batch_id=r.get("batch_id", ""),
                    n_files=nf,
                    patch_files=r.get("patch_files")))
            eval_db.export_csv(conn, out_path, run_tag)
        finally:
            conn.close()

    ordered = sorted(groups.items())
    workers = max(1, args.workers)
    print(f"\nRunning {len(ordered)} groups, {workers} at a time "
          f"x {args.max_workers} containers/group "
          f"(~{workers * args.max_workers} containers max).", flush=True)

    # Warm the HuggingFace dataset cache once, single-threaded, before the pool.
    # The harness calls load_swebench_dataset() inside each group; with --workers
    # > 1 the first groups would otherwise race to populate a cold HF cache
    # concurrently and fail during dataset load. One eager load here makes that
    # cache hot so every worker thread hits it warm. Cheap no-op when already
    # cached; harmless at --workers 1.
    all_instance_ids = sorted({r["instance_id"] for grp in groups.values()
                               for r in grp})
    from swebench.harness.utils import load_swebench_dataset
    print(f"Warming dataset cache ({dataset_name} [{args.split}], "
          f"{len(all_instance_ids)} instances)...", flush=True)
    load_swebench_dataset(dataset_name, args.split, all_instance_ids)

    # The SWE-bench harness writes its report (<model>.<run_id>.json) and its
    # run_evaluation logs to the *current working directory*. paths.cwd redirects
    # to results/reports/ for the whole batch so those land under results/ instead
    # of polluting the experiment root. One process-wide chdir (set before the
    # pool starts, restored after) is thread-safe; a per-thread chdir would not
    # be. All paths used inside run_group (out_path, db_path) are absolute.
    #
    # One bad group must not sink the others: collect failures, keep going, and
    # report them at the end (exit non-zero). Already-completed groups are
    # durable in the DB regardless.
    failures: list[tuple[str, str, str, Exception]] = []
    with paths.cwd(paths.REPORTS_DIR):
        with ThreadPoolExecutor(max_workers=workers) as pool:
            future_key = {pool.submit(run_group, kg): kg[0] for kg in ordered}
            for fut in as_completed(future_key):
                model, arm, rep = future_key[fut]
                try:
                    fut.result()
                except Exception as exc:  # noqa: BLE001 - surface, don't abort the batch
                    failures.append((model, arm, rep, exc))
                    print(f"ERROR: group {model or '(none)'} {arm} rep {rep} "
                          f"failed: {exc}", file=sys.stderr, flush=True)

    conn = eval_db.connect(db_path)
    try:
        n_total = eval_db.export_csv(conn, out_path, run_tag)
        n_res = eval_db.count_resolved(conn, run_tag)
    finally:
        conn.close()

    print(f"\nWrote {n_total} rows to {out_path} "
          f"({n_res} resolved / {n_total} runs).")
    print(f"Results stored in {db_path} [run_tag={run_tag}].")
    if failures:
        print(f"WARNING: {len(failures)} of {len(ordered)} groups failed: "
              + ", ".join(f"{m or '(none)'} {a} rep {r}" for m, a, r, _ in failures),
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
