#!/usr/bin/env python3
"""Compare two arms on the GRANULAR eval signal (pass_rate / failure mode),
not just the binary resolved verdict.

Reads an eval_results.csv produced by evaluate_pro_patches.py (the version that
emits pass_rate / failure_mode / f2p_* / p2p_* columns) and, for the two arms,
reports:

  * pass_rate (fraction of required tests passing, F2P+P2P pooled) -- ALL reps
    AND the NON-RESOLVED subset. The non-resolved subset CONDITIONS on the
    outcome, so it's shown next to the all-reps view to keep that selection
    visible rather than hidden.
  * Mann-Whitney U (normal approx, tie-corrected ranks) + rank-biserial effect
    size + a bootstrap 95% CI for the mean difference (treatment - control).
  * F2P pass (did the fix land) vs P2P pass (did it break anything), per arm.
  * failure_mode mix per arm.
  * failing-test breadth (from the sibling eval_test_failures.csv): distinct
    tests failed per arm + the most recurring failing tests (which single test is
    blocking resolution), so "treatment breaks fewer OTHER tests" is measured,
    not eyeballed.

DESCRIPTIVE for a single-instance batch: reps are provider nondeterminism on ONE
task, not independent instances, so read effects as direction, not proof.

Usage:
  python3 experiment/analysis/analyze_pro_eval_granular.py \
      --csv experiment/results/pro_runs/<batch>/eval_results.csv
  ... --arms swebp_control swebp_treatment   # control first, treatment second
"""
from __future__ import annotations

import argparse
import csv
import math
import random
import statistics as st
import sys
from pathlib import Path


def fnum(row: dict, key: str):
    v = row.get(key, "")
    return float(v) if v not in ("", "None", None) else None


def rate(rows: list[dict], passed_k: str, total_k: str) -> list[float]:
    """Per-rep ratio passed/total, skipping reps with no tests in that category."""
    out = []
    for r in rows:
        p, t = fnum(r, passed_k), fnum(r, total_k)
        if t:
            out.append(p / t)
    return out


def mannwhitney(a: list[float], b: list[float]):
    """Two-sided Mann-Whitney U (normal approximation with tie-corrected average
    ranks) + rank-biserial effect size. Sign of rank-biserial: + means a > b."""
    comb = sorted([(v, 0) for v in a] + [(v, 1) for v in b])
    n1, n2 = len(a), len(b)
    if not n1 or not n2:
        return None
    ranks = [0.0] * len(comb)
    i = 0
    while i < len(comb):
        j = i
        while j < len(comb) and comb[j][0] == comb[i][0]:
            j += 1
        avg = (i + 1 + j) / 2
        for k in range(i, j):
            ranks[k] = avg
        i = j
    r1 = sum(ranks[k] for k in range(len(comb)) if comb[k][1] == 0)
    u1 = r1 - n1 * (n1 + 1) / 2
    u2 = n1 * n2 - u1
    u = min(u1, u2)
    mu = n1 * n2 / 2
    sd = (n1 * n2 * (n1 + n2 + 1) / 12) ** 0.5
    z = (u - mu) / sd if sd else 0.0
    p = math.erfc(abs(z) / 2 ** 0.5)
    # u1 is the U for group a; CLES = u1/(n1*n2) ~= P(a>b). rank-biserial =
    # 2*CLES - 1, so POSITIVE means group a (treatment) tends to rank higher.
    rank_biserial = 2 * u1 / (n1 * n2) - 1
    return u, z, p, rank_biserial


def boot_mean_diff(a: list[float], b: list[float], n: int):
    """Bootstrap 95% CI for mean(b) - mean(a)."""
    if not a or not b:
        return None
    diffs = []
    for _ in range(n):
        ma = st.mean(random.choice(a) for _ in a)
        mb = st.mean(random.choice(b) for _ in b)
        diffs.append(mb - ma)
    diffs.sort()
    return diffs[int(0.025 * n)], diffs[int(0.975 * n)]


def failure_breadth(fail_rows: list[dict], ctrl_name: str, treat_name: str,
                    top: int) -> None:
    """Per-arm distinct-test breadth + the most recurring failing tests, from the
    long-format eval_test_failures.csv. Distinguishes 'one stubborn shared test'
    (instance-intrinsic) from 'scattered breakage' (a cratering tail)."""
    from collections import Counter, defaultdict

    def short(n: str) -> str:
        return n.split("::", 1)[-1] if "::" in n else n

    arms = [ctrl_name, treat_name]
    print("\n--- failing-test breadth (from eval_test_failures.csv) ---")
    for a in arms:
        ar = [r for r in fail_rows if r["arm"] == a]
        reps: dict = defaultdict(list)
        for r in ar:
            reps[r["rep"]].append(r)
        one_away = sum(1 for v in reps.values() if len(v) == 1)
        distinct = len({r["test_name"] for r in ar})
        print(f"  {a:18s} {len(ar):3} fail-rows / {len(reps)} reps;  "
              f"{distinct} distinct tests;  {one_away} reps exactly 1 test away")

    print(f"\n--- top {top} recurring failing tests (count = # reps) ---")
    by_test = Counter(r["test_name"] for r in fail_rows)
    for name, c in by_test.most_common(top):
        per = Counter(r["arm"] for r in fail_rows if r["test_name"] == name)
        kind = next(r["kind"] for r in fail_rows if r["test_name"] == name)
        split = "  ".join(f"{a.split('_')[-1]}={per.get(a, 0)}" for a in arms)
        print(f"  {c:3} [{kind:12s}] {split}   {short(name)}")


