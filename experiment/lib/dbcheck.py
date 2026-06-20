"""SQLite integrity check, shared by check_dbs.py and run_one.py.

Both verify a code-index DB before trusting it; this is the one implementation.
Uses ``PRAGMA quick_check`` (much faster than ``integrity_check`` -- it skips the
index/table cross-consistency pass but still catches structural corruption) via
the ``sqlite3`` CLI, so it works on DBs built by any toolchain.

Transient failures are NOT corruption. Under heavy concurrency the check can time
out or hit a lock; those are retried, and if they persist the DB is reported as
*unverified but usable* (ok=True, with an "unverified" detail) rather than
corrupt. Real corruption produces a fast, definitive quick_check result -- never a
timeout -- so this can't hide a genuinely bad DB, but it does stop a saturated
disk from masquerading as corruption and halting a whole batch.
"""
from __future__ import annotations

import subprocess
import time
from pathlib import Path

# stderr fragments meaning "couldn't reach the data right now" (retry), as
# opposed to "the data is bad" (corruption).
_TRANSIENT = ("database is locked", "database is busy", "locking protocol")


def integrity_ok(db_path: Path, timeout: int = 60, retries: int = 3) -> tuple[bool, str]:
    """Return (ok, detail).

    ok is True when ``PRAGMA quick_check`` reports "ok", OR when the check could
    not be completed due to transient contention (timeout / lock) after
    ``retries`` attempts -- such a DB is reported usable with an "unverified"
    detail, since real corruption yields a definitive result quickly. ok is
    False only when a completed check reports a problem (genuine corruption) or
    sqlite3 cannot be run at all.
    """
    last = "unknown"
    for attempt in range(1, retries + 1):
        try:
            # ``.timeout`` (a dot-command) sets the busy timeout silently; the
            # ``PRAGMA busy_timeout=`` form would echo its value into stdout and
            # corrupt the quick_check result.
            r = subprocess.run(
                ["sqlite3", "-cmd", ".timeout 5000", str(db_path),
                 "PRAGMA quick_check;"],
                capture_output=True, text=True, timeout=timeout,
            )
        except subprocess.TimeoutExpired:
            last = f"timeout after {timeout}s"            # transient -> retry
            time.sleep(0.5 * attempt)
            continue
        except (OSError, subprocess.SubprocessError) as exc:
            return False, str(exc)                        # can't run sqlite3 at all

        out = r.stdout.strip()
        err = (r.stderr or "").strip()
        if r.returncode == 0:
            if out == "ok":
                return True, "ok"
            return False, out or "quick_check reported problems"   # real corruption
        # Non-zero exit: a lock/busy is transient; anything else (e.g. "file is
        # not a database", "malformed") is a genuine, definitive failure.
        if any(t in err.lower() for t in _TRANSIENT):
            last = err or "database is locked"
            time.sleep(0.5 * attempt)
            continue
        return False, err or out or f"sqlite3 exited {r.returncode}"

    # Only transient failures across all attempts -> usable, not corrupt.
    return True, f"unverified ({last}); proceeding -- transient, not corruption"
