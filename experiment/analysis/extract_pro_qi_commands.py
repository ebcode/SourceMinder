#!/usr/bin/env python3
"""Extract one row per shell command from SWE-bench **Pro** trajectories.

Pro analog of extract_qi_commands.py. The per-command analysis (qi flag
vocabulary, zero-result rate, antipatterns) is identical; only the trajectory
PARSING differs, because Pro and Verified store actions differently:

  Verified: each assistant message carries ``extra.actions`` and tool output
            arrives in a ``role == "tool"`` message keyed by ``tool_call_id``.
  Pro:      the command is a ```bash block inside the assistant message's
            markdown content, and its output is the NEXT ``role == "user"``
            message (the action_observation_template, wrapping <returncode>).

This script parses the Pro layout and emits the SAME CSV schema
(``CSV_FIELDS``) via the shared ``build_command_row`` -- so report_qi_commands.py
(and report_pro_qi_commands.py) read either experiment's output unchanged.

Usage:
  experiment/.venv_pro/bin/python experiment/analysis/extract_pro_qi_commands.py \
      --logs experiment/logs/deepseek--deepseek-v4-flash/pro_pilot_ansible_n40 \
      --out  experiment/results/pro_runs/<batch>/qi_commands.csv
"""
from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # -> experiment/
from lib import paths  # noqa: E402
from analysis.extract_qi_commands import (  # noqa: E402
    CSV_FIELDS, build_command_row,
)
from analysis.analyze_pro_trajectories import (  # noqa: E402
    BASH_RE, norm_model, parse_run_id,
)


def _pro_model(messages: list[dict], data: dict) -> str:
    """Normalized model id, from the API's own per-message accounting (Pro
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
    # batch is the dir above the arm, unless that's the logs root or a model dir
    # holding the arm directly (no batch). pro_pilot is the default, not a batch.
    above = path.parent.parent.parent.name
    batch = "" if above in ("logs", "pro_pilot") else above
    # logs/pro_pilot/<arm>/... -> tag the default batch so rows are groupable.
    if above == "pro_pilot":
        batch = "pro_pilot"
    return arm, instance, batch


def rows_for_pro(path: Path) -> list[dict]:
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
        # The command's output is the next user message (the observation), which
        # wraps a <returncode> block; ignore anything else (e.g. a format-error
        # reprompt) so output isn't mis-paired.
        output = ""
        nxt = messages[turn_idx + 1] if turn_idx + 1 < len(messages) else None
        if (nxt and nxt.get("role") == "user"
                and "<returncode>" in str(nxt.get("content", ""))):
            output = str(nxt.get("content", ""))
        out.append(build_command_row(meta, turn_idx, 0, command, output))
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
        rows.extend(rows_for_pro(p))
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
