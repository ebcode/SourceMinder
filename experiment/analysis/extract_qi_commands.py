#!/usr/bin/env python3
"""Extract one row per shell command from mini-swe-agent trajectories.

This is the per-COMMAND companion to analyze_trajectories.py (which is
per-run). Each assistant action is paired to its tool output via
``tool_call_id`` and written to a CSV: one row per command the agent ran, with
the size of the resulting output and -- for qi commands -- which flags were
used. Aggregation/reporting lives in report_qi_commands.py; this script only
produces the raw table so you can slice it yourself.

Output columns:
  arm, instance, run_id, model, batch_id   -- provenance (from the path)
  turn_idx, cmd_idx                        -- message index, action index in turn
  tool                                     -- qi | grep | read | other (primary)
  command                                  -- the full shell command string
  output_chars, output_tokens_approx       -- size of the paired tool output
  returncode, is_error                     -- parsed from <returncode>N</returncode>
  qi_pure                                  -- 1 if a qi command with no grep/read/pipe
  qi_limit, qi_limit_per_file, qi_toc, qi_expand, qi_include, qi_exclude,
  qi_within, qi_and, qi_def, qi_usage, qi_raw   -- flag present (1/0), qi rows only

Defaults to the prompt_study batch.

Usage:
  python3 experiment/analysis/extract_qi_commands.py
  python3 experiment/analysis/extract_qi_commands.py --batch prompt_study
  python3 experiment/analysis/extract_qi_commands.py --logs experiment/logs --out /tmp/cmds.csv
"""
from __future__ import annotations

import argparse
import csv
import json
import re
import shlex
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # -> experiment/
from lib import cmds, paths
from lib.trajmeta import infer_path_meta

# Tool output is not API-counted; approximate at ~4 chars/token to stay
# consistent with analyze_trajectories.py.
CHARS_PER_TOKEN = 4.0

# Exact-token flag sets, so -l and -lpf never collide (a substring match would).
QI_FLAGS = {
    "qi_limit": {"-l", "--limit"},
    "qi_limit_per_file": {"-lpf", "--limit-per-file"},
    "qi_toc": {"--toc"},
    "qi_expand": {"-e", "--expand"},
    "qi_include": {"-i", "--include-context"},
    "qi_exclude": {"-x", "--exclude-context"},
    "qi_within": {"-w", "--within"},
    "qi_and": {"--and", "--same-line"},
    "qi_def": {"--def"},
    "qi_usage": {"--usage"},
    "qi_raw": {"--raw"},
}
QI_FLAG_COLS = list(QI_FLAGS)

_RC_RE = re.compile(r"<returncode>(-?\d+)</returncode>")
# qi prints "Found N matches" on a hit and "No results" on a miss. Captured for
# the zero-result rate (a qi call that found nothing = the model searched for a
# symbol that doesn't exist). Empty for non-search qi (e.g. --toc) and errors.
_FOUND_RE = re.compile(r"Found (\d+) match")
_NORESULT_RE = re.compile(r"\bNo results\b")


def _tokens(cmd: str) -> list[str]:
    """Tokenize a shell command, tolerating quoting the agent may have mangled."""
    try:
        return shlex.split(cmd)
    except ValueError:
        return cmd.split()


def _primary_tool(cmd: str) -> str:
    """Classify a command by its primary exploration tool (qi wins ties)."""
    qi, grep, read = cmds.count_tools(cmd)
    if qi:
        return "qi"
    if grep:
        return "grep"
    if read:
        return "read"
    return "other"


def _qi_pure(cmd: str) -> bool:
    """True when output is attributable to qi alone (no grep/read, no pipe)."""
    qi, grep, read = cmds.count_tools(cmd)
    return bool(qi) and grep == 0 and read == 0 and "|" not in cmd


def _returncode(output: str) -> int | None:
    m = _RC_RE.search(output)
    return int(m.group(1)) if m else None


