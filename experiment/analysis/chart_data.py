#!/usr/bin/env python3
"""Dump the exact numeric series a single-instance Pro chart plots.

Since the model can't view the PNGs analyze_pro_stats.py renders, checking
whether a chart "looks right" has repeatedly meant re-deriving its numbers
with a throwaway script. This tool calls the *same* data-prep functions the
chart-drawing code calls (radar_ratios(), _data_cumulative_cost(), etc. --
see analyze_pro_stats.py) so what's printed here is provably what got
plotted, not a second computation that could drift from it.

Usage:
  experiment/.venv_pro/bin/python experiment/analysis/chart_data.py \
      --dir experiment/results/pro_runs/<batch> --list

  experiment/.venv_pro/bin/python experiment/analysis/chart_data.py \
      --dir experiment/results/pro_runs/<batch> turn_count [--raw] [--json]

Chart names match experiment/docs/CHART_INVENTORY.md's single-instance table:
  turn_count, total_tokens, cost, duration_sec, patch_lines, files_touched,
  resolve_rate, cumulative_cost, log_size_range, explore_calls,
  explore_tokens, radar
"""
from __future__ import annotations

import argparse
import json
import statistics as st
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze_pro_stats import (  # noqa: E402
    ARM_ORDER, ARM_LABEL, METRICS, STRIP_METRICS, RADAR_AXES,
    EXPLORE_TOOLS, EXPLORE_TOKEN_TOOLS,
    fnum, load_arms, load_explore, _explore_per_arm,
    _data_resolve_rate, _data_cumulative_cost, _data_log_size_range,
    radar_metrics, radar_ratios,
)

METRIC_COLS = {col for col, _label, _pct in METRICS}
NON_METRIC_CHARTS = ["resolve_rate", "cumulative_cost", "log_size_range",
                     "explore_calls", "explore_tokens", "radar"]


def _summary(vals: list[float]) -> dict:
    if not vals:
        return {"n": 0}
    return {"n": len(vals), "min": min(vals), "median": st.median(vals),
            "mean": st.fmean(vals), "max": max(vals)}


def _print_summary_table(per_arm: dict[str, list[float]]) -> None:
    print(f"{'arm':<12}{'n':>4}{'min':>12}{'median':>12}{'mean':>12}{'max':>12}")
    for a in ARM_ORDER:
        s = _summary(per_arm.get(a, []))
        if s["n"] == 0:
            print(f"{ARM_LABEL[a]:<12}{0:>4}{'—':>12}{'—':>12}{'—':>12}{'—':>12}")
            continue
        print(f"{ARM_LABEL[a]:<12}{s['n']:>4}{s['min']:>12.4g}"
              f"{s['median']:>12.4g}{s['mean']:>12.4g}{s['max']:>12.4g}")


def _available_charts(arms: dict, run_dir: Path) -> list[str]:
    out = []
    for col in METRIC_COLS:
        if any(fnum(r.get(col)) is not None for a in ARM_ORDER for r in arms[a]):
            out.append(col)
    out.append("resolve_rate")  # always available once arms are loaded
    if any(_data_cumulative_cost(arms).values()):
        out.append("cumulative_cost")
    if any(_data_log_size_range(arms).values()):
        out.append("log_size_range")
    explore = load_explore(run_dir)
    if explore:
        out.append("explore_calls")
        out.append("explore_tokens")
    if radar_ratios(run_dir) is not None:
        out.append("radar")
    return sorted(out)


def _metric_data(arms: dict, col: str) -> dict[str, list[float]]:
    return {a: [fnum(r.get(col)) for r in arms[a] if fnum(r.get(col)) is not None]
            for a in ARM_ORDER}


def cmd_metric(arms: dict, col: str, raw: bool, as_json: bool) -> None:
    data = _metric_data(arms, col)
    if as_json:
        print(json.dumps(data, indent=2))
        return
    if raw:
        for a in ARM_ORDER:
            print(f"{ARM_LABEL[a]}: {data[a]}")
    else:
        kind = "strip" if col in STRIP_METRICS else "box"
        print(f"# {col} ({kind} chart)")
        _print_summary_table(data)


def cmd_resolve_rate(arms: dict, as_json: bool) -> None:
    data = _data_resolve_rate(arms)
    if as_json:
        print(json.dumps({a: {"k": k, "n": n, "rate": p, "ci_lo": lo, "ci_hi": hi}
                          for a, (k, n, p, lo, hi) in data.items()}, indent=2))
        return
    print(f"{'arm':<12}{'k/n':>8}{'rate':>10}{'95% CI':>18}")
    for a in ARM_ORDER:
        k, n, p, lo, hi = data[a]
        print(f"{ARM_LABEL[a]:<12}{f'{k}/{n}':>8}{p:>10.0%}"
              f"{f'[{lo:.0%}, {hi:.0%}]':>18}")


