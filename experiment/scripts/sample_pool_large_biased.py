#!/usr/bin/env python3
"""Draw a *large-file-biased* sample of instances from ``pool.csv``.

Where ``sample_pool.py`` balances across repos (so the n_files mix is whatever
the draw happens to give), this sampler deliberately front-loads instances with
many edited files, to test the hypothesis that qi's value grows with the amount
of cross-file exploration a task requires.

Allocation -- "descending n_files round-robin, repo-balanced within a bucket".
Group the pool by ``n_files``. Make repeated passes; each pass walks the n_files
values high -> low and takes one instance from every non-empty bucket. Within a
bucket, prefer the repo used fewest times so far (random tie-break), so the draw
stays spread across projects. Stop once N are picked.

Consequences of that one rule:

  * the rarest large-file instances (e.g. the lone 5/6/21-file ones) are pulled
    in the first pass -- guaranteed inclusion;
  * the resulting n_files distribution is **deterministic** for a given pool +N
    (it falls out of the bucket sizes); the seed only chooses *which* instance
    represents each bucket and how repos are tie-broken;
  * coverage tilts toward n_files > 2 rather than mirroring the pool (which is
    dominated by 1-2 file instances).

Reproducibility mirrors ``sample_pool.py``: the resolved instances, their pinned
image digests, and the pool's content hash are frozen into a JSON manifest --
the manifest, not the seed, is the source of truth. The manifest schema is
identical to ``sample_pool.py``'s (plus a ``strategy`` field) so the rest of the
pipeline consumes it unchanged.

Usage:
  python3 experiment/scripts/sample_pool_large_biased.py --n 25 --seed 126660
  python3 experiment/scripts/sample_pool_large_biased.py --n 25            # seed auto-generated
  python3 experiment/scripts/sample_pool_large_biased.py --n 25 --ids-out run_instances.txt
"""
from __future__ import annotations

import argparse
import json
import random
import sys
import time
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))         # -> scripts/
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))     # -> experiment/
from lib import paths
from sample_pool import DEFAULT_POOL, _owner_repo, load_pool

STRATEGY = "large-biased"


def draw(rows: list[dict], n: int, rng: random.Random) -> list[dict]:
    """Descending-n_files round-robin with a repo-balanced within-bucket pick.

    Each pass walks n_files high -> low and pulls one row from every non-empty
    bucket; within a bucket the least-used repo wins (random tie-break). Sorted
    iteration keeps the RNG sequence deterministic for a given seed.
    """
    buckets: dict[int, list[dict]] = defaultdict(list)
    for r in rows:
        buckets[r["n_files"]].append(r)
    # Deterministic starting order within each bucket before any RNG draws.
    for nf in buckets:
        buckets[nf].sort(key=lambda r: r["instance_id"])

    target = min(n, len(rows))
    repo_used: Counter[str] = Counter()
    picked: list[dict] = []
    while len(picked) < target:
        for nf in sorted(buckets, reverse=True):
            bucket = buckets[nf]
            if not bucket:
                continue
            fewest = min(repo_used[r["repo"]] for r in bucket)
            candidates = [r for r in bucket if repo_used[r["repo"]] == fewest]
            choice = rng.choice(candidates)
            bucket.remove(choice)
            picked.append(choice)
            repo_used[choice["repo"]] += 1
            if len(picked) >= target:
                break
    picked.sort(key=lambda r: r["instance_id"])
    return picked


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pool", type=Path, default=DEFAULT_POOL,
                    help=f"Pool CSV (default: {DEFAULT_POOL})")
    ap.add_argument("--n", type=int, default=25,
                    help="Number of instances to draw (default: 25)")
    ap.add_argument("--seed", type=int, default=None,
                    help="RNG seed; if omitted, one is generated and recorded")
    ap.add_argument("--out", type=Path, default=None,
                    help="Manifest JSON path "
                         "(default: results/samples/sample_<ts>_seed<seed>_largebiased.json)")
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
        "strategy": STRATEGY,
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

    out = args.out or paths.SAMPLES_DIR / f"sample_{ts}_seed{seed}_largebiased.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(manifest, indent=2) + "\n")

    if args.ids_out:
        args.ids_out.parent.mkdir(parents=True, exist_ok=True)
        with open(args.ids_out, "w") as f:
            f.write(f"# {len(picked)} large-file-biased SWE-bench Verified instances"
                    f" (seed={seed}, strategy={STRATEGY})\n")
            f.write(f"# Generated: {ts}\n")
            f.write(f"# Manifest: {out}\n")
            for r in picked:
                f.write(f"{r['instance_id']} "
                        f"{_owner_repo(r['instance_id'])} "
                        f"{r['n_files']}\n")

    print(f"Drew {len(picked)} instances (seed={seed}, strategy={STRATEGY}) -> {out}",
          file=sys.stderr)
    print("Allocation by repo:", file=sys.stderr)
    for repo, c in sorted(alloc.items()):
        nf = ", ".join(str(f) for f in sorted(n_files_by_repo[repo]))
        print(f"  {repo:14s} {c}  n_files: [{nf}]", file=sys.stderr)

    nf_dist: dict[int, int] = defaultdict(int)
    for r in picked:
        nf_dist[r["n_files"]] += 1
    print("n_files distribution:", file=sys.stderr)
    for nf in sorted(nf_dist, reverse=True):
        print(f"  {nf:3d} files: {nf_dist[nf]:2d} instances", file=sys.stderr)
    gt2 = sum(c for nf, c in nf_dist.items() if nf > 2)
    print(f"  -> {gt2}/{len(picked)} instances have n_files > 2", file=sys.stderr)

    if args.ids_out:
        print(f"Instance ids -> {args.ids_out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
