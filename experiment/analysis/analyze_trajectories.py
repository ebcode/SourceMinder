#!/usr/bin/env python3
"""Extract per-run metrics from mini-swe-agent trajectory files.

Walks a logs directory for ``*.traj.json`` files, pulls per-turn token usage
from each assistant message's ``extra.response.usage`` (the DeepSeek API's own
accounting), and writes one row per run to a CSV plus a by-arm summary.

Primary metrics (per PREREGISTRATION.md §7.1):
  - total_input_tokens   sum of prompt_tokens across all turns
  - peak_prompt_tokens   max prompt_tokens in any single turn (context pressure)
  - total_tool_output    approximate tokens of tool output shown to the model

Trajectory layout handled (all are accepted):
  logs/<model>/<arm>/<instance>/<run_id>.traj.json   (run_experiment.py)
  logs/<arm>/<instance>/<run_id>.traj.json           (legacy, no model dir)
  logs/<instance>_<arm>.traj.json                    (legacy compare.sh)

Usage:
  python3 experiment/analysis/analyze_trajectories.py \
      --logs experiment/logs --dir experiment/analysis/20260616_223000
"""
from __future__ import annotations

import argparse
import csv
import json
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # -> experiment/
from lib import cmds, paths
from lib.trajmeta import ARMS, infer_path_meta, batch_of, n_files_of, patch_files_of

# Tool-output tokens are not API-counted. The DeepSeek tokenizer is the exact
# answer (PREREGISTRATION Open Q4); until that is wired in we approximate at
# ~4 chars/token, which is adequate for the pilot's descriptive stats.
CHARS_PER_TOKEN = 4.0


def analyze_one(path: Path) -> dict | None:
    try:
        data = json.loads(path.read_text())
    except (json.JSONDecodeError, OSError) as exc:
        print(f"WARNING: skipping {path}: {exc}", file=sys.stderr)
        return None

    messages = data.get("messages", [])
    info = data.get("info", {})
    model, batch, arm, instance = infer_path_meta(path)

    prompt_toks: list[int] = []
    completion_toks: list[int] = []
    reasoning_toks = 0
    cached_toks = 0
    qi_n = grep_n = read_n = 0

    for msg in messages:
        extra = msg.get("extra")
        if isinstance(extra, dict):
            resp = extra.get("response")
            if isinstance(resp, dict) and isinstance(resp.get("usage"), dict):
                u = resp["usage"]
                prompt_toks.append(u.get("prompt_tokens", 0))
                completion_toks.append(u.get("completion_tokens", 0))
                reasoning_toks += (u.get("completion_tokens_details") or {}).get("reasoning_tokens", 0)
                cached_toks += (u.get("prompt_tokens_details") or {}).get("cached_tokens", 0)
            for action in extra.get("actions") or []:
                cmd = action.get("command", "") if isinstance(action, dict) else ""
                dq, dg, dr = cmds.count_tools(cmd)
                qi_n += dq
                grep_n += dg
                read_n += dr

    # Approximate tool-output tokens from the observation (role == "tool") msgs.
    tool_chars = sum(
        len(str(m.get("content", ""))) for m in messages if m.get("role") == "tool"
    )

    if not prompt_toks:
        print(f"WARNING: no usage data in {path}", file=sys.stderr)
        return None

    submission = (info.get("submission") or "").strip()
    return {
        "batch_id": batch or batch_of(path),
        "run_id": path.name.replace(".traj.json", ""),
        "model": model,
        "instance_id": instance,
        "arm": arm,
        "n_files": n_files_of(path),
        "patch_files": patch_files_of(submission),
        "exit_status": info.get("exit_status", ""),
        "turn_count": len(prompt_toks),
        "total_input_tokens": sum(prompt_toks),
        "peak_prompt_tokens": max(prompt_toks),
        "total_completion_tokens": sum(completion_toks),
        "total_reasoning_tokens": reasoning_toks,
        "total_cached_tokens": cached_toks,
        "tool_output_tokens_approx": round(tool_chars / CHARS_PER_TOKEN),
        "qi_invocations": qi_n,
        "grep_invocations": grep_n,
        "file_read_invocations": read_n,
        "submitted": bool(submission),
        "source": str(path),
    }


def summarize(rows: list[dict]) -> None:
    metrics = ("total_input_tokens", "peak_prompt_tokens", "tool_output_tokens_approx")
    # Group by model: arms are only comparable within the same model.
    models = sorted({r["model"] for r in rows})
    for model in models:
        model_rows = [r for r in rows if r["model"] == model]
        label = model or "(unknown model)"
        print(f"\n=== Summary by arm: {label} ===")
        for arm in ARMS:
            arm_rows = [r for r in model_rows if r["arm"] == arm]
            if not arm_rows:
                continue
            print(f"\n[{arm}] n={len(arm_rows)} runs")
            for m in metrics:
                vals = [r[m] for r in arm_rows]
                med = statistics.median(vals)
                print(f"  {m:28s} median={med:>10,.0f}  min={min(vals):>9,}  max={max(vals):>9,}")
            qi = sum(r["qi_invocations"] for r in arm_rows)
            grep = sum(r["grep_invocations"] for r in arm_rows)
            print(f"  {'qi / grep invocations':28s} {qi} / {grep}")

        ctrl = [r["peak_prompt_tokens"] for r in model_rows if r["arm"] == "control"]
        treat = [r["peak_prompt_tokens"] for r in model_rows if r["arm"] == "treatment"]
        if ctrl and treat:
            cm = statistics.median(ctrl)
            tm = statistics.median(treat)
            if cm:
                print(f"\n  median peak prompt tokens: treatment vs control = {(tm - cm) / cm:+.1%}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--logs", type=Path, default=paths.LOGS_DIR,
                    help=f"directory of *.traj.json files (default: {paths.LOGS_DIR})")
    ap.add_argument("--dir", type=Path, default=None,
                    help="Analysis output directory (default: results/runs/<timestamp>/)")
    ap.add_argument("--batch", default=None, metavar="BATCH_ID",
                    help="Filter to trajectories whose manifest batch_id matches; "
                         "also sets the output directory to results/runs/<batch>/")
    args = ap.parse_args()

    logs_dir = Path(args.logs)
    if not logs_dir.is_dir():
        print(f"ERROR: not a directory: {logs_dir}", file=sys.stderr)
        return 1

    out_dir = args.dir or paths.new_run_dir(batch_id=args.batch or "")
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / "runs.csv"

    traj_files = sorted(logs_dir.rglob("*.traj.json"))
    if not traj_files:
        print(f"No *.traj.json files under {logs_dir}", file=sys.stderr)
        return 1

    rows = [r for r in (analyze_one(p) for p in traj_files) if r]
    if args.batch:
        rows = [r for r in rows if r.get("batch_id") == args.batch]
    if not rows:
        print("No analyzable runs found.", file=sys.stderr)
        return 1

    with out_path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    print(f"Wrote {len(rows)} run(s) -> {out_path}")
    summarize(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
