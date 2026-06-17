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
import sqlite3
import time
from dataclasses import dataclass
from pathlib import Path

# Shared store across analysis runs; rows are namespaced by run_tag.
DEFAULT_DB = Path("experiment/analysis/eval_results.db")

# Columns mirrored into eval_results.csv (the join surface merge_results.py uses).
CSV_COLUMNS = ["arm", "instance_id", "rep", "exit_status",
               "has_patch", "outcome", "resolved"]


@dataclass
class EvalResult:
    """One run's harness verdict.

    ``resolved`` is *derived* from ``outcome`` (never passed in), so the stored
    boolean can't drift out of step with the categorical outcome.
    """
    arm: str
    instance_id: str
    rep: str
    exit_status: str
    has_patch: bool
    outcome: str  # resolved | unresolved | error | empty_patch | incomplete
    dataset: str

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
    conn.execute(
        """
        CREATE TABLE IF NOT EXISTS eval_results (
            run_tag     TEXT NOT NULL,
            arm         TEXT NOT NULL,
            instance_id TEXT NOT NULL,
            rep         TEXT NOT NULL,
            exit_status TEXT,
            has_patch   INTEGER,
            outcome     TEXT,
            resolved    INTEGER,
            dataset     TEXT,
            updated_at  TEXT,
            PRIMARY KEY (run_tag, arm, instance_id, rep)
        )
        """
    )
    conn.commit()


def upsert(conn: sqlite3.Connection, run_tag: str, r: EvalResult) -> None:
    """Insert or replace one result row, keyed by (run_tag, arm, instance, rep)."""
    conn.execute(
        """
        INSERT OR REPLACE INTO eval_results
          (run_tag, arm, instance_id, rep, exit_status, has_patch,
           outcome, resolved, dataset, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (run_tag, r.arm, r.instance_id, r.rep, r.exit_status, int(r.has_patch),
         r.outcome, int(r.resolved), r.dataset, time.strftime("%Y-%m-%d %H:%M:%S")),
    )
    conn.commit()


def export_csv(conn: sqlite3.Connection, csv_path: Path, run_tag: str) -> int:
    """Write all rows for ``run_tag`` to ``csv_path`` in the legacy layout.

    Returns the number of data rows written. Safe to call repeatedly (e.g.
    after every group) so a crashed eval still leaves a usable CSV.
    """
    rows = conn.execute(
        """
        SELECT arm, instance_id, rep, exit_status, has_patch, outcome, resolved
        FROM eval_results WHERE run_tag = ?
        ORDER BY arm, rep, instance_id
        """,
        (run_tag,),
    ).fetchall()
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    with csv_path.open("w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(CSV_COLUMNS)
        writer.writerows(rows)
    return len(rows)


def count_resolved(conn: sqlite3.Connection, run_tag: str) -> int:
    return conn.execute(
        "SELECT COUNT(*) FROM eval_results WHERE run_tag = ? AND resolved = 1",
        (run_tag,),
    ).fetchone()[0]
