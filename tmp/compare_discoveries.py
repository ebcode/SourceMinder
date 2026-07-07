#!/usr/bin/env python3
"""Compare qi responses vs grep responses for the same symbols."""

import json, re

BASE = "experiment/logs/xiaomi_mimo--mimo-v2.5-pro/pro_pilot_ansible_mimo_v2.5-pro"
INST = "instance_ansible__ansible-6cc97447aac5816745278f3735af128afb255c81-v0f01c69f1e2528b935359cfe578530722bca2c59"
FNAME = f"{INST}.rep01.traj.json"

BACKTICK = re.compile(r"```bash\s*\n(.*?)\n```", re.DOTALL)
ERR_MSG = "Please always provide EXACTLY ONE action in triple backticks"

def strip_cd(cmd):
    return re.sub(r"^(cd\s+/app\s*[;&]{1,2}\s*|cd /app && |cd /app ; )", "", cmd)

# Load both trajectories
def load_msgs(arm):
    arm_dir = "swebp_" + arm
    path = f"{BASE}/{arm_dir}/{INST}/{FNAME}"
    with open(path) as f:
        return json.load(f)["messages"]

control_msgs = load_msgs("control")
treat_msgs = load_msgs("treatment")

# Collect: for each assistant turn, the command and the next user response
def get_interactions(msgs):
    interactions = []
    for i, m in enumerate(msgs):
        if m["role"] != "assistant":
            continue
        content = m["content"]
        bt = BACKTICK.findall(content)
        if not bt:
            continue
        cmd = strip_cd(bt[0].strip())
        turn = len([mm for mm in msgs[:i] if mm["role"] == "assistant"])

        # Get user response
        resp = None
        for k in range(i + 1, len(msgs)):
            if msgs[k]["role"] == "user":
                resp = msgs[k]["content"]
                break

        interactions.append({
            "turn": turn,
            "cmd": cmd,
            "resp": resp or "",
            "is_qi": cmd.startswith("qi "),
            "is_grep": any(cmd.split()[0].startswith(p) for p in ["grep", "find"]),
            "is_cat": cmd.startswith("cat ") and not cmd.startswith("cat >"),
        })

    return interactions

ctrl_int = get_interactions(control_msgs)
treat_int = get_interactions(treat_msgs)

# Key symbols to track
KEY_SYMBOLS = [
    "set_temporary_context",
    "copy_with_new_env",
    "_AnsibleMapping", 
    "timedout",
    "fail_json",
    "_UNSET",
    "deprecate",
    "is_controller",
    "lookup.*error",
    "help_text",
    "_is_traceback",
    "DeprecationSummary",
]

print("=" * 70)
print("  WHAT EACH ARM SAW FOR KEY SYMBOLS")
print("=" * 70)

for symbol in KEY_SYMBOLS:
    print(f"\n{'─' * 60}")
    print(f"  Symbol: {symbol}")
    print(f"{'─' * 60}")

    # Control: grep results
    ctrl_hits = [x for x in ctrl_int if re.search(symbol, x["cmd"])]
    if ctrl_hits:
        print(f"  Control (grep/sed approach):")
        for h in ctrl_hits[:5]:
            resp = h["resp"]
            # Show what was discovered
            if resp:
                lines = resp.strip().split("\n")
                if len(lines) <= 3:
                    summary = resp[:250].replace("\n", " | ")
                else:
                    summary = f"{lines[0][:120]} ... ({len(lines)} lines total)"
                print(f"    T{h['turn']:3d}: {h['cmd'][:100]}...")
                print(f"          → {summary[:200]}")
            else:
                print(f"    T{h['turn']:3d}: {h['cmd'][:120]}... (no response)")

    # Treatment: qi results
    treat_qi = [x for x in treat_int if x["is_qi"] and re.search(symbol, x["cmd"])]
    treat_grep = [x for x in treat_int if x["is_grep"] and re.search(symbol, x["cmd"])]

    if treat_qi:
        print(f"  Treatment (qi approach):")
        for h in treat_qi[:5]:
            resp = h["resp"]
            # Parse qi output
            if resp:
                # qi wraps output in <returncode> tags
                lines = [l for l in resp.split("\n") if l.strip() and "<returncode>" not in l]
                if len(lines) <= 4:
                    summary = " | ".join(l.strip()[:120] for l in lines if l.strip())
                else:
                    summary = f"{lines[0].strip()[:120] if lines else 'EMPTY'} ... ({len(lines)} lines)"
                print(f"    T{h['turn']:3d}: {h['cmd'][:100]}")
                if not summary.strip():
                    summary = "EMPTY/NOTHING FOUND"
                print(f"          → {summary[:250]}")
            else:
                print(f"    T{h['turn']:3d}: {h['cmd'][:120]}... (no response)")

    if treat_grep and not treat_qi:
        print(f"  Treatment (fell back to grep for this):")
        for h in treat_grep[:3]:
            print(f"    T{h['turn']:3d}: {h['cmd'][:120]}...")

