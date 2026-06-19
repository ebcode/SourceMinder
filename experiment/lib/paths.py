"""Canonical filesystem roots for the experiment.

Every path is derived from this file's location, so they resolve correctly no
matter the current working directory -- replacing the copy-pasted
``REPO_ROOT = Path(__file__).parent.parent`` blocks and the hardcoded
``"experiment/..."`` argparse defaults that only worked from the repo root.

Layout (generated artifacts live under ``results/``, never beside the code)::

    experiment/
      analysis/  scripts/  config/  lib/   # code + config
      data/                               # curated inputs (pool.csv)
      dbs/                                # per-instance qi indexes
      logs/                               # trajectories: <model>/<arm>/<instance>/
      results/                           # ALL generated output
        runs/<ts>/                       # runs.csv, eval_results.csv, charts/
        reports/                         # harness <model>.<run_id>.json
        samples/                         # sample_<ts>_seed<seed>.json
        eval_results.db                  # WAL store (+ -wal/-shm sidecars)
"""
from __future__ import annotations

import os
import time
from contextlib import contextmanager
from pathlib import Path
from typing import Iterator

# lib/paths.py -> lib -> experiment -> repo root
EXPERIMENT_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = EXPERIMENT_DIR.parent

# Code / config / inputs
ANALYSIS_DIR = EXPERIMENT_DIR / "analysis"
SCRIPTS_DIR = EXPERIMENT_DIR / "scripts"
CONFIG_DIR = EXPERIMENT_DIR / "config"
DATA_DIR = EXPERIMENT_DIR / "data"

# Per-instance qi indexes and agent trajectories
DBS_DIR = EXPERIMENT_DIR / "dbs"
LOGS_DIR = EXPERIMENT_DIR / "logs"

# All generated artifacts
RESULTS_DIR = EXPERIMENT_DIR / "results"
RUNS_DIR = RESULTS_DIR / "runs"
REPORTS_DIR = RESULTS_DIR / "reports"
SAMPLES_DIR = RESULTS_DIR / "samples"
EVAL_DB = RESULTS_DIR / "eval_results.db"


def batch_run_dir(batch_id: str) -> Path:
    """Path for a named batch run directory: ``results/runs/<batch_id>/``.

    Does not create it -- callers ``mkdir(parents=True)`` when they write.
    """
    return RUNS_DIR / batch_id


def new_run_dir(ts: str | None = None, batch_id: str = "") -> Path:
    """Path for a run directory under ``results/runs/``.

    When ``batch_id`` is given, returns ``results/runs/<batch_id>/``.
    Otherwise falls back to a timestamped path (current behavior).
    Does not create it -- callers ``mkdir(parents=True)`` when they write.
    """
    if batch_id:
        return batch_run_dir(batch_id)
    return RUNS_DIR / (ts or time.strftime("%Y%m%d_%H%M%S"))


@contextmanager
def cwd(target: Path) -> Iterator[Path]:
    """Run a block with the process working directory at ``target``.

    The SWE-bench harness writes its report (``<model>.<run_id>.json``) and its
    ``logs/run_evaluation/`` tree to the *current* working directory -- it has no
    output-path argument. Wrap harness calls in ``with paths.cwd(REPORTS_DIR):``
    so those artifacts land under ``results/`` instead of polluting the repo
    root. ``target`` is created if missing; the previous cwd is restored on exit.

    Process-wide and NOT thread-safe: enter it once around a sequential batch,
    never per worker thread.
    """
    target.mkdir(parents=True, exist_ok=True)
    prev = Path.cwd()
    os.chdir(target)
    try:
        yield target
    finally:
        os.chdir(prev)