def compare(label: str, ctrl: list[dict], treat: list[dict], boot: int) -> None:
    cp = [fnum(r, "pass_rate") for r in ctrl if fnum(r, "pass_rate") is not None]
    tp = [fnum(r, "pass_rate") for r in treat if fnum(r, "pass_rate") is not None]
    print(f"\n--- {label} ---  control n={len(cp)}  treatment n={len(tp)}")
    if not cp or not tp:
        print("  (insufficient pass_rate data in one arm)")
        return
    print(f"  pass_rate control: median={st.median(cp):.3f} mean={st.mean(cp):.3f}"
          f"  min={min(cp):.3f} max={max(cp):.3f}")
    print(f"  pass_rate treat:   median={st.median(tp):.3f} mean={st.mean(tp):.3f}"
          f"  min={min(tp):.3f} max={max(tp):.3f}")
    mw = mannwhitney(tp, cp)
    if mw:
        u, z, p, rbc = mw
        print(f"  MWU U={u:.0f} z={z:.2f} p={p:.3f}  rank-biserial={rbc:+.2f} "
              f"(+ = treatment higher)")
    ci = boot_mean_diff(cp, tp, boot)
    if ci:
        print(f"  bootstrap 95% CI, mean diff (treat - control): "
              f"[{ci[0]:+.3f}, {ci[1]:+.3f}]")


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--csv", type=Path, required=True,
                    help="eval_results.csv with the granular columns")
    ap.add_argument("--arms", nargs=2, metavar=("CONTROL", "TREATMENT"),
                    default=["swebp_control", "swebp_treatment"],
                    help="control arm then treatment arm (default: "
                         "swebp_control swebp_treatment)")
    ap.add_argument("--boot", type=int, default=10000, help="bootstrap resamples")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--failures-csv", type=Path, default=None,
                    help="eval_test_failures.csv (default: sibling of --csv)")
    ap.add_argument("--top", type=int, default=8,
                    help="how many recurring failing tests to list (default: 8)")
    args = ap.parse_args()

    if not args.csv.is_file():
        print(f"ERROR: not found: {args.csv}", file=sys.stderr)
        return 1
    rows = list(csv.DictReader(args.csv.open()))
    if "pass_rate" not in (rows[0] if rows else {}):
        print("ERROR: CSV lacks granular columns (pass_rate). Re-run "
              "evaluate_pro_patches.py to backfill.", file=sys.stderr)
        return 1
    random.seed(args.seed)

    ctrl_name, treat_name = args.arms
    C = [r for r in rows if r["arm"] == ctrl_name]
    T = [r for r in rows if r["arm"] == treat_name]

    print("=" * 64)
    print(f"GRANULAR EVAL COMPARISON -- {args.csv}")
    print(f"  control = {ctrl_name} (n={len(C)})   treatment = {treat_name} (n={len(T)})")
    print("  DESCRIPTIVE: reps are provider nondeterminism; direction, not proof.")
    print("=" * 64)

    # resolve rate (binary), for context
    for name, rs in ((ctrl_name, C), (treat_name, T)):
        res = sum(1 for r in rs if r["resolved"] == "1")
        print(f"  {name:18s} resolved {res}/{len(rs)}")

    compare("ALL reps (pass_rate available)", C, T, args.boot)
    compare("NON-RESOLVED only", [r for r in C if r["resolved"] == "0"],
            [r for r in T if r["resolved"] == "0"], args.boot)

    print("\n--- F2P (did the fix land) vs P2P (did it break things), mean per rep ---")
    for name, rs in ((ctrl_name, C), (treat_name, T)):
        f2p = rate(rs, "f2p_passed", "f2p_total")
        p2p = rate(rs, "p2p_passed", "p2p_total")
        f2p_m = f"{st.mean(f2p):.3f}" if f2p else "n/a"
        p2p_m = f"{st.mean(p2p):.3f}" if p2p else "n/a"
        print(f"  {name:18s} F2P pass={f2p_m}   P2P pass={p2p_m}")

    print("\n--- failure_mode mix ---")
    from collections import Counter
    for name, rs in ((ctrl_name, C), (treat_name, T)):
        mix = Counter(r["failure_mode"] for r in rs)
        print(f"  {name:18s} {dict(mix)}")

    fail_path = args.failures_csv or args.csv.with_name("eval_test_failures.csv")
    if fail_path.is_file():
        fail_rows = list(csv.DictReader(fail_path.open()))
        if fail_rows:
            failure_breadth(fail_rows, ctrl_name, treat_name, args.top)
    else:
        print(f"\n(no eval_test_failures.csv at {fail_path} -- skipping breadth)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
