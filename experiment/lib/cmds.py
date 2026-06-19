"""Detect exploration-tool invocations in an agent's shell command.

One canonical definition of "did this command call qi / a grep-like tool / a
file-dump tool", so the mechanism metric is identical across analyzers. Before
this, ``analyze_trajectories.py`` and ``traj_diff.py`` used *different* regexes
(traj_diff only matched ``qi`` right after ``&&``, undercounting), so the same
trajectory could report different qi-usage depending on which tool you asked.

The patterns match a token at a command boundary (start of string, or after
whitespace / ``;`` / ``|`` / ``&`` / ``(``) so ``cd x && qi foo`` and
``a | grep b`` both count, while ``equip`` or ``acatalog`` do not.
"""
from __future__ import annotations

import re

QI_RE = re.compile(r"(^|[\s;|&(])qi(\s|$)")
GREP_RE = re.compile(r"(^|[\s;|&(])(grep|rg|ag|ack)(\s|$)")
READ_RE = re.compile(r"(^|[\s;|&(])(cat|sed|head|tail|less|more)(\s|$)")


def count_tools(cmd: str) -> tuple[int, int, int]:
    """Return (qi, grep, file_read) invocation counts in one command string."""
    return (
        len(QI_RE.findall(cmd)),
        len(GREP_RE.findall(cmd)),
        len(READ_RE.findall(cmd)),
    )


def uses_qi(cmd: str) -> bool:
    """Whether the command invokes qi at all (for per-turn flagging)."""
    return QI_RE.search(cmd) is not None
