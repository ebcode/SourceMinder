#!/usr/bin/env python3
"""Rep-level statistics for the SWE-bench **Pro** experiment (single instance).

The Pro pilot is N=1 instance x many reps per arm, so the instance-paired design
of analyze_stats.py does not apply. This script instead compares the two arms'
**rep distributions** on the one instance: for each metric it reports per-arm
median/mean/IQR, a Mann-Whitney U test (rank-sum, normal approximation), and a
bootstrap CI for the difference in medians. It also reports each arm's resolve
rate (Wilson interval). Stdlib-only -- no scipy/numpy needed.

Consumes runs_with_success.csv (analyze_pro_trajectories + evaluate_pro_patches,
joined by merge_results.py). Writes pro_stats_summary.csv and prints a report.

Usage:
  experiment/.venv_pro/bin/python experiment/analysis/analyze_pro_stats.py \
      --dir experiment/results/pro_runs/<batch> [--no-charts]
"""
from __future__ import annotations

import argparse
import csv
import math
import random
import statistics as st
import sys
from pathlib import Path

METRICS = [
    ("turn_count", "turns", False),
    ("total_input_tokens", "input tokens", False),
    ("total_tokens", "total tokens", False),
    ("cost", "cost ($)", True),
    ("patch_chars", "patch chars", False),
]
CONTROL, TREATMENT = "swebp_control", "swebp_treatment"


