#!/usr/bin/env python3
"""Compare grep/sed work in Control vs qi work in Treatment.
What did Control learn that Treatment missed?"""

import json, re
from collections import Counter

BASE = "experiment/logs/xiaomi_mimo--mimo-v2.5-pro/pro_pilot_ansible_mimo_v2.5-pro"
INST = "instance_ansible__ansible-6cc97447aac5816745278f3735af128afb255c81-v0f01c69f1e2528b935359cfe578530722bca2c59"
FNAME = f"{INST}.rep01.traj.json"

BACKTICK = re.compile(r"```bash\s*\n(.*?)\n```", re.DOTALL)

def strip_cd(cmd):
    return re.sub(r"^(cd\s+/app\s*[;&]{1,2}\s*|cd /app && |cd /app ; )", "", cmd)

def get_commands(path, arm):
    with open(path) as f:
        data = json.load(f)
    msgs = data["messages"]

    grep_cmds = []      # grep/find patterns searched
    sed_view = []       # sed/head/nl used to view code
    cat_files = []       # files explicitly catted
    qi_cmds = []         # qi queries
    edit_cmds = []       # actual edits
    test_cmds = []       # pytest runs
    python_scripts = []  # ad-hoc python

    for i, m in enumerate(msgs):
        if m["role"] != "assistant":
            continue
        content = m["content"]
        bt_blocks = BACKTICK.findall(content)
        if not bt_blocks:
            continue
        cmd = bt_blocks[0].strip()
        cmd_stripped = strip_cd(cmd)
        lines = cmd_stripped.split("\n")
        first = lines[0].strip()

        # Get user response (observation)
        user_resp = None
        for k in range(i + 1, len(msgs)):
            if msgs[k]["role"] == "user":
                user_resp = msgs[k]["content"]
                break

        # Check if this was an error
        is_error = user_resp and "Please always provide EXACTLY ONE action" in user_resp

        # grep / find commands
        if any(first.startswith(p) for p in ["grep ", "rg ", "find "]):
            # Extract the search pattern / goal
            grep_cmds.append({
                "turn": len([mm for mm in msgs[:i] if mm["role"] == "assistant"]),
                "cmd": first[:200],
                "full": cmd_stripped[:400],
                "error": is_error,
                "response": user_resp[:200] if user_resp else ""
            })

        # sed/cat/nl for viewing
        if any(first.startswith(p) for p in ["sed -n", "nl ", "cat ", "head ", "tail "]):
            sed_view.append({
                "turn": len([mm for mm in msgs[:i] if mm["role"] == "assistant"]),
                "cmd": first[:200],
                "response": user_resp[:200] if user_resp else ""
            })

        # cat for viewing (dedup)
        if first.startswith("cat ") and not first.startswith("cat >"):
            cat_files.append({
                "turn": len([mm for mm in msgs[:i] if mm["role"] == "assistant"]),
                "file": first if len(first) < 100 else first[:100],
            })

        # qi
        if first.startswith("qi "):
            qi_cmds.append({
                "turn": len([mm for mm in msgs[:i] if mm["role"] == "assistant"]),
                "cmd": first[:200],
                "full": cmd_stripped[:400],
                "error": is_error,
                "response": user_resp[:300] if user_resp else ""
            })

        # edits
        if re.match(r"sed\s+-i", first) or (first.startswith("cat >") and "/app/" in first):
            edit_cmds.append({
                "turn": len([mm for mm in msgs[:i] if mm["role"] == "assistant"]),
                "cmd": first[:200],
            })

        # python fix scripts
        if re.match(r"python\s+/tmp/", first) or re.match(r"cat\s+>\s+/tmp/.*fix", first):
            python_scripts.append({
                "turn": len([mm for mm in msgs[:i] if mm["role"] == "assistant"]),
                "cmd": first[:200],
            })

        # test runs
        if "pytest" in first or "python -m pytest" in first:
            test_cmds.append({
                "turn": len([mm for mm in msgs[:i] if mm["role"] == "assistant"]),
                "cmd": first[:200],
            })

    return {
        "grep": grep_cmds,
        "sed_view": sed_view,
        "cat_files": cat_files,
        "qi": qi_cmds,
        "edits": edit_cmds,
        "tests": test_cmds,
        "pyscripts": python_scripts,
    }


