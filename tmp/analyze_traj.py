#!/usr/bin/env python3
"""Statistical analysis of .traj.json files comparing control vs treatment arms."""

import json
import re
import sys
from collections import Counter
from pathlib import Path

BASE = Path("experiment/logs/xiaomi_mimo--mimo-v2.5-pro/pro_pilot_ansible_mimo_v2.5-pro")

TRAJ_PATHS = {
    "control": BASE / "swebp_control" / "instance_ansible__ansible-6cc97447aac5816745278f3735af128afb255c81-v0f01c69f1e2528b935359cfe578530722bca2c59" / "instance_ansible__ansible-6cc97447aac5816745278f3735af128afb255c81-v0f01c69f1e2528b935359cfe578530722bca2c59.rep01.traj.json",
    "treatment": BASE / "swebp_treatment" / "instance_ansible__ansible-6cc97447aac5816745278f3735af128afb255c81-v0f01c69f1e2528b935359cfe578530722bca2c59" / "instance_ansible__ansible-6cc97447aac5816745278f3735af128afb255c81-v0f01c69f1e2528b935359cfe578530722bca2c59.rep01.traj.json",
}


def analyze(path, label):
    with open(path) as f:
        data = json.load(f)

    msgs = data["messages"]
    info = data["info"]

    # --- 1. Turn count and exit ---
    user_msgs = [m for m in msgs if m["role"] == "user"]
    asst_msgs = [m for m in msgs if m["role"] == "assistant"]
    turns = len(asst_msgs)

    exit_status = info.get("exit_status", "???")
    submission = info.get("submission", "")
    solved = "Submitted" in exit_status

    # --- 2. Format / parse errors ---
    format_errors = 0
    parse_errors = 0
    error_samples = []
    error_turn_indices = []

    for i, m in enumerate(msgs):
        if m["role"] != "user":
            continue
        content = m["content"]
        lo = content.lower()

        is_format = any(
            kw in lo
            for kw in [
                "invalid action format",
                "please use",
                "enclose your bash action",
                "action could not be parsed",
                "no action found",
            ]
        )
        is_parse = any(
            kw in lo
            for kw in [
                "syntax error",
                "parse error",
                "bash:",
                "unexpected token",
                "command not found",
                "no such file",
                "cannot execute",
            ]
        )

        if is_format:
            format_errors += 1
            error_turn_indices.append(i)
            # Get the preceding assistant message
            prev = None
            for k in range(i - 1, -1, -1):
                if msgs[k]["role"] == "assistant":
                    prev = msgs[k]["content"]
                    break
            if len(error_samples) < 5:
                error_samples.append(
                    {"type": "format", "user": content[:400], "assistant": prev[:400] if prev else ""}
                )

        if is_parse:
            parse_errors += 1

    # --- 3. Command format patterns in assistant messages ---
    cmd_formats = Counter()
    cmd_format_history = []  # (turn_index, format_type)
    for idx, m in enumerate(asst_msgs):
        content = m["content"]
        if "```bash" in content:
            cmd_formats["backtick_bash"] += 1
            cmd_format_history.append((idx, "backtick_bash"))
        elif "<command>" in content.lower():
            cmd_formats["xml_command"] += 1
            cmd_format_history.append((idx, "xml_command"))
        elif "```sh" in content:
            cmd_formats["backtick_sh"] += 1
            cmd_format_history.append((idx, "backtick_sh"))
        elif "```" in content:
            cmd_formats["backtick_other"] += 1
            cmd_format_history.append((idx, "backtick_other"))
        else:
            cmd_formats["no_backtick_or_tag"] += 1
            cmd_format_history.append((idx, "no_detectable_command"))

    # --- 4. Content length stats ---
    asst_lens = [len(m["content"]) for m in asst_msgs]
    user_lens = [len(m["content"]) for m in user_msgs[1:]]  # skip system prompt

    # --- 5. Turn-by-turn: when do errors cluster? ---
    error_clusters = []
    if error_turn_indices:
        cluster_start = error_turn_indices[0]
        cluster_end = error_turn_indices[0]
        for ei in error_turn_indices[1:]:
            if ei - cluster_end <= 10:  # within 10 messages
                cluster_end = ei
            else:
                error_clusters.append((cluster_start, cluster_end))
                cluster_start = ei
                cluster_end = ei
        error_clusters.append((cluster_start, cluster_end))

    # --- 6. Turn position analysis for xml vs backtick ---
    xml_turns = [idx for idx, fmt in cmd_format_history if fmt == "xml_command"]
    bt_turns = [idx for idx, fmt in cmd_format_history if fmt == "backtick_bash"]
    format_errors_at = set()
    for i in range(len(msgs)):
        if msgs[i]["role"] == "user":
            lo = msgs[i]["content"].lower()
            if "invalid action format" in lo or "please use" in lo:
                # Find which assistant turn this was a response to
                asst_idx = None
                for k in range(i - 1, -1, -1):
                    if msgs[k]["role"] == "assistant":
                        asst_idx = len([m for m in msgs[:k] if m["role"] == "assistant"])
                        break
                if asst_idx is not None:
                    format_errors_at.add(asst_idx)

    # Print results
    print(f"{'=' * 60}")
    print(f"  {label.upper()}")
    print(f"{'=' * 60}")
    print(f"  Exit status:     {exit_status}")
    print(f"  Solved:          {'YES' if solved else 'NO'}")
    print(f"  Total turns:     {turns}")
    print(f"  Total messages:  {len(msgs)} (system=1, user={len(user_msgs)}, assistant={turns})")

    print(f"\n  --- Errors ---")
    print(f"  Format errors (wrong command syntax):     {format_errors}")
    print(f"  Parse/execution errors (bash failures):   {parse_errors}")
    error_rate = format_errors / turns * 100 if turns > 0 else 0
    print(f"  Format error rate:                        {error_rate:.1f}% of turns")

    print(f"\n  --- Command Format Distribution ---")
    for fmt, count in cmd_formats.most_common():
        pct = count / turns * 100 if turns > 0 else 0
        print(f"    {fmt:30s} {count:4d}  ({pct:5.1f}%)")

    if xml_turns:
        print(f"\n  --- XML <command> turns: {len(xml_turns)} ---")
        xml_errors = len([t for t in xml_turns if t in format_errors_at])
        print(f"    XML turns that got format errors: {xml_errors}/{len(xml_turns)}")
        # Distribution: early vs late
        first_third = xml_turns[: max(1, len(xml_turns) // 3)]
        last_third = xml_turns[-max(1, len(xml_turns) // 3) :]
        print(f"    First 1/3 XML turns: {first_third[:10]}{'...' if len(first_third) > 10 else ''}")
        print(f"    Last  1/3 XML turns: {last_third[:10]}{'...' if len(last_third) > 10 else ''}")

    if bt_turns:
        print(f"\n  --- Backtick-bash turns: {len(bt_turns)} ---")
        bt_errors = len([t for t in bt_turns if t in format_errors_at])
        print(f"    Backtick turns that got format errors: {bt_errors}/{len(bt_turns)}")

    print(f"\n  --- Content Length Stats ---")
    if asst_lens:
        print(f"    Assistant: min={min(asst_lens)}, max={max(asst_lens)}, "
              f"mean={sum(asst_lens)/len(asst_lens):.0f}, median={sorted(asst_lens)[len(asst_lens)//2]}")
    if user_lens:
        print(f"    User obs:  min={min(user_lens)}, max={max(user_lens)}, "
              f"mean={sum(user_lens)/len(user_lens):.0f}, median={sorted(user_lens)[len(user_lens)//2]}")

    print(f"\n  --- Error Clusters ---")
    print(f"  Number of error clusters: {len(error_clusters)}")
    for cs, ce in error_clusters:
        print(f"    Messages {cs}-{ce} ({ce - cs + 1} span)")

    if error_samples:
        print(f"\n  --- Sample Format Errors ---")
        for idx, ex in enumerate(error_samples):
            print(f"  [{idx + 1}] Assistant said:")
            # Find the command pattern in assistant
            asst = ex["assistant"]
            cmd_match = re.search(r"(```[\s\S]*?```|<command>[\s\S]*?</command>)", asst)
            if cmd_match:
                print(f"      {cmd_match.group(0)[:300]}")
            else:
                print(f"      {asst[:200]}")
            print(f"      -> Response: {ex['user'][:300]}")
            print()

    # --- 7. Did the model ever learn / switch formats? ---
    # Track when the model switched between backtick and XML
    format_switches = 0
    prev_fmt = None
    for _, fmt in cmd_format_history:
        if prev_fmt and prev_fmt != fmt:
            format_switches += 1
        prev_fmt = fmt
    print(f"\n  --- Format Stability ---")
    print(f"  Format switches: {format_switches}")

    # Track streaks
    streaks = []
    if cmd_format_history:
        curr_fmt = cmd_format_history[0][1]
        curr_len = 1
        for _, fmt in cmd_format_history[1:]:
            if fmt == curr_fmt:
                curr_len += 1
            else:
                streaks.append((curr_fmt, curr_len))
                curr_fmt = fmt
                curr_len = 1
        streaks.append((curr_fmt, curr_len))
    print(f"  Format streaks: ")
    for fmt, length in streaks:
        print(f"    {fmt}: {length} turns")

    print()
    return {
        "label": label,
        "turns": turns,
        "solved": solved,
        "format_errors": format_errors,
        "parse_errors": parse_errors,
        "cmd_formats": cmd_formats,
        "xml_turns": xml_turns,
        "bt_turns": bt_turns,
        "format_switches": format_switches,
        "asst_lens": asst_lens,
    }


def main():
    results = {}
    for label in ["control", "treatment"]:
        path = TRAJ_PATHS[label]
        if path.exists():
            results[label] = analyze(path, label)
        else:
            print(f"MISSING: {path}")

    # --- Comparative summary ---
    if len(results) == 2:
        c = results["control"]
        t = results["treatment"]
        print(f"{'=' * 60}")
        print(f"  COMPARISON: CONTROL vs TREATMENT")
        print(f"{'=' * 60}")
        print(f"  Turns:         {c['turns']:4d}  vs  {t['turns']:4d}  (delta: {t['turns'] - c['turns']:+d})")
        print(f"  Solved:        {str(c['solved']):>5s}  vs  {str(t['solved']):>5s}")
        print(f"  Format errors: {c['format_errors']:4d}  vs  {t['format_errors']:4d}  (delta: {t['format_errors'] - c['format_errors']:+d})")
        print(f"  Parse errors:  {c['parse_errors']:4d}  vs  {t['parse_errors']:4d}  (delta: {t['parse_errors'] - c['parse_errors']:+d})")
        print(f"  XML turns:     {len(c['xml_turns']):4d}  vs  {len(t['xml_turns']):4d}  (delta: {len(t['xml_turns']) - len(c['xml_turns']):+d})")
        print(f"  BT  turns:     {len(c['bt_turns']):4d}  vs  {len(t['bt_turns']):4d}  (delta: {len(t['bt_turns']) - len(c['bt_turns']):+d})")
        print(f"  Fmt switches:  {c['format_switches']:4d}  vs  {t['format_switches']:4d}")

        # Wasted turns: format errors + likely recovery turns
        control_wasted = c["format_errors"] * 2  # each error wastes the error turn + a correction turn
        treatment_wasted = t["format_errors"] * 2
        control_productive = c["turns"] - control_wasted
        treatment_productive = t["turns"] - treatment_wasted
        print(f"\n  --- Turn Efficiency ---")
        print(f"  Est. wasted turns (2x format errors):")
        print(f"    Control:   {control_wasted} wasted, {control_productive} productive (of {c['turns']})")
        print(f"    Treatment: {treatment_wasted} wasted, {treatment_productive} productive (of {t['turns']})")

        # If treatment had control's efficiency
        if c["turns"] > 0:
            control_efficiency = (c["turns"] - c["format_errors"]) / c["turns"]
            treatment_efficiency = (t["turns"] - t["format_errors"]) / t["turns"] if t["turns"] > 0 else 0
            print(f"\n  Efficiency ratio (non-error turns / total):")
            print(f"    Control:   {control_efficiency:.1%}")
            print(f"    Treatment: {treatment_efficiency:.1%}")
            print(f"    Delta:     {treatment_efficiency - control_efficiency:+.1%}")

        # Time-to-submit equivalent
        print(f"\n  --- If treatment had control's format-error rate ---")
        if c["turns"] > 0:
            c_error_rate = c["format_errors"] / c["turns"]
            if t["turns"] > 0:
                expected_treatment_errors = c_error_rate * t["turns"]
                excess_errors = t["format_errors"] - expected_treatment_errors
                recoverable_turns = excess_errors  # each excess error could have been a productive turn
                print(f"    Control error rate: {c_error_rate:.3f} per turn")
                print(f"    Treatment expected errors at control rate: {expected_treatment_errors:.1f}")
                print(f"    Treatment actual: {t['format_errors']}")
                print(f"    Excess errors: {excess_errors:.1f}")
                if excess_errors > 0:
                    # Could these turns have made the difference?
                    print(f"    If each excess error was instead a productive turn,")
                    t_efficient_turns = t["turns"] - t["format_errors"] + excess_errors
                    print(f"    treatment would have had {t_efficient_turns:.0f} effective turns")
                    if t_efficient_turns >= c["turns"] - c["format_errors"]:
                        print(f"    => This WOULD reach or exceed control's productive turn count ({c['turns'] - c['format_errors']})")

        # Step budget analysis
        print(f"\n  --- Step Budget ---")
        cfg_c = None
        cfg_t = None
        for label, path in TRAJ_PATHS.items():
            if path.exists():
                with open(path) as f:
                    cfg = json.load(f)["info"].get("config", {})
                    agent_cfg = cfg.get("agent", {})
                    step_limit = agent_cfg.get("step_limit", "?")
                    print(f"    {label}: step_limit={step_limit}")
                    if label == "control":
                        cfg_c = agent_cfg
                    else:
                        cfg_t = agent_cfg
        print(f"    Control submitted on turn {c['turns']}")
        print(f"    Treatment exhausted budget at turn {t['turns']}")


if __name__ == "__main__":
    main()
