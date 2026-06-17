#!/usr/bin/env python3
"""Extract per-run metrics from mini-swe-agent trajectory files.

Walks a logs directory for ``*.traj.json`` files, pulls per-turn token usage
from each assistant message's ``extra.response.usage`` (the DeepSeek API's own
accounting), and writes one row per run to a CSV plus a by-arm summary.

Primary metrics (per PREREGISTRATION.md §7.1):
  - total_input_tokens   sum of prompt_tokens across all turns
  - peak_prompt_tokens   max prompt_tokens in any single turn (context pressure)
  - total_tool_output    approximate tokens of tool output shown to the model

Trajectory layout handled (both are accepted):
  logs/<arm>/<instance>/<run_id>.traj.json      (run_experiment.py)
  logs/<instance>_<arm>.traj.json               (legacy compare.sh)

Usage:
  python3 experiment/analysis/analyze_trajectories.py \
      --logs experiment/logs --dir experiment/analysis/20260616_223000
"""
from __future__ import annotations

import argparse
import csv
import json
import re
import statistics
import sys
import time
from pathlib import Path

# Tool-output tokens are not API-counted. The DeepSeek tokenizer is the exact
# answer (PREREGISTRATION Open Q4); until that is wired in we approximate at
# ~4 chars/token, which is adequate for the pilot's descriptive stats.
CHARS_PER_TOKEN = 4.0

ARMS = ("control", "treatment")
QI_RE = re.compile(r"(^|[\s;|&(])qi(\s|$)")
GREP_RE = re.compile(r"(^|[\s;|&(])(grep|rg|ag|ack)(\s|$)")
READ_RE = re.compile(r"(^|[\s;|&(])(cat|sed|head|tail|less|more)(\s|$)")


def infer_arm_instance(path: Path) -> tuple[str, str]:
    """Derive (arm, instance_id) from the path, preferring directory layout."""
    parts = path.parts
    arm = next((p for p in parts if p in ARMS), "")
    # nested layout: .../<arm>/<instance>/<run_id>.traj.json
    if arm and path.parent.parent.name == arm:
        return arm, path.parent.name
    # flat layout: <instance>_<arm>.traj.json
    stem = path.name.replace(".traj.json", "")
    for a in ARMS:
        if stem.endswith("_" + a):
            return a, stem[: -(len(a) + 1)]
    return arm, stem


def analyze_one(path: Path) -> dict | None:
    try:
        data = json.loads(path.read_text())
    except (json.JSONDecodeError, OSError) as exc:
        print(f"WARNING: skipping {path}: {exc}", file=sys.stderr)
        return None

    messages = data.get("messages", [])
    info = data.get("info", {})
    arm, instance = infer_arm_instance(path)

    prompt_toks: list[int] = []
    completion_toks: list[int] = []
    reasoning_toks = 0
    cached_toks = 0
    qi_n = grep_n = read_n = 0

    for msg in messages:
        extra = msg.get("extra")
        if isinstance(extra, dict):
            resp = extra.get("response")
            if isinstance(resp, dict) and isinstance(resp.get("usage"), dict):
                u = resp["usage"]
                prompt_toks.append(u.get("prompt_tokens", 0))
                completion_toks.append(u.get("completion_tokens", 0))
                reasoning_toks += (u.get("completion_tokens_details") or {}).get("reasoning_tokens", 0)
                cached_toks += (u.get("prompt_tokens_details") or {}).get("cached_tokens", 0)
            for action in extra.get("actions") or []:
                cmd = action.get("command", "") if isinstance(action, dict) else ""
                qi_n += len(QI_RE.findall(cmd))
                grep_n += len(GREP_RE.findall(cmd))
                read_n += len(READ_RE.findall(cmd))

    # Approximate tool-output tokens from the observation (role == "tool") msgs.
    tool_chars = sum(
        len(str(m.get("content", ""))) for m in messages if m.get("role") == "tool"
    )

    if not prompt_toks:
        print(f"WARNING: no usage data in {path}", file=sys.stderr)
        return None

    return {
        "run_id": path.name.replace(".traj.json", ""),
        "instance_id": instance,
        "arm": arm,
        "exit_status": info.get("exit_status", ""),
        "turn_count": len(prompt_toks),
        "total_input_tokens": sum(prompt_toks),
        "peak_prompt_tokens": max(prompt_toks),
        "total_completion_tokens": sum(completion_toks),
        "total_reasoning_tokens": reasoning_toks,
        "total_cached_tokens": cached_toks,
        "tool_output_tokens_approx": round(tool_chars / CHARS_PER_TOKEN),
        "qi_invocations": qi_n,
        "grep_invocations": grep_n,
        "file_read_invocations": read_n,
        "submitted": bool(info.get("submission")),
        "source": str(path),
    }


def summarize(rows: list[dict]) -> None:
    metrics = ("total_input_tokens", "peak_prompt_tokens", "tool_output_tokens_approx")
    print("\n=== Summary by arm ===")
    for arm in ARMS:
        arm_rows = [r for r in rows if r["arm"] == arm]
        if not arm_rows:
            continue
        print(f"\n[{arm}] n={len(arm_rows)} runs")
        for m in metrics:
            vals = [r[m] for r in arm_rows]
            med = statistics.median(vals)
            print(f"  {m:28s} median={med:>10,.0f}  min={min(vals):>9,}  max={max(vals):>9,}")
        qi = sum(r["qi_invocations"] for r in arm_rows)
        grep = sum(r["grep_invocations"] for r in arm_rows)
        print(f"  {'qi / grep invocations':28s} {qi} / {grep}")

    ctrl = {r["peak_prompt_tokens"] for r in rows if r["arm"] == "control"}
    treat = {r["peak_prompt_tokens"] for r in rows if r["arm"] == "treatment"}
    if ctrl and treat:
        cm = statistics.median([r["peak_prompt_tokens"] for r in rows if r["arm"] == "control"])
        tm = statistics.median([r["peak_prompt_tokens"] for r in rows if r["arm"] == "treatment"])
        if cm:
            print(f"\n  median peak prompt tokens: treatment vs control = {(tm - cm) / cm:+.1%}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--logs", default="experiment/logs", help="directory of *.traj.json files")
    ap.add_argument("--dir", type=Path, default=None,
                    help="Analysis output directory (default: analysis/<timestamp>/)")
    args = ap.parse_args()

    logs_dir = Path(args.logs)
    if not logs_dir.is_dir():
        print(f"ERROR: not a directory: {logs_dir}", file=sys.stderr)
        return 1

    out_dir = args.dir or Path(f"experiment/analysis/{time.strftime('%Y%m%d_%H%M%S')}")
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / "runs.csv"

    traj_files = sorted(logs_dir.rglob("*.traj.json"))
    if not traj_files:
        print(f"No *.traj.json files under {logs_dir}", file=sys.stderr)
        return 1

    rows = [r for r in (analyze_one(p) for p in traj_files) if r]
    if not rows:
        print("No analyzable runs found.", file=sys.stderr)
        return 1

    with out_path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    print(f"Wrote {len(rows)} run(s) -> {out_path}")
    summarize(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
