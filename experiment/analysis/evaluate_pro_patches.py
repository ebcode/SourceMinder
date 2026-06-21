#!/usr/bin/env python3
"""Evaluate SWE-bench **Pro** patches in parallel -> eval_results.csv.

Pro analog of evaluate_patches.py. Instead of the Princeton swebench harness it
drives the vendored Scale evaluator's ``eval_with_docker`` as a LIBRARY (the
vendor tree stays read-only; no ``cd vendor/... && ... ../../`` dance). It walks
the Pro logs for each run's ``.pred``, runs every (arm, rep) patch through the
Pro Docker eval, and writes one row per run keyed (model, arm, instance_id, rep)
so merge_results.py can join it onto runs.csv.

Two hazards in the upstream evaluator are handled here by giving every run its
own ``output_dir``:
  * its eval_results.json is keyed by instance_id alone, so N reps of one
    instance would collapse to a single result; and
  * ``prepare_run`` puts each run's workspace in a SHARED ``<out>/<uid>/workspace``,
    so parallel reps of the same instance would clobber each other.
Per-run isolated output dirs (``<dir>/eval/<arm>/<run_id>/``) fix both.

Usage:
  experiment/.venv_pro/bin/python experiment/analysis/evaluate_pro_patches.py \
      --dir experiment/results/pro_runs/<batch> --workers 5 [--run-prefix oldprompt_]
"""
from __future__ import annotations

import argparse
import concurrent.futures as cf
import csv
import importlib.util
import json
import sys
import threading
from pathlib import Path

EXPERIMENT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(EXPERIMENT_DIR))
from lib import paths  # noqa: E402

VENDOR_EVAL = EXPERIMENT_DIR / "vendor" / "swebench_pro_os" / "swe_bench_pro_eval.py"
DEFAULT_LOGS = paths.LOGS_DIR / "pro_pilot"
DEFAULT_SUBSET = EXPERIMENT_DIR / "data" / "swebench_pro"
DEFAULT_SCRIPTS = EXPERIMENT_DIR / "vendor" / "swebench_pro_os" / "run_scripts"
DEFAULT_DOCKERHUB = "jefzda"
DEFAULT_ARMS = ["swebp_control", "swebp_treatment"]
RAW_FIELDS = ["instance_id", "repo", "before_repo_set_cmd",
              "selected_test_files_to_run", "base_commit",
              "fail_to_pass", "pass_to_pass"]

_print_lock = threading.Lock()


def load_vendor_eval():
    spec = importlib.util.spec_from_file_location("sbp_eval", VENDOR_EVAL)
    mod = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(VENDOR_EVAL.parent))
    spec.loader.exec_module(mod)
    return mod


def norm_model(s: str) -> str:
    return (s or "").split("/")[-1].strip().lower()


def parse_run_id(path: Path, instance_id: str) -> str:
    stem = path.name[: -len(".pred")] if path.name.endswith(".pred") else path.stem
    if stem == instance_id:
        return "base"
    if stem.startswith(instance_id + "."):
        return stem[len(instance_id) + 1 :]
    return stem


