#!/usr/bin/env python3
"""Recover working-tree patches for empty-submission reps and re-score them.

Some agent runs solve the task -- the fix is sitting in the working tree -- yet
submit an *empty* patch because the final submit command echoed the sentinel
(``COMPLETE_TASK_AND_SUBMIT_FINAL_OUTPUT``) without piping ``cat patch.txt``.
The harness then records an empty patch and scores the run unresolved. Because
that slip is more frequent in the qi (treatment) arm, it biases the success
comparison *against* qi.

This script repairs the measurement, not the agent:

  1. Find every rep whose recorded submission is blank.
  2. Recover a candidate patch from the trajectory's last ``git diff`` tool
     output (the working-tree diff the agent produced before submitting).
  3. Re-score ONLY the reps where a patch is actually present -- "test against
     the patch being present" -- by re-running the SWE-bench harness on the
     recovered patch. Reps with no recoverable diff are left as genuine empties.

Corrected verdicts are written to the DB under run_tag ``<orig>__corr`` so the
as-submitted rows stay intact; the re-analysis overlays the corrected rows on
top of the originals. The harness is invoked with a distinct run_id (model
suffixed ``__corr``) so cached empty-patch reports are never reused.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # -> experiment/
from lib import paths
from lib.trajmeta import infer_path_meta, rep_of

import eval_db
from evaluate_patches import DATASETS, evaluate_group


def _flatten(content) -> str:
    """Tool message content -> a single string for substring scanning."""
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        return "\n".join(
            str(c.get("text", c)) if isinstance(c, dict) else str(c)
            for c in content
        )
    return str(content)


def recover_patch(traj: list[dict]) -> str:
    """Extract the last working-tree git diff shown in a trajectory.

    Returns the diff text (``diff --git`` .. end of the harness ``<output>``
    block), or "" when no diff is present. The most recent diff wins -- it is
    the closest to the (failed) submission.
    """
    found = ""
    for m in traj:
        if m.get("role") != "tool":
            continue
        s = _flatten(m.get("content"))
        idx = s.find("diff --git")
        if idx < 0:
            continue
        body = s[idx:]
        # Tool outputs are wrapped as <output> ... </output>; a diff may be
        # followed by <exception>/<returncode> from a chained command. Cut at
        # whichever wrapper marker comes first.
        for marker in ("</output>", "<exception>", "<returncode>"):
            j = body.find(marker)
            if j > 0:
                body = body[:j]
        found = body.rstrip() + "\n"
    return found


def patch_present(patch: str) -> bool:
    """A recovered string counts as a real patch only with a diff + a hunk."""
    return bool(patch) and "diff --git" in patch and "@@" in patch


def collect_empty_reps(logs: Path) -> list[dict]:
    """One record per rep whose recorded submission is blank."""
    out: list[dict] = []
    for path in sorted(logs.rglob("*.traj.json")):
        try:
            data = json.loads(path.read_text())
        except (json.JSONDecodeError, OSError) as exc:
            print(f"WARNING: skipping {path}: {exc}", file=sys.stderr)
            continue
        info = data.get("info", {})
        if (info.get("submission") or "").strip():
            continue  # has a real submission already
        model, _batch, arm, instance = infer_path_meta(path)
        traj = data.get("trajectory") or data.get("messages") or []
        recovered = recover_patch(traj)
        out.append({
            "model": model, "arm": arm, "instance_id": instance,
            "rep": rep_of(path), "exit_status": info.get("exit_status", ""),
            "patch": recovered, "has_patch": patch_present(recovered),
        })
    return out


def primary_tag(conn, model: str) -> str | None:
    """The model's as-submitted run_tag (excludes any prior __corr tag)."""
    row = conn.execute(
        "SELECT run_tag FROM eval_results "
        "WHERE model = ? AND run_tag NOT LIKE '%\\_\\_corr' ESCAPE '\\' "
        "ORDER BY run_tag DESC LIMIT 1",
        (model,),
    ).fetchone()
    return row[0] if row else None


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--logs", type=Path, default=paths.LOGS_DIR,
                    help=f"Logs directory (default: {paths.LOGS_DIR})")
    ap.add_argument("--subset", default="verified", choices=list(DATASETS))
    ap.add_argument("--split", default="test")
    ap.add_argument("--namespace", default="swebench")
    ap.add_argument("--max-workers", type=int, default=3)
    ap.add_argument("--timeout", type=int, default=1800)
    ap.add_argument("--db", type=Path, default=eval_db.DEFAULT_DB)
    ap.add_argument("--dry-run", action="store_true",
                    help="Report recoverable reps without invoking Docker")
    args = ap.parse_args()

    if not args.logs.is_dir():
        print(f"ERROR: logs directory not found: {args.logs}", file=sys.stderr)
        return 1

    empties = collect_empty_reps(args.logs)
    if not empties:
        print(f"No empty-submission reps under {args.logs}.")
        return 0

    recoverable = [e for e in empties if e["has_patch"]]
    print(f"Empty-submission reps: {len(empties)}  |  "
          f"with a recoverable patch: {len(recoverable)}  |  "
          f"genuinely empty: {len(empties) - len(recoverable)}")
    by_model: dict[str, list[dict]] = {}
    for e in empties:
        by_model.setdefault(e["model"], []).append(e)
    for model, grp in sorted(by_model.items()):
        rec = sum(1 for e in grp if e["has_patch"])
        print(f"  {model or '(none)':32s} {len(grp):2d} empty, {rec:2d} recoverable")
        for e in sorted(grp, key=lambda x: (x["arm"], x["instance_id"], x["rep"])):
            tag = "RECOVER" if e["has_patch"] else "no-patch"
            print(f"      [{tag}] {e['arm']:9s} {e['instance_id']:28s} rep {e['rep']}")

    if args.dry_run:
        print("\n[dry-run] no Docker evaluation performed.")
        return 0
    if not recoverable:
        print("\nNothing to re-score.")
        return 0

    dataset_name = DATASETS[args.subset]
    namespace = None if args.namespace.lower() == "none" else args.namespace

    # Group recoverable reps by (model, arm, rep) -- one harness invocation each,
    # mirroring evaluate_patches. The model is suffixed __corr ONLY to derive a
    # distinct run_id (and thus a fresh report path); DB rows keep the real model.
    groups: dict[tuple[str, str, str], list[dict]] = {}
    for e in recoverable:
        groups.setdefault((e["model"], e["arm"], e["rep"]), []).append(e)

    db_path = args.db.resolve()
    summary: list[tuple] = []
    # Recovered reps are scored under a fresh run_id (model suffixed __corr) with
    # rewrite_reports left FALSE: these instances originally produced empty
    # patches and so were never executed, so there is no cached container output
    # to reuse -- they must run fresh. (rewrite_reports=True filters out any
    # instance lacking a prior test_output.txt -- exactly these -- and the
    # harness then reports them as errors.) paths.cwd keeps the harness's report
    # + run_evaluation logs under results/ instead of the repo root.
    with paths.cwd(paths.REPORTS_DIR):
        for (model, arm, rep), grp in sorted(groups.items()):
            print(f"\n=== Re-scoring recovered {model or '(none)'} {arm} rep {rep} "
                  f"({len(grp)} instances) ===", flush=True)
            outcome = evaluate_group(
                grp, f"{model}__corr" if model else "corr", arm, rep,
                dataset_name, args.split, args.max_workers, args.timeout, namespace,
            )
            conn = eval_db.connect(db_path)
            try:
                base = primary_tag(conn, model) or model
                run_tag = f"{base}__corr"
                for r in sorted(grp, key=lambda x: x["instance_id"]):
                    oc = outcome.get(r["instance_id"], "incomplete")
                    eval_db.upsert(conn, run_tag, eval_db.EvalResult(
                        model=r["model"], arm=r["arm"], instance_id=r["instance_id"],
                        rep=r["rep"], exit_status=r["exit_status"],
                        has_patch=True, outcome=oc, dataset=dataset_name))
                    summary.append((model, arm, r["instance_id"], r["rep"], oc))
            finally:
                conn.close()

    print("\n--- Corrected outcomes (recovered patches) ---")
    res = sum(1 for s in summary if s[4] == "resolved")
    print(f"  re-scored {len(summary)} reps -> {res} now resolved")
    for s in sorted(summary):
        print(f"  {s[0]:28s} {s[1]:9s} {s[2]:28s} rep {s[3]}: {s[4]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
