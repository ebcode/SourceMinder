"""Extract the set of files a unified diff touches, from ``diff --git`` headers."""
from __future__ import annotations

import re

_DIFF_RE = re.compile(r"^diff --git a/.+ b/(.+)$", re.MULTILINE)


def diff_files(patch: str) -> set[str]:
    return set(_DIFF_RE.findall(patch or ""))
