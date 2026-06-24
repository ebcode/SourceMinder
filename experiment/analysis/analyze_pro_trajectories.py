#!/usr/bin/env python3
"""Extract per-run metrics from SWE-bench **Pro** trajectory files.

Pro analog of analyze_trajectories.py. Same token source -- each assistant
message's ``extra.response.usage`` (the DeepSeek API's own accounting, which Pro
trajectories DO carry) -- but adapted to the Pro on-disk layout and arm names,
and with no dependence on run_experiment.py's batch manifests (the Pro runners
don't write them).

Pro layout handled:
  logs/pro_pilot/<arm>/<instance_id>/<instance_id>.<run_id>.traj.json
  logs/pro_pilot/<arm>/<instance_id>/<instance_id>.traj.json   (run_id -> "base")

where <arm> is e.g. swebp_control / swebp_treatment and <run_id> is e.g. rep01
or oldprompt_rep01 (see run_pro_reps.py --run-id-prefix).

Writes one row per run to <dir>/runs.csv with the columns merge_results.py joins
on (model, arm, instance_id, run_id) plus the per-run metrics.

Usage:
  experiment/.venv_pro/bin/python experiment/analysis/analyze_pro_trajectories.py \
      --dir experiment/results/pro_runs/<batch> [--run-prefix oldprompt_]
"""
from __future__ import annotations

import argparse
import csv
import re
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # -> experiment/
from lib import cmds, paths  # noqa: E402
import json  # noqa: E402

DEFAULT_LOGS = paths.LOGS_DIR / "pro_pilot"
DEFAULT_ARMS = ["swebp_control", "swebp_treatment"]
CHARS_PER_TOKEN = 4.0
BASH_RE = re.compile(r"```bash\s*(.+?)```", re.S)
DIFF_FILE_RE = re.compile(r"^\+\+\+ b/(.+)", re.M)


def norm_model(s: str) -> str:
    """deepseek/deepseek-v4-flash -> deepseek-v4-flash (stable join key)."""
    return (s or "").split("/")[-1].strip().lower()


def parse_run_id(path: Path, instance_id: str) -> str:
    stem = path.name
    for suf in (".traj.json", ".pred"):
        if stem.endswith(suf):
            stem = stem[: -len(suf)]
            break
    if stem == instance_id:
        return "base"
    if stem.startswith(instance_id + "."):
        return stem[len(instance_id) + 1 :]
    return stem


def _patch_stats(submission: str) -> tuple[int, int]:
    if not submission.strip():
        return 0, 0
    files = DIFF_FILE_RE.findall(submission)
    lines = submission.count("\n")
    return len(set(files)), lines


