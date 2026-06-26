#!/usr/bin/env python3
"""Corrected analysis: XML tool_call may contain valid ```bash block and succeed."""

import json
import re
from collections import Counter

BASE = "experiment/logs/xiaomi_mimo--mimo-v2.5-pro/pro_pilot_ansible_mimo_v2.5-pro"
INST_DIR = "instance_ansible__ansible-6cc97447aac5816745278f3735af128afb255c81-v0f01c69f1e2528b935359cfe578530722bca2c59"
FNAME = f"{INST_DIR}.rep01.traj.json"

PATHS = {
    "control": f"{BASE}/swebp_control/{INST_DIR}/{FNAME}",
    "treatment": f"{BASE}/swebp_treatment/{INST_DIR}/{FNAME}",
}

ERR_MSG = "Please always provide EXACTLY ONE action in triple backticks"

BACKTICK_RE = re.compile(r"```bash\s*\n(.*?)\n```", re.DOTALL)

for arm in ["control", "treatment"]:
    with open(PATHS[arm]) as f:
        data = json.load(f)
    msgs = data["messages"]

    total_turns = len([m for m in msgs if m["role"] == "assistant"])

    # --- Per-turn classification ---
    turns_with_xml = 0
    xml_with_valid_bt = 0       # XML tool_call that CONTAINS a valid ```bash block
    xml_without_bt = 0          # XML tool_call WITHOUT a valid ```bash block
    xml_that_succeeded = 0      # XML + bt block that the harness accepted (no format error in user response)
    xml_that_failed = 0         # XML + bt block that the harness STILL rejected

    bt_without_xml = 0
    bt_without_xml_succeeded = 0
    bt_without_xml_failed = 0

    no_command_at_all = 0
    no_command_failed = 0

    format_errors_total = 0
    xml_wrong_cmd = Counter()   # What was inside XML that had no valid backtick?

    action_dist = Counter()

    for i, m in enumerate(msgs):
        if m["role"] != "assistant":
            continue

        content = m["content"]
        has_xml = "<tool_call>" in content.lower() or "<function" in content.lower()
        bt_blocks = BACKTICK_RE.findall(content)
        has_valid_bt = len(bt_blocks) >= 1

        # Check user response (next user message) for format error
        user_resp = None
        for k in range(i + 1, len(msgs)):
            if msgs[k]["role"] == "user":
                user_resp = msgs[k]["content"]
                break
        got_format_error = user_resp and ERR_MSG in user_resp
        if got_format_error:
            format_errors_total += 1

        if has_xml and has_valid_bt:
            turns_with_xml += 1
            xml_with_valid_bt += 1
            cmd = bt_blocks[0].strip()
            if cmd.startswith("sed "):
                action_dist["xml+bt_sed"] += 1
            elif cmd.startswith("cat ") or cmd.startswith("nl ") or cmd.startswith("head "):
                action_dist["xml+bt_view"] += 1
            elif cmd.startswith("ls ") or cmd.startswith("cd "):
                action_dist["xml+bt_nav"] += 1
            elif cmd.startswith("grep ") or cmd.startswith("rg "):
                action_dist["xml+bt_grep"] += 1
            elif cmd.startswith("qi "):
                action_dist["xml+bt_qi"] += 1
            elif cmd.startswith("python ") or cmd.startswith("pytest"):
                action_dist["xml+bt_test"] += 1
            elif cmd.startswith("echo ") or cmd.startswith("printf "):
                action_dist["xml+bt_echo"] += 1
            else:
                action_dist["xml+bt_other"] += 1
            if got_format_error:
                xml_that_failed += 1
            else:
                xml_that_succeeded += 1

        elif has_xml and not has_valid_bt:
            turns_with_xml += 1
            xml_without_bt += 1
            # What did it put instead?
            if "<parameter" in content.lower() or "<argument" in content.lower():
                xml_wrong_cmd["has_param_but_no_bt"] += 1
            else:
                xml_wrong_cmd["bare_xml_no_param"] += 1
            # Always a format error
            action_dist["xml_no_bt"] += 1

        elif not has_xml and has_valid_bt:
            bt_without_xml += 1
            cmd = bt_blocks[0].strip()
            if cmd.startswith("sed "):
                action_dist["bt_sed"] += 1
            elif cmd.startswith("cat ") or cmd.startswith("nl ") or cmd.startswith("head "):
                action_dist["bt_view"] += 1
            elif cmd.startswith("ls ") or cmd.startswith("cd "):
                action_dist["bt_nav"] += 1
            elif cmd.startswith("grep ") or cmd.startswith("rg "):
                action_dist["bt_grep"] += 1
            elif cmd.startswith("qi "):
                action_dist["bt_qi"] += 1
            elif cmd.startswith("python ") or cmd.startswith("pytest"):
                action_dist["bt_test"] += 1
            elif cmd.startswith("echo ") or cmd.startswith("printf "):
                action_dist["bt_echo"] += 1
            else:
                action_dist["bt_other"] += 1
            if got_format_error:
                bt_without_xml_failed += 1
            else:
                bt_without_xml_succeeded += 1

        else:
            no_command_at_all += 1
            if got_format_error:
                no_command_failed += 1
            action_dist["no_command"] += 1

    # --- Print ---
    print(f"{'='*65}")
    print(f"  {arm.upper()}")
    print(f"{'='*65}")

    print(f"\n  Total assistant turns: {total_turns}")

    print(f"\n  -- Format Breakdown --")
    print(f"  Total format errors:               {format_errors_total}")
    print(f"")
    print(f"  XML tool_call WITH valid ```bash:   {xml_with_valid_bt:3d}")
    print(f"    -> harness accepted:              {xml_that_succeeded:3d}")
    print(f"    -> harness rejected (why?):       {xml_that_failed:3d}")
    print(f"")
    print(f"  XML tool_call WITHOUT ```bash:      {xml_without_bt:3d}")
    print(f"    -> always format error")
    print(f"")
    print(f"  Backtick-only (no XML):             {bt_without_xml:3d}")
    print(f"    -> accepted:                      {bt_without_xml_succeeded:3d}")
    print(f"    -> rejected (why?):               {bt_without_xml_failed:3d}")
    print(f"")
    print(f"  No command at all:                  {no_command_at_all:3d}")
    print(f"    -> rejected:                      {no_command_failed:3d}")

    # Close the gap: format_errors_total should equal xml_without_bt + xml_that_failed + bt_without_xml_failed + no_command_failed
    accounted = xml_without_bt + xml_that_failed + bt_without_xml_failed + no_command_failed
    print(f"\n  Error accounting: {format_errors_total} total, {accounted} accounted for")

    print(f"\n  -- Action Distribution (by wrapper + command type) --")
    for act, count in action_dist.most_common():
        pct = count / total_turns * 100
        bar = "#" * max(1, int(pct))
        print(f"    {act:25s} {count:4d} ({pct:5.1f}%) {bar}")

    # Recoveries: after a format error, what was the next successful action?
    recovery = Counter()
    for i, m in enumerate(msgs):
        if m["role"] != "user" or i <= 1:
            continue
        if ERR_MSG not in m["content"]:
            continue
        # This user msg is a format error response. Find the NEXT assistant msg.
        for k in range(i + 1, len(msgs)):
            if msgs[k]["role"] == "assistant":
                content = msgs[k]["content"]
                has_xml = "<tool_call>" in content.lower()
                has_bt = bool(BACKTICK_RE.findall(content))
                # Also check if this NEXT message got a format error too
                next_user = None
                for j in range(k + 1, len(msgs)):
                    if msgs[j]["role"] == "user":
                        next_user = msgs[j]["content"]
                        break
                did_recover = not (next_user and ERR_MSG in next_user)
                if has_xml and has_bt and did_recover:
                    recovery["xml+bt_recovered"] += 1
                elif has_xml and not has_bt:
                    recovery["xml_no_bt_still_failing"] += 1
                elif not has_xml and has_bt:
                    recovery["bt_only_recovered"] += 1
                else:
                    recovery["still_no_command"] += 1
                break

    print(f"\n  -- Recovery After Format Error --")
    for act, count in recovery.most_common():
        pct = count / format_errors_total * 100 if format_errors_total else 0
        print(f"    {act:30s} {count:3d} ({pct:.0f}%)")

    # Learning: does the model stop using XML over time?
    print(f"\n  -- XML Usage Over Time --")
    thirds = total_turns // 3
    for chunk_name, start, end in [("First 1/3", 0, thirds), ("Middle 1/3", thirds, 2*thirds), ("Final 1/3", 2*thirds, total_turns)]:
        asst_msgs = [m for m in msgs if m["role"] == "assistant"][start:end]
        xml_count = sum(1 for m in asst_msgs if "<tool_call>" in m["content"].lower())
        bt_count = sum(1 for m in asst_msgs if BACKTICK_RE.findall(m["content"]))
        total_in_chunk = len(asst_msgs)
        print(f"    {chunk_name:12s}: {xml_count:3d}/{total_in_chunk:3d} XML, {bt_count:3d}/{total_in_chunk:3d} with backtick block")

    print()
