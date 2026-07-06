#!/usr/bin/env python3
"""Find which SWE-bench Pro instances are cheapest to solve.

Scans all runs_with_success.csv files under results/pro_runs/*/, pools
every row where task_success=1, computes median turns / tokens / cost / wall-time
per (instance, model, arm), and ranks the cheapest (fewest median resources).

Why: every quick pilot needs a cheap smoke-test instance. Instead of guessing, this
finds the one solved in the fewest turns/tokens/cost across all prior batches.

Usage:
  experiment/.venv_pro/bin/python experiment/analysis/instance_cheapest.py
  experiment/.venv_pro/bin/python experiment/analysis/instance_cheapest.py --arm control
  experiment/.venv_pro/bin/python experiment/analysis/instance_cheapest.py --model haiku --metric cost --top 10
"""
from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

EXPERIMENT_DIR = Path(__file__).resolve().parent.parent
DEFAULT_DIR = EXPERIMENT_DIR / "results" / "pro_runs"


def load_runs(search_dir: Path) -> list[dict]:
    """Load all runs_with_success.csv rows from every batch dir under search_dir."""
    rows = []
    for csv_path in sorted(search_dir.glob("*/runs_with_success.csv")):
        batch = csv_path.parent.name
        try:
            with csv_path.open(newline="") as f:
                reader = csv.DictReader(f)
                for row in reader:
                    row["__batch"] = batch
                    rows.append(row)
        except Exception as e:
            print(f"WARNING: skipping {csv_path}: {e}", file=sys.stderr)
    return rows


def load_wall_times(search_dir: Path) -> dict[tuple[str, str, str], float]:
    """Load wall_time.csv files; return {(instance_id, model, rep): duration_sec}."""
    wall = {}
    for csv_path in sorted(search_dir.glob("*/wall_time.csv")):
        try:
            with csv_path.open(newline="") as f:
                for row in csv.DictReader(f):
                    dur = row.get("duration_sec", "")
                    if dur:
                        key = (row["instance_id"], row["model"], row["rep"])
                        wall[key] = float(dur)
        except Exception:
            pass
    return wall


