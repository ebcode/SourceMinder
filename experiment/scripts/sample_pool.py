#!/usr/bin/env python3
"""Draw a reproducible, balanced sample of instances from ``pool.csv``.

The pool (see ``build_pool.py``) is the fixed universe; this script draws the N
instances a single experiment run actually evaluates, balanced across projects
(repos) so a django-heavy pool doesn't yield a django-heavy run.

Allocation -- "balanced, capped at availability". Slots are handed out one at a
time, each to a project that still has un-drawn images and currently holds the
*fewest* slots (random tie-break). Consequences, all from that one rule:

  * more projects than N  -> N random projects get 1 each, the rest don't run;
  * fewer projects        -> even fill (5 projects, N=20 -> ~4 each);
  * a project with only 1 image takes its 1 and drops out, and its unused share
    overflows to the remaining least-filled projects (1/5/5/5/4, not 4/4/4/4/4).

No arbitrary per-repo cap is needed: a project's cap is how many images it has.

Reproducibility. The draw is seeded (a random seed is generated and recorded if
none is given). The resolved instances -- with their pinned image digests and
the pool's content hash -- are frozen into a JSON manifest, so a run replays
exactly even if the RNG implementation or the pool later changes. The manifest,
not the seed, is the source of truth.

Usage:
  python3 experiment/scripts/sample_pool.py --n 20 --seed 1234
  python3 experiment/scripts/sample_pool.py --n 20            # seed auto-generated
  python3 experiment/scripts/sample_pool.py --n 20 --ids-out run_instances.txt
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
    """Return (rows, pool_sha256, header_meta).

    Rows are the data lines of ``pool.csv``; the sha256 is over the *entire*
    file (so a manifest is tied to exact pool contents); header_meta pulls the
    ``# key: value`` comment lines (dataset, revision, ...) for provenance.
    """
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


def allocate(capacities: dict[str, int], n: int, rng: random.Random) -> dict[str, int]:
    """Balanced allocation of ``n`` slots across projects, capped at availability.

    Each slot goes to a least-filled project that still has capacity (random
    tie-break). Returns {project: count} for projects that received >=1 slot.
    Iteration is over sorted projects so the RNG sequence is deterministic.
    """
    alloc: dict[str, int] = {p: 0 for p in capacities}
    target = min(n, sum(capacities.values()))
    for _ in range(target):
        candidates = [p for p in sorted(capacities) if alloc[p] < capacities[p]]
        fewest = min(alloc[p] for p in candidates)
        least_filled = [p for p in candidates if alloc[p] == fewest]
        alloc[rng.choice(least_filled)] += 1
    return {p: c for p, c in alloc.items() if c > 0}


def draw(rows: list[dict], n: int, rng: random.Random) -> list[dict]:
    """Pick the sampled instance rows: balanced allocation, then a random draw
    of the allocated count from each chosen project."""
    by_repo: dict[str, list[dict]] = defaultdict(list)
    for r in rows:
        by_repo[r["repo"]].append(r)
    capacities = {repo: len(items) for repo, items in by_repo.items()}

    alloc = allocate(capacities, n, rng)
    picked: list[dict] = []
    for repo in sorted(alloc):
        population = sorted(by_repo[repo], key=lambda r: r["instance_id"])
        picked.extend(rng.sample(population, alloc[repo]))
    picked.sort(key=lambda r: r["instance_id"])
    return picked


def _owner_repo(instance_id: str) -> str:
    """Derive owner/repo from instance_id like django/django."""
    parts = instance_id.split("__")
    if len(parts) == 2:
        return f"{parts[0]}/{parts[1].split('-')[0]}"
    return instance_id


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pool", type=Path, default=DEFAULT_POOL,
                    help=f"Pool CSV (default: {DEFAULT_POOL})")
    ap.add_argument("--n", type=int, default=20,
                    help="Number of instances to draw (default: 20)")
    ap.add_argument("--seed", type=int, default=None,
                    help="RNG seed; if omitted, one is generated and recorded")
    ap.add_argument("--out", type=Path, default=None,
                    help="Manifest JSON path "
                         "(default: results/samples/sample_<ts>_seed<seed>.json)")
    ap.add_argument("--ids-out", type=Path, default=None,
                    help="Also write the sampled instance_ids, one per line "
                         "(for pipeline consumption)")
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

    # Generate-and-record a seed when none is given, so every run is replayable.
    seed = args.seed if args.seed is not None else random.SystemRandom().randrange(2**31)
    rng = random.Random(seed)

    picked = draw(rows, args.n, rng)
    if len(picked) < args.n:
        print(f"WARNING: pool has only {len(rows)} instances; drew {len(picked)} "
              f"of {args.n} requested.", file=sys.stderr)

    alloc: dict[str, int] = defaultdict(int)
    n_files_by_repo: dict[str, list[int]] = defaultdict(list)
    for r in picked:
        alloc[r["repo"]] += 1
        n_files_by_repo[r["repo"]].append(r["n_files"])

    ts = time.strftime("%Y%m%d_%H%M%S")
    manifest = {
        "generated": ts,
        "seed": seed,
        "n_requested": args.n,
        "n_sampled": len(picked),
        "dataset": meta.get("dataset", ""),
        "pool": str(args.pool),
        "pool_sha256": pool_sha,
        "pool_size": len(rows),
        "allocation": dict(sorted(alloc.items())),
        "instances": [
            {"instance_id": r["instance_id"], "repo": r["repo"],
             "n_files": r["n_files"], "image": r["image"], "digest": r["digest"]}
            for r in picked
        ],
    }

    out = args.out or paths.SAMPLES_DIR / f"sample_{ts}_seed{seed}.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(manifest, indent=2) + "\n")

    if args.ids_out:
        args.ids_out.parent.mkdir(parents=True, exist_ok=True)
        with open(args.ids_out, "w") as f:
            f.write(f"# {len(picked)} balanced SWE-bench Verified instances"
                    f" (seed={seed})\n")
            f.write(f"# Generated: {ts}\n")
            f.write(f"# Manifest: {out}\n")
            for r in picked:
                f.write(f"{r['instance_id']} "
                        f"{_owner_repo(r['instance_id'])} "
                        f"{r['n_files']}\n")

    print(f"Drew {len(picked)} instances (seed={seed}) -> {out}", file=sys.stderr)
    print("Allocation by repo:", file=sys.stderr)
    for repo, c in sorted(alloc.items()):
        nf = ", ".join(str(f) for f in sorted(n_files_by_repo[repo]))
        print(f"  {repo:14s} {c}  n_files: [{nf}]", file=sys.stderr)

    nf_dist: dict[int, int] = defaultdict(int)
    for r in picked:
        nf_dist[r["n_files"]] += 1
    print("n_files distribution:", file=sys.stderr)
    for nf in sorted(nf_dist):
        print(f"  {nf:3d} files: {nf_dist[nf]:2d} instances", file=sys.stderr)

    if args.ids_out:
        print(f"Instance ids -> {args.ids_out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
