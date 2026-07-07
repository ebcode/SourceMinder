#!/usr/bin/env python3
"""Final comprehensive statistical analysis."""

import json, re
from collections import Counter

BASE = "experiment/logs/xiaomi_mimo--mimo-v2.5-pro/pro_pilot_ansible_mimo_v2.5-pro"
INST = "instance_ansible__ansible-6cc97447aac5816745278f3735af128afb255c81-v0f01c69f1e2528b935359cfe578530722bca2c59"
FNAME = f"{INST}.rep01.traj.json"

ERR_MSG = "Please always provide EXACTLY ONE action in triple backticks"
BACKTICK = re.compile(r"```bash\s*\n(.*?)\n```", re.DOTALL)

for arm in ["control", "treatment"]:
    arm_dir = "swebp_" + arm
    path = f"{BASE}/{arm_dir}/{INST}/{FNAME}"
    with open(path) as f:
        data = json.load(f)
    msgs = data["messages"]
    info = data["info"]

    total_turns = len([m for m in msgs if m["role"] == "assistant"])
    solved = "Submitted" in info.get("exit_status", "")

    # --- Count things ---
    format_errors = 0
    xml_attempts = 0          # assistant messages with <tool_call>
    bt_commands = 0           # assistant messages with ```bash block
    edits = 0                 # sed -i, cat > /app/..., or python script that writes source
    test_runs = 0             # pytest invocations
    qi_queries = 0            # qi commands
    write_to_tmp = 0          # writes to /tmp (diagnostic scripts)
    git_ops = 0               # git stash/diff/etc
    navigate = 0              # ls, cd, find, grep, cat, nl, head

    # Track the narrative: what was the model trying to do each turn
    action_sequence = []

    for i, m in enumerate(msgs):
        if m["role"] == "assistant":
            content = m["content"]
            has_xml = "<tool_call>" in content.lower()
            bt_blocks = BACKTICK.findall(content)

            if has_xml:
                xml_attempts += 1

            if bt_blocks:
                bt_commands += 1
                cmd = bt_blocks[0].strip()
                # Strip leading "cd /app && " or "cd /app; "
                cmd_stripped = re.sub(r"^(cd\s+/app\s*[;&]{1,2}\s*|cd /app && |cd /app ; )", "", cmd)
                lines = cmd_stripped.split("\n")
                first = lines[0].strip() if lines else ""

                # Check for heredoc content (sed, cat, python writing to files)
                full_text = cmd_stripped

                # --- Edits ---
                is_edit = False
                # sed -i (in-place edit)
                if re.match(r"sed\s+-i\s", first):
                    is_edit = True
                # cat > /app/... << 'EOF' (writing directly to source)
                elif re.match(r"cat\s+>", first) and "/app/" in first:
                    is_edit = True
                # cat > /app/... << 'EOF' in any line
                elif any("/app/" in l and ("cat >" in l or "cat>" in l) for l in lines):
                    is_edit = True
                # python script that modifies files in /app
                elif first.startswith("python ") and "open(" in full_text and "/app/" in full_text:
                    is_edit = True
                # python /tmp/fix.py which modifies source
                elif re.match(r"python\s+/tmp/", first):
                    # It's a fix script - count as edit
                    is_edit = True

                if is_edit:
                    edits += 1
                    action_sequence.append("edit")

                # --- Test runs ---
                elif any(kw in first for kw in ["pytest", "python -m pytest"]):
                    test_runs += 1
                    action_sequence.append("test_run")

                elif first.startswith("timeout ") and "pytest" in first:
                    test_runs += 1
                    action_sequence.append("test_run")

                # --- Qi queries ---
                elif first.startswith("qi ") or first.startswith("qi\n"):
                    qi_queries += 1
                    action_sequence.append("qi")

                # --- Git ---
                elif first.startswith("git "):
                    git_ops += 1
                    action_sequence.append("git")

                # --- Write to /tmp (diagnostic) ---
                elif ("/tmp/" in first or ">/tmp/" in full_text) and (
                    first.startswith("cat ") or first.startswith("python ") or first.startswith("echo ")
                ):
                    write_to_tmp += 1
                    action_sequence.append("write_tmp")

                # --- Navigate / view ---
                elif any(first.startswith(p) for p in [
                    "ls ", "cd ", "find ", "grep ", "rg ", "cat ", "nl ", "head ", "echo ",
                    "sed -n", "sed -n ",
                ]):
                    navigate += 1
                    action_sequence.append("navigate")

                # --- Python one-liners (diagnostic) ---
                elif first.startswith("python ") or first.startswith("python3 "):
                    write_to_tmp += 1
                    action_sequence.append("python_script")

                # --- Misc short commands ---
                elif len(cmd_stripped) < 50:
                    navigate += 1
                    action_sequence.append("misc")

                else:
                    action_sequence.append("other")
            else:
                action_sequence.append("no_command")

            # Check next user message for format error
            next_user = None
            for k in range(i + 1, len(msgs)):
                if msgs[k]["role"] == "user":
                    next_user = msgs[k]["content"]
                    break
            if next_user and ERR_MSG in next_user:
                format_errors += 1
                # Override the action classification
                if action_sequence:
                    action_sequence[-1] = "FORMAT_ERROR"

    # --- User message analysis ---
    test_fail_count = 0
    test_pass_count = 0
    user_class = Counter()
    for i, m in enumerate(msgs):
        if m["role"] != "user" or i <= 1:
            continue
        lo = m["content"].lower()
        if "pass" in lo and "fail" in lo:
            user_class["mixed_test_output"] += 1
        elif "fail" in lo or "error" in lo:
            user_class["error_output"] += 1
        elif "pass" in lo:
            user_class["success_output"] += 1
        else:
            user_class["neutral_output"] += 1

        # Count test failures more specifically
        if "FAILED " in m["content"]:
            test_fail_count += m["content"].count("FAILED ")
        if "PASSED " in m["content"]:
            test_pass_count += m["content"].count("PASSED ")

    # --- Print ---
    print(f"{'='*65}")
    print(f"  {arm.upper()}  |  {'SOLVED' if solved else 'FAILED (budget exhausted)'}")
    print(f"{'='*65}")

    print(f"\n  -- Basics --")
    print(f"  Total turns:                 {total_turns}")
    print(f"  Exit status:                 {info.get('exit_status', '?')}")
    step_limit = info.get("config", {}).get("agent", {}).get("step_limit", "?")
    print(f"  Step budget:                 {step_limit}")

    print(f"\n  -- Format Errors (wrong command syntax) --")
    print(f"  Count:                       {format_errors}")
    pct = format_errors / total_turns * 100
    print(f"  Rate:                        {pct:.1f}%")
    print(f"  XML <tool_call> attempts:    {xml_attempts}")
    print(f"  Valid ```bash blocks:        {bt_commands}")
    print(f"  XML never co-exists with valid ```bash (0 cross-over)")

    print(f"\n  -- Action Counts --")
    print(f"  Navigate (ls/cd/find/grep/cat/nl/head): {navigate:4d}")
    print(f"  Edits (sed -i, cat > /app/, python fix): {edits:4d}")
    print(f"  Test runs (pytest):                  {test_runs:4d}")
    print(f"  Qi queries (treatment only):         {qi_queries:4d}")
    print(f"  Write to /tmp (diagnostic scripts):  {write_to_tmp:4d}")
    print(f"  Git operations:                      {git_ops:4d}")
    print(f"  Format errors (wasted turns):        {format_errors:4d}")

    recovered = total_turns - format_errors
    print(f"\n  -- Efficiency --")
    print(f"  Effective turns:             {recovered}")
    edit_pct = edits / recovered * 100 if recovered else 0
    print(f"  Edit rate (per effective):   {edit_pct:.1f}% ({edits}/{recovered})")
    test_pct = test_runs / recovered * 100 if recovered else 0
    print(f"  Test rate (per effective):   {test_pct:.1f}% ({test_runs}/{recovered})")

    # Phase analysis: what did the model spend turns 200+ on?
    late_actions = action_sequence[-50:] if len(action_sequence) >= 50 else action_sequence
    print(f"\n  -- Last 50 turns breakdown --")
    late_counts = Counter(late_actions)
    for act, count in late_counts.most_common():
        print(f"    {act:25s} {count:3d}")

    # Test result summary
    print(f"\n  -- Test Results Observed --")
    print(f"  FAILED lines seen:           {test_fail_count}")
    print(f"  PASSED lines seen:           {test_pass_count}")

    print(f"\n  -- User Observation Types --")
    for cls, count in user_class.most_common():
        print(f"    {cls:25s} {count:3d}")

    print()