def _qi_result_count(output: str) -> int | None:
    """qi match count: N from 'Found N matches', 0 from 'No results', else None."""
    m = _FOUND_RE.search(output)
    if m:
        return int(m.group(1))
    if _NORESULT_RE.search(output):
        return 0
    return None


def rows_for(path: Path) -> list[dict]:
    try:
        data = json.loads(path.read_text())
    except (json.JSONDecodeError, OSError) as exc:
        print(f"WARNING: skipping {path}: {exc}", file=sys.stderr)
        return []

    messages = data.get("messages", [])
    model, batch, arm, instance = infer_path_meta(path)
    run_id = path.name.replace(".traj.json", "")

    # Pair outputs to actions by tool_call_id (turns can issue several commands).
    out_by_id = {
        m.get("tool_call_id"): str(m.get("content", ""))
        for m in messages
        if m.get("role") == "tool"
    }

    out: list[dict] = []
    for turn_idx, msg in enumerate(messages):
        extra = msg.get("extra")
        if not isinstance(extra, dict):
            continue
        for cmd_idx, action in enumerate(extra.get("actions") or []):
            if not isinstance(action, dict):
                continue
            command = action.get("command", "") or ""
            output = out_by_id.get(action.get("tool_call_id"), "")
            tool = _primary_tool(command)
            rc = _returncode(output)
            res = _qi_result_count(output) if tool == "qi" else None
            tokset = set(_tokens(command)) if tool == "qi" else set()

            row = {
                "arm": arm,
                "instance": instance,
                "run_id": run_id,
                "model": model,
                "batch_id": batch,
                "turn_idx": turn_idx,
                "cmd_idx": cmd_idx,
                "tool": tool,
                "command": command,
                "output_chars": len(output),
                "output_tokens_approx": round(len(output) / CHARS_PER_TOKEN),
                "returncode": rc if rc is not None else "",
                "is_error": "" if rc is None else int(rc != 0),
                "qi_pure": int(_qi_pure(command)),
                "qi_results": res if res is not None else "",
            }
            for col, flags in QI_FLAGS.items():
                row[col] = int(bool(tokset & flags)) if tool == "qi" else ""
            out.append(row)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--logs", type=Path, default=paths.LOGS_DIR,
                    help=f"directory of *.traj.json files (default: {paths.LOGS_DIR})")
    ap.add_argument("--batch", default="prompt_study", metavar="BATCH_ID",
                    help="filter to this batch id (from the path); "
                         "'' for all (default: prompt_study)")
    ap.add_argument("--out", type=Path, default=None,
                    help="output CSV (default: results/runs/<batch>/qi_commands.csv)")
    args = ap.parse_args()

    logs_dir = Path(args.logs)
    if not logs_dir.is_dir():
        print(f"ERROR: not a directory: {logs_dir}", file=sys.stderr)
        return 1

    traj_files = sorted(logs_dir.rglob("*.traj.json"))
    if not traj_files:
        print(f"No *.traj.json files under {logs_dir}", file=sys.stderr)
        return 1

    rows: list[dict] = []
    for p in traj_files:
        rows.extend(rows_for(p))
    if args.batch:
        rows = [r for r in rows if r["batch_id"] == args.batch]
    if not rows:
        print(f"No commands found (batch={args.batch!r}).", file=sys.stderr)
        return 1

    out_path = args.out or (paths.new_run_dir(batch_id=args.batch or "all")
                            / "qi_commands.csv")
    out_path.parent.mkdir(parents=True, exist_ok=True)

    fields = ["arm", "instance", "run_id", "model", "batch_id", "turn_idx",
              "cmd_idx", "tool", "command", "output_chars",
              "output_tokens_approx", "returncode", "is_error", "qi_pure",
              "qi_results", *QI_FLAG_COLS]
    with out_path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)

    n_runs = len({(r["model"], r["arm"], r["instance"], r["run_id"]) for r in rows})
    n_qi = sum(1 for r in rows if r["tool"] == "qi")
    print(f"Wrote {len(rows)} command(s) from {n_runs} run(s) "
          f"({n_qi} qi) -> {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
