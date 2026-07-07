#!/usr/bin/env python3
"""Analyze all ansible trajectory JSONs across models/arms."""
import json, os, sys, re
from collections import defaultdict
from pathlib import Path

BASE = Path("experiment/logs")

def parse_tool_calls_from_content(content):
    """Count qi, grep, bash, edit, test calls in text content."""
    if isinstance(content, list):
        content = " ".join(str(c) for c in content)
    content = str(content)
    
    qi = grep = bash = edit = test_run = 0
    
    # Count ```bash blocks as bash calls
    bash_blocks = re.findall(r'```bash\n(.*?)```', content, re.DOTALL)
    for block in bash_blocks:
        bash += 1
        # Count qi and grep within bash blocks
        qi += len(re.findall(r'(?:^|\s)qi\s', block))
        grep += len(re.findall(r'(?:^|\s)grep\s', block))
        edit += len(re.findall(r'(?:^|\s)(sed|perl\s+-i)\s', block))
        test_run += len(re.findall(r'(?:pytest|python.*test|run_tests)', block))
    
    return qi, grep, bash, edit, test_run


def analyze_traj(path, arm, model, rep):
    with open(path) as f:
        traj = json.load(f)
    
    msgs = traj.get("messages", [])
    assistant_msgs = [m for m in msgs if m.get("role") == "assistant"]
    turns = len(assistant_msgs)
    
    total_qi = total_grep = total_bash = total_edit = total_test = 0
    total_tokens = 0
    
    for m in msgs:
        content = m.get("content", "")
        qi, gr, ba, ed, te = parse_tool_calls_from_content(content)
        total_qi += qi
        total_grep += gr
        total_bash += ba
        total_edit += ed
        total_test += te
    
    # Token count - check for usage info
    # Try to sum token counts from the completion
    if "usage" in traj:
        total_tokens = traj["usage"].get("total_tokens", 0)
    elif "total_tokens" in traj:
        total_tokens = traj["total_tokens"]
    else:
        # Estimate from message content lengths
        for m in msgs:
            content = str(m.get("content", ""))
            total_tokens += len(content) // 3  # rough estimate
    
    return {
        "model": model,
        "arm": arm,
        "rep": rep,
        "turns": turns,
        "qi": total_qi,
        "grep": total_grep,
        "bash": total_bash,
        "edit": total_edit,
        "test": total_test,
        "tokens_est": total_tokens,
    }


def main():
    results = []
    
    # Walk all ansible trajectory JSONs
    for traj_path in sorted(BASE.rglob("pro_pilot_ansible_*/swebp_*/**/*.traj.json")):
        parts = traj_path.parts
        # Extract model from path: experiment/logs/<provider>--<model>/<batch_name>/<arm>/...
        # Model is parts[2] which is "<provider>--<model>"
        # Batch is parts[3]
        # Arm is parts[4]
        model_dir = parts[2]
        batch = parts[3]
        arm = parts[4]
        
        # Extract rep from filename
        m = re.search(r'\.rep(\d+)\.traj\.json$', traj_path.name)
        if not m:
            continue
        rep = f"rep{m.group(1)}"
        
        model_name = model_dir.split("--", 1)[-1] if "--" in model_dir else model_dir
        
        try:
            r = analyze_traj(str(traj_path), arm, model_name, rep)
            r["batch"] = batch
            results.append(r)
        except Exception as e:
            print(f"Error reading {traj_path}: {e}", file=sys.stderr)
    
    if not results:
        print("No results found")
        return
    
    # Group by model
    models = sorted(set(r["model"] for r in results))
    
    # Print per-model summary
    print(f"{'Model':<22} {'Arm':<16} {'Runs':>5} {'Resolved':>9} {'MeanTurns':>10} {'MeanTokens':>12} {'MeanQi':>8} {'MeanGrep':>10} {'MeanBash':>10} {'MeanEdit':>10} {'MeanTest':>10}")
    print("-" * 140)
    
    import csv
    eval_data = {}  # (model, arm, rep) -> resolved
    eval_dirs = [
        "experiment/results/pro_runs/deepseek_v4_flash_ansible",
        "experiment/results/pro_runs/mimo_v2.5-pro_ansible",
        "experiment/results/pro_runs/deepseek_v4_pro_ansible",
    ]
    for ed in eval_dirs:
        csv_path = Path(ed) / "eval_results.csv"
        if csv_path.exists():
            with open(csv_path) as f:
                for row in csv.DictReader(f):
                    key = (row.get("model",""), row["arm"], row["rep"])
                    eval_data[key] = int(row.get("resolved", 0))
    
    for model in models:
        model_results = [r for r in results if r["model"] == model]
        arms = sorted(set(r["arm"] for r in model_results))
        
        for arm in arms:
            arm_results = [r for r in model_results if r["arm"] == arm]
            n = len(arm_results)
            resolved = sum(eval_data.get((model, arm, r["rep"]), 0) for r in arm_results)
            
            mean_turns = sum(r["turns"] for r in arm_results) / n
            mean_tokens = sum(r["tokens_est"] for r in arm_results) / n
            mean_qi = sum(r["qi"] for r in arm_results) / n
            mean_grep = sum(r["grep"] for r in arm_results) / n
            mean_bash = sum(r["bash"] for r in arm_results) / n
            mean_edit = sum(r["edit"] for r in arm_results) / n
            mean_test = sum(r["test"] for r in arm_results) / n
            
            print(f"{model:<22} {arm:<16} {n:>5} {resolved:>5}/{n:<3} {mean_turns:>10.1f} {mean_tokens:>12.0f} {mean_qi:>8.1f} {mean_grep:>10.1f} {mean_bash:>10.1f} {mean_edit:>10.1f} {mean_test:>10.1f}")
    
    # Per-model treatment vs control summary
    print("\n--- Treatment vs Control (pooled) ---")
    print(f"{'Model':<22} {'Arm':<16} {'MeanTurns':>10} {'MeanTokens':>12} {'MeanQi':>8} {'MeanGrep':>10}")
    print("-" * 90)
    for model in models:
        model_results = [r for r in results if r["model"] == model]
        for arm in ["swebp_control", "swebp_treatment"]:
            arm_results = [r for r in model_results if r["arm"] == arm]
            if not arm_results:
                continue
            n = len(arm_results)
            mean_turns = sum(r["turns"] for r in arm_results) / n
            mean_tokens = sum(r["tokens_est"] for r in arm_results) / n
            mean_qi = sum(r["qi"] for r in arm_results) / n
            mean_grep = sum(r["grep"] for r in arm_results) / n
            print(f"{model:<22} {arm:<16} {mean_turns:>10.1f} {mean_tokens:>12.0f} {mean_qi:>8.1f} {mean_grep:>10.1f}")
    
    # Turn distribution
    print("\n--- Turn Distribution ---")
    for model in models:
        model_results = [r for r in results if r["model"] == model]
        for arm in ["swebp_control", "swebp_treatment"]:
            arm_results = [r for r in model_results if r["arm"] == arm]
            if not arm_results:
                continue
            turns = sorted(r["turns"] for r in arm_results)
            print(f"{model:<22} {arm:<16} min={min(turns):>3} max={max(turns):>3} median={turns[len(turns)//2]:>3} n={len(turns)}")


if __name__ == "__main__":
    main()
