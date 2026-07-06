#!/usr/bin/env python3
"""One-command status dashboard over all Pro batch result dirs.

Each Pro batch is one instance x N reps per arm (see PRO_ANALYZE.md). This
script scans every `results/pro_runs/<batch>/` dir and prints one row per batch
so the cross-instance survey stops being hand-rebuilt and the canonical manifest
stays honest. It reads only already-computed per-batch artifacts -- it never
re-parses trajectories.

Columns:
  batch                 dir name; a leading '*' marks batches in the canonical
                        cross_instance_manifest.txt
  model                 from runs_with_success.csv
  instance              short label (strip org + sha), like cross_batch_compare
  reps (ctrl/treat)     rep count per arm
  resolve (ctrl->treat) resolved/n per arm from eval_results.csv
  tokens d%             median pct-change in total_tokens (neg = treatment less);
                        a trailing '*' marks mwu_p < 0.05
  cost d%               median pct-change in cost (same convention)
  wall                  y/n -- wall_time.csv present with rows
  tax                   y -- format-tax model (deepseek/mimo); token/cost
                        inflated, see PRO_ANALYZE.md -> Format tax
  charts                count of files in charts/

Usage:
  experiment/.venv_pro/bin/python experiment/analysis/pro_batch_status.py
  # restrict to the canonical 5:
  ... pro_batch_status.py --manifest experiment/analysis/cross_instance_manifest.txt
  # machine-readable:
  ... pro_batch_status.py --csv > status.csv

Stdlib only (csv, argparse, pathlib), matching the other analysis scripts.
Shared helpers are imported from analyze_pro_stats.py / cross_batch_compare.py
rather than copied.
"""
from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

# Shared helpers live alongside this script. The script's own dir is sys.path[0]
# when invoked by full path, so a plain import resolves (mirrors cross_batch_compare).
sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze_pro_stats import CONTROL, TREATMENT, fnum  # noqa: E402
from cross_batch_compare import short_label  # noqa: E402

DEFAULT_PRO_RUNS = Path(__file__).resolve().parent.parent / "results" / "pro_runs"
DEFAULT_MANIFEST = Path(__file__).resolve().parent / "cross_instance_manifest.txt"

# Format tax is a model property (per-turn turns wasted on reasoning_content/XML
# parsing -- see PRO_ANALYZE.md -> Format tax). cross_batch_compare keys it by
# instance for the canonical 5; here we scan every batch, so we key by model slug
# to flag tax models on any instance.
FORMAT_TAX_MODEL_SUBSTR = ("deepseek", "mimo")

MISSING = "—"  # em dash for absent values


def read_csv(path: Path) -> list[dict]:
    if not path.is_file():
        return []
    with path.open(newline="") as fh:
        return list(csv.DictReader(fh))


def is_format_tax(model: str) -> bool:
    m = (model or "").lower()
    return any(s in m for s in FORMAT_TAX_MODEL_SUBSTR)


def resolve_counts(eval_rows: list[dict]) -> dict:
    """{arm: (resolved, n)} from eval_results.csv 'resolved' column (truthy)."""
    out = {CONTROL: [0, 0], TREATMENT: [0, 0]}
    for r in eval_rows:
        arm = r.get("arm")
        if arm not in out:
            continue
        out[arm][1] += 1
        val = str(r.get("resolved", "")).strip().lower()
        if val in ("1", "true", "yes", "resolved"):
            out[arm][0] += 1
    return out


def metric_pct(stats_rows: list[dict], metric: str) -> str:
    """Median pct-change for `metric`, with '*' when mwu_p < 0.05."""
    for r in stats_rows:
        if r.get("metric") != metric:
            continue
        pct = fnum(r.get("pct_change_median"))
        p = fnum(r.get("mwu_p"))
        star = "*" if (p == p and p < 0.05) else ""  # p==p screens out NaN
        sign = "+" if pct >= 0 else ""
        return f"{sign}{pct * 100:.0f}%{star}"
    return MISSING


