#!/usr/bin/env python3
"""Join eval_results.csv onto runs.csv -> runs_with_success.csv.

`analyze_trajectories.py` writes per-run token metrics to `runs.csv`;
`evaluate_patches.py` writes per-run harness outcomes to `eval_results.csv`.
Both are keyed by the same run identity — `(arm, instance_id, run_id/rep)` — so
this script left-joins the eval columns onto the metrics table and produces a
single `runs_with_success.csv` for statistical analysis.

Keeping the two source CSVs separate is deliberate: evaluation is a heavyweight
Docker pass, and re-running the analyzer must never clobber harness results.
This merge regenerates the combined view on demand.

A run present in `runs.csv` but absent from `eval_results.csv` (eval not run yet,
or the trajectory was missing at eval time) gets `task_success = False` and
`outcome = not_evaluated` — never silently dropped.

Usage:
  python3 experiment/analysis/merge_results.py \
      --dir experiment/analysis/20260616_223000
"""
from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # -> experiment/
from lib import paths

# runs.csv calls the rep column "run_id"; eval_results.csv calls it "rep".
# Both hold the same value, so the join key is (model, arm, instance_id, that
# value). model is part of the key: the same (arm, instance, rep) is run once
# per model, so omitting it would cross-join different models' runs.
def key_runs(row: dict) -> tuple[str, str, str, str]:
    return (row["model"], row["arm"], row["instance_id"], row["run_id"])


def key_eval(row: dict) -> tuple[str, str, str, str]:
    return (row["model"], row["arm"], row["instance_id"], row["rep"])


def load(path: Path) -> list[dict]:
    with path.open(newline="") as fh:
        return list(csv.DictReader(fh))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", type=Path, default=None,
                    help="Run directory containing runs.csv and eval_results.csv "
                         "(default: the most recent results/runs/<timestamp>/)")
    ap.add_argument("--batch", default=None, metavar="BATCH_ID",
                    help="Resolve the run directory as results/runs/<batch>/ "
                         "(alias for --dir results/runs/<batch>)")
    args = ap.parse_args()

    out_dir = args.dir
    if out_dir is None and args.batch:
        out_dir = paths.batch_run_dir(args.batch)
    if out_dir is None:
        existing = sorted(p for p in paths.RUNS_DIR.glob("*") if p.is_dir())
        if not existing:
            print(f"ERROR: no run directories under {paths.RUNS_DIR}; pass --dir",
                  file=sys.stderr)
            return 1
        out_dir = existing[-1]
    runs_path = out_dir / "runs.csv"
    eval_path = out_dir / "eval_results.csv"
    out_path = out_dir / "runs_with_success.csv"

    for label, p in [("runs", runs_path), ("eval", eval_path)]:
        if not p.is_file():
            print(f"ERROR: {label} not found: {p}", file=sys.stderr)
            return 1

    out_dir.mkdir(parents=True, exist_ok=True)
    runs = load(runs_path)
    eval_by_key = {key_eval(r): r for r in load(eval_path)}

    # Columns contributed by the eval side (everything except the join keys).
    extra_cols = ["outcome", "task_success"]
    out_fields = list(runs[0].keys()) + extra_cols if runs else []

    n_matched = 0
    with out_path.open("w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=out_fields)
        writer.writeheader()
        for row in runs:
            ev = eval_by_key.get(key_runs(row))
            if ev is not None:
                row["outcome"] = ev["outcome"]
                row["task_success"] = ev["resolved"]  # "1"/"0" from the harness
                n_matched += 1
            else:
                row["outcome"] = "not_evaluated"
                row["task_success"] = "0"
            writer.writerow(row)

    n_success = sum(1 for r in runs if r.get("task_success") == "1")
    print(f"Wrote {len(runs)} rows to {out_path} "
          f"({n_matched} matched eval rows, {n_success} resolved).")
    if n_matched < len(runs):
        print(f"  {len(runs) - n_matched} run(s) had no eval row "
              f"-> task_success=0, outcome=not_evaluated.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
