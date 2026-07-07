#!/usr/bin/env python3
"""Build experiment/data/pro_resolve_rates.csv from the vendored frontier trajectories.  [WIP -- experiment/tmp]

For every instance in experiment/data/pool_pro.csv, join the per-model
eval_results.json files under experiment/vendor/swebench_pro_os/traj/<model>/
and emit the frontier resolve rate plus per-model resolved flags.

Join key: pool instance_ids carry a placeholder version tag (`-vnan`) while the
traj instance_ids carry a real `-v<hex>` tag, so both are reduced to the stem
(everything up to the trailing `-v...`) before matching.  A model with no entry
for an instance is left blank and excluded from the rate denominator.

Usage:
    python3 experiment/tmp/build_pro_resolve_rates.py
    python3 experiment/tmp/build_pro_resolve_rates.py --out /tmp/x.csv
"""

import argparse
import csv
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
POOL = ROOT / "data" / "pool_pro.csv"
TRAJ = ROOT / "vendor" / "swebench_pro_os" / "traj"
DEFAULT_OUT = ROOT / "data" / "pro_resolve_rates.csv"

VERSION_TAG = re.compile(r"-v[^-]*$")


def stem(instance_id):
    """Strip the trailing -v<...> version tag so pool and traj ids align."""
    return VERSION_TAG.sub("", instance_id)


def read_pool():
    with POOL.open() as fh:
        rows = [ln for ln in fh if not ln.startswith("#")]
    return list(csv.DictReader(rows))


def read_traj():
    """Return (model_names, {stem: {model: bool}})."""
    if not TRAJ.is_dir():
        sys.exit(f"traj dir not found: {TRAJ}")
    models = []
    by_stem = {}
    for model_dir in sorted(TRAJ.iterdir()):
        results = model_dir / "eval_results.json"
        if not results.is_file():
            continue
        model = model_dir.name
        models.append(model)
        with results.open() as fh:
            for iid, resolved in json.load(fh).items():
                by_stem.setdefault(stem(iid), {})[model] = bool(resolved)
    if not models:
        sys.exit(f"no eval_results.json under {TRAJ}")
    return models, by_stem


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = ap.parse_args()

    pool = read_pool()
    models, by_stem = read_traj()

    header = (["instance_id", "repo", "repo_language",
               "n_resolved", "n_scored", "resolve_rate"] + models)

    n_covered = 0
    with args.out.open("w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(header)
        for r in pool:
            per_model = by_stem.get(stem(r["instance_id"]), {})
            if per_model:
                n_covered += 1
            n_scored = len(per_model)
            n_resolved = sum(1 for v in per_model.values() if v)
            rate = f"{n_resolved / n_scored:.4f}" if n_scored else ""
            flags = ["" if m not in per_model
                     else ("1" if per_model[m] else "0") for m in models]
            w.writerow([r["instance_id"], r["repo"], r["repo_language"],
                        n_resolved, n_scored, rate] + flags)

    print(f"wrote {args.out}  ({len(pool)} instances, "
          f"{n_covered} with frontier data, {len(pool) - n_covered} uncovered)")
    print(f"models ({len(models)}): {', '.join(models)}")


if __name__ == "__main__":
    main()
