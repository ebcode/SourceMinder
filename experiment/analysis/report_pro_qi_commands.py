#!/usr/bin/env python3
"""Per-arm qi-command report for SWE-bench **Pro** runs.

The per-command CSV from extract_pro_qi_commands.py uses the *same* schema as the
Verified extractor, so the whole reporting machinery in report_qi_commands.py
applies unchanged -- this is a thin entry point that only swaps the default CSV
path to a Pro results location. All tables (essentials with -p adoption + the
antipattern misuse rates, output-by-type, abandonment, composability, cross-model)
come straight from report_qi_commands.

Usage:
  experiment/.venv_pro/bin/python experiment/analysis/report_pro_qi_commands.py \
      --csv experiment/results/pro_runs/<batch>/qi_commands.csv
  ... --cross-model        # arm-by-model matrix instead of per-model detail
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # -> experiment/
from analysis import report_qi_commands as R  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--csv", type=Path, required=True,
                    help="per-command CSV from extract_pro_qi_commands.py")
    ap.add_argument("--model", default=None, metavar="SUBSTR",
                    help="only report models whose id contains SUBSTR")
    ap.add_argument("--cross-model", action="store_true",
                    help="print an arm-by-model matrix instead of per-model detail")
    args = ap.parse_args()

    if not args.csv.is_file():
        print(f"ERROR: CSV not found: {args.csv}\n"
              f"  Run extract_pro_qi_commands.py first.", file=sys.stderr)
        return 1

    rows = R.load(args.csv)
    models = sorted({r["model"] for r in rows})
    if args.model:
        models = [m for m in models if args.model in m]
        if not models:
            print(f"ERROR: no model matching {args.model!r} in {args.csv}",
                  file=sys.stderr)
            return 1

    rows = [r for r in rows if r["model"] in models]
    out: list[str] = []
    p = out.append
    p(f"qi command report (Pro) -- {args.csv}")
    p("\nDESCRIPTIVE ONLY: few runs per arm; read medians as direction.")
    if args.cross_model:
        R.cross_model(rows, p)
    else:
        for model in models:
            mrows = [r for r in rows if r["model"] == model]
            p(f"\n{'#' * 70}\n# MODEL: {model or '(unknown)'}  "
              f"({len(mrows)} commands)\n{'#' * 70}")
            R.report_block(mrows, p)
    print("\n".join(out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