def analyze_one(path: Path) -> dict | None:
    try:
        data = json.loads(path.read_text())
    except (json.JSONDecodeError, OSError) as exc:
        print(f"WARNING: skipping {path}: {exc}", file=sys.stderr)
        return None

    messages = data.get("messages", [])
    info = data.get("info", {})
    # arm = the dir above the instance dir; instance = the dir holding the file.
    instance_id = path.parent.name
    arm = path.parent.parent.name

    prompt_toks: list[int] = []
    completion_toks: list[int] = []
    reasoning_toks = cached_toks = 0
    qi_n = grep_n = read_n = 0
    # qi-usage quality rollups (per shared lib.cmds detectors): -p adoption and
    # the three misuse markers, counted per qi sub-command across the run.
    qi_parent_n = qi_dotted_n = qi_quoted_n = qi_abs_n = qi_verbose_n = 0
    model = ""

    for msg in messages:
        if msg.get("role") == "assistant":
            mm = BASH_RE.search(str(msg.get("content", "")))
            if mm:
                block = mm.group(1)
                dq, dg, dr = cmds.count_tools(block)
                qi_n += dq
                grep_n += dg
                read_n += dr
                for sub in cmds.qi_subcommands(block):
                    qi_parent_n += cmds.qi_parent_filter(sub)
                    qi_dotted_n += cmds.qi_dotted_pattern(sub)
                    qi_quoted_n += cmds.qi_quoted_phrase(sub)
                    qi_abs_n += cmds.qi_abs_path_filter(sub)
                    qi_verbose_n += cmds.qi_verbose_filter(sub)
        extra = msg.get("extra")
        if isinstance(extra, dict):
            resp = extra.get("response")
            if isinstance(resp, dict) and isinstance(resp.get("usage"), dict):
                u = resp["usage"]
                prompt_toks.append(u.get("prompt_tokens", 0))
                completion_toks.append(u.get("completion_tokens", 0))
                reasoning_toks += (u.get("completion_tokens_details") or {}).get("reasoning_tokens", 0)
                cached_toks += (u.get("prompt_tokens_details") or {}).get("cached_tokens", 0)
                model = model or resp.get("model", "")

    if not prompt_toks:
        print(f"WARNING: no usage data in {path}", file=sys.stderr)
        return None

    # Observation tokens: Pro renders tool output as user messages wrapping a
    # <returncode> block (the action_observation_template). Count those only, so
    # the instance_template (the task prompt) isn't miscounted as tool output.
    tool_chars = sum(
        len(str(m.get("content", "")))
        for m in messages
        if m.get("role") == "user" and "<returncode>" in str(m.get("content", ""))
    )

    model = model or info.get("config", {}).get("model", {}).get("model_name", "")
    stats = info.get("model_stats", {}) or {}
    submission = (info.get("submission") or "").strip()
    files_touched, patch_lines = _patch_stats(submission)
    return {
        "model": norm_model(model),
        "arm": arm,
        "instance_id": instance_id,
        "run_id": parse_run_id(path, instance_id),
        "exit_status": info.get("exit_status", ""),
        "turn_count": len(prompt_toks),
        "api_calls": stats.get("api_calls", len(prompt_toks)),
        "cost": stats.get("instance_cost", ""),
        "total_input_tokens": sum(prompt_toks),
        "peak_prompt_tokens": max(prompt_toks),
        "total_completion_tokens": sum(completion_toks),
        "total_tokens": sum(prompt_toks) + sum(completion_toks),
        "total_reasoning_tokens": reasoning_toks,
        "total_cached_tokens": cached_toks,
        "tool_output_tokens_approx": round(tool_chars / CHARS_PER_TOKEN),
        "qi_invocations": qi_n,
        "grep_invocations": grep_n,
        "file_read_invocations": read_n,
        "qi_parent_calls": qi_parent_n,
        "qi_dotted_name": qi_dotted_n,
        "qi_quoted_phrase": qi_quoted_n,
        "qi_abs_path": qi_abs_n,
        "qi_verbose_calls": qi_verbose_n,
        "submitted": bool(submission),
        "patch_chars": len(submission),
        "patch_lines": patch_lines,
        "files_touched": files_touched,
        "source": str(path),
    }


def summarize(rows: list[dict]) -> None:
    metrics = ("turn_count", "total_input_tokens", "peak_prompt_tokens", "total_tokens")
    for arm in sorted({r["arm"] for r in rows}):
        arm_rows = [r for r in rows if r["arm"] == arm]
        print(f"\n[{arm}] n={len(arm_rows)} runs")
        for m in metrics:
            vals = [r[m] for r in arm_rows]
            print(f"  {m:28s} median={statistics.median(vals):>10,.0f}  "
                  f"min={min(vals):>9,}  max={max(vals):>9,}")
        qi = sum(r["qi_invocations"] for r in arm_rows)
        grep = sum(r["grep_invocations"] for r in arm_rows)
        print(f"  {'qi / grep invocations':28s} {qi} / {grep}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--logs", type=Path, default=DEFAULT_LOGS,
                    help=f"Pro logs root (default: {DEFAULT_LOGS})")
    ap.add_argument("--dir", type=Path, required=True,
                    help="Output dir for runs.csv (e.g. results/pro_runs/<batch>)")
    ap.add_argument("--arms", nargs="+", default=DEFAULT_ARMS,
                    help=f"Arms to include (default: {' '.join(DEFAULT_ARMS)})")
    ap.add_argument("--run-prefix", default="",
                    help="Only include runs whose run_id starts with this prefix "
                         "(scopes a rep batch, e.g. 'oldprompt_')")
    args = ap.parse_args()

    if not args.logs.is_dir():
        print(f"ERROR: not a directory: {args.logs}", file=sys.stderr)
        return 1
    args.dir.mkdir(parents=True, exist_ok=True)

    rows = []
    for arm in args.arms:
        for p in sorted((args.logs / arm).rglob("*.traj.json")):
            r = analyze_one(p)
            if r and r["run_id"].startswith(args.run_prefix):
                rows.append(r)
    if not rows:
        print("No analyzable runs found.", file=sys.stderr)
        return 1

    out_path = args.dir / "runs.csv"
    with out_path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    print(f"Wrote {len(rows)} run(s) -> {out_path}")
    summarize(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
