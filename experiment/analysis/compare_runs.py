#!/usr/bin/env python3
"""Side-by-side comparison of two .traj.json files.

Usage:
  python3 experiment/analysis/compare_runs.py control.traj.json treatment.traj.json
"""

import json
import re
import sys
from pathlib import Path


def extract(path: Path) -> dict:
    d = json.loads(path.read_text())
    msgs = d["messages"]
    info = d["info"]

    turns = sum(1 for m in msgs if m["role"] == "assistant")
    exit_status = info.get("exit_status", "?")
    submitted = bool((info.get("submission") or "").strip())
    patch_lines = len((info.get("submission") or "").splitlines()) if submitted else 0

    prompt = completion = reasoning = cached = 0
    qi_calls = grep_calls = read_calls = 0
    timeline: list[str] = []

    for m in msgs:
        if m["role"] != "assistant":
            continue
        extra = m.get("extra", {})
        usage = (extra.get("response") or {}).get("usage", {})
        if usage:
            prompt += usage.get("prompt_tokens", 0)
            completion += usage.get("completion_tokens", 0)
            reasoning += (usage.get("completion_tokens_details") or {}).get("reasoning_tokens", 0)
            cached += (usage.get("prompt_tokens_details") or {}).get("cached_tokens", 0)
        for a in extra.get("actions", []):
            cmd = a.get("command", "")
            short = cmd.replace("cd /testbed && ", "")
            if re.search(r'\bqi\b', short):
                qi_calls += 1
                timeline.append(f"qi: {short[:80]}")
            elif re.search(r'\b(grep|rg)\b', short):
                grep_calls += 1
                timeline.append(f"grep: {short[:80]}")
            elif re.search(r'\b(cat|head|tail|sed|find|python|ls)\b', short):
                read_calls += 1
                timeline.append(f"read: {short[:80]}")
            else:
                timeline.append(f"bash: {short[:80]}")

    return {
        "turns": turns,
        "exit_status": exit_status,
        "submitted": submitted,
        "patch_lines": patch_lines,
        "input_tokens": prompt,
        "completion_tokens": completion,
        "reasoning_tokens": reasoning,
        "cached_tokens": cached,
        "qi_calls": qi_calls,
        "grep_calls": grep_calls,
        "read_calls": read_calls,
        "timeline": timeline,
    }


def fmt(n: int) -> str:
    return f"{n:,}"


def side_by_side(left: str, right: str, width: int = 28) -> str:
    return f"{left:<{width}}  {right}"


HEADER = 28

def main() -> None:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <ctrl.traj.json> <trt.traj.json>", file=sys.stderr)
        sys.exit(1)

    ctrl = extract(Path(sys.argv[1]))
    trt  = extract(Path(sys.argv[2]))

    print(side_by_side("", "control", HEADER) + "  treatment")
    print(side_by_side("", "-------", HEADER) + "  --------")
    print(side_by_side("Turns:", str(ctrl["turns"]), HEADER) + "  " + str(trt["turns"]))
    print(side_by_side("Exit:", ctrl["exit_status"], HEADER) + "  " + trt["exit_status"])
    print(side_by_side("Submitted:", "yes" if ctrl["submitted"] else "no", HEADER) + "  " + ("yes" if trt["submitted"] else "no"))
    print(side_by_side("Patch lines:", str(ctrl["patch_lines"]), HEADER) + "  " + str(trt["patch_lines"]))
    print()
    print(side_by_side("Input tokens:", fmt(ctrl["input_tokens"]), HEADER) + "  " + fmt(trt["input_tokens"]))
    print(side_by_side("Completion tokens:", fmt(ctrl["completion_tokens"]), HEADER) + "  " + fmt(trt["completion_tokens"]))
    print(side_by_side("Reasoning tokens:", fmt(ctrl["reasoning_tokens"]), HEADER) + "  " + fmt(trt["reasoning_tokens"]))
    print(side_by_side("Cached tokens:", fmt(ctrl["cached_tokens"]), HEADER) + "  " + fmt(trt["cached_tokens"]))
    print()
    print(side_by_side("Tool calls:", f"qi {ctrl['qi_calls']}  grep {ctrl['grep_calls']}  read {ctrl['read_calls']}", HEADER)
          + f"  qi {trt['qi_calls']}  grep {trt['grep_calls']}  read {trt['read_calls']}")

    # Tool call timeline
    print(f"\n{'─'*70}")
    print("Tool call timeline")
    print(f"{'─'*70}")
    max_len = max(len(ctrl["timeline"]), len(trt["timeline"]))
    for i in range(max_len):
        c = ctrl["timeline"][i] if i < len(ctrl["timeline"]) else ""
        t = trt["timeline"][i] if i < len(trt["timeline"]) else ""
        print(side_by_side(f"{i+1:2d}. {c[:55]}", "", 60) + t[:60])


if __name__ == "__main__":
    main()