def fnum(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


def iqr(vals):
    if len(vals) < 2:
        return (vals[0], vals[0]) if vals else (0, 0)
    q = st.quantiles(vals, n=4)
    return q[0], q[2]


def mannwhitney_u(a, b):
    """Two-sided Mann-Whitney U with normal approximation + tie correction.
    Returns (U, z, p). Adequate for the pilot's small, descriptive samples."""
    n1, n2 = len(a), len(b)
    if n1 == 0 or n2 == 0:
        return float("nan"), float("nan"), float("nan")
    combined = sorted([(v, 0) for v in a] + [(v, 1) for v in b])
    ranks = [0.0] * len(combined)
    i = 0
    while i < len(combined):
        j = i
        while j + 1 < len(combined) and combined[j + 1][0] == combined[i][0]:
            j += 1
        avg = (i + j) / 2 + 1  # average rank (1-based)
        for k in range(i, j + 1):
            ranks[k] = avg
        i = j + 1
    r1 = sum(rk for rk, (_, grp) in zip(ranks, combined) if grp == 0)
    u1 = r1 - n1 * (n1 + 1) / 2
    u = min(u1, n1 * n2 - u1)
    mu = n1 * n2 / 2
    # tie-corrected sigma
    n = n1 + n2
    tie_term = 0
    i = 0
    while i < len(combined):
        j = i
        while j + 1 < len(combined) and combined[j + 1][0] == combined[i][0]:
            j += 1
        t = j - i + 1
        tie_term += t ** 3 - t
        i = j + 1
    sigma = math.sqrt((n1 * n2 / 12) * ((n + 1) - tie_term / (n * (n - 1)))) if n > 1 else 0
    if sigma == 0:
        return u, 0.0, 1.0
    z = (u - mu) / sigma
    p = 2 * (1 - 0.5 * (1 + math.erf(abs(z) / math.sqrt(2))))
    return u, z, p


def boot_median_diff(a, b, iters=10000, seed=42):
    """Bootstrap 95% CI for median(treatment) - median(control)."""
    if not a or not b:
        return (float("nan"), float("nan"))
    rng = random.Random(seed)
    diffs = []
    for _ in range(iters):
        ra = [rng.choice(a) for _ in a]
        rb = [rng.choice(b) for _ in b]
        diffs.append(st.median(rb) - st.median(ra))
    diffs.sort()
    lo = diffs[int(0.025 * len(diffs))]
    hi = diffs[int(0.975 * len(diffs)) - 1]
    return lo, hi


def wilson(k, n, z=1.96):
    if n == 0:
        return (0.0, 0.0, 0.0)
    p = k / n
    denom = 1 + z * z / n
    center = (p + z * z / (2 * n)) / denom
    half = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / denom
    return p, max(0.0, center - half), min(1.0, center + half)


# A run is "clean" if it reached a normal Submitted/Completed terminal AND
# produced a non-empty patch. A blow-up is the failure mode qi is meant to curb:
# a crash (e.g. exit_status=ValueError), a step-limit/LimitsExceeded run, or a
# run that thrashed and submitted nothing. The median can't see a single such
# tail event in a small rep set, so we report its RATE explicitly.
CLEAN_EXITS = {"Submitted", "Completed"}


def is_blowup(row) -> bool:
    return (row.get("exit_status", "") not in CLEAN_EXITS
            or row.get("outcome") == "empty_patch")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", type=Path, required=True,
                    help="Run dir holding runs_with_success.csv")
    ap.add_argument("--bootstrap-iters", type=int, default=10000)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--no-charts", action="store_true",
                    help="Skip matplotlib rendering (charts are best-effort anyway)")
    args = ap.parse_args()

    csv_path = args.dir / "runs_with_success.csv"
    if not csv_path.is_file():
        print(f"ERROR: not found: {csv_path}", file=sys.stderr)
        return 1
    with csv_path.open(newline="") as f:
        rows = list(csv.DictReader(f))

    arms = {CONTROL: [], TREATMENT: []}
    for r in rows:
        if r["arm"] in arms:
            arms[r["arm"]].append(r)
    nC, nT = len(arms[CONTROL]), len(arms[TREATMENT])
    print(f"=== Pro rep-level stats ===")
    print(f"instance(s): {sorted({r['instance_id'] for r in rows})}")
    print(f"reps: control n={nC}, treatment n={nT}\n")
    if not nC or not nT:
        print("Need reps in BOTH arms to compare.", file=sys.stderr)
        return 1

    summary_rows = []
    # Both median and mean are shown: the median is robust but blind to a single
    # catastrophic run, the mean is tail-sensitive. Delta + MWU are on the mean/
    # ranks; for a tail effect the mean and the blow-up rate below are the lens.
    print(f"{'metric':<13}{'ctrl med':>11}{'ctrl mean':>12}{'treat med':>12}"
          f"{'treat mean':>12}{'Δmean':>8}{'MWU p':>8}")
    print("-" * 76)
    for col, label, _is_cost in METRICS:
        cv = [fnum(r.get(col)) for r in arms[CONTROL]]
        tv = [fnum(r.get(col)) for r in arms[TREATMENT]]
        cv = [x for x in cv if x is not None]
        tv = [x for x in tv if x is not None]
        if not cv or not tv:
            continue
        cmed, tmed = st.median(cv), st.median(tv)
        cmean, tmean = st.fmean(cv), st.fmean(tv)
        dmed, dmean = tmed - cmed, tmean - cmean
        lo, hi = boot_median_diff(cv, tv, args.bootstrap_iters, args.seed)
        _, _, p = mannwhitney_u(cv, tv)
        pctmean = f"{dmean / cmean * 100:+.0f}%" if cmean else "n/a"
        print(f"{label:<13}{cmed:>11,.4g}{cmean:>12,.4g}{tmed:>12,.4g}"
              f"{tmean:>12,.4g}{pctmean:>8}{p:>8.3f}")
        cq = iqr(cv)
        tq = iqr(tv)
        summary_rows.append(dict(
            metric=col, control_n=nC, treatment_n=nT,
            control_median=cmed, treatment_median=tmed, delta_median=dmed,
            control_mean=cmean, treatment_mean=tmean, delta_mean=dmean,
            pct_change_median=(dmed / cmed if cmed else ""),
            pct_change_mean=(dmean / cmean if cmean else ""),
            control_iqr_lo=cq[0], control_iqr_hi=cq[1],
            treatment_iqr_lo=tq[0], treatment_iqr_hi=tq[1],
            boot_ci_lo=lo, boot_ci_hi=hi, mwu_p=p))

    # resolve rate per arm
    print()
    for arm in (CONTROL, TREATMENT):
        k = sum(1 for r in arms[arm] if r.get("task_success") == "1")
        n = len(arms[arm])
        p, lo, hi = wilson(k, n)
        print(f"  {arm:18s} resolved {k}/{n} = {p:.0%}  (Wilson95 [{lo:.0%}, {hi:.0%}])")
        summary_rows.append(dict(metric=f"resolve_rate[{arm}]", control_n=n,
                                 treatment_n="", control_median=k, treatment_median=n,
                                 delta_median=p, pct_change_mean="", boot_ci_lo=lo,
                                 boot_ci_hi=hi, mwu_p=""))

    # blow-up rate per arm -- the tail metric the median misses (qi's main
    # observed benefit on weak models: fewer crash/step-limit/empty-patch runs).
    print()
    for arm in (CONTROL, TREATMENT):
        n = len(arms[arm])
        b = sum(1 for r in arms[arm] if is_blowup(r))
        p, lo, hi = wilson(b, n)
        bad = ", ".join(f"{r['run_id']}({r.get('exit_status') or r.get('outcome')})"
                        for r in arms[arm] if is_blowup(r)) or "none"
        print(f"  {arm:18s} blow-ups {b}/{n} = {p:.0%}  (Wilson95 [{lo:.0%}, {hi:.0%}])  [{bad}]")
        summary_rows.append(dict(metric=f"blowup_rate[{arm}]", control_n=n,
                                 control_median=b, treatment_median=n, delta_median=p,
                                 boot_ci_lo=lo, boot_ci_hi=hi, mwu_p=""))

    out_path = args.dir / "pro_stats_summary.csv"
    keys = sorted({k for r in summary_rows for k in r})
    with out_path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=keys)
        w.writeheader()
        w.writerows(summary_rows)
    print(f"\nWrote {out_path}")

    if not args.no_charts:
        try:
            import matplotlib
            matplotlib.use("Agg")
            import matplotlib.pyplot as plt
            fig, axes = plt.subplots(1, len(METRICS), figsize=(4 * len(METRICS), 4))
            for ax, (col, label, _) in zip(axes, METRICS):
                data = [[fnum(r.get(col)) for r in arms[a] if fnum(r.get(col)) is not None]
                        for a in (CONTROL, TREATMENT)]
                ax.boxplot(data, labels=["control", "treatment"])
                ax.set_title(label)
            fig.tight_layout()
            chart = args.dir / "pro_stats_box.png"
            fig.savefig(chart, dpi=110)
            print(f"Wrote {chart}")
        except Exception as exc:  # noqa: BLE001
            print(f"(charts skipped: {exc})", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
