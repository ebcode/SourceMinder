#!/usr/bin/env python3
"""
Materialize a local SWE-bench Pro dataset that mini-swe-agent can consume.

Why this exists
---------------
mini-swe-agent resolves a SWE-bench instance's Docker image via
``get_swebench_docker_image_name()``, which checks ``instance["image_name"]`` /
``instance["docker_image"]`` and otherwise falls back to the Verified naming
pattern ``swebench/sweb.eval.x86_64.<id>``. SWE-bench Pro carries its image as a
``dockerhub_tag`` field under the ``jefzda/sweap-images`` namespace -- a field
mini does not know about. ``get_sb_environment`` then *overwrites* any image set
in our config, so the only place an override sticks is the instance row itself.

So we load ``ScaleAI/SWE-bench_Pro``, add an ``image_name`` column
(``jefzda/sweap-images:{dockerhub_tag}``) that mini *does* read, and write it
back out as parquet that ``load_dataset(path, split="test")`` can load (HF's
``save_to_disk`` produces a ``load_from_disk`` format, which mini does not use).

It also emits a pool CSV mirroring ``data/pool.csv`` so the indexer and the
sampler have per-instance metadata (image, language, base_commit, n_files, and
the ``before_repo_set_cmd`` repo-setup script Pro images need at startup).

Usage
-----
    python3 experiment/prep_pro_dataset.py
    python3 experiment/prep_pro_dataset.py --limit 20   # quick smoke run
"""
from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[0]))  # -> experiment/
from lib import paths

HF_DATASET = "ScaleAI/SWE-bench_Pro"
HF_SPLIT = "test"
IMAGE_NAMESPACE = "jefzda/sweap-images"


def _decode_json_string(s: str) -> str:
    """If *s* is a JSON-encoded string (surrounded by double-quotes with
    \\n / \\t / \\uXXXX escape sequences), decode it to a plain string.
    Otherwise return the original.

    The upstream SWE-bench Pro dataset stores problem_statement, requirements,
    and interface as JSON strings, which means newlines appear as literal ``\\n``
    in the raw data.  The downstream agent must see real newlines.
    """
    s = s.strip()
    if s.startswith('"') and s.endswith('"'):
        try:
            return json.loads(s)
        except (json.JSONDecodeError, UnicodeDecodeError):
            pass
    return s


# The official Pro scaffold (scaleapi/mini-swe-agent) folds three dataset fields
# into the single problem_statement it shows the agent. Format byte-verified
# against vendor/swebench_pro_mini/data/swebench_pro_mini_example_instances.yaml:
#   {problem_statement}\n\nRequirements:\n{requirements}\n\nNew interfaces introduced:\n{interface}
def compose_problem_statement(problem_statement: str, requirements: str,
                              interface: str) -> str:
    return (f"{_decode_json_string(problem_statement)}\n\n"
            f"Requirements:\n{_decode_json_string(requirements)}"
            f"\n\nNew interfaces introduced:\n{_decode_json_string(interface)}")

OUT_DATASET_DIR = paths.DATA_DIR / "swebench_pro"
OUT_PARQUET = OUT_DATASET_DIR / "test.parquet"
OUT_POOL = paths.DATA_DIR / "pool_pro.csv"

# A gold-patch hunk header: ``diff --git a/<path> b/<path>``. Counting distinct
# b/ paths gives the changed-file count the sampler biases on (mirrors Verified's
# n_files), excluding the separate test_patch.
_DIFF_RE = re.compile(r"^diff --git a/.+ b/(.+)$", re.MULTILINE)


def image_name_for(dockerhub_tag: str) -> str:
    """``<tag>`` -> ``jefzda/sweap-images:<tag>`` (the value mini reads)."""
    return f"{IMAGE_NAMESPACE}:{dockerhub_tag}"


def n_files_in_patch(patch: str) -> int:
    """Count distinct files touched by a gold patch."""
    return len(set(_DIFF_RE.findall(patch or "")))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--limit", type=int, default=0,
                        help="Only process the first N instances (0 = all)")
    args = parser.parse_args()

    from datasets import load_dataset

    print(f"Loading {HF_DATASET} split={HF_SPLIT} ...", flush=True)
    ds = load_dataset(HF_DATASET, split=HF_SPLIT)
    if args.limit:
        ds = ds.select(range(min(args.limit, len(ds))))
    print(f"  {len(ds)} instances")

    # Add the image_name column the scaffold reads for image resolution.
    images = [image_name_for(tag) for tag in ds["dockerhub_tag"]]
    ds = ds.add_column("image_name", images)
    # repo_name is the directory under / where the repo lives (Pro images: /app).
    ds = ds.add_column("repo_name", ["app"] * len(ds))

    # Overwrite problem_statement with the official composed form (problem +
    # requirements + interface) so the agent receives the full Pro task spec.
    # The raw requirements/interface columns are kept (harmless, unused).
    composed = [compose_problem_statement(p, r, i)
                for p, r, i in zip(ds["problem_statement"], ds["requirements"],
                                   ds["interface"])]
    ds = ds.remove_columns("problem_statement").add_column("problem_statement", composed)

    OUT_DATASET_DIR.mkdir(parents=True, exist_ok=True)
    print(f"Writing parquet -> {OUT_PARQUET}")
    ds.to_parquet(str(OUT_PARQUET))

    # Pool CSV: one row per instance with the metadata the indexer + sampler need.
    print(f"Writing pool -> {OUT_POOL}")
    with OUT_POOL.open("w", newline="") as f:
        f.write("# SourceMinder SWE-bench Pro pool -- the fixed universe a run draws from.\n")
        f.write(f"# dataset: {HF_DATASET} split {HF_SPLIT} ({len(ds)} instances)\n")
        f.write(f"# image_registry: {IMAGE_NAMESPACE} (tag per instance in image column)\n")
        f.write("# repo_setup runs before_repo_set_cmd at container start (repo is at /app, not pre-positioned)\n")
        w = csv.writer(f)
        w.writerow(["instance_id", "repo", "repo_language", "n_files",
                    "image", "base_commit"])
        for r in ds:
            w.writerow([r["instance_id"], r["repo"], r["repo_language"],
                        n_files_in_patch(r["patch"]), r["image_name"],
                        r["base_commit"]])

    # Language distribution -- the sampler uses this to span qi's supported set.
    from collections import Counter
    langs = Counter(ds["repo_language"])
    print(f"Done. repo_language distribution: {dict(langs)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
