"""Load the SWE-bench Pro dataset without HF's directory-glob footgun.

``datasets.load_dataset(dir_path, split="test")`` on a local directory globs
every file in that directory matching the split name, not just the intended
parquet -- a stray ``test.parquet.bak`` (or any other ``test*`` file dropped
in ``data/swebench_pro/`` for a backup) silently gets concatenated into the
"test" split. Downstream code building ``{instance_id: row}`` dicts then has
duplicate keys, and the last file in glob order (alphabetically after
``test.parquet``) wins -- overwriting corrected rows with stale ones.

This wrapper pins the load to exactly ``<subset>/<split>.parquet`` when
``subset`` is a local directory, and falls back to plain ``load_dataset`` for
everything else (e.g. a HF hub dataset id from ``DATASET_MAPPING``).
"""
from __future__ import annotations

from pathlib import Path


def load_pro_dataset(subset, split: str = "test"):
    from datasets import load_dataset

    local_dir = Path(subset)
    if local_dir.is_dir():
        parquet_file = local_dir / f"{split}.parquet"
        if not parquet_file.exists():
            raise FileNotFoundError(f"{parquet_file} not found")
        return load_dataset("parquet", data_files=str(parquet_file), split="train")
    return load_dataset(str(subset), split=split)
