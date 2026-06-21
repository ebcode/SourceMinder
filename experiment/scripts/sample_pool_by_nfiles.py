#!/usr/bin/env python3
"""Draw a round-robin sample from ``pool.csv``, ordered by ``n_files`` descending.

The sampler distributes picks across n_files tiers: start at the highest n_files
tier, take one instance, move to the next-highest, and so on down to n_files=1,
then wrap back around and take the second instance from each tier that still has
one. This guarantees that small-N samples include the largest (most complex)
instances, while larger N progressively fills in lower n_files tiers.

Within each n_files tier, instances are drawn in sorted ``instance_id`` order
(deterministic) unless ``--seed`` is given, which shuffles each tier.

Examples with the current 100-instance pool::

  --n 1   →  n_files=21: sympy__sympy-13091
  --n 2   →  + n_files=6:  sympy__sympy-16597
  --n 3   →  + n_files=5:  django__django-11532
  --n 8   →  (wrap: nf=21/6/5 exhausted) + second nf=4 instance
  --n 100 →  all instances

Usage:
  python3 experiment/scripts/sample_pool_by_nfiles.py --n 20
  python3 experiment/scripts/sample_pool_by_nfiles.py --n 20 --seed 1234
  python3 experiment/scripts/sample_pool_by_nfiles.py --n 20 --ids-out run_instances.txt
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import random
import sys
import time
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # -> experiment/
from lib import paths

DEFAULT_POOL = paths.DATA_DIR / "pool.csv"


def load_pool(path: Path) -> tuple[list[dict], str, dict[str, str]]:
    """Return (rows, pool_sha256, header_meta)."""
    text = path.read_text()
    meta: dict[str, str] = {}
    data_lines: list[str] = []
    for line in text.splitlines():
        if line.startswith("#"):
            body = line.lstrip("#").strip()
            if ":" in body:
                k, v = body.split(":", 1)
                meta[k.strip()] = v.strip()
        else:
            data_lines.append(line)
    rows = list(csv.DictReader(data_lines))
    for r in rows:
        r["n_files"] = int(r["n_files"])
    return rows, hashlib.sha256(text.encode()).hexdigest(), meta


def round_robin_draw(
    rows: list[dict],
    n: int,
    rng: random.Random | None,
) -> list[dict]:
    """Round-robin through n_files tiers (descending), one per tier per round.

    Each round walks from highest n_files to lowest, taking the next unused
    instance from each tier that still has one. Tiers are never skipped: a tier
    with only one instance contributes once, then drops out for later rounds.
    """
    by_nf: dict[int, list[dict]] = defaultdict(list)
    for r in rows:
        by_nf[r["n_files"]].append(r)

    nf_levels = sorted(by_nf.keys(), reverse=True)

    for nf in nf_levels:
        by_nf[nf].sort(key=lambda r: r["instance_id"])
        if rng is not None:
            rng.shuffle(by_nf[nf])

    idx: dict[int, int] = {nf: 0 for nf in nf_levels}
    picked: list[dict] = []

    while len(picked) < n:
        any_picked = False
        for nf in nf_levels:
            if idx[nf] < len(by_nf[nf]):
                picked.append(by_nf[nf][idx[nf]])
                idx[nf] += 1
                any_picked = True
                if len(picked) >= n:
                    break
        if not any_picked:
            break

    picked.sort(key=lambda r: r["instance_id"])
    return picked


def _owner_repo(instance_id: str) -> str:
    """Derive owner/repo from instance_id like django/django."""
    parts = instance_id.split("__")
    if len(parts) == 2:
        return f"{parts[0]}/{parts[1].split('-')[0]}"
    return instance_id


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument(
        "--pool", type=Path, default=DEFAULT_POOL,
        help=f"Pool CSV (default: {DEFAULT_POOL})",
    )
    ap.add_argument(
        "--n", type=int, default=20,
        help="Number of instances to draw (default: 20)",
    )
    ap.add_argument(
        "--seed", type=int, default=None,
        help="RNG seed for per-tier shuffle; if omitted, tiers use sorted "
             "instance_id order (deterministic)",
    )
    ap.add_argument(
        "--out", type=Path, default=None,
        help="Manifest JSON path "
             "(default: results/samples/sample_<ts>_seed<seed>_nfiles.json)",
    )
    ap.add_argument(
        "--ids-out", type=Path, default=None,
        help="Also write the sampled instance_ids, one per line "
             "(for pipeline consumption)",
    )
    args = ap.parse_args()

    if not args.pool.is_file():
        print(f"ERROR: pool not found: {args.pool}", file=sys.stderr)
        return 1
    if args.n < 1:
        print("ERROR: --n must be >= 1", file=sys.stderr)
        return 1

    rows, pool_sha, meta = load_pool(args.pool)
    if not rows:
        print(f"ERROR: no rows in {args.pool}", file=sys.stderr)
        return 1

    rng = random.Random(args.seed) if args.seed is not None else None

    picked = round_robin_draw(rows, args.n, rng)
    if len(picked) < args.n:
        print(
            f"WARNING: pool has only {len(rows)} instances; drew "
            f"{len(picked)} of {args.n} requested.",
            file=sys.stderr,
        )

    alloc: dict[str, int] = defaultdict(int)
    nf_by_repo: dict[str, list[int]] = defaultdict(list)
    for r in picked:
        alloc[r["repo"]] += 1
        nf_by_repo[r["repo"]].append(r["n_files"])

    ts = time.strftime("%Y%m%d_%H%M%S")
    seed_str = f"_seed{args.seed}" if args.seed is not None else ""
    manifest = {
        "generated": ts,
        "seed": args.seed,
        "n_requested": args.n,
        "n_sampled": len(picked),
        "dataset": meta.get("dataset", ""),
        "pool": str(args.pool),
        "pool_sha256": pool_sha,
        "pool_size": len(rows),
        "allocation": dict(sorted(alloc.items())),
        "instances": [
            {
                "instance_id": r["instance_id"],
                "repo": r["repo"],
                "n_files": r["n_files"],
                "image": r["image"],
                "digest": r["digest"],
            }
            for r in picked
        ],
    }

    out = (
        args.out
        or paths.SAMPLES_DIR / f"sample_{ts}{seed_str}_nfiles.json"
    )
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(manifest, indent=2) + "\n")

    if args.ids_out:
        args.ids_out.parent.mkdir(parents=True, exist_ok=True)
        with open(args.ids_out, "w") as f:
            f.write(
                f"# {len(picked)} n_files-round-robin SWE-bench Verified instances"
                f" (seed={args.seed})\n"
            )
            f.write(f"# Generated: {ts}\n")
            f.write(f"# Manifest: {out}\n")
            for r in picked:
                f.write(
                    f"{r['instance_id']} "
                    f"{_owner_repo(r['instance_id'])} "
                    f"{r['n_files']}\n"
                )

    print(
        f"Drew {len(picked)} instances (seed={args.seed}) -> {out}",
        file=sys.stderr,
    )
    print("Allocation by repo:", file=sys.stderr)
    for repo, c in sorted(alloc.items()):
        nf = ", ".join(str(f) for f in sorted(nf_by_repo[repo]))
        print(f"  {repo:14s} {c}  n_files: [{nf}]", file=sys.stderr)

    nf_dist: dict[int, int] = defaultdict(int)
    for r in picked:
        nf_dist[r["n_files"]] += 1
    print("n_files distribution:", file=sys.stderr)
    for nf in sorted(nf_dist, reverse=True):
        print(f"  {nf:3d} files: {nf_dist[nf]:2d} instances", file=sys.stderr)

    if args.ids_out:
        print(f"Instance ids -> {args.ids_out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
