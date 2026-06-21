#!/usr/bin/env python3
"""Draw a language-balanced, large-file-biased sample from ``pool_pro.csv``.

SWE-bench Pro spans 4 languages (go, python, js, ts) across 11 repos. This
sampler first allocates slots across languages (balanced, capped at
availability), then within each language draws instances with a
descending-n_files round-robin that is repo-balanced within each n_files bucket
(same algorithm as ``sample_pool_large_biased.py``).

Consequences:
  * Every language with enough instances gets its fair share (for N=20, ~5 each).
  * The rarest large-file instances in each language are pulled in the first pass.
  * repos are spread within each language so no single project dominates.

Usage:
  python3 experiment/scripts/sample_pool_pro.py --n 20
  python3 experiment/scripts/sample_pool_pro.py --n 20 --seed 1234
  python3 experiment/scripts/sample_pool_pro.py --n 20 --ids-out pro_instances.txt
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import random
import sys
import time
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # -> experiment/
from lib import paths

DEFAULT_POOL = paths.DATA_DIR / "pool_pro.csv"
STRATEGY = "language-balanced-large-biased"
DATASET = "ScaleAI/SWE-bench_Pro"


def load_pool(path: Path) -> tuple[list[dict], str, dict[str, str]]:
    """Return (rows, pool_sha256, header_meta) from a Pro pool CSV."""
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


def allocate_languages(
    rows: list[dict], n: int, rng: random.Random,
) -> tuple[dict[str, int], dict[str, list[dict]]]:
    """Balanced allocation of *n* slots across languages, capped at availability.

    Returns (alloc, by_lang) where alloc is {lang: count} for languages that
    received >=1 slot, and by_lang is {lang: [rows]}.
    """
    by_lang: dict[str, list[dict]] = defaultdict(list)
    for r in rows:
        by_lang[r["repo_language"]].append(r)
    capacities = {lang: len(items) for lang, items in by_lang.items()}
    alloc: dict[str, int] = {lang: 0 for lang in capacities}
    target = min(n, sum(capacities.values()))
    for _ in range(target):
        candidates = [lang for lang in sorted(capacities) if alloc[lang] < capacities[lang]]
        fewest = min(alloc[lang] for lang in candidates)
        least_filled = [lang for lang in candidates if alloc[lang] == fewest]
        alloc[rng.choice(least_filled)] += 1
    return {lang: c for lang, c in alloc.items() if c > 0}, by_lang


def draw_large_biased(rows: list[dict], n: int, rng: random.Random) -> list[dict]:
    """Descending-n_files round-robin with repo-balanced within-bucket pick.

    Identical algorithm to ``sample_pool_large_biased.py:draw()``.
    """
    buckets: dict[int, list[dict]] = defaultdict(list)
    for r in rows:
        buckets[r["n_files"]].append(r)
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
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--pool", type=Path, default=DEFAULT_POOL,
                    help=f"Pro pool CSV (default: {DEFAULT_POOL})")
    ap.add_argument("--n", type=int, default=20,
                    help="Number of instances to draw (default: 20)")
    ap.add_argument("--seed", type=int, default=None,
                    help="RNG seed; if omitted, one is generated and recorded")
    ap.add_argument("--out", type=Path, default=None,
                    help="Manifest JSON path "
                         "(default: results/samples/pro_sample_<ts>_seed<seed>.json)")
    ap.add_argument("--ids-out", type=Path, default=None,
                    help="Also write the sampled instance_ids, one per line")
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

    # Allocate slots across languages.
    lang_alloc, by_lang = allocate_languages(rows, args.n, rng)

    # Within each language, draw large-file-biased.
    picked: list[dict] = []
    for lang in sorted(lang_alloc):
        lang_rows = list(by_lang[lang])  # copy so draw_large_biased can mutate
        drawn = draw_large_biased(lang_rows, lang_alloc[lang], rng)
        picked.extend(drawn)

    if len(picked) < args.n:
        print(f"WARNING: pool has only {len(rows)} instances; drew {len(picked)} "
              f"of {args.n} requested.", file=sys.stderr)

    picked.sort(key=lambda r: r["instance_id"])

    # Stats.
    alloc_repo: dict[str, int] = defaultdict(int)
    alloc_lang: dict[str, int] = defaultdict(int)
    nf_by_repo: dict[str, list[int]] = defaultdict(list)
    nf_by_lang: dict[str, list[int]] = defaultdict(list)
    for r in picked:
        alloc_repo[r["repo"]] += 1
        alloc_lang[r["repo_language"]] += 1
        nf_by_repo[r["repo"]].append(r["n_files"])
        nf_by_lang[r["repo_language"]].append(r["n_files"])

    ts = time.strftime("%Y%m%d_%H%M%S")
    manifest = {
        "generated": ts,
        "strategy": STRATEGY,
        "seed": seed,
        "n_requested": args.n,
        "n_sampled": len(picked),
        "dataset": DATASET,
        "pool": str(args.pool),
        "pool_sha256": pool_sha,
        "pool_size": len(rows),
        "allocation_by_language": {lang: {"count": alloc_lang[lang],
                                           "n_files": sorted(nf_by_lang[lang])}
                                    for lang in sorted(alloc_lang)},
        "allocation_by_repo": dict(sorted(alloc_repo.items())),
        "instances": [
            {"instance_id": r["instance_id"], "repo": r["repo"],
             "repo_language": r["repo_language"], "n_files": r["n_files"],
             "image": r["image"], "base_commit": r["base_commit"]}
            for r in picked
        ],
    }

    out = args.out or paths.SAMPLES_DIR / f"pro_sample_{ts}_seed{seed}.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(manifest, indent=2) + "\n")

    if args.ids_out:
        args.ids_out.parent.mkdir(parents=True, exist_ok=True)
        with open(args.ids_out, "w") as f:
            f.write(f"# {len(picked)} language-balanced large-file-biased "
                    f"SWE-bench Pro instances (seed={seed}, strategy={STRATEGY})\n")
            f.write(f"# Generated: {ts}\n")
            f.write(f"# Manifest: {out}\n")
            f.write(f"# Columns: instance_id repo repo_language n_files\n")
            for r in picked:
                f.write(f"{r['instance_id']} "
                        f"{r['repo']} "
                        f"{r['repo_language']} "
                        f"{r['n_files']}\n")

    print(f"Drew {len(picked)} instances (seed={seed}, strategy={STRATEGY}) -> {out}",
          file=sys.stderr)
    print("Allocation by language:", file=sys.stderr)
    for lang in sorted(alloc_lang):
        nf_str = ", ".join(str(f) for f in sorted(nf_by_lang[lang], reverse=True))
        print(f"  {lang:8s} {alloc_lang[lang]:2d}  n_files: [{nf_str}]",
              file=sys.stderr)
    print("Allocation by repo:", file=sys.stderr)
    for repo, c in sorted(alloc_repo.items()):
        nf_str = ", ".join(str(f) for f in sorted(nf_by_repo[repo], reverse=True))
        print(f"  {repo:30s} {c:2d}  n_files: [{nf_str}]",
              file=sys.stderr)

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
