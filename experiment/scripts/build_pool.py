#!/usr/bin/env python3
"""Build the experiment's sampling pool (``pool.csv``) from the image list.

The pool is the fixed universe of SWE-bench Verified instances a run draws its
sample from. Each row pins one instance to the *content digest* of its prebuilt
eval image, fetched from the registry with ``docker manifest inspect`` -- no
image is pulled, so all 70 digests cost a handful of small manifest requests
rather than ~100 GB of layers.

Why pin digests: ``:latest`` is a moving tag. Recording the ``sha256:`` digest
makes the pool an immutable contract -- a future re-pull that doesn't match is a
loud failure instead of silent contamination, and the preregistration can state
exactly which image produced each result. The images themselves stay on Docker
Hub (availability); this file guarantees *identity* (reproducibility).

Image <-> instance_id: SWE-bench munges ``__`` to ``_1776_`` for Docker tag
safety, so ``...x86_64.django_1776_django-10554:latest`` <-> ``django__django-10554``.

Usage:
  python3 experiment/scripts/build_pool.py \
      --images experiment/verified_docker_images.txt \
      --out experiment/data/pool.csv
"""
from __future__ import annotations

import argparse
import csv
import json
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # -> experiment/
from lib import paths
from lib.naming import instance_id_of

# The HF dataset revision these instances were curated against (see the offline
# cache path and the harness's resolve logs). Pinned so the pool, the dataset,
# and the images all reference one frozen world.
DATASET = "princeton-nlp/SWE-bench_Verified"
DATASET_REVISION = "c104f840cc67f8b6eec6f759ebc8b2693d585d4a"

LINE_RE = re.compile(r"^(?P<image>\S+)\s*#\s*(?P<n_files>\d+)\s*files?", re.I)


def fetch_digest(image: str) -> str:
    """Registry content digest for ``image`` (matches a pulled RepoDigest).

    Uses ``docker manifest inspect -v`` so only the manifest is fetched, not the
    image layers. Raises CalledProcessError on a missing/unauthorized image.
    """
    out = subprocess.run(
        ["docker", "manifest", "inspect", "-v", image],
        capture_output=True, text=True, check=True,
    ).stdout
    data = json.loads(out)
    # Single-arch images return a dict; a manifest list returns a list -- these
    # eval images are x86_64-only, but handle both for safety.
    entry = data[0] if isinstance(data, list) else data
    return entry["Descriptor"]["digest"]


def parse_images(images_path: Path) -> list[tuple[str, int]]:
    """Return [(image, n_files)] from the curated list, skipping #-comments."""
    rows: list[tuple[str, int]] = []
    for line in images_path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = LINE_RE.match(line)
        if not m:
            print(f"WARNING: unparseable line: {line!r}", file=sys.stderr)
            continue
        rows.append((m["image"], int(m["n_files"])))
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--images", type=Path,
                    default=paths.EXPERIMENT_DIR / "verified_docker_images.txt",
                    help="Curated <image>  # <n> files list")
    ap.add_argument("--out", type=Path, default=paths.DATA_DIR / "pool.csv",
                    help=f"Output pool CSV (default: {paths.DATA_DIR / 'pool.csv'})")
    args = ap.parse_args()

    if not args.images.is_file():
        print(f"ERROR: image list not found: {args.images}", file=sys.stderr)
        return 1

    images = parse_images(args.images)
    if not images:
        print(f"ERROR: no images parsed from {args.images}", file=sys.stderr)
        return 1

    print(f"Pinning {len(images)} images via registry manifests "
          "(no layers pulled)...", file=sys.stderr)
    rows: list[dict] = []
    failures: list[tuple[str, str]] = []
    for i, (image, n_files) in enumerate(images, 1):
        iid = instance_id_of(image)
        try:
            digest = fetch_digest(image)
        except (subprocess.CalledProcessError, KeyError, json.JSONDecodeError) as exc:
            detail = getattr(exc, "stderr", "").strip() or str(exc)
            failures.append((image, detail))
            print(f"  [{i:2d}/{len(images)}] FAILED {iid}: {detail}",
                  file=sys.stderr)
            continue
        rows.append({
            "instance_id": iid,
            "repo": iid.split("__", 1)[0],
            "n_files": n_files,
            "image": image,
            "digest": digest,
        })
        print(f"  [{i:2d}/{len(images)}] {iid} -> {digest[:19]}...",
              file=sys.stderr)

    rows.sort(key=lambda r: r["instance_id"])
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", newline="") as fh:
        fh.write("# SourceMinder experiment sampling pool -- the fixed universe a "
                 "run draws from.\n")
        fh.write(f"# dataset: {DATASET} @ {DATASET_REVISION}\n")
        fh.write("# image_registry: docker.io/swebench (digests pin identity; "
                 "images stay on Docker Hub)\n")
        fh.write(f"# generated: {time.strftime('%Y%m%d_%H%M%S')} by "
                 "scripts/build_pool.py\n")
        fh.write(f"# count: {len(rows)}\n")
        writer = csv.DictWriter(
            fh, fieldnames=["instance_id", "repo", "n_files", "image", "digest"])
        writer.writeheader()
        writer.writerows(rows)

    print(f"\nWrote {len(rows)} rows to {args.out}.", file=sys.stderr)
    if failures:
        print(f"ERROR: {len(failures)} image(s) failed to pin -- pool is "
              "INCOMPLETE, do not commit:", file=sys.stderr)
        for image, detail in failures:
            print(f"  {image}: {detail}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
