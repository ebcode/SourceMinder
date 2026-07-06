"""Repo-level navigation-surface sizes sourced from local code indexes.

Extracted from experiment/tmp/qi_fit_signals.py (2026-07-05) so pro_select.py
and qi_fit_signals.py share one implementation.  The per-instance DBs live in
experiment/dbs/ (DB filename == f"{instance_id}.db"); they carry the only
faithful measure of qi's navigation surface (indexed files + definitions), so
sizes are sourced here rather than from raw repo bytes.
"""
from __future__ import annotations

import sqlite3
from pathlib import Path
from typing import Iterable


def index_size(instance_id: str, dbs_dir: Path) -> tuple[int, int] | None:
    """(files, defs) from one instance's code index, or None if no DB / unreadable.

    files = distinct indexed files (qi's file surface); defs = definition rows
    (navigable symbols -- de-confounds comment/string density, itself a language
    trait).  Read-only; a partial/broken index just returns small numbers, which
    the repo-level max() below discards in favour of a fuller sibling commit."""
    db = dbs_dir / f"{instance_id}.db"
    if not db.is_file():
        return None
    try:
        conn = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
        try:
            files = conn.execute(
                "SELECT COUNT(DISTINCT directory || filename) FROM code_index").fetchone()[0]
            defs = conn.execute(
                "SELECT COUNT(*) FROM code_index WHERE is_definition = 1").fetchone()[0]
        finally:
            conn.close()
        return files, defs
    except sqlite3.Error:
        return None


def build_repo_size_map(instance_repo_pairs: Iterable[tuple[str, str]],
                        dbs_dir: Path) -> dict[str, tuple[int, int]]:
    """{repo: (repo_files, repo_defs)} sourced from local code indexes.

    Repo-level, NOT instance-level: size is a repo property (~commit-invariant),
    and our handful of DBs span most repos, so joining by repo restores whole-pool
    coverage -- every instance of an indexed repo gets a size even if that specific
    commit was never indexed.  max() across a repo's commits picks the most-complete
    index, guarding against a partial one.  (Historical note: the flipt 80- vs
    398-file spread that motivated max() turned out to be genuine repo growth
    between 2022 and later commits, not a partial index -- max() is still right,
    it just means "the biggest surface any benchmark commit of this repo offers".)
    Repos with zero local DBs are absent -> render NULL, never 0."""
    m: dict[str, tuple[int, int]] = {}
    for instance_id, repo in instance_repo_pairs:
        sz = index_size(instance_id, dbs_dir)
        if sz is None:
            continue
        pf, pd = m.get(repo, (0, 0))
        m[repo] = (max(pf, sz[0]), max(pd, sz[1]))
    return m
