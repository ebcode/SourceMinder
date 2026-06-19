#!/usr/bin/env python3
"""SQLite-backed store for SWE-bench patch-evaluation results.

``evaluate_patches.py`` writes one row per ``(run_tag, arm, instance_id, rep)``
here instead of appending to a plaintext CSV. A shared database with WAL
journaling lets multiple eval processes (e.g. sharded across ``(arm, rep)``
groups) write concurrently without clobbering each other -- the failure mode a
single ``"w"``-mode CSV handle suffers when two runs target the same file.

Idempotency: the primary key ``(run_tag, arm, instance_id, rep)`` plus
``INSERT OR REPLACE`` means a re-run or ``--retry-failed`` re-evaluation
overwrites the prior row in place rather than appending a duplicate.

CSV stays the interchange format for ``merge_results.py``: ``export_csv()``
renders the rows for one ``run_tag`` to the same 7-column layout the old
pipeline produced, so downstream joins are unchanged.
"""
from __future__ import annotations

import csv
import os
import sqlite3
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # -> experiment/
from lib import paths

# Shared store across analysis runs; rows are namespaced by run_tag.
DEFAULT_DB = paths.EVAL_DB

# Columns mirrored into eval_results.csv (the join surface merge_results.py uses).
CSV_COLUMNS = ["batch_id", "n_files", "patch_files",
               "model", "arm", "instance_id", "rep", "exit_status",
               "has_patch", "outcome", "resolved"]


@dataclass
class EvalResult:
    """One run's harness verdict.

    ``resolved`` is *derived* from ``outcome`` (never passed in), so the stored
    boolean can't drift out of step with the categorical outcome.
    """
    model: str
    arm: str
    instance_id: str
    rep: str
    exit_status: str
    has_patch: bool
    outcome: str  # resolved | unresolved | error | empty_patch | incomplete
    dataset: str
    batch_id: str = ""
    n_files: int | None = None
    patch_files: int | None = None

    @property
    def resolved(self) -> bool:
        return self.outcome == "resolved"


def connect(db_path: Path = DEFAULT_DB) -> sqlite3.Connection:
    """Open the results DB in WAL mode with a generous busy timeout.

    WAL lets readers and a single writer coexist; ``busy_timeout`` makes a
    second writer wait for the lock instead of failing with 'database is
    locked'. Per-result writes are tiny next to the minutes-long Docker
    evaluations, so write contention is negligible.
    """
    db_path.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(db_path), timeout=60.0)
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA synchronous=NORMAL")
    conn.execute("PRAGMA busy_timeout=60000")
    init_schema(conn)
    return conn


def init_schema(conn: sqlite3.Connection) -> None:
    # ``model`` is part of the identity: the same (arm, instance_id, rep) is
    # evaluated independently per model, so it must be in the primary key or a
    # second model's rows would overwrite the first's.
    cols = [r[1] for r in conn.execute("PRAGMA table_info(eval_results)").fetchall()]
    legacy = bool(cols) and "model" not in cols
    if legacy:
        # Rebuild the table to put ``model`` in the PK (ALTER can't change a PK).
        # Old rows predate the per-model layout; keep them (model='') rather than
        # silently dropping, and warn so they can be re-evaluated.
        conn.execute("ALTER TABLE eval_results RENAME TO eval_results_legacy")

    conn.execute(
        """
        CREATE TABLE IF NOT EXISTS eval_results (
            run_tag     TEXT NOT NULL,
            batch_id    TEXT NOT NULL DEFAULT '',
            model       TEXT NOT NULL,
            arm         TEXT NOT NULL,
            instance_id TEXT NOT NULL,
            rep         TEXT NOT NULL,
            n_files     INTEGER,
            patch_files INTEGER,
            exit_status TEXT,
            has_patch   INTEGER,
            outcome     TEXT,
            resolved    INTEGER,
            dataset     TEXT,
            updated_at  TEXT,
            PRIMARY KEY (run_tag, model, arm, instance_id, rep)
        )
        """
    )
    # Additive column migrations — safe on every connect (no-op when present).
    for col, defn in [
        ("batch_id",    "TEXT NOT NULL DEFAULT ''"),
        ("n_files",     "INTEGER"),
        ("patch_files", "INTEGER"),
    ]:
        if col not in cols:
            conn.execute(f"ALTER TABLE eval_results ADD COLUMN {col} {defn}")
    if legacy:
        conn.execute(
            """
            INSERT INTO eval_results
              (run_tag, model, arm, instance_id, rep, exit_status,
               has_patch, outcome, resolved, dataset, updated_at)
            SELECT run_tag, '', arm, instance_id, rep, exit_status,
                   has_patch, outcome, resolved, dataset, updated_at
            FROM eval_results_legacy
            """
        )
        n = conn.execute("SELECT COUNT(*) FROM eval_results_legacy").fetchone()[0]
        conn.execute("DROP TABLE eval_results_legacy")
        print(f"eval_db: migrated {n} legacy row(s) to model='' "
              "(pre-dating per-model logs; re-evaluate to assign a model).",
              file=sys.stderr)
    conn.commit()


def upsert(conn: sqlite3.Connection, run_tag: str, r: EvalResult) -> None:
    """Insert or replace one result row, keyed by (run_tag, arm, instance, rep)."""
    conn.execute(
        """
        INSERT OR REPLACE INTO eval_results
          (run_tag, batch_id, model, arm, instance_id, rep, n_files, patch_files,
           exit_status, has_patch, outcome, resolved, dataset, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (run_tag, r.batch_id, r.model, r.arm, r.instance_id, r.rep,
         r.n_files, r.patch_files,
         r.exit_status, int(r.has_patch), r.outcome, int(r.resolved),
         r.dataset, time.strftime("%Y-%m-%d %H:%M:%S")),
    )
    conn.commit()


def export_csv(conn: sqlite3.Connection, csv_path: Path, run_tag: str) -> int:
    """Write all rows for ``run_tag`` to ``csv_path`` in the legacy layout.

    Returns the number of data rows written. Safe to call repeatedly (e.g.
    after every group) so a crashed eval still leaves a usable CSV.

    The write is atomic and concurrency-safe on its own: each call renders to a
    unique temp file in the target directory and ``os.replace``s it over
    ``csv_path``. The DB is the source of truth, so every snapshot is complete;
    the atomic rename means a reader never sees a half-written file and two
    concurrent exporters (parallel ``--workers``) can't interleave into one
    handle -- the last rename simply wins with a valid full snapshot. Callers
    need no external lock.
    """
    rows = conn.execute(
        """
        SELECT batch_id, n_files, patch_files,
               model, arm, instance_id, rep, exit_status, has_patch, outcome, resolved
        FROM eval_results WHERE run_tag = ?
        ORDER BY model, arm, rep, instance_id
        """,
        (run_tag,),
    ).fetchall()
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(
        dir=str(csv_path.parent), prefix=".eval_results.", suffix=".tmp")
    try:
        with os.fdopen(fd, "w", newline="") as fh:
            writer = csv.writer(fh)
            writer.writerow(CSV_COLUMNS)
            writer.writerows(rows)
        os.replace(tmp, csv_path)
    except BaseException:
        if os.path.exists(tmp):
            os.unlink(tmp)
        raise
    return len(rows)


def count_resolved(conn: sqlite3.Connection, run_tag: str) -> int:
    return conn.execute(
        "SELECT COUNT(*) FROM eval_results WHERE run_tag = ? AND resolved = 1",
        (run_tag,),
    ).fetchone()[0]
