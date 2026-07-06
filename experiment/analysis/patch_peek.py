#!/usr/bin/env python3
"""Show gold-patch files for a SWE-bench Pro instance from test.parquet.

Given an instance_id substring, prints the diff --git headers (files touched),
patch line counts, and test counts — everything needed to judge qi-friendliness
without pulling the Docker image.

Usage:
    experiment/.venv_pro/bin/python experiment/analysis/patch_peek.py <instance_id_substr>

    # Exact match:
    experiment/.venv_pro/bin/python experiment/analysis/patch_peek.py ansible-f86c58e2

    # Multiple matches — lists them, pick one:
    experiment/.venv_pro/bin/python experiment/analysis/patch_peek.py tutanota --full

    # Show full patch content (not just headers):
    experiment/.venv_pro/bin/python experiment/analysis/patch_peek.py ansible-f86c58e2 --full
"""

from __future__ import annotations

import ast
import re
import sys
from pathlib import Path

import pyarrow.parquet as pq

PARQUET = Path(__file__).resolve().parent.parent / "data" / "swebench_pro" / "test.parquet"


def show_instance(instance_id: str, full: bool = False) -> None:
    t = pq.read_table(PARQUET)
    df = t.to_pandas()
    matches = df[df["instance_id"].str.contains(instance_id, case=False, regex=False)]

    if len(matches) == 0:
        print(f"No match for: {instance_id}")
        return

    if len(matches) > 1:
        print(f"{len(matches)} matches. Use a more specific id:\n")
        for _, r in matches.iterrows():
            print(f"  {r['instance_id']}")
        return

    row = matches.iloc[0]
    f2p = ast.literal_eval(row["fail_to_pass"]) if row["fail_to_pass"] else []
    p2p = ast.literal_eval(row["pass_to_pass"]) if row["pass_to_pass"] else []
    patch = row.get("patch", "") or ""

    # Extract files from diff --git headers
    files: list[str] = []
    for line in patch.split("\n"):
        if line.startswith("diff --git "):
            parts = line.split()
            if len(parts) >= 4:
                files.append(parts[3][2:])  # strip b/ prefix
        elif line.startswith("--- /dev/null"):
            # new file: diff --git a/path b/path  then --- /dev/null
            pass
        elif line.startswith("--- a/") and line not in files:
            # Fallback — some patches may not have diff --git headers
            pass

    patch_lines = len(patch.split("\n")) if patch else 0

    print(f"instance:  {row['instance_id']}")
    print(f"repo:      {row['repo']}")
    print(f"language:  {row['repo_language']}")
    print(f"files:     {len(files)}")
    print(f"n_f2p:     {len(f2p)}")
    print(f"n_p2p:     {len(p2p)}")
    print(f"lines:     {patch_lines}")
    if full:
        print(f"image:     {row.get('image_name', '?')}")
    print(f"\n--- gold patch files ---")
    for f in files:
        print(f"  {f}")

    if full:
        print(f"\n--- full patch ({patch_lines} lines) ---")
        print(patch)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    instance_arg = sys.argv[1]
    show_full = len(sys.argv) > 2 and sys.argv[2] == "--full"
    show_instance(instance_arg, show_full)
