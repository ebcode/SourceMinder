#!/usr/bin/env python3
"""Deep statistical comparison of control vs treatment trajectories."""

import json
import re
from collections import Counter
from pathlib import Path

BASE = Path(
    "experiment/logs/xiaomi_mimo--mimo-v2.5-pro/pro_pilot_ansible_mimo_v2.5-pro"
)
INST_DIR = "instance_ansible__ansible-6cc97447aac5816745278f3735af128afb255c81-v0f01c69f1e2528b935359cfe578530722bca2c59"
FNAME = f"{INST_DIR}.rep01.traj.json"

PATHS = {
    "control": BASE / "swebp_control" / INST_DIR / FNAME,
    "treatment": BASE / "swebp_treatment" / INST_DIR / FNAME,
}


def classify_user_message(content):
    """Classify a user (observation) message."""
    lo = content.lower()

    if "please always provide exactly one action" in lo:
        return "format_error"

    if "exit due to" in lo or "submitted" in lo:
        return "exit_event"

    if "fail" in lo and ("=====" in content or "test_" in content or "====" in content):
        return "test_failure"

    if "pass" in lo and "=====" in content:
        return "test_success"

    if "error" in lo and ("traceback" in lo or "exception" in lo or "modulenotfound" in lo):
        return "runtime_error"

    if re.search(r"^\w+\.py\b", content.strip()) and "pass" not in lo and "fail" not in lo:
        return "test_output_neutral"

    # Command output (from bash commands)
    if content.strip() and len(content) < 500:
        return "short_output"

    if "diff" in lo:
        return "diff_output"

    if content.strip().startswith("OBSERVATION:") or "observation" in lo[:50]:
        return "observation"

    return "other_output"


def classify_assistant_message(content):
    """Classify assistant's action."""
    lo = content.lower()

    # Check for XML tool_call pattern
    if "<tool_call>" in lo or "<function" in lo:
        return "xml_tool_call"

    # Check for backtick-bash commands
    bt_count = content.count("```")
    has_bash = "```bash" in content

    if has_bash and bt_count >= 2:
        # What kind of bash command?
        bash_blocks = re.findall(r"```bash\s*\n(.*?)\n```", content, re.DOTALL)
        if bash_blocks:
            cmd = bash_blocks[0].strip()
            if cmd.startswith("sed "):
                return "action_edit_sed"
            elif cmd.startswith("cat ") or cmd.startswith("nl ") or cmd.startswith("head "):
                return "action_view_file"
            elif cmd.startswith("cd ") or cmd.startswith("ls "):
                return "action_navigate"
            elif cmd.startswith("grep ") or cmd.startswith("rg "):
                return "action_grep"
            elif cmd.startswith("qi ") or cmd.startswith("qi\n"):
                return "action_qi"
            elif cmd.startswith("python ") or cmd.startswith("pytest") or cmd.startswith("python3"):
                return "action_run_test"
            elif cmd.startswith("git "):
                return "action_git"
            elif cmd.startswith("echo ") or cmd.startswith("printf "):
                return "action_echo"
            elif len(cmd) < 10:
                return "action_trivial"
            else:
                return "action_other_bash"
        return "action_backtick_no_cmd"

    # No backticks or XML tags
    return "text_only_no_action"