def cmd_cumulative_cost(arms: dict, raw: bool, as_json: bool) -> None:
    data = _data_cumulative_cost(arms)
    if as_json:
        print(json.dumps(data, indent=2))
        return
    for a in ARM_ORDER:
        cum = data[a]
        total = cum[-1] if cum else 0.0
        if raw:
            print(f"{ARM_LABEL[a]} (n={len(cum)}, total=${total:.4f}): {cum}")
        else:
            print(f"{ARM_LABEL[a]:<12}n={len(cum):<4}total=${total:.4f}")


def cmd_log_size_range(arms: dict, raw: bool, as_json: bool) -> None:
    data = _data_log_size_range(arms)
    if as_json:
        print(json.dumps(data, indent=2))
        return
    if raw:
        for a in ARM_ORDER:
            print(f"{ARM_LABEL[a]}: {data[a]}")
    else:
        _print_summary_table(data)


def cmd_explore(arms: dict, run_dir: Path, field: str, segs: list[str],
                raw: bool, as_json: bool) -> None:
    explore = load_explore(run_dir)
    if raw:
        per_run = {f"{a}/{rid}": v[field] for (a, rid), v in explore.items()}
        if as_json:
            print(json.dumps(per_run, indent=2))
        else:
            for k, v in sorted(per_run.items()):
                print(f"{k}: {v}")
        return
    per = _explore_per_arm(explore, field, segs)
    if as_json:
        print(json.dumps(per, indent=2))
        return
    print(f"{'segment':<12}" + "".join(f"{ARM_LABEL[a]:>14}" for a in ARM_ORDER))
    for s in segs:
        print(f"{s:<12}" + "".join(f"{per[a][s]:>14.2f}" for a in ARM_ORDER))


def cmd_radar(run_dir: Path, as_json: bool) -> None:
    metrics = radar_metrics(run_dir)
    ratios = radar_ratios(run_dir)
    if as_json:
        print(json.dumps({"metrics": metrics, "ratios": ratios}, indent=2))
        return
    print(f"{'axis':<18}" + "".join(f"{ARM_LABEL[a]:>14}" for a in ARM_ORDER)
          + f"{'ratio (t/c)':>14}")
    for ax in RADAR_AXES:
        vals = "".join(f"{metrics[a][ax]:>14.4g}" for a in ARM_ORDER)
        r = ratios[ax] if ratios else float("nan")
        print(f"{ax:<18}{vals}{r:>14.3f}")


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", type=Path, required=True,
                    help="Run dir holding runs_with_success.csv")
    ap.add_argument("chart", nargs="?",
                    help="Chart name (see --list for what's available here)")
    ap.add_argument("--list", action="store_true",
                    help="List chart names with data available in this dir")
    ap.add_argument("--raw", action="store_true",
                    help="Print the full underlying series instead of a summary")
    ap.add_argument("--json", action="store_true", help="Machine-readable output")
    args = ap.parse_args()

    if not (args.dir / "runs_with_success.csv").is_file():
        print(f"ERROR: not found: {args.dir / 'runs_with_success.csv'}", file=sys.stderr)
        return 1
    arms = load_arms(args.dir, quiet=True)
    if not arms[ARM_ORDER[0]] and not arms[ARM_ORDER[1]]:
        print("ERROR: no rows in either arm", file=sys.stderr)
        return 1

    if args.list or not args.chart:
        for name in _available_charts(arms, args.dir):
            print(name)
        return 0

    name = args.chart
    if name in METRIC_COLS:
        cmd_metric(arms, name, args.raw, args.json)
    elif name == "resolve_rate":
        cmd_resolve_rate(arms, args.json)
    elif name == "cumulative_cost":
        cmd_cumulative_cost(arms, args.raw, args.json)
    elif name == "log_size_range":
        cmd_log_size_range(arms, args.raw, args.json)
    elif name == "explore_calls":
        cmd_explore(arms, args.dir, "calls", EXPLORE_TOOLS, args.raw, args.json)
    elif name == "explore_tokens":
        cmd_explore(arms, args.dir, "tokens", EXPLORE_TOKEN_TOOLS, args.raw, args.json)
    elif name == "radar":
        cmd_radar(args.dir, args.json)
    else:
        print(f"ERROR: unknown chart '{name}'. Run --list to see what's "
              f"available in {args.dir}.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
