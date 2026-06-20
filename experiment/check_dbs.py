#!/usr/bin/env python3
"""Validate all SQLite databases in experiment/dbs/ with PRAGMA integrity_check.

Usage:
  python3 experiment/check_dbs.py
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[0]))  # -> experiment/
from lib import paths
from lib.dbcheck import integrity_ok

DBS_DIR = paths.DBS_DIR


def main() -> int:
    if not DBS_DIR.exists():
        print(f"ERROR: {DBS_DIR} not found", file=sys.stderr)
        return 1

    dbs = sorted(DBS_DIR.glob("*.db"))
    if not dbs:
        print("No .db files found.")
        return 0

    passed = failed = 0
    for db in dbs:
        ok, detail = integrity_ok(db)
        size = db.stat().st_size // (1024 * 1024)
        status = "OK" if ok else f"FAILED — {detail!r}"
        print(f"  [{status}]  {db.name}  ({size}M)")
        passed, failed = (passed + 1, failed) if ok else (passed, failed + 1)

    print(f"\n{passed} passed, {failed} failed, {len(dbs)} total")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
