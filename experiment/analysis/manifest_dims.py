#!/usr/bin/env python3
"""Print dimensions (n_files, n_f2p, n_p2p, patch_lines) for each instance in the cross-instance manifest."""
import ast, sys
from pathlib import Path

import pandas as pd
import pyarrow.parquet as pq

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "analysis" / "cross_instance_manifest.txt"
PARQUET = ROOT / "data" / "swebench_pro" / "test.parquet"


def main():
    lines = [l.strip() for l in MANIFEST.read_text().splitlines() if l.strip() and not l.startswith("#")]
    if not lines:
        print("ERROR: manifest is empty", file=sys.stderr)
        sys.exit(1)

    # resolve each manifest line to a batch dir and read its runs.csv
    batch_iids = {}
    for rel in lines:
        batch_dir = (MANIFEST.parent / rel).resolve()
        runs_csv = batch_dir / "runs.csv"
        if not runs_csv.exists():
            print(f"WARNING: {batch_dir.name}: runs.csv not found", file=sys.stderr)
            continue
        iid = pd.read_csv(runs_csv)["instance_id"].iloc[0]
        batch_iids[batch_dir.name] = iid

    # read relevant columns from parquet
    t = pq.read_table(PARQUET, columns=["instance_id", "repo_language", "patch", "fail_to_pass", "pass_to_pass"])
    parquet_rows = {r["instance_id"]: r for r in t.to_pylist()}

    print(f"{'batch':<35s} {'lang':>4s} {'n_files':>7s} {'n_f2p':>6s} {'n_p2p':>6s} {'patch_lines':>11s}")
    print("-" * 78)
    for batch_name, iid in batch_iids.items():
        r = parquet_rows.get(iid)
        if r is None:
            print(f"{batch_name:<35s} {'?':>4s} {'?':>7s} {'?':>6s} {'?':>6s} {'?':>11s}")
            continue
        nf = r["patch"].count("diff --git a/")
        f2p = len(ast.literal_eval(r["fail_to_pass"]))
        p2p = len(ast.literal_eval(r["pass_to_pass"]))
        pl = r["patch"].count("\n") + 1
        print(f"{batch_name:<35s} {r['repo_language']:>4s} {nf:>7d} {f2p:>6d} {p2p:>6d} {pl:>11d}")


if __name__ == "__main__":
    main()
