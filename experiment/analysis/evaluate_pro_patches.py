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

Outputs two CSVs:
  * eval_results.csv -- one row per run. Beyond the binary ``resolved`` it now
    carries the granular signal the verdict hides: ``failure_mode``
    (resolved / bug_not_fixed / regression / both); ``pass_rate`` with
    ``required_passed/required_total`` -- the fraction of all required tests
    (FAIL_TO_PASS + PASS_TO_PASS pooled) that passed, so "1 test away" (~0.98) is
    distinguishable from "20 away" (~0.60); and the per-category counts
    ``f2p_total/f2p_passed/p2p_total/p2p_passed`` (FAIL_TO_PASS = did the fix
    land; PASS_TO_PASS = did it break anything).
  * eval_test_failures.csv -- long format, one row per required test that did NOT
    pass: (model, arm, instance_id, rep, kind, test_name, status). Lets you see
    exactly which tests failed without re-reading the per-rep output.json.

``resolved`` is unchanged: (FAIL_TO_PASS u PASS_TO_PASS) all PASSED.

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


def classify_failure(f2p_total: int, f2p_passed: int,
                     p2p_total: int, p2p_passed: int) -> str:
    """Refine the binary verdict into a failure mode (what 'unresolved' hides):
      resolved      -- every FAIL_TO_PASS and PASS_TO_PASS test passes
      bug_not_fixed -- a FAIL_TO_PASS test still fails (the fix didn't land)
      regression    -- the bug is fixed but a PASS_TO_PASS test broke
      both          -- failures on both sides
    """
    f2p_ok = f2p_passed == f2p_total
    p2p_ok = p2p_passed == p2p_total
    if f2p_ok and p2p_ok:
        return "resolved"
    if not f2p_ok and not p2p_ok:
        return "both"
    return "bug_not_fixed" if not f2p_ok else "regression"


