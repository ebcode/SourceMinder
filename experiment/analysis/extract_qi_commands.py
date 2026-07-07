#!/usr/bin/env python3
"""Extract one row per shell command from SWE-bench Pro trajectories.

Each assistant action is paired to its tool output and written to a CSV: one
row per command the agent ran, with the size of the resulting output and -- for
qi commands -- which flags were used. Aggregation/reporting lives in
report_qi_commands.py; this script only produces the raw table so you can slice
it yourself.

Trajectory format: the command is a ```bash block inside the assistant
message's markdown content, and its output is the NEXT ``role == "user"``
message (the action_observation_template, wrapping <returncode>).

Output columns:
  arm, instance, run_id, model, batch_id   -- provenance (from the path)
  turn_idx, cmd_idx                        -- message index, action index in turn
  tool                                     -- qi | grep | read | other (primary)
  command                                  -- the full shell command string
  output_chars, output_tokens_approx       -- size of the paired tool output
  returncode, is_error                     -- parsed from <returncode>N</returncode>
  qi_pure                                  -- 1 if the action is only qi (+ echo separators)
  qi_limit, qi_limit_per_file, qi_toc, qi_expand, qi_include, qi_exclude,
  qi_within, qi_and, qi_def, qi_usage, qi_raw, qi_parent, qi_file, qi_type,
  qi_modifier, qi_scope                          -- flag present (1/0), qi rows only
  qi_dotted_name, qi_quoted_phrase, qi_abs_path  -- misuse markers (1/0), qi rows only

Usage:
  experiment/.venv_pro/bin/python experiment/analysis/extract_qi_commands.py \\
      --logs experiment/logs/deepseek--deepseek-v4-flash/pro_pilot_ansible_n40 \\
      --out  experiment/results/pro_runs/<batch>/qi_commands.csv
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
from lib import cmds, paths  # noqa: E402
from analysis.analyze_pro_trajectories import (  # noqa: E402
    BASH_RE, norm_model, parse_run_id,
)

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
    # Column filters whose adoption we want to track (e.g. did teaching -p land?).
    "qi_parent": {"-p", "--parent"},
    "qi_file": {"-f", "--file"},
    "qi_type": {"-t", "--type"},
    "qi_modifier": {"-m", "--modifier"},
    "qi_scope": {"-s", "--scope"},
    "qi_verbose": {"-v", "--verbose"},
}
QI_FLAG_COLS = list(QI_FLAGS)

# Misuse columns: not simple flag-presence, so detected by lib.cmds helpers.
#   qi_dotted_name  -- a qualified parent.symbol pattern passed whole (finds nothing)
#   qi_quoted_phrase-- a multi-word phrase in quotes (matched as literal text)
#   qi_abs_path     -- a -f value that is an absolute /app/... path
QI_ANTIPATTERN_COLS = ["qi_dotted_name", "qi_quoted_phrase", "qi_abs_path"]

_RC_RE = re.compile(r"<returncode>(-?\d+)</returncode>")
_TIMEOUT_RE = re.compile(r"timed out and has been killed")
# qi prints "Found N matches" on a hit and "No results" on a miss. Captured for
# the zero-result rate (a qi call that found nothing = the model searched for a
# symbol that doesn't exist). Empty for non-search qi (e.g. --toc) and errors.
# Under -q (quiet) the "Found N matches" footer is dropped, so a hit shows only
# the result table; detect those by counting data rows ("<lineno> | ..."), qi's
# per-match row format. "No results" still prints under -q, so misses are
# unambiguous in both modes.
_FOUND_RE = re.compile(r"Found (\d+) match")
# A true miss prints a zero verdict. "No results" is the legacy wording (older
# trajectories); "0 matches" is the current verdict; the filtered-miss branch
# suppresses the verdict and is recognized by "excluded by filters" instead.
_NORESULT_RE = re.compile(r"No results|\b0 matches\b|excluded by")
_QI_ROW_RE = re.compile(r"^\s*\d+\s*\|", re.M)
# A true miss (qi_results==0) comes in flavors. "excluded by filters" => the
# symbol exists but the agent's -f/-i/-x filters removed every match (a
# self-inflicted, prompt-actionable miss); "No partial matches" => genuinely
# absent (not even a wildcard hit); "not indexed" => qi declined to index it
# (e.g. a pure number). Used to split the zero-result rate by cause.
_FILTER_MISS_RE = re.compile(r"excluded by")
_ABSENT_RE = re.compile(r"No partial matches found")
_NOTIDX_RE = re.compile(r"is not indexed")


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
    """True when output is attributable to qi alone: the action's segments are
    only qi and echo separators (no grep/read/ls/pipe/other program). echo is a
    no-op the agent prints between qi outputs; anything else adds foreign output.
    See cmds.only_qi_and_echo."""
    return cmds.only_qi_and_echo(cmd)


def _returncode(output: str) -> int | None:
    m = _RC_RE.search(output)
    return int(m.group(1)) if m else None


def _qi_result_count(output: str) -> int | None:
    """qi match count: N from 'Found N matches'; under -q (footer dropped) the
    number of result table rows; 0 from 'No results'; else None (non-search
    output such as --toc/--expand, help text, or errors).

    Table rows are counted BEFORE 'No results' is checked: an exact-match miss
    that qi auto-retries with wildcards prints 'No results' AND then a result
    table ('Retrying with partial matches for X: ...'). Those are hits (the
    agent got rows), not zero-result misses."""
    m = _FOUND_RE.search(output)
    if m:
        return int(m.group(1))
    rows = len(_QI_ROW_RE.findall(output))
    if rows:
        return rows
    if _NORESULT_RE.search(output):
        return 0
    return None


def _qi_miss_kind(output: str, count: int | None) -> str:
    """Why a true miss (count==0) found nothing: 'filtered' (excluded by the
    agent's own filters), 'absent' (no such symbol, not even a partial),
    'not_indexed' (qi declined it), or 'other'. Empty for hits/non-searches."""
    if count != 0:
        return ""
    if _FILTER_MISS_RE.search(output):
        return "filtered"
    if _ABSENT_RE.search(output):
        return "absent"
    if _NOTIDX_RE.search(output):
        return "not_indexed"
    return "other"


def build_command_row(meta: dict, turn_idx: int, cmd_idx: int,
                      command: str, output: str,
                      forced_rc: int | None = None) -> dict:
    """Build one per-command CSV row from a command + its tool output.

    ``meta`` carries provenance: arm, instance, run_id, model, batch_id.
    ``forced_rc`` overrides the returncode parsed from output (used for timeout
    events where the harness omits the <returncode> tag)."""
    tool = _primary_tool(command)
    rc = forced_rc if forced_rc is not None else _returncode(output)
    res = _qi_result_count(output) if tool == "qi" else None
    tokset = set(_tokens(command)) if tool == "qi" else set()

    row = {
        "arm": meta.get("arm", ""),
        "instance": meta.get("instance", ""),
        "run_id": meta.get("run_id", ""),
        "model": meta.get("model", ""),
        "batch_id": meta.get("batch_id", ""),
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
        "qi_miss_kind": _qi_miss_kind(output, res) if tool == "qi" else "",
    }
    for col, flags in QI_FLAGS.items():
        row[col] = int(bool(tokset & flags)) if tool == "qi" else ""
    if tool == "qi":
        subs = cmds.qi_subcommands(command)
        row["qi_dotted_name"] = int(any(cmds.qi_dotted_pattern(s) for s in subs))
        row["qi_quoted_phrase"] = int(any(cmds.qi_quoted_phrase(s) for s in subs))
        row["qi_abs_path"] = int(any(cmds.qi_abs_path_filter(s) for s in subs))
    else:
        for col in QI_ANTIPATTERN_COLS:
            row[col] = ""
    return row


# Column order for the per-command CSV.
CSV_FIELDS = ["arm", "instance", "run_id", "model", "batch_id", "turn_idx",
              "cmd_idx", "tool", "command", "output_chars",
              "output_tokens_approx", "returncode", "is_error", "qi_pure",
              "qi_results", "qi_miss_kind", *QI_FLAG_COLS, *QI_ANTIPATTERN_COLS]


def _pro_model(messages: list[dict], data: dict) -> str:
    """Normalized model id from the API's per-message accounting (Pro
    trajectories carry it under extra.response), falling back to the config."""
    for m in messages:
        extra = m.get("extra")
        if isinstance(extra, dict):
            resp = extra.get("response")
            if isinstance(resp, dict) and resp.get("model"):
                return norm_model(resp["model"])
    cfg = data.get("info", {}).get("config", {}).get("model", {})
    return norm_model(cfg.get("model_name", ""))


def _arm_instance_batch(path: Path) -> tuple[str, str, str]:
    """arm/instance/batch from the path. Pro nests as
    logs/<model>/<batch>/<arm>/<instance>/<file> (named batch) or
    logs/pro_pilot/<arm>/<instance>/<file>; both put the instance dir directly
    inside the arm dir, which is how we read it back."""
    instance = path.parent.name
    arm = path.parent.parent.name
    above = path.parent.parent.parent.name
    batch = "" if above in ("logs", "pro_pilot") else above
    if above == "pro_pilot":
        batch = "pro_pilot"
    return arm, instance, batch


def rows_for(path: Path) -> list[dict]:
    try:
        data = json.loads(path.read_text())
    except (json.JSONDecodeError, OSError) as exc:
        print(f"WARNING: skipping {path}: {exc}", file=sys.stderr)
        return []

    messages = data.get("messages", [])
    arm, instance, batch = _arm_instance_batch(path)
    meta = {
        "arm": arm,
        "instance": instance,
        "batch_id": batch,
        "model": _pro_model(messages, data),
        "run_id": parse_run_id(path, instance),
    }

    out: list[dict] = []
    for turn_idx, msg in enumerate(messages):
        if msg.get("role") != "assistant":
            continue
        m = BASH_RE.search(str(msg.get("content", "")))
        if not m:
            continue
        command = m.group(1).strip()
        output = ""
        forced_rc = None
        nxt = messages[turn_idx + 1] if turn_idx + 1 < len(messages) else None
        if nxt and nxt.get("role") == "user":
            nxt_content = str(nxt.get("content", ""))
            if "<returncode>" in nxt_content:
                output = nxt_content
            elif _TIMEOUT_RE.search(nxt_content):
                # Harness killed the command: no <returncode> tag, but the
                # partial output is still shown to the agent. Use 124 (the
                # standard timeout exit code) so is_error fires correctly.
                output = nxt_content
                forced_rc = 124
        out.append(build_command_row(meta, turn_idx, 0, command, output, forced_rc))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--logs", type=Path, default=paths.LOGS_DIR / "pro_pilot",
                    help=f"directory of Pro *.traj.json files "
                         f"(default: {paths.LOGS_DIR / 'pro_pilot'})")
    ap.add_argument("--out", type=Path, required=True,
                    help="output CSV (e.g. results/pro_runs/<batch>/qi_commands.csv)")
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
    if not rows:
        print("No commands found.", file=sys.stderr)
        return 1

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_FIELDS)
        writer.writeheader()
        writer.writerows(rows)

    n_runs = len({(r["model"], r["arm"], r["instance"], r["run_id"]) for r in rows})
    n_qi = sum(1 for r in rows if r["tool"] == "qi")
    print(f"Wrote {len(rows)} command(s) from {n_runs} run(s) "
          f"({n_qi} qi) -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
