#!/usr/bin/env python3
"""Deep-dive: what did each qi query actually return? Show full output."""

import json, re

BASE = "experiment/logs/xiaomi_mimo--mimo-v2.5-pro/pro_pilot_ansible_mimo_v2.5-pro"
INST = "instance_ansible__ansible-6cc97447aac5816745278f3735af128afb255c81-v0f01c69f1e2528b935359cfe578530722bca2c59"
FNAME = f"{INST}.rep01.traj.json"

BACKTICK = re.compile(r"```bash\s*\n(.*?)\n```", re.DOTALL)

def strip_cd(c):
    return re.sub(r"^(cd\s+/app\s*[;&]{1,2}\s*|cd /app && |cd /app ; )", "", c)

treat_path = f"{BASE}/swebp_treatment/{INST}/{FNAME}"
with open(treat_path) as f:
    data = json.load(f)
msgs = data["messages"]

# Collect all qi interactions with full context
qi_interactions = []
for i, m in enumerate(msgs):
    if m["role"] != "assistant":
        continue
    content = m["content"]
    if "<tool_call>" in content.lower():
        continue  # skip XML - those are format errors
    bt = BACKTICK.findall(content)
    if not bt:
        continue
    cmd = strip_cd(bt[0].strip())
    if not cmd.startswith("qi ") and not cmd.startswith("qi\n"):
        continue

    turn = len([mm for mm in msgs[:i] if mm["role"] == "assistant"])

    # Get the full user response
    resp = None
    for k in range(i + 1, len(msgs)):
        if msgs[k]["role"] == "user":
            resp = msgs[k]["content"]
            break

    qi_interactions.append({
        "turn": turn,
        "cmd": cmd,
        "resp": resp or "",
        "assistant_full": content,
    })

print(f"Total qi queries: {len(qi_interactions)}")
print()

for qi in qi_interactions:
    cmd = qi["cmd"]
    resp = qi["resp"]
    turn = qi["turn"]

    # Parse the qi output (strip returncode tags, xml wrappers)
    clean = resp
    clean = re.sub(r"<returncode>\d+</returncode>", "", clean)
    clean = re.sub(r"</?output>", "", clean)
    clean = re.sub(r"</?obs.*?>", "", clean, flags=re.IGNORECASE)

    # Count non-empty lines
    lines = [l for l in clean.split("\n") if l.strip()]
    num_lines = len(lines)

    # Check for qi-specific error/warning patterns
    has_warning = "warning" in clean.lower() and ("no results" in clean.lower() or "not found" in clean.lower() or "empty" in clean.lower())
    is_empty = num_lines <= 1
    is_short = 2 <= num_lines <= 3

    # Classify
    status = "EMPTY" if is_empty else ("SHORT" if is_short else f"{num_lines} lines")
    if has_warning:
        status = status + " + WARNING"

    # What was the command asking?
    # Extract pattern and flags
    flags = []
    pattern = ""
    parts = cmd.split()
    i_p = 1
    while i_p < len(parts):
        if parts[i_p].startswith("-"):
            flags.append(parts[i_p])
            if parts[i_p] in ("-p", "-e", "-i", "-f", "-l", "-m", "-w", "-x"):
                i_p += 1  # skip value
        elif not pattern:
            pattern = parts[i_p].strip("'\"")
        i_p += 1

    print(f"T{turn:3d} [{status:20s}] {cmd[:130]}")
    if has_warning:
        # Show the warning
        for line in lines:
            if "warning" in line.lower() or "no results" in line.lower() or "empty" in line.lower():
                print(f"       WARNING: {line.strip()[:150]}")
    if is_empty or is_short:
        print(f"       → {clean.strip()[:200]}")
    elif num_lines <= 8:
        # Show all
        for line in lines[:8]:
            print(f"       {line.strip()[:150]}")
    else:
        # Show first 3 and last 2
        for line in lines[:3]:
            print(f"       {line.strip()[:150]}")
        print(f"       ... ({num_lines - 5} more lines) ...")
        for line in lines[-2:]:
            print(f"       {line.strip()[:150]}")
    print()

# Now analyze patterns
print("=" * 70)
print("  ANALYSIS: Query patterns and problems")
print("=" * 70)

# 1. Empty queries
empty_queries = [q for q in qi_interactions
                 if len([l for l in re.sub(r"</?output>|</?returncode>|<returncode>\d+</returncode>", "", q["resp"]).split("\n") if l.strip()]) <= 1]
print(f"\n  Empty / near-empty results: {len(empty_queries)}")
for q in empty_queries:
    clean_cmd = q["cmd"].replace("2>&1", "").replace("; echo '===';", "").replace("| head -", "").strip()
    print(f"    T{q['turn']}: {clean_cmd[:150]}")
    print(f"        Response: {q['resp'][:200]}")

# 2. Overly broad queries (single char, wildcard, common words)
broad = [q for q in qi_interactions if len(q["cmd"].split()) >= 2
         and len(q["cmd"].split()[1].strip("'\"")) <= 2]
print(f"\n  Overly-broad single-char patterns: {len(broad)}")

# 3. Queries with -v (verbose) vs --raw - which was more useful?
verbose = [q for q in qi_interactions if "-v" in q["cmd"]]
raw = [q for q in qi_interactions if "--raw" in q["cmd"]]
print(f"\n  Verbose (-v) queries: {len(verbose)}")
print(f"  Raw (--raw) queries: {len(raw)}")

# Did the model use -v and --raw together? (they're incompatible)
both = [q for q in qi_interactions if "-v" in q["cmd"] and "--raw" in q["cmd"]]
print(f"  Both -v and --raw: {len(both)} (likely ignored one)")

# 4. Queries with -p (parent) - were they effective?
parent_queries = [q for q in qi_interactions if " -p " in q["cmd"]]
print(f"\n  Parent (-p) queries: {len(parent_queries)}")
for q in parent_queries:
    # Extract parent
    m = re.search(r"-p\s+(\S+)", q["cmd"])
    parent = m.group(1) if m else "?"
    # Extract pattern
    parts = q["cmd"].split()
    pattern = ""
    for p in parts[1:]:
        if not p.startswith("-"):
            pattern = p.strip("'\"")
            break
    # Count lines in response
    clean = re.sub(r"</?returncode>|<returncode>\d+</returncode>|</?output>", "", q["resp"])
    lines = [l for l in clean.split("\n") if l.strip()]
    print(f"    T{q['turn']:3d}: qi '{pattern}' -p {parent}  → {len(lines)} output lines")

# 5. Queries that the model then grepped for RIGHT AFTER (qi failed?)
print(f"\n  Qi→grep transitions (qi didn't give enough):")
for i, qi in enumerate(qi_interactions):
    turn = qi["turn"]
    # Check the NEXT assistant turn
    next_asst = None
    for k in range(i + 1, len(qi_interactions)):
        if qi_interactions[k]["turn"] > turn:
            # Find grep commands between this qi and the next qi
            break
    # Actually, check if the same command contains grep adjacent to qi
    if "grep " in qi["cmd"] or "find " in qi["cmd"]:
        print(f"    T{turn:3d}: qi+grep in same command: {qi['cmd'][:120]}...")