def analyze(arm):
    arm_dir = "swebp_" + arm
    path = f"{BASE}/{arm_dir}/{INST}/{FNAME}"
    data = get_commands(path, arm)

    print(f"\n{'='*65}")
    print(f"  {arm.upper()}")
    print(f"{'='*65}")

    if data["qi"]:
        print(f"\n  -- Qi Queries ({len(data['qi'])} total) --")
        for q in data["qi"]:
            print(f"    T{q['turn']:3d}: {q['cmd']}")
            if q["response"] and not q["error"]:
                # Show first few lines of response
                lines = q["response"].strip().split("\n")
                if len(lines) == 0 or (len(lines) <= 2 and not lines[0].strip()):
                    print(f"          -> EMPTY / NO RESULTS")
                elif len(lines) <= 2:
                    print(f"          -> {lines[0][:120]}")
                else:
                    print(f"          -> {lines[0][:120]}")
                    if len(lines) > 3:
                        print(f"             ({len(lines)} lines)")

    if data["grep"]:
        print(f"\n  -- Grep/Find Queries ({len(data['grep'])} total) --")
        for g in data["grep"]:
            result = "ERROR" if g["error"] else ("match" if g["response"] and g["response"].strip()[:1] != "" else "EMPTY?")
            print(f"    T{g['turn']:3d}: {g['cmd'][:130]}")

    if data["cat_files"]:
        print(f"\n  -- Files Read (cat) --")
        # Show unique files
        seen = set()
        for c in data["cat_files"]:
            if c["file"] not in seen:
                seen.add(c["file"])
                print(f"    T{c['turn']:3d}: {c['file']}")

    if data["edits"]:
        print(f"\n  -- Actual Edits ({len(data['edits'])} total) --")
        for e in data["edits"]:
            print(f"    T{e['turn']:3d}: {e['cmd'][:150]}")

    if data["pyscripts"]:
        print(f"\n  -- Fix Scripts (python /tmp/...) ({len(data['pyscripts'])} total) --")
        for p in data["pyscripts"]:
            print(f"    T{p['turn']:3d}: {p['cmd'][:150]}")

    if data["tests"]:
        print(f"\n  -- Test Runs ({len(data['tests'])} total) --")
        for t in data["tests"]:
            print(f"    T{t['turn']:3d}: {t['cmd'][:150]}")

    return data


control = analyze("control")
treatment = analyze("treatment")

# Cross-reference: what terms did control grep for that treatment tried with qi?
print(f"\n{'='*65}")
print(f"  CROSS-REFERENCE: Grep vs Qi")
print(f"{'='*65}")

# Extract search terms from control's grep commands
control_terms = set()
for g in control["grep"]:
    cmd = g["cmd"]
    # Extract what's being searched for (patterns in grep, filenames in find)
    # grep -rn "pattern" → extract pattern
    m = re.search(r'grep\s+(?:-[a-zA-Z]+\s+)*["\']([^"\']+)["\']', cmd)
    if m:
        control_terms.add(m.group(1))
    # grep pattern file
    m = re.search(r'grep\s+(?:-[a-zA-Z]+\s+)*(\S+)\s', cmd)
    if m and not m.group(1).startswith("-"):
        term = m.group(1)
        if not term.startswith("/") and len(term) > 2:
            control_terms.add(term)

# Extract search terms from treatment's qi commands
treatment_terms = set()
for q in treatment["qi"]:
    cmd = q["cmd"]
    # qi 'pattern' → extract pattern
    m = re.search(r"qi\s+['\"](\S+?)['\"]", cmd)
    if m:
        treatment_terms.add(m.group(1))
    # qi pattern → first non-flag arg
    parts = cmd.split()
    for p in parts[1:]:
        if not p.startswith("-") and len(p) > 1 and not p.startswith("'"):
            treatment_terms.add(p.strip("'\""))

print(f"\n  Control searched for (grep): {sorted(control_terms)[:30]}")
if control_terms:
    print(f"    ({len(control_terms)} unique terms)")
print(f"\n  Treatment searched for (qi): {sorted(treatment_terms)[:40]}")
if treatment_terms:
    print(f"    ({len(treatment_terms)} unique terms)")

# Overlap
overlap = control_terms & treatment_terms
print(f"\n  Overlap: {sorted(overlap) if overlap else 'NONE'}")
print(f"  In control but not treatment: {len(control_terms - treatment_terms)} terms")
print(f"  In treatment but not control: {len(treatment_terms - control_terms)} terms")
