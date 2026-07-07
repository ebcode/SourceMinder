#!/usr/bin/env python3
"""
traj-diff: Compare two mini-swe-agent trajectory files side by side.
Usage: python traj_diff.py <traj1.json> <traj2.json>
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # -> experiment/
from lib import cmds


def load_traj(path: Path) -> dict:
    data = json.loads(Path(path).read_text())
    info = data["info"]
    turns = []
    cum_prompt = cum_completion = 0
    for msg in data["messages"]:
        if msg["role"] != "assistant":
            continue
        usage = msg.get("extra", {}).get("response", {}).get("usage", {})
        cum_prompt += usage.get("prompt_tokens", 0)
        cum_completion += usage.get("completion_tokens", 0)
        for action in msg.get("extra", {}).get("actions", []):
            cmd = action.get("command", "")
            turns.append({"n": len(turns) + 1, "cmd": cmd,
                          "cum_prompt": cum_prompt, "qi": cmds.uses_qi(cmd)})

    submission = info.get("submission") or ""
    patch_lines = sum(1 for line in submission.splitlines()
                      if line.startswith(("+", "-")) and not line.startswith(("+++", "---")))
    parts = Path(path).parts
    label = (f"{parts[-3]}/{parts[-1].replace('.traj.json', '')}"
             if len(parts) >= 3 else Path(path).stem)

    return {
        "label": label,
        "exit_status": info["exit_status"],
        "api_calls": info["model_stats"]["api_calls"],
        "step_limit": info["config"]["agent"]["step_limit"],
        "total_prompt": cum_prompt,
        "total_completion": cum_completion,
        "qi_calls": sum(1 for turn in turns if turn["qi"]),
        "has_patch": bool(submission.strip()),
        "patch_lines": patch_lines,
        "turns": turns,
    }


def pct(new, old):
    return f"{(new - old) / old * 100:+.0f}%" if old else "N/A"


def traj_diff(path1: Path, path2: Path) -> None:
    left, right = load_traj(path1), load_traj(path2)
    W = 24

    print(f'\n{"":30} {left["label"]:>{W}} {right["label"]:>{W}}')
    print("─" * (30 + W * 2 + 2))

    for label, lval, rval in [
        ("exit_status",       left["exit_status"],   right["exit_status"]),
        ("turns / limit",     f"{left['api_calls']}/{left['step_limit']}",
                              f"{right['api_calls']}/{right['step_limit']}"),
        ("prompt tokens",     f"{left['total_prompt']:,}",
                              f"{right['total_prompt']:,}  {pct(right['total_prompt'], left['total_prompt'])}"),
        ("completion tokens", f"{left['total_completion']:,}",
                              f"{right['total_completion']:,}  {pct(right['total_completion'], left['total_completion'])}"),
        ("qi calls",          str(left["qi_calls"]), str(right["qi_calls"])),
        ("patch produced",    "yes" if left["has_patch"] else "no",
                              "yes" if right["has_patch"] else "no"),
        ("patch diff lines",  str(left["patch_lines"]), str(right["patch_lines"])),
    ]:
        print(f"{label:30} {lval:>{W}} {rval:>{W}}")

    # Token growth curve
    print("\nTOKEN GROWTH  (cumulative prompt tokens, every 10 turns)")
    print(f"  {'turn':>5}  {'A':>14}  {'B':>14}  {'B-A':>10}")
    n = max(len(left["turns"]), len(right["turns"]))
    seen = set()
    for i in sorted(set(list(range(0, n, 10)) + [n - 1])):
        if i < 0 or i in seen:
            continue
        seen.add(i)
        lcum = left["turns"][i]["cum_prompt"] if i < len(left["turns"]) else left["turns"][-1]["cum_prompt"]
        rcum = right["turns"][i]["cum_prompt"] if i < len(right["turns"]) else right["turns"][-1]["cum_prompt"]
        print(f"  {i+1:>5}  {lcum:>14,}  {rcum:>14,}  {rcum-lcum:>+10,}")

    # Qi calls in B (the right-hand trajectory)
    qi_turns = [turn for turn in right["turns"] if turn["qi"]]
    if qi_turns:
        print(f"\nQI CALLS in {right['label']}  ({len(qi_turns)} total)")
        for turn in qi_turns:
            print(f"  turn {turn['n']:>3}  ctx {turn['cum_prompt']:>10,}  {turn['cmd'][:72]}")

    # Last 8 commands — spot stuck-in-loop
    TAIL = 8
    ltail, rtail = left["turns"][-TAIL:], right["turns"][-TAIL:]
    CW = 46
    print(f"\nLAST {TAIL} COMMANDS")
    print(f"  {'A':<{CW}}  {'B':<{CW}}")
    for i in range(max(len(ltail), len(rtail))):
        lcmd = ltail[i]["cmd"][:CW] if i < len(ltail) else ""
        rcmd = rtail[i]["cmd"][:CW] if i < len(rtail) else ""
        qi_mark = "*" if i < len(rtail) and rtail[i]["qi"] else " "
        print(f"  {lcmd:<{CW}} {qi_mark}{rcmd:<{CW}}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("traj_a", type=Path, help="first trajectory (column A)")
    ap.add_argument("traj_b", type=Path, help="second trajectory (column B)")
    args = ap.parse_args()
    traj_diff(args.traj_a, args.traj_b)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
