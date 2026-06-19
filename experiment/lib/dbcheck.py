"""SQLite integrity check, shared by check_dbs.py and run_pilot.py.

Both verify a code-index DB with ``PRAGMA integrity_check`` before trusting it;
this is the one implementation. Uses the ``sqlite3`` CLI (already a project
dependency) so it works on DBs built by any toolchain.
"""
from __future__ import annotations

import subprocess
from pathlib import Path


def integrity_ok(db_path: Path, timeout: int = 30) -> tuple[bool, str]:
    """Return (ok, detail). ``ok`` is True only when the check reports "ok";
    ``detail`` is the check output (or the error) for logging on failure."""
    try:
        r = subprocess.run(
            ["sqlite3", str(db_path), "PRAGMA integrity_check;"],
            capture_output=True, text=True, timeout=timeout,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        return False, str(exc)
    detail = r.stdout.strip()
    if r.returncode != 0:
        return False, detail or r.stderr.strip()
    return detail == "ok", detail