def to_set(v) -> set:
    """fail_to_pass / pass_to_pass come as a list or its string repr."""
    if isinstance(v, (list, tuple, set)):
        return set(v)
    return set(eval(v)) if v else set()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--logs", type=Path, default=DEFAULT_LOGS, help=f"(default: {DEFAULT_LOGS})")
    ap.add_argument("--dir", type=Path, required=True,
                    help="Output dir for eval_results.csv (e.g. results/pro_runs/<batch>)")
    ap.add_argument("--arms", nargs="+", default=DEFAULT_ARMS)
    ap.add_argument("--run-prefix", default="", help="Only eval runs whose run_id starts with this")
    ap.add_argument("--exclude", nargs="+", default=[],
                    help="Skip these runs, named as <arm>_<run_id> (i.e. the per-run "
                         "log basename without .log), e.g. swebp_control_rep05")
    ap.add_argument("--workers", type=int, default=5, help="Parallel eval workers (default: 5)")
    ap.add_argument("--subset", type=Path, default=DEFAULT_SUBSET)
    ap.add_argument("--scripts-dir", type=Path, default=DEFAULT_SCRIPTS)
    ap.add_argument("--dockerhub-username", default=DEFAULT_DOCKERHUB)
    ap.add_argument("--redo", action="store_true", help="Re-run even if a prior output exists")
    ap.add_argument("--block-network", action="store_true")
    args = ap.parse_args()

    # The vendored evaluator reads dockerfiles via paths RELATIVE to its own dir
    # (swe_bench_pro_eval.py:58/62 -> "dockerfiles/base_dockerfile/<iid>/Dockerfile"),
    # so it only works with CWD == the vendor dir. Resolve all our paths to
    # absolute first, then chdir there -- the proper form of the old
    # `cd vendor/swebench_pro_os && ... ../../` dance.
    import os
    args.logs = args.logs.resolve()
    args.dir = args.dir.resolve()
    args.subset = Path(args.subset).resolve()
    args.scripts_dir = args.scripts_dir.resolve()
    os.chdir(VENDOR_EVAL.parent)

    sbp = load_vendor_eval()
    import pandas as pd
    from datasets import load_dataset

    # --- gather predictions ---
    preds = []  # (arm, instance_id, run_id, patch, model)
    for arm in args.arms:
        for p in sorted((args.logs / arm).rglob("*.pred")):
            rec = json.loads(p.read_text())
            iid = rec["instance_id"]
            rid = parse_run_id(p, iid)
            if not rid.startswith(args.run_prefix):
                continue
            if f"{arm}_{rid}" in args.exclude:
                print(f"  excluding {arm}_{rid}")
                continue
            preds.append((arm, iid, rid,
                          rec.get("model_patch") or rec.get("patch") or "",
                          norm_model(rec.get("model_name_or_path", ""))))
    if not preds:
        print("ERROR: no predictions found", file=sys.stderr)
        return 1
    print(f"{len(preds)} prediction(s) to evaluate across {args.workers} worker(s)")

    # --- raw sample rows (one Series per instance) ---
    ds = load_dataset(str(args.subset), split="test")
    by_id = {r["instance_id"]: r for r in ds}

    eval_root = args.dir / "eval"

    def run_task(task):
        arm, iid, rid, patch, model = task
        row = by_id.get(iid)
        if row is None:
            return dict(model=model, arm=arm, instance_id=iid, rep=rid,
                        outcome="missing_instance", resolved=0)
        if not patch.strip():
            return dict(model=model, arm=arm, instance_id=iid, rep=rid,
                        outcome="empty_patch", resolved=0)
        sample = pd.Series({k: row[k] for k in RAW_FIELDS})
        out_dir = eval_root / arm / rid
        out_dir.mkdir(parents=True, exist_ok=True)
        try:
            output = sbp.eval_with_docker(
                patch, sample, str(out_dir), args.dockerhub_username,
                str(args.scripts_dir), prefix=f"{arm}_{rid}",
                redo=args.redo, block_network=args.block_network)
            if not output:
                return dict(model=model, arm=arm, instance_id=iid, rep=rid,
                            outcome="error", resolved=0)
            passed = {t["name"] for t in output["tests"] if t["status"] == "PASSED"}
            required = to_set(row["fail_to_pass"]) | to_set(row["pass_to_pass"])
            ok = required <= passed
            return dict(model=model, arm=arm, instance_id=iid, rep=rid,
                        outcome="resolved" if ok else "unresolved", resolved=int(ok))
        except Exception as exc:  # noqa: BLE001 -- record, never crash the batch
            with _print_lock:
                print(f"  [{arm} {rid}] exception: {exc}", file=sys.stderr)
            return dict(model=model, arm=arm, instance_id=iid, rep=rid,
                        outcome="error", resolved=0)

    results = []
    done = 0
    with cf.ThreadPoolExecutor(max_workers=args.workers) as pool:
        futs = {pool.submit(run_task, t): t for t in preds}
        for fut in cf.as_completed(futs):
            r = fut.result()
            results.append(r)
            done += 1
            with _print_lock:
                print(f"  [{done}/{len(preds)}] {r['arm']} {r['rep']}: "
                      f"{r['outcome']} (resolved={r['resolved']})", flush=True)

    args.dir.mkdir(parents=True, exist_ok=True)
    out_path = args.dir / "eval_results.csv"
    fields = ["model", "arm", "instance_id", "rep", "outcome", "resolved"]
    results.sort(key=lambda r: (r["arm"], r["rep"]))
    with out_path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(results)

    n_res = sum(r["resolved"] for r in results)
    print(f"\nWrote {len(results)} row(s) -> {out_path}  ({n_res} resolved)")
    for arm in args.arms:
        ar = [r for r in results if r["arm"] == arm]
        if ar:
            print(f"  {arm:18s} resolved {sum(x['resolved'] for x in ar)}/{len(ar)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