def survey_batch(batch_dir: Path) -> dict:
    runs = read_csv(batch_dir / "runs_with_success.csv")
    stats = read_csv(batch_dir / "pro_stats_summary.csv")
    evals = read_csv(batch_dir / "eval_results.csv")
    wall = read_csv(batch_dir / "wall_time.csv")
    charts = batch_dir / "charts"

    model = runs[0]["model"] if runs else ""
    instance = short_label(runs[0]["instance_id"]) if runs else MISSING

    reps = {CONTROL: 0, TREATMENT: 0}
    for r in runs:
        if r.get("arm") in reps:
            reps[r["arm"]] += 1

    rc = resolve_counts(evals)
    resolve = (f"{rc[CONTROL][0]}/{rc[CONTROL][1]}->{rc[TREATMENT][0]}/{rc[TREATMENT][1]}"
               if evals else MISSING)

    return {
        "batch": batch_dir.name,
        "model": model or MISSING,
        "instance": instance,
        "reps": f"{reps[CONTROL]}/{reps[TREATMENT]}" if runs else MISSING,
        "resolve": resolve,
        "tokens": metric_pct(stats, "total_tokens"),
        "cost": metric_pct(stats, "cost"),
        "wall": "y" if wall else "n",
        "tax": "y" if is_format_tax(model) else "",
        "charts": str(sum(1 for _ in charts.iterdir())) if charts.is_dir() else "0",
    }


COLUMNS = [
    ("batch", "batch"),
    ("model", "model"),
    ("instance", "instance"),
    ("reps", "reps (ctrl/treat)"),
    ("resolve", "resolve (ctrl->treat)"),
    ("tokens", "tokens d%"),
    ("cost", "cost d%"),
    ("wall", "wall"),
    ("tax", "tax"),
    ("charts", "charts"),
]


def print_table(rows: list[dict]) -> None:
    headers = [h for _, h in COLUMNS]
    keys = [k for k, _ in COLUMNS]
    widths = [len(h) for h in headers]
    for row in rows:
        for i, k in enumerate(keys):
            widths[i] = max(widths[i], len(str(row.get(k, ""))))
    line = "  ".join(h.ljust(widths[i]) for i, h in enumerate(headers))
    print(line)
    print("  ".join("-" * widths[i] for i in range(len(headers))))
    for row in rows:
        print("  ".join(str(row.get(k, "")).ljust(widths[i])
                        for i, k in enumerate(keys)))
    print()
    print("legend: *batch = in canonical manifest; tokens/cost *N% = mwu_p < 0.05; "
          f"{MISSING} = artifact missing (batch needs reprocessing)")


def write_csv(rows: list[dict]) -> None:
    w = csv.DictWriter(sys.stdout, fieldnames=[k for k, _ in COLUMNS])
    w.writeheader()
    for row in rows:
        w.writerow({k: row.get(k, "") for k, _ in COLUMNS})


def load_manifest(path: Path) -> set[str]:
    names = set()
    for raw in path.read_text().splitlines():
        line = raw.split("#", 1)[0].strip()
        if line:
            names.add((path.parent / line).resolve().name)
    return names


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pro-runs", type=Path, default=DEFAULT_PRO_RUNS,
                    help="results/pro_runs dir to scan (default: alongside script)")
    ap.add_argument("--manifest", type=Path,
                    help="restrict to batches listed in this manifest file")
    ap.add_argument("--csv", action="store_true", help="emit CSV instead of a table")
    args = ap.parse_args()

    if not args.pro_runs.is_dir():
        print(f"pro_batch_status: no such dir: {args.pro_runs}", file=sys.stderr)
        return 1

    # Always know the canonical set (to mark it). --manifest additionally
    # restricts the scan to its listed batches.
    if args.manifest:
        canonical = load_manifest(args.manifest)
        restrict = canonical
    else:
        canonical = load_manifest(DEFAULT_MANIFEST) if DEFAULT_MANIFEST.is_file() else set()
        restrict = None

    batch_dirs = sorted(d for d in args.pro_runs.iterdir()
                        if d.is_dir() and d.name != "_cross"
                        and (restrict is None or d.name in restrict))
    if not batch_dirs:
        print("pro_batch_status: no batch dirs found", file=sys.stderr)
        return 1

    rows = [survey_batch(d) for d in batch_dirs]
    # Star the canonical-manifest batches in the batch column (table only).
    if canonical and not args.csv:
        for row in rows:
            if row["batch"] in canonical:
                row["batch"] = "*" + row["batch"]
    # Sort by instance then model so related batches group together.
    rows.sort(key=lambda r: (r["instance"], r["model"], r["batch"]))

    if args.csv:
        write_csv(rows)
    else:
        print_table(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
