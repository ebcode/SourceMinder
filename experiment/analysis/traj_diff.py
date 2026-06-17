#!/usr/bin/env python3
"""
traj-diff: Compare two mini-swe-agent trajectory files side by side.
Usage: python traj_diff.py <traj1.json> <traj2.json>
"""

import json
import re
import sys
from pathlib import Path

_QI_RE = re.compile(r"(?:^|&&\s*)qi\b")


def load_traj(path):
    t = json.load(open(path))
    info = t["info"]
    turns = []
    cum_prompt = cum_completion = 0
    for m in t["messages"]:
        if m["role"] != "assistant":
            continue
        usage = m.get("extra", {}).get("response", {}).get("usage", {})
        cum_prompt += usage.get("prompt_tokens", 0)
        cum_completion += usage.get("completion_tokens", 0)
        for act in m.get("extra", {}).get("actions", []):
            cmd = act.get("command", "")
            turns.append({"n": len(turns) + 1, "cmd": cmd,
                          "cum_prompt": cum_prompt, "qi": bool(_QI_RE.search(cmd))})

    sub = info.get("submission") or ""
    patch_lines = sum(1 for l in sub.splitlines()
                      if l.startswith(("+", "-")) and not l.startswith(("+++", "---")))
    parts = Path(path).parts
    label = f"{parts[-3]}/{parts[-1].replace('.traj.json','')}" if len(parts) >= 3 else Path(path).stem

    return {
        "label": label,
        "exit_status": info["exit_status"],
        "api_calls": info["model_stats"]["api_calls"],
        "step_limit": info["config"]["agent"]["step_limit"],
        "total_prompt": cum_prompt,
        "total_completion": cum_completion,
        "qi_calls": sum(1 for t in turns if t["qi"]),
        "has_patch": bool(sub.strip()),
        "patch_lines": patch_lines,
        "turns": turns,
    }


def pct(new, old):
    return f"{(new - old) / old * 100:+.0f}%" if old else "N/A"


def traj_diff(path1, path2):
    a, b = load_traj(path1), load_traj(path2)
    W = 24

    print(f'\n{"":30} {a["label"]:>{W}} {b["label"]:>{W}}')
    print("─" * (30 + W * 2 + 2))

    for label, va, vb in [
        ("exit_status",       a["exit_status"],     b["exit_status"]),
        ("turns / limit",     f"{a['api_calls']}/{a['step_limit']}",
                              f"{b['api_calls']}/{b['step_limit']}"),
        ("prompt tokens",     f"{a['total_prompt']:,}",
                              f"{b['total_prompt']:,}  {pct(b['total_prompt'], a['total_prompt'])}"),
        ("completion tokens", f"{a['total_completion']:,}",
                              f"{b['total_completion']:,}  {pct(b['total_completion'], a['total_completion'])}"),
        ("qi calls",          str(a["qi_calls"]),   str(b["qi_calls"])),
        ("patch produced",    "yes" if a["has_patch"] else "no",
                              "yes" if b["has_patch"] else "no"),
        ("patch diff lines",  str(a["patch_lines"]), str(b["patch_lines"])),
    ]:
        print(f"{label:30} {va:>{W}} {vb:>{W}}")

    # Token growth curve
    print(f"\nTOKEN GROWTH  (cumulative prompt tokens, every 10 turns)")
    print(f"  {'turn':>5}  {'A':>14}  {'B':>14}  {'B-A':>10}")
    n = max(len(a["turns"]), len(b["turns"]))
    seen = set()
    for i in sorted(set(list(range(0, n, 10)) + [n - 1])):
        if i < 0 or i in seen:
            continue
        seen.add(i)
        ca = a["turns"][i]["cum_prompt"] if i < len(a["turns"]) else a["turns"][-1]["cum_prompt"]
        cb = b["turns"][i]["cum_prompt"] if i < len(b["turns"]) else b["turns"][-1]["cum_prompt"]
        print(f"  {i+1:>5}  {ca:>14,}  {cb:>14,}  {cb-ca:>+10,}")

    # Qi calls in B
    qi = [t for t in b["turns"] if t["qi"]]
    if qi:
        print(f"\nQI CALLS in {b['label']}  ({len(qi)} total)")
        for t in qi:
            print(f"  turn {t['n']:>3}  ctx {t['cum_prompt']:>10,}  {t['cmd'][:72]}")

    # Last 8 commands — spot stuck-in-loop
    TAIL = 8
    at, bt = a["turns"][-TAIL:], b["turns"][-TAIL:]
    CW = 46
    print(f"\nLAST {TAIL} COMMANDS")
    print(f"  {'A':<{CW}}  {'B':<{CW}}")
    for i in range(max(len(at), len(bt))):
        ca = at[i]["cmd"][:CW] if i < len(at) else ""
        cb = bt[i]["cmd"][:CW] if i < len(bt) else ""
        qi_mark = "*" if i < len(bt) and bt[i]["qi"] else " "
        print(f"  {ca:<{CW}} {qi_mark}{cb:<{CW}}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("usage: traj_diff.py <traj1.json> <traj2.json>", file=sys.stderr)
        sys.exit(1)
    traj_diff(sys.argv[1], sys.argv[2])
