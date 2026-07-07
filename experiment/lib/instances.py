"""Parse instance-list files.

A list file is one instance id per line; blank lines and ``#`` comments are
skipped, and only the first whitespace-delimited token of a line is taken (so
``django__django-11099  # 2 files`` yields ``django__django-11099``). Shared by
``run_experiment.py``, ``pre_index.py``, and ``evaluate_patches.py``, which each
had their own copy of this loop.
"""
from __future__ import annotations

from pathlib import Path


def parse_instance_ids(path: Path) -> list[str]:
    ids: list[str] = []
    for line in Path(path).read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        ids.append(line.split()[0])
    return ids