def result_row(model, arm, iid, rid, outcome, resolved, *, failure_mode=None,
               f2p_total="", f2p_passed="", p2p_total="", p2p_passed="",
               failures=None) -> dict:
    """Build one summary row. ``failures`` (per-test long-format rows) is stashed
    under ``_failures`` and split out before the summary CSV is written. For the
    non-test outcomes (error/empty/missing) failure_mode defaults to the outcome.

    required_total/required_passed pool FAIL_TO_PASS + PASS_TO_PASS, and pass_rate
    is the fraction passing across that pool -- partial credit that distinguishes
    "1 test away" (e.g. 0.98) from "20 away" (e.g. 0.60). pass_rate is "" when
    there are no required tests or for the non-test outcomes."""
    req_total = (f2p_total + p2p_total) if isinstance(f2p_total, int) else ""
    req_passed = (f2p_passed + p2p_passed) if isinstance(f2p_passed, int) else ""
    pass_rate = round(req_passed / req_total, 4) if isinstance(req_total, int) and req_total else ""
    return dict(
        model=model, arm=arm, instance_id=iid, rep=rid,
        outcome=outcome, resolved=resolved,
        failure_mode=failure_mode if failure_mode is not None else outcome,
        f2p_total=f2p_total, f2p_passed=f2p_passed,
        p2p_total=p2p_total, p2p_passed=p2p_passed,
        required_total=req_total, required_passed=req_passed, pass_rate=pass_rate,
        _failures=failures or [],
    )


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
            return result_row(model, arm, iid, rid, "missing_instance", 0)
        if not patch.strip():
            return result_row(model, arm, iid, rid, "empty_patch", 0)
        sample = pd.Series({k: row[k] for k in RAW_FIELDS})
        out_dir = eval_root / arm / rid
        out_dir.mkdir(parents=True, exist_ok=True)
        try:
            output = sbp.eval_with_docker(
                patch, sample, str(out_dir), args.dockerhub_username,
                str(args.scripts_dir), prefix=f"{arm}_{rid}",
                redo=args.redo, block_network=args.block_network)
            if not output:
                return result_row(model, arm, iid, rid, "error", 0)
            status_by = {t["name"]: t["status"] for t in output["tests"]}
            passed = {n for n, s in status_by.items() if s == "PASSED"}
            f2p = to_set(row["fail_to_pass"])
            p2p = to_set(row["pass_to_pass"])
            f2p_passed = len(f2p & passed)
            p2p_passed = len(p2p & passed)
            ok = (f2p | p2p) <= passed   # resolved verdict, unchanged
            # one long-format row per required test that did NOT pass (a true
            # FAILED, or MISSING if it never ran -- e.g. a collection error).
            failures = [
                dict(model=model, arm=arm, instance_id=iid, rep=rid, kind=kind,
                     test_name=t, status=status_by.get(t, "MISSING"))
                for kind, names in (("fail_to_pass", f2p), ("pass_to_pass", p2p))
                for t in sorted(names - passed)
            ]
            return result_row(
                model, arm, iid, rid,
                "resolved" if ok else "unresolved", int(ok),
                failure_mode=classify_failure(len(f2p), f2p_passed,
                                              len(p2p), p2p_passed),
                f2p_total=len(f2p), f2p_passed=f2p_passed,
                p2p_total=len(p2p), p2p_passed=p2p_passed,
                failures=failures)
        except Exception as exc:  # noqa: BLE001 -- record, never crash the batch
            with _print_lock:
                print(f"  [{arm} {rid}] exception: {exc}", file=sys.stderr)
            return result_row(model, arm, iid, rid, "error", 0)

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

    # Split the per-test failure rows (long format) off the summary rows. Pop so
    # the summary DictWriter sees only its own fields.
    test_failures = []
    for r in results:
        test_failures.extend(r.pop("_failures", []))

    # Read-modify-write merge: refresh only the runs we just evaluated and keep
    # every other row intact, so a targeted re-eval (e.g.
    # --arms swebp_treatment --run-prefix rep01) updates a single run's row(s)
    # without clobbering the rest of the batch. A full-batch run refreshes every
    # key, reproducing the old whole-file behavior. (To start clean, delete the
    # CSVs first.) Runs are keyed by (arm, instance_id, rep).
    touched = {(r["arm"], r["instance_id"], r["rep"]) for r in results}

    out_path = args.dir / "eval_results.csv"
    fields = ["model", "arm", "instance_id", "rep", "outcome", "resolved",
              "failure_mode", "pass_rate", "required_passed", "required_total",
              "f2p_total", "f2p_passed", "p2p_total", "p2p_passed"]
    merged = list(results)
    if out_path.exists():
        with out_path.open(newline="") as f:
            for row in csv.DictReader(f):
                if (row["arm"], row["instance_id"], row["rep"]) in touched:
                    continue  # superseded by a freshly-evaluated row above
                row = {k: row.get(k, "") for k in fields}
                row["resolved"] = int(row["resolved"] or 0)
                try:
                    row["pass_rate"] = float(row["pass_rate"])
                except (ValueError, TypeError):
                    pass  # "" for empty/missing/error rows -- left as-is
                merged.append(row)
    merged.sort(key=lambda r: (r["arm"], r["rep"]))
    with out_path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(merged)

    # Separate long-format CSV: one row per required test that did not pass. Drop
    # every old failure row for the touched runs (a now-resolved run contributes
    # zero), then add the fresh ones.
    fail_path = args.dir / "eval_test_failures.csv"
    tf_fields = ["model", "arm", "instance_id", "rep", "kind", "test_name", "status"]
    merged_tf = list(test_failures)
    if fail_path.exists():
        with fail_path.open(newline="") as f:
            for row in csv.DictReader(f):
                if (row["arm"], row["instance_id"], row["rep"]) in touched:
                    continue
                merged_tf.append({k: row.get(k, "") for k in tf_fields})
    merged_tf.sort(key=lambda r: (r["arm"], r["rep"], r["kind"], r["test_name"]))
    with fail_path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=tf_fields)
        w.writeheader()
        w.writerows(merged_tf)

    from collections import Counter
    n_res = sum(r["resolved"] for r in merged)
    print(f"\nWrote {len(merged)} row(s) -> {out_path}  ({n_res} resolved; "
          f"{len(results)} refreshed this run)")
    print(f"Wrote {len(merged_tf)} failing-test row(s) -> {fail_path}")
    print(f"  failure modes: {dict(Counter(r['failure_mode'] for r in merged))}")
    for arm in sorted({r["arm"] for r in merged}):
        ar = [r for r in merged if r["arm"] == arm]
        if ar:
            res = sum(x["resolved"] for x in ar)
            # partial credit: mean pass_rate across required (F2P+P2P) tests
            rates = [x["pass_rate"] for x in ar if isinstance(x["pass_rate"], float)]
            mr = (sum(rates) / len(rates)) if rates else float("nan")
            print(f"  {arm:18s} resolved {res}/{len(ar)}  "
                  f"mean pass_rate: {mr:.0%} (n={len(rates)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
