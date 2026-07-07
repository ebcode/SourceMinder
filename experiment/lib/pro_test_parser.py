"""Reparse Tutanota-style ospec output for a real per-test verdict when the
vendored ``parser.py`` (``vendor/swebench_pro_os/run_scripts/.../parser.py``)
misfires.

The vendored parser only recognizes a suite as "passed" via the literal string
``"All N assertions passed"`` -- the phrasing ospec prints ONLY when zero
assertions fail. The instant a single assertion fails anywhere in an
8000+-assertion suite, the real summary line reads ``"N out of M assertions
failed"`` instead, which the vendored parser's regexes don't match at all. It
then falls back to scanning stderr for any line containing ``"Error:"`` and
reports the *entire suite* as a synthetic ``Build/Runtime Error``, discarding
every real per-test result -- even though the suite actually ran to
completion and only one, likely unrelated, test failed.

This module re-derives the truth directly from the raw stdout/stderr the
harness already saved, without touching vendor/ or re-running Docker:
  * ``parse_suite_completion`` finds either summary line and tells you whether
    the suite genuinely finished (as opposed to crashing before any summary
    was printed at all -- a real build/runtime failure).
  * ``extract_failing_suites`` pulls the set of per-suite basenames (e.g.
    ``ConfigFileTest``) that ospec reports as failing, from the ``NAME >
    description:`` lines it prints in stderr.
  * ``reparse_required_tests`` combines both to answer the only question that
    matters here: for a given set of required (FAIL_TO_PASS/PASS_TO_PASS) test
    file paths, did each one's suite actually fail, or is the failure
    confined to some other, unrelated test?
"""
from __future__ import annotations

import re
from pathlib import Path

_PASSED_RE = re.compile(r"All (\d+) assertions passed")
_FAILED_RE = re.compile(r"(\d+) out of (\d+) assertions failed")
_FAILING_SUITE_RE = re.compile(r"^(\S+) > .+:$")


def parse_suite_completion(stdout: str) -> dict | None:
    """Returns {"total_failed": int, "total_assertions": int} if the suite
    printed a real summary line (it genuinely ran to completion), else None
    (no summary line at all -- a real build/runtime crash, not a test
    failure)."""
    m = _PASSED_RE.search(stdout)
    if m:
        return {"total_failed": 0, "total_assertions": int(m.group(1))}
    m = _FAILED_RE.search(stdout)
    if m:
        return {"total_failed": int(m.group(1)), "total_assertions": int(m.group(2))}
    return None


def extract_failing_suites(stderr: str) -> set[str]:
    """ospec prints each failure twice: once as it streams, once in a final
    ``NAME > description:`` line with the actual error attached. Only the
    colon-suffixed form reliably identifies the failing suite's basename."""
    failing = set()
    for line in stderr.splitlines():
        m = _FAILING_SUITE_RE.match(line.strip())
        if m:
            failing.add(m.group(1))
    return failing


def reparse_required_tests(stdout: str, stderr: str, required: set) -> dict | None:
    """Returns {test_name: "PASSED"|"FAILED"} for every test in ``required``
    if the suite genuinely completed, else None if it didn't (real crash --
    the caller should fall back to whatever the vendored parser said)."""
    completion = parse_suite_completion(stdout)
    if completion is None:
        return None
    failing_suites = extract_failing_suites(stderr)
    return {
        t: "FAILED" if Path(t).stem in failing_suites else "PASSED"
        for t in required
    }