def analyze(path, label):
    with open(path) as f:
        data = json.load(f)

    msgs = data["messages"]
    info = data["info"]

    total_turns = len([m for m in msgs if m["role"] == "assistant"])

    # Classify every message
    user_classes = Counter()
    asst_classes = Counter()
    format_errors = 0
    test_fail_count = 0
    test_pass_count = 0
    edit_attempts = 0
    test_run_attempts = 0
    navigate_actions = 0
    qi_queries = 0
    grep_actions = 0
    thought_only = 0

    # Track the evolution: what does the model do after format errors?
    recovery_actions = []

    for i, m in enumerate(msgs):
        if m["role"] == "user" and i > 1:
            cls = classify_user_message(m["content"])
            user_classes[cls] += 1
            if cls == "format_error":
                format_errors += 1
            elif cls == "test_failure":
                test_fail_count += 1
            elif cls == "test_success":
                test_pass_count += 1

        elif m["role"] == "assistant":
            cls = classify_assistant_message(m["content"])
            asst_classes[cls] += 1
            if cls == "action_edit_sed":
                edit_attempts += 1
            elif cls == "action_run_test":
                test_run_attempts += 1
            elif cls == "action_navigate":
                navigate_actions += 1
            elif cls == "action_qi":
                qi_queries += 1
            elif cls == "action_grep":
                grep_actions += 1
            elif cls == "text_only_no_action":
                thought_only += 1

            # Was this a recovery from a format error?
            prev_user = None
            for k in range(i - 1, -1, -1):
                if msgs[k]["role"] == "user":
                    prev_user = classify_user_message(msgs[k]["content"])
                    break
            if prev_user == "format_error":
                recovery_actions.append(cls)

    # --- PRINT ---
    print(f"{'='*65}")
    print(f"  {label.upper()}")
    print(f"{'='*65}")

    # Summary
    print(f"\n  -- Summary --")
    print(f"  Total turns:              {total_turns}")
    print(f"  Exit status:              {info.get('exit_status', '?')}")
    print(
        f"  Solved:                   {'YES' if 'Submitted' in info.get('exit_status', '') else 'NO'}"
    )
    print(f"  API calls:                {info.get('model_stats', {}).get('api_calls', '?')}")

    # Format errors
    print(f"\n  -- Format errors ('EXACTLY ONE action') --")
    print(f"  Count:                    {format_errors}")
    print(f"  Rate:                     {format_errors / total_turns * 100:.1f}%")
    print(f"  Effective turns:          {total_turns - format_errors}")
    print(f"  Effective turn rate:      {(total_turns - format_errors) / total_turns * 100:.1f}%")

    # Assistant action distribution
    print(f"\n  -- Assistant Action Distribution --")
    for action, count in asst_classes.most_common():
        pct = count / total_turns * 100
        bar = "#" * max(1, int(pct / 2))
        print(f"    {action:30s} {count:4d} ({pct:5.1f}%) {bar}")

    # User observation distribution
    print(f"\n  -- User Observation Distribution --")
    for cls, count in user_classes.most_common():
        pct = count / (total_turns + 1) * 100
        print(f"    {cls:30s} {count:4d} ({pct:5.1f}%)")

    # Efficiency metrics
    print(f"\n  -- Efficiency Metrics --")
    productive_turns = edit_attempts + test_run_attempts + navigate_actions + qi_queries
    overhead_turns = total_turns - productive_turns - format_errors
    print(f"  Productive turns:         {productive_turns}")
    print(f"    Edit attempts:          {edit_attempts}")
    print(f"    Test runs:              {test_run_attempts}")
    print(f"    Navigate (ls/cd):       {navigate_actions}")
    print(f"    Qi queries:             {qi_queries}")
    print(f"    Grep queries:           {grep_actions}")
    print(f"  Overhead (thought/etc):   {overhead_turns}")
    print(f"  Format errors (wasted):   {format_errors}")

    # Recovery from errors
    if recovery_actions:
        recovery_dist = Counter(recovery_actions)
        print(f"\n  -- Recovery After Format Error --")
        print(f"  Format errors recovered:  {len(recovery_actions)}")
        for act, count in recovery_dist.most_common(5):
            print(f"    {act}: {count}")

    # Test outcomes
    print(f"\n  -- Test Outcomes --")
    print(f"  Test failures observed:   {test_fail_count}")
    print(f"  Test successes observed:  {test_pass_count}")

    # Budget analysis
    print(f"\n  -- Budget Analysis --")
    step_limit = info.get("config", {}).get("agent", {}).get("step_limit", "?")
    print(f"  Step limit:               {step_limit}")
    remaining = step_limit - total_turns
    print(f"  Remaining budget:         {remaining} turns")

    return {
        "label": label,
        "turns": total_turns,
        "format_errors": format_errors,
        "effective": total_turns - format_errors,
        "asst_classes": asst_classes,
        "test_fail_count": test_fail_count,
        "test_pass_count": test_pass_count,
        "solved": "Submitted" in info.get("exit_status", ""),
    }


def main():
    results = {}
    for label in ["control", "treatment"]:
        if PATHS[label].exists():
            results[label] = analyze(PATHS[label], label)

    if len(results) == 2:
        c = results["control"]
        t = results["treatment"]
        print(f"\n{'='*65}")
        print(f"  COMPARISON TABLE")
        print(f"{'='*65}")
        rows = [
            ("Exit status", "Submitted" if c["solved"] else "EOFError", "EOFError" if not t["solved"] else "Submitted"),
            ("Solved", "YES" if c["solved"] else "NO", "YES" if t["solved"] else "NO"),
            ("Total turns", str(c["turns"]), str(t["turns"])),
            ("Format errors", str(c["format_errors"]), str(t["format_errors"])),
            ("Error rate", f"{c['format_errors']/c['turns']*100:.1f}%", f"{t['format_errors']/t['turns']*100:.1f}%"),
            ("Effective turns", str(c["effective"]), str(t["effective"])),
            ("Test failures seen", str(c["test_fail_count"]), str(t["test_fail_count"])),
        ]
        col_w = max(len(r[0]) for r in rows)
        for name, cv, tv in rows:
            print(f"  {name:<{col_w}s}  {cv:>10s}  vs  {tv:>10s}")

        # Combined analysis
        print(f"\n  -- Combined Analysis --")
        extra_effective = t["effective"] - c["effective"]
        extra_errors = t["format_errors"] - c["format_errors"]
        print(f"  Treatment had {extra_effective:+d} more effective turns than control.")
        print(f"  Treatment had {extra_errors:+d} more format errors than control.")
        if extra_effective > 0 and not t["solved"]:
            print(f"  Despite {extra_effective} MORE effective turns, treatment did NOT solve.")
            print(f"  This suggests the treatment was less efficient per effective turn.")
            # Calculate efficiency: effective turns needed to solve
            if c["solved"]:
                print(f"  Control solved in {c['turns']} turns ({c['effective']} effective).")
                print(f"  Treatment used {t['turns']} turns ({t['effective']} effective) and failed.")
                excess_over_control = t["effective"] - c["effective"]
                print(f"  Treatment had {excess_over_control} more effective turns to work with.")
                print(f"  => Problem is not format errors; it's that the treatment's")
                print(f"     additional tools/instructions didn't lead to faster solving.")

        # Qi vs grep comparison
        print(f"\n  -- Qi vs Grep Usage --")
        for lbl, res in [("control", c), ("treatment", t)]:
            qi = res["asst_classes"].get("action_qi", 0)
            grep = res["asst_classes"].get("action_grep", 0)
            edits = res["asst_classes"].get("action_edit_sed", 0)
            tests = res["asst_classes"].get("action_run_test", 0)
            print(f"  {lbl}: qi={qi}, grep={grep}, edits={edits}, test_runs={tests}")


if __name__ == "__main__":
    main()