# -- Detailed: compare qi vs grep for specific critical findings --
print(f"\n{'=' * 70}")
print(f"  CRITICAL: What grep found that qi would miss")
print(f"{'=' * 70}")

# 1. "..." (Ellipsis) - grep-only discovery
ctrl_ellipsis = [x for x in ctrl_int if "\.\.\." in x["cmd"] and "grep" in x["cmd"]]
treat_ellipsis_qi = [x for x in treat_int if x["is_qi"] and "\.\.\." in x["cmd"]]
treat_ellipsis_grep = [x for x in treat_int if x["is_grep"] and "\.\.\." in x["cmd"]]

print(f"\n  Python Ellipsis '...':")
print(f"    Control: {len(ctrl_ellipsis)} grep hits (T{ctrl_ellipsis[0]['turn']}: \"{ctrl_ellipsis[0]['cmd'][:80]}...\" )")
print(f"    Treatment qi: {len(treat_ellipsis_qi)} hits")
print(f"    Treatment grep: {len(treat_ellipsis_grep)} hits")
print(f"    → Ellipsis is literal syntax, not a symbol. qi can't find it.")

# 2. Cross-cutting regex patterns
cross_patterns = [
    ("errors.*warn", "error handling with warnings"),
    ("help_text.*error", "help text error messages"),
    ("deprecation_warnings_enabled", "deprecation warnings flag"),
]
for pattern, desc in cross_patterns:
    ctrl_hits = [x for x in ctrl_int if pattern in x["cmd"]]
    treat_qi = [x for x in treat_int if x["is_qi"] and pattern in x["cmd"]]
    treat_grep = [x for x in treat_int if x["is_grep"] and pattern in x["cmd"]]
    print(f"\n  Regex pattern '{pattern}' ({desc}):")
    print(f"    Control grep: {len(ctrl_hits)} hits")
    print(f"    Treatment qi: {len(treat_qi)} hits")
    print(f"    Treatment grep: {len(treat_grep)} hits")
    if len(treat_grep) > 0 and len(treat_qi) == 0:
        print(f"    → Treatment had to use grep for this; qi can't do regex patterns")

# 3. String occurrences vs symbol definitions
print(f"\n  String occurrence vs. symbol definition:")
for term in ["timedout", "_UNSET", "is_controller"]:
    ctrl_grep = [x for x in ctrl_int if term in x["cmd"] and (x["is_grep"] or x["is_cat"])]
    treat_qi = [x for x in treat_int if x["is_qi"] and term in x["cmd"]]
    treat_grep = [x for x in treat_int if x["is_grep"] and term in x["cmd"]]
    print(f"    '{term}': Control grep={len(ctrl_grep)}, Treat qi={len(treat_qi)}, Treat grep={len(treat_grep)}")

# 4. Show qi responses that look empty/useless
print(f"\n{'=' * 70}")
print(f"  Qi RESPONSES: Did qi return actionable results?")
print(f"{'=' * 70}")

qi_interactions = [x for x in treat_int if x["is_qi"]]
empty_qis = []
for qi in qi_interactions:
    resp = qi["resp"]
    # Strip returncode tags
    clean = re.sub(r"<returncode>\d+</returncode>", "", resp).strip()
    lines = [l for l in clean.split("\n") if l.strip()]
    if len(lines) <= 2:
        empty_qis.append(qi)

if empty_qis:
    print(f"\n  Qi queries with 2 or fewer lines of output ({len(empty_qis)}/{len(qi_interactions)}):")
    for qi in empty_qis:
        resp = qi["resp"]
        clean = re.sub(r"<returncode>\d+</returncode>", "", resp).strip()
        lines = [l for l in clean.split("\n") if l.strip()]
        print(f"    T{qi['turn']:3d}: {qi['cmd'][:100]}")
        print(f"          → {clean[:150] if clean else '(empty)'}")
