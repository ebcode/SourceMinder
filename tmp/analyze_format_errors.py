#!/usr/bin/env python3
"""Dig into format errors: what was the model doing wrong, and how did it recover?"""

import json
from collections import Counter

BASE = "experiment/logs/xiaomi_mimo--mimo-v2.5-pro/pro_pilot_ansible_mimo_v2.5-pro"

for arm in ["swebp_control", "swebp_treatment"]:
    path = (
        f"{BASE}/{arm}/"
        "instance_ansible__ansible-6cc97447aac5816745278f3735af128afb255c81-v0f01c69f1e2528b935359cfe578530722bca2c59/"
        "instance_ansible__ansible-6cc97447aac5816745278f3735af128afb255c81-v0f01c69f1e2528b935359cfe578530722bca2c59.rep01.traj.json"
    )
    with open(path) as f:
        data = json.load(f)
    msgs = data["messages"]

    err_msg = "Please always provide EXACTLY ONE action in triple backticks"

    # Collect format errors with preceding assistant context
    fmt_errors = []
    for i, m in enumerate(msgs):
        if m["role"] != "user" or i == 1:
            continue
        if err_msg in m["content"]:
            prev = None
            for k in range(i - 1, -1, -1):
                if msgs[k]["role"] == "assistant":
                    prev = msgs[k]["content"]
                    break
            fmt_errors.append({"msg_idx": i, "prev_asst": prev or ""})

    print(f"{'=' * 60}")
    print(f"  {arm.upper()}")
    print(f"{'=' * 60}")
    print(f"  Total format errors: {len(fmt_errors)}")
    print(f"  Total turns: {len([m for m in msgs if m['role'] == 'assistant'])}")

    # Categorize what the model emitted instead of a valid backtick-bash block
    wrong = Counter()
    samples = {k: [] for k in [
        "no_code_block",
        "xml_command_tag",
        "multiple_blocks",
        "no_bash_label",
        "bash_but_rejected",
        "only_thought",
        "other",
    ]}

    for fe in fmt_errors:
        content = fe["prev_asst"]

        has_triple = "```" in content
        has_xml = "<command>" in content.lower()

        if not has_triple and not has_xml:
            wrong["no_code_block_at_all"] += 1
            if len(samples["no_code_block"]) < 2:
                samples["no_code_block"].append(content[:300])
        elif has_xml and not has_triple:
            wrong["xml_command_tag"] += 1
            if len(samples["xml_command_tag"]) < 2:
                samples["xml_command_tag"].append(content[:300])
        elif has_triple:
            bt_count = content.count("```")
            if bt_count >= 4:
                wrong["multiple_code_blocks"] += 1
            elif "```bash" in content:
                wrong["bash_label_but_rejected"] += 1
                if len(samples["bash_but_rejected"]) < 2:
                    # Show the bash block
                    start = content.index("```bash")
                    samples["bash_but_rejected"].append(content[start:start + 300])
            elif "```" in content:
                # Has backticks but no bash label
                bt_idx = content.index("```")
                # What label was used?
                snippet = content[bt_idx : bt_idx + 100]
                label = snippet.split("\n")[0].strip()
                wrong[f"no_bash_label__{label}"] += 1
                if len(samples["no_bash_label"]) < 2:
                    samples["no_bash_label"].append(content[bt_idx : bt_idx + 300])
        else:
            # Has text but no backticks or xml
            wrong["text_only_no_command"] += 1

    print(f"\n  Error categories:")
    for pat, count in wrong.most_common():
        pct = count / len(fmt_errors) * 100
        print(f"    {pat:35s} {count:3d}  ({pct:5.1f}%)")

    # Show samples
    for cat, exs in samples.items():
        if exs:
            print(f"\n  Sample [{cat}]:")
            for idx, ex in enumerate(exs):
                print(f"    [{idx+1}] {ex[:250]}")
                print()

    # Turn positions
    turn_map = {}
    turn_num = 0
    for i, m in enumerate(msgs):
        if m["role"] == "assistant":
            turn_num += 1
        turn_map[i] = turn_num

    error_turns = sorted(set(turn_map.get(fe["msg_idx"], 0) for fe in fmt_errors))
    print(f"  Error turn positions (first 30): {error_turns[:30]}")
    print(f"  Error turn positions (last 10):  {error_turns[-10:]}")

    # Gap analysis
    if len(error_turns) >= 2:
        gaps = [error_turns[i + 1] - error_turns[i] for i in range(len(error_turns) - 1)]
        print(f"\n  Gap stats: min={min(gaps)}, max={max(gaps)}, mean={sum(gaps)/len(gaps):.1f}")
        tight = sum(1 for g in gaps if g <= 2)
        print(f"  Errors with <=2 turns since previous: {tight}/{len(gaps)}")

        # Consecutive error runs
        runs_list = []
        curr_start = error_turns[0]
        for i in range(1, len(error_turns)):
            if error_turns[i] - error_turns[i - 1] > 1:
                runs_list.append(curr_start)
                runs_list.append(error_turns[i - 1])
                curr_start = error_turns[i]
        runs_list.append(curr_start)
        runs_list.append(error_turns[-1])
        # runs_list is [s1, e1, s2, e2, ...]
        run_lengths = [(runs_list[i], runs_list[i+1] - runs_list[i] + 1) for i in range(0, len(runs_list), 2)]
        max_run = max(run_lengths, key=lambda x: x[1])
        print(f"  Consecutive error runs: {[rl for _, rl in run_lengths]}")
        print(f"  Longest consecutive error run: {max_run[1]} turns (starts at turn {max_run[0]})")

        # Error density over time
        thirds = len(error_turns) // 3
        first = error_turns[:thirds]
        mid = error_turns[thirds : 2 * thirds]
        last = error_turns[2 * thirds :]
        print(f"\n  Error distribution by third:")
        print(f"    First 1/3:  {len(first)} errors (turns {first[0] if first else '?'}-{first[-1] if first else '?'})")
        print(f"    Middle 1/3: {len(mid)} errors (turns {mid[0] if mid else '?'}-{mid[-1] if mid else '?'})")
        print(f"    Final 1/3:  {len(last)} errors (turns {last[0] if last else '?'}-{last[-1] if last else '?'})")

    # Turn efficiency
    total_turns = len([m for m in msgs if m["role"] == "assistant"])
    wasted = len(fmt_errors)  # each error wastes at least 1 turn (the one that was rejected)
    pct_wasted = wasted / total_turns * 100
    print(f"\n  Turn waste: {wasted}/{total_turns} turns wasted ({pct_wasted:.1f}%)")
    effective = total_turns - wasted
    print(f"  Effective turns: {effective}/{total_turns}")

    print()
