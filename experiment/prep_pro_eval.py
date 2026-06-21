#!/usr/bin/env python3
"""
Build the two inputs the official SWE-bench Pro evaluator (swe_bench_pro_eval.py)
needs, from our agent's predictions. SourceMinder-side glue: the vendored eval
script stays untouched, we just feed it.

It produces, into --out-dir:
  raw_sample.jsonl   one row per evaluated instance with the columns the eval
                     reads: instance_id, repo, before_repo_set_cmd,
                     selected_test_files_to_run, base_commit, fail_to_pass,
                     pass_to_pass. The list-valued fields are kept as their
                     string reprs because the eval calls eval() on them.
  patches.json       [{instance_id, patch, prefix}] -- the gathered-patch format
                     swe_bench_pro_eval.py --patch_path expects.

Predictions input (--preds) may be either:
  - a batch-runner preds.json   ({iid: {model_patch, ...}}), or
  - a directory of run_pro_one.py .pred files (<iid>/<iid>*.pred JSON).

Run with the Pro venv (has datasets):
    experiment/.venv_pro/bin/python experiment/prep_pro_eval.py \
        --preds logs/pro_pilot/swebp_control/preds.json \
        --prefix swebp_control --out-dir results/pro_eval/swebp_control
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

EXPERIMENT_DIR = Path(__file__).resolve().parent
DEFAULT_SUBSET = str(EXPERIMENT_DIR / "data" / "swebench_pro")

# Columns swe_bench_pro_eval.py reads from the raw sample (see create_entryscript
# and the scoring in main()). Dockerfiles come from the eval repo on disk, not here.
RAW_FIELDS = ["instance_id", "repo", "before_repo_set_cmd",
              "selected_test_files_to_run", "base_commit",
              "fail_to_pass", "pass_to_pass"]


def load_predictions(preds: Path) -> dict[str, str]:
    """Return {instance_id: patch_text} from a preds.json or a dir of .pred files."""
    out: dict[str, str] = {}
    if preds.is_dir():
        for pred_file in sorted(preds.rglob("*.pred")):
            rec = json.loads(pred_file.read_text())
            out[rec["instance_id"]] = rec.get("model_patch") or rec.get("patch") or ""
    else:
        data = json.loads(preds.read_text())
        for iid, rec in data.items():
            out[iid] = rec.get("model_patch") or rec.get("patch") or ""
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--preds", required=True,
                    help="preds.json (batch runner) or a dir of .pred files")
    ap.add_argument("--prefix", default="", help="prefix tag for each patch entry")
    ap.add_argument("--out-dir", required=True, help="where to write the eval inputs")
    ap.add_argument("--subset", default=DEFAULT_SUBSET, help="dataset path/name")
    ap.add_argument("--split", default="test")
    args = ap.parse_args()

    from datasets import load_dataset

    patches = load_predictions(Path(args.preds))
    if not patches:
        print("ERROR: no predictions found", flush=True)
        return 1
    print(f"{len(patches)} prediction(s) loaded")

    ds = load_dataset(args.subset, split=args.split)
    by_id = {r["instance_id"]: r for r in ds}

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    raw_rows, patch_entries, missing = [], [], []
    for iid, patch in patches.items():
        row = by_id.get(iid)
        if row is None:
            missing.append(iid)
            continue
        raw_rows.append({k: row[k] for k in RAW_FIELDS})
        patch_entries.append({"instance_id": iid, "patch": patch, "prefix": args.prefix})

    if missing:
        print(f"WARNING: {len(missing)} instance(s) not in dataset: {missing[:3]}")

    raw_path = out_dir / "raw_sample.jsonl"
    with raw_path.open("w") as f:
        for row in raw_rows:
            f.write(json.dumps(row) + "\n")
    patches_path = out_dir / "patches.json"
    patches_path.write_text(json.dumps(patch_entries, indent=2) + "\n")

    print(f"wrote {raw_path} ({len(raw_rows)} rows)")
    print(f"wrote {patches_path} ({len(patch_entries)} patches)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
