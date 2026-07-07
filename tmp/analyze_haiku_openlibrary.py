#!/usr/bin/env python3
"""Analyze the Claude Haiku 4.5 openlibrary control-vs-treatment run.

Claude uses <invoke name="bash"><parameter name="command">...</parameter></invoke> command
format (not triple-backtick bash), so this is a fresh classifier rather than a
reuse of the MiMo-format scripts in tmp/.
"""
import json, glob, os, re, sys
from collections import Counter

import sys
D = sys.argv[1] if len(sys.argv) > 1 else "experiment/logs/anthropic--claude-haiku-4-5-20251001/pro_pilot_openlibrary_haiku_v2"

# Claude Haiku emits ```bash blocks (one action per turn). The <invoke> XML
# format shows up only in the first message before the harness corrects it.
BACKTICK_RE = re.compile(r'```bash\s*\n(.*?)```', re.DOTALL)
INVOKE_CMD_RE = re.compile(r'<parameter\s+name="command">(.*?)</parameter>', re.DOTALL)
INVOKE_RE = re.compile(r'<invoke\s+name="([^"]+)"')

def extract_cmds(c):
    cmds = BACKTICK_RE.findall(c)
    cmds += INVOKE_CMD_RE.findall(c)
    return cmds

def strip_cd(cmd):
    return re.sub(r'^(cd\s+\S+\s*(&&|;)\s*)+', '', cmd.strip())

def classify(cmd):
    c = strip_cd(cmd)
    first = c.strip()
    # qi
    if re.match(r'(\S*/)?qi(\s|$)', first):
        return "qi"
    if re.match(r'(grep|rg|ag|find\s+\S+\s+-name|ack)\b', first):
        return "grep"
    if re.match(r'(sed\s+-i|perl\s+-i|patch\b|python[0-9.]*\s+-c\s+.{0,40}(open|write))', first):
        return "edit"
    if re.search(r'>\s*\S+\.(py|txt|md|cfg|ini|yaml|yml|json)\b', first) and '>>' not in first.split('\n')[0]:
        return "edit"
    if re.search(r'<<\s*[\'"]?EOF', first) and re.search(r'>\s*\S', first):
        return "edit"  # heredoc write
    if re.search(r'\b(pytest|py\.test|tox|unittest|python[0-9.]*\s+-m\s+pytest|nosetests)\b', first):
        return "test"
    if re.match(r'(cat|less|head|tail|ls|wc|file|tree|nl)\b', first):
        return "read"
    if re.match(r'(git)\b', first):
        return "git"
    if re.match(r'(echo)\b', first):
        return "echo"
    return "other"

def analyze_one(path):
    t = json.load(open(path))
    info = t.get("info", {})
    msgs = t.get("messages", [])
    assistant = [m for m in msgs if m.get("role") == "assistant"]
    actions = Counter()
    n_invokes_total = 0
    multi_invoke_turns = 0
    qi_cmds = []
    grep_cmds = []
    test_pass = test_fail = 0
    for m in assistant:
        c = m.get("content", "")
        c = c if isinstance(c, str) else str(c)
        cmds = extract_cmds(c)
        if len(cmds) > 1:
            multi_invoke_turns += 1
        n_invokes_total += len(cmds)
        for cmd in cmds:
            kind = classify(cmd)
            actions[kind] += 1
            if kind == "qi":
                qi_cmds.append(strip_cd(cmd).strip().split('\n')[0][:120])
            elif kind == "grep":
                grep_cmds.append(strip_cd(cmd).strip().split('\n')[0][:120])
    # count test outcomes in user observations
    for m in msgs:
        if m.get("role") == "user":
            c = m.get("content", "")
            c = c if isinstance(c, str) else str(c)
            test_pass += len(re.findall(r'\bPASSED\b', c))
            test_fail += len(re.findall(r'\bFAILED\b', c))
    return {
        "turns": len(assistant),
        "exit": info.get("exit_status"),
        "actions": actions,
        "n_invokes": n_invokes_total,
        "multi_invoke_turns": multi_invoke_turns,
        "qi": len(qi_cmds),
        "grep": len(grep_cmds),
        "qi_cmds": qi_cmds,
        "grep_cmds": grep_cmds,
        "test_pass": test_pass,
        "test_fail": test_fail,
        "tokens": info.get("model_stats", {}).get("instance_cost") or info.get("model_stats", {}).get("tokens_sent"),
        "model_stats": info.get("model_stats", {}),
    }

def main():
    rows = {}
    for arm in ["swebp_control", "swebp_treatment"]:
        files = sorted(glob.glob(f"{D}/{arm}/*/*.traj.json"))
        rows[arm] = []
        for f in files:
            rep = re.search(r'\.rep(\d+)\.', f).group(1)
            r = analyze_one(f)
            r["rep"] = rep
            rows[arm].append(r)

    KINDS = ["qi", "grep", "edit", "test", "read", "git", "echo", "other"]
    for arm in ["swebp_control", "swebp_treatment"]:
        print(f"\n===== {arm} =====")
        hdr = f"{'rep':>4} {'turns':>6} {'exit':>12} {'qi':>4} {'grep':>5} {'edit':>5} {'test':>5} {'read':>5} {'multi':>6} {'PASS':>6} {'FAIL':>6}"
        print(hdr)
        agg = Counter()
        tt = tq = tg = 0
        for r in rows[arm]:
            a = r["actions"]
            print(f"{r['rep']:>4} {r['turns']:>6} {str(r['exit']):>12} {r['qi']:>4} {r['grep']:>5} {a['edit']:>5} {a['test']:>5} {a['read']:>5} {r['multi_invoke_turns']:>6} {r['test_pass']:>6} {r['test_fail']:>6}")
            for k in KINDS: agg[k]+=a[k]
            tt+=r['turns']; tq+=r['qi']; tg+=r['grep']
        n=len(rows[arm])
        if n:
            print(f"{'MEAN':>4} {tt/n:>6.0f} {'':>12} {tq/n:>4.0f} {tg/n:>5.0f} {agg['edit']/n:>5.0f} {agg['test']/n:>5.0f} {agg['read']/n:>5.0f}")

    # qi command samples from treatment
    print("\n===== sample qi commands (treatment) =====")
    seen=set()
    for r in rows["swebp_treatment"]:
        for c in r["qi_cmds"]:
            if c not in seen:
                seen.add(c); print("  ", c)

if __name__ == "__main__":
    main()
