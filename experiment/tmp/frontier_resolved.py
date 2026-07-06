#!/usr/bin/env python3
"""Look up per-model frontier resolve rates for a SWE-bench Pro instance.  [WIP -- experiment/tmp]

The vendored SWE-bench Pro repo ships one eval-results file per frontier model
run at:

    experiment/vendor/swebench_pro_os/traj/<model>/eval_results.json

Each file maps a full instance_id -> resolved (bool).  Given a short hash ref
(e.g. 3ff19cf7c4), this script finds the matching instance in every model file
and prints whether each model resolved it, plus an aggregate rate.

Usage:
    python3 experiment/tmp/frontier_resolved.py 3ff19cf7c4
    python3 experiment/tmp/frontier_resolved.py 3ff19cf7c4 --json

Notes:
  * Models do not all cover the same instance set (the -paper runs used a $2
    cost cap, the -10132025 runs are the 250-turn leaderboard configs), so a
    model with no entry for the instance is reported as 'absent' and excluded
    from the resolve-rate denominator.
  * Matching is substring-based against the instance_id; if a ref matches more
    than one distinct instance the script errors rather than guess.
"""

import argparse
import json
import sys
from pathlib import Path

TRAJ_DIR = (
    Path(__file__).resolve().parents[1]
    / "vendor"
    / "swebench_pro_os"
    / "traj"
)


def load_model_results():
    """Return {model_name: {instance_id: bool}} for every eval_results.json."""
    if not TRAJ_DIR.is_dir():
        sys.exit(f"traj dir not found: {TRAJ_DIR}")
    out = {}
    for model_dir in sorted(TRAJ_DIR.iterdir()):
        results = model_dir / "eval_results.json"
        if not results.is_file():
            continue
        with results.open() as fh:
            out[model_dir.name] = json.load(fh)
    if not out:
        sys.exit(f"no eval_results.json files under {TRAJ_DIR}")
    return out


def match_instance(model_results, ref):
    """Find the single instance_id containing `ref` across all models."""
    ids = set()
    for results in model_results.values():
        ids.update(k for k in results if ref in k)
    if not ids:
        sys.exit(f"no instance matches ref '{ref}'")
    if len(ids) > 1:
        joined = "\n  ".join(sorted(ids))
        sys.exit(f"ref '{ref}' matches multiple instances:\n  {joined}")
    return ids.pop()


def lookup(model_results, instance_id):
    """Return {model: True|False|None} where None means the model has no entry."""
    return {
        model: (bool(results[instance_id]) if instance_id in results else None)
        for model, results in model_results.items()
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ref", help="short hash ref, e.g. 3ff19cf7c4")
    ap.add_argument("--json", action="store_true", help="emit JSON instead of a table")
    args = ap.parse_args()

    model_results = load_model_results()
    instance_id = match_instance(model_results, args.ref)
    per_model = lookup(model_results, instance_id)

    scored = {m: v for m, v in per_model.items() if v is not None}
    n_resolved = sum(1 for v in scored.values() if v)
    n_scored = len(scored)

    if args.json:
        print(json.dumps({
            "instance_id": instance_id,
            "resolved": n_resolved,
            "scored": n_scored,
            "rate": (n_resolved / n_scored) if n_scored else None,
            "models": per_model,
        }, indent=2))
        return

    print(instance_id)
    print()
    for model in sorted(per_model):
        v = per_model[model]
        label = "absent" if v is None else ("True" if v else "False")
        print(f"  {model:35} {label}")
    print()
    rate = f"{n_resolved / n_scored * 100:.1f}%" if n_scored else "n/a"
    print(f"  resolved {n_resolved}/{n_scored}  ({rate})")


if __name__ == "__main__":
    main()