def median(vals: list[float]) -> float:
    if not vals:
        return 0.0
    s = sorted(vals)
    n = len(s)
    if n % 2:
        return s[n // 2]
    return (s[n // 2 - 1] + s[n // 2]) / 2.0


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--dir", default=str(DEFAULT_DIR),
                    help=f"Directory containing per-batch result dirs (default: {DEFAULT_DIR})")
    ap.add_argument("--arm", default=None,
                    help="Filter by arm (control, treatment)")
    ap.add_argument("--model", default=None,
                    help="Filter by model (substring match, e.g. 'haiku' or 'deepseek-v4-flash')")
    ap.add_argument("--metric", default="turns", choices=["turns", "tokens", "cost", "wall"],
                    help="Metric to rank by (default: turns)")
    ap.add_argument("--top", type=int, default=20,
                    help="Show top N cheapest instances (default: 20)")
    ap.add_argument("--min-reps", type=int, default=1,
                    help="Require at least N successful reps per (instance, model, arm) (default: 1)")
    args = ap.parse_args()

    search_dir = Path(args.dir)
    if not search_dir.is_dir():
        print(f"ERROR: not a directory: {search_dir}", file=sys.stderr)
        return 1

    rows = load_runs(search_dir)
    if not rows:
        print("No runs_with_success.csv files found.", file=sys.stderr)
        return 1

    # Filter to successful solves only
    solved = [r for r in rows if r.get("task_success", "").strip() in ("1", "True", "true")]

    # Arm filter
    if args.arm:
        solved = [r for r in solved if r["arm"] == args.arm]

    # Model filter (substring match against the model column)
    if args.model:
        model_lower = args.model.lower()
        solved = [r for r in solved if model_lower in r.get("model", "").lower()]

    if not solved:
        print("No successful solves match the filters.", file=sys.stderr)
        return 0

    # Load wall times for duration metric
    wall_map = load_wall_times(search_dir) if args.metric == "wall" else {}

    # Group by (instance, model, arm) and collect metric values
    from collections import defaultdict
    groups = defaultdict(list)

    for r in solved:
        key = (r["instance_id"], r["model"], r["arm"])
        if args.metric == "turns":
            groups[key].append(float(r.get("turn_count", 0)))
        elif args.metric == "tokens":
            groups[key].append(float(r.get("total_tokens", 0)))
        elif args.metric == "cost":
            groups[key].append(float(r.get("cost", 0)))
        elif args.metric == "wall":
            dur = wall_map.get((r["instance_id"], r["model"], r["run_id"]))
            if dur is not None:
                groups[key].append(dur)

    # Compute median and filter by min_reps
    results = []
    for (instance_id, model, arm), vals in groups.items():
        n = len(vals)
        if n < args.min_reps:
            continue
        results.append({
            "instance_id": instance_id,
            "model": model,
            "arm": arm,
            "n": n,
            "median": median(vals),
        })

    if not results:
        print("No groups meet --min-reps threshold.", file=sys.stderr)
        return 0

    results.sort(key=lambda x: x["median"])

    # Display
    metric_labels = {"turns": "turns", "tokens": "tokens", "cost": "$", "wall": "s"}
    label = metric_labels[args.metric]

    header = f"{'median':>10}  {'n':>3}  arm            model                        instance"
    print(header)
    print("-" * len(header))

    for r in results[: args.top]:
        if args.metric == "cost":
            median_str = f"${r['median']:.4f}"
        elif args.metric == "wall":
            mins = int(r["median"] // 60)
            secs = int(r["median"] % 60)
            median_str = f"{mins}m{secs:02d}s"
        elif args.metric == "tokens":
            if r["median"] >= 1_000_000:
                median_str = f"{r['median'] / 1_000_000:.1f}M"
            elif r["median"] >= 1_000:
                median_str = f"{r['median'] / 1_000:.0f}K"
            else:
                median_str = str(int(r["median"]))
        else:
            median_str = str(int(r["median"]))

        print(f"{median_str:>10}  {r['n']:>3}  {r['arm']:<14} {r['model']:<28} {r['instance_id']}")

    print(f"\n{len(results)} total (instance, model, arm) combos with >= {args.min_reps} successful rep(s)")

    # Also show repo summary: which instances from which repos are cheapest (pooled
    # across models/arms, taking the best model's median)
    print(f"\n-- Cheapest per-instance (best model/arm) [{args.metric}] --")
    best_per_instance = {}
    for r in results:
        iid = r["instance_id"]
        if iid not in best_per_instance or r["median"] < best_per_instance[iid]["median"]:
            best_per_instance[iid] = r

    # Group by repo
    repo_groups = defaultdict(list)
    for r in best_per_instance.values():
        # Extract repo from instance_id: instance_owner__repo-<hash>
        parts = r["instance_id"].split("__", 1)
        if len(parts) > 1:
            repo = parts[1].rsplit("-", 1)[0]
        else:
            repo = "unknown"
        repo_groups[repo].append(r)

    for repo in sorted(repo_groups):
        cheapest = min(repo_groups[repo], key=lambda x: x["median"])
        if args.metric == "cost":
            val = f"${cheapest['median']:.4f}"
        elif args.metric == "wall":
            val = f"{int(cheapest['median'] // 60)}m{int(cheapest['median'] % 60):02d}s"
        elif args.metric == "tokens":
            val = f"{cheapest['median'] / 1_000_000:.1f}M" if cheapest['median'] >= 1_000_000 else f"{cheapest['median'] / 1_000:.0f}K"
        else:
            val = str(int(cheapest['median']))
        print(f"  {repo:<50} {val:>10}  ({cheapest['model']}, {cheapest['arm']}, n={cheapest['n']})")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
