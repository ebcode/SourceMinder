#!/usr/bin/env python3
"""Statistical analysis of the qi context-preservation experiment.

Consumes ``runs_with_success.csv`` (token metrics from
``analyze_trajectories.py`` left-joined with harness outcomes by
``merge_results.py``) and produces descriptive statistics, exploratory
inferential tests, and Priority-1 visualizations (see ANALYZE_STATS_PLAN.md).

Design commitments worth keeping in view while reading this file:

  * The unit of analysis is the **instance**, not the run. Each instance
    contributes several correlated reps per arm, so every inferential test
    operates on per-instance summaries (paired Wilcoxon on the per-instance
    median differences) or clusters by instance (bootstrap that resamples
    instances, not runs). Pooling raw runs would treat correlated reps as
    independent and inflate the degrees of freedom (PLAN §4.4).

  * Arms are only comparable **within a model**. ``runs_with_success.csv`` may
    carry rows for more than one model (logs live under ``logs/<model>/...``);
    we group by ``model`` and never pool across models (PLAN §6.3a). With a
    single model the output is flat; with several it is split per model.

  * This is a **pilot**. P-values are exploratory and labelled as such; the
    headline is the effect size (raw median difference) with a clustered
    bootstrap CI (PLAN §4.5).

Data model: the merged CSV is read into a plain list of row dicts (one per run)
with numeric columns coerced to float (NaN when blank/missing). All aggregation
is done with the stdlib ``csv`` module and ``numpy`` -- no pandas. The small
filter/aggregate helpers (``fvals``, ``_arm``, ``_inst``, ``per_instance_*``)
stand in for the handful of groupby/pivot operations the analysis needs.

Dependencies: ``numpy`` and ``scipy`` (Wilcoxon / Spearman) are hard
requirements -- imported at load time, so the script fails fast if either is
missing. ``matplotlib`` is the one soft dependency: charts are skipped (with a
warning) if it is not importable, and ``--no-charts`` skips them on request.

Usage:
  # point at a run directory (reads runs_with_success.csv from it, writes there);
  # --dir means the same thing here as in merge_results.py / evaluate_patches.py
  python3 experiment/analysis/analyze_stats.py \
      --dir experiment/results/runs/<ts> --bootstrap-iters 10000

  # or name an explicit CSV (output lands beside it unless --dir overrides)
  python3 experiment/analysis/analyze_stats.py \
      --csv experiment/results/runs/<ts>/runs_with_success.csv

  # defaults: newest results/runs/<ts>/runs_with_success.csv, output beside it
  python3 experiment/analysis/analyze_stats.py
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from collections import Counter
from pathlib import Path

import numpy as np
from scipy import stats as scipy_stats

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # -> experiment/
from lib import paths

try:
    import matplotlib
    matplotlib.use("Agg")  # headless: no display needed for PNG output
    import matplotlib.pyplot as plt
    from matplotlib.ticker import MaxNLocator
except ImportError:  # the one soft dependency -- charts skip, text/json don't
    plt = None

ARMS: tuple[str, ...] = ("control", "treatment")  # default; overwritten from CSV data

# Primary token metrics (PLAN §1.4) -- the inferential tests run on these three.
TOKEN_METRICS = ("peak_prompt_tokens", "total_input_tokens", "total_tokens", "tool_output_tokens_approx")
# Everything that gets a descriptive block (PLAN §1.1).
DESCRIPTIVE_METRICS = (
    "total_input_tokens", "peak_prompt_tokens", "tool_output_tokens_approx",
    "turn_count", "total_completion_tokens", "total_reasoning_tokens",
    "total_cached_tokens", "total_tokens",
)
MECHANISM_METRICS = ("qi_invocations", "grep_invocations", "file_read_invocations")

# Numeric columns to coerce on load (CSV gives everything as strings).
NUMERIC_COLS = set(DESCRIPTIVE_METRICS) | set(MECHANISM_METRICS) | {"task_success"}

Z95 = 1.959963984540054  # two-sided 95% normal quantile (Wilson CI, etc.)

Row = dict  # one trajectory run; numeric cols are float (NaN if absent/blank)


# --------------------------------------------------------------------------- #
# Row helpers (the groupby/pivot vocabulary the rest of the file builds on)
# --------------------------------------------------------------------------- #
def _to_float(s) -> float:
    try:
        return float(s)
    except (TypeError, ValueError):
        return float("nan")


def fvals(rows: list[Row], key: str) -> np.ndarray:
    """All finite float values of ``key`` across ``rows`` (NaN/missing dropped)."""
    out = [r[key] for r in rows
           if isinstance(r.get(key), (int, float))
           and not (isinstance(r.get(key), float) and math.isnan(r[key]))]
    return np.array(out, dtype=float)


def has_metric(rows: list[Row], key: str) -> bool:
    """True if ``key`` is a column in the data (present on any row)."""
    return any(key in r for r in rows)


def _arm(rows: list[Row], arm: str) -> list[Row]:
    return [r for r in rows if r["arm"] == arm]


def _inst(rows: list[Row], instance_id: str) -> list[Row]:
    return [r for r in rows if r["instance_id"] == instance_id]


def _instances(rows: list[Row]) -> list[str]:
    return sorted({r["instance_id"] for r in rows})


def _nfiles_map(rows: list[Row]) -> dict[str, int]:
    """instance_id -> n_files (int), skipping instances with no count."""
    out: dict[str, int] = {}
    for r in rows:
        nf = r.get("n_files")
        if (r["instance_id"] not in out and isinstance(nf, (int, float))
                and not (isinstance(nf, float) and math.isnan(nf))):
            out[r["instance_id"]] = int(nf)
    return out


# --------------------------------------------------------------------------- #
# Loading
# --------------------------------------------------------------------------- #
def load_n_files(path: Path) -> dict[str, int]:
    """instance_id -> n_files (gold-patch file count), the size moderator.

    Accepts either ``data/pool.csv`` (``instance_id,repo,n_files,...`` with a
    header, ``#`` comment lines skipped) or an instances-list file whose lines
    end in the count (``django__django-11532 django/django 5``). Returns an
    empty map if the file is missing or carries no counts.
    """
    if not path or not Path(path).is_file():
        return {}
    text = Path(path).read_text()
    data_lines = [ln for ln in text.splitlines() if ln.strip() and not ln.lstrip().startswith("#")]
    if not data_lines:
        return {}
    out: dict[str, int] = {}
    header = data_lines[0]
    if "," in header and "n_files" in header:  # pool.csv-style
        for row in csv.DictReader(data_lines):
            try:
                out[row["instance_id"]] = int(row["n_files"])
            except (KeyError, ValueError, TypeError):
                continue
    else:  # whitespace list; trailing token is the count when numeric
        for ln in data_lines:
            toks = ln.split()
            if len(toks) >= 2 and toks[-1].isdigit():
                out[toks[0]] = int(toks[-1])
    return out


def load(csv_path: Path, n_files_map: dict[str, int] | None = None) -> list[Row]:
    """Read the merged CSV into row dicts; coerce numerics, derive repo/n_files."""
    n_files_map = n_files_map or {}
    with Path(csv_path).open(newline="") as fh:
        rows = list(csv.DictReader(fh))
    for r in rows:
        # Pre-model CSVs (legacy single-model runs) lack a model column; treat
        # as one unnamed model so the rest of the pipeline still groups.
        if not r.get("model"):
            r["model"] = ""
        for col in NUMERIC_COLS:
            if col in r:
                r[col] = _to_float(r[col])
        r["repo"] = r["instance_id"].split("__", 1)[0]
        # n_files: prefer the column already in the CSV (written by
        # analyze_trajectories since the named-batch refactor); fall back to
        # the external pool.csv map for legacy CSVs that predate the column.
        nf_raw = r.get("n_files", "")
        if nf_raw not in ("", None):
            r["n_files"] = _to_float(nf_raw)
        else:
            nf = n_files_map.get(r["instance_id"])
            r["n_files"] = float(nf) if nf is not None else float("nan")
        # patch_files: unique files in the agent's submitted diff (outcome var).
        pf_raw = r.get("patch_files", "")
        r["patch_files"] = _to_float(pf_raw) if pf_raw not in ("", None) else float("nan")
    return rows


# --------------------------------------------------------------------------- #
# Descriptive statistics
# --------------------------------------------------------------------------- #
def describe(vals: np.ndarray) -> dict:
    """median/IQR/mean/SD/min/max/n for a 1-D array (NaNs dropped)."""
    v = np.asarray(vals, dtype=float)
    v = v[~np.isnan(v)]
    if v.size == 0:
        return {k: None for k in ("n", "median", "iqr", "q1", "q3", "mean", "sd", "min", "max")}
    q1, q3 = np.percentile(v, [25, 75])
    return {
        "n": int(v.size),
        "median": float(np.median(v)),
        "q1": float(q1), "q3": float(q3), "iqr": float(q3 - q1),
        "mean": float(np.mean(v)),
        "sd": float(np.std(v, ddof=1)) if v.size > 1 else 0.0,
        "min": float(np.min(v)), "max": float(np.max(v)),
    }


def wilson_ci(successes: int, n: int, z: float = Z95) -> tuple[float, float, float]:
    """Wilson score interval for a binomial proportion. Returns (rate, lo, hi)."""
    if n == 0:
        return (float("nan"), float("nan"), float("nan"))
    p = successes / n
    denom = 1 + z * z / n
    centre = (p + z * z / (2 * n)) / denom
    half = (z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n))) / denom
    return (p, max(0.0, centre - half), min(1.0, centre + half))


# --------------------------------------------------------------------------- #
# Per-instance aggregation (the unit of analysis -- PLAN §2.2)
# --------------------------------------------------------------------------- #
def per_instance_medians(rows: list[Row], metric: str) -> list[dict]:
    """One record per instance with control/treatment median + difference.

    Only instances present in *both* arms are kept -- the paired design needs a
    matched pair (PLAN §4.1). Each record: instance_id, control, treatment,
    diff (treatment - control).
    """
    out = []
    for inst in _instances(rows):
        sub = _inst(rows, inst)
        c = fvals(_arm(sub, "control"), metric)
        t = fvals(_arm(sub, "treatment"), metric)
        if c.size == 0 or t.size == 0:
            continue
        mc, mt = float(np.median(c)), float(np.median(t))
        out.append({"instance_id": inst, "control": mc, "treatment": mt, "diff": mt - mc})
    return out


def per_instance_success(rows: list[Row]) -> list[dict]:
    """Per-instance success *rate* per arm (paired on instances present in both)."""
    out = []
    for inst in _instances(rows):
        sub = _inst(rows, inst)
        c = fvals(_arm(sub, "control"), "task_success")
        t = fvals(_arm(sub, "treatment"), "task_success")
        if c.size == 0 or t.size == 0:
            continue
        mc, mt = float(c.mean()), float(t.mean())
        out.append({"instance_id": inst, "control": mc, "treatment": mt, "diff": mt - mc})
    return out


def resolved_lens(rows: list[Row]) -> dict:
    """Successful-runs lens (PLAN §3.6) -- DESCRIPTIVE sidebar, not an adjustment.

    Filters to ``task_success == 1`` and reports, per token metric, the paired
    per-instance comparison restricted to instances that *both* arms resolved at
    least once. This is the cleanest like-for-like view (same instance, same
    successful outcome), but filtering on a post-treatment outcome conditions on
    a collider (PLAN §4.1): treatment can change which runs succeed, so this can
    *introduce* bias and must never be read as the primary effect. The real
    confound defense is the full paired design (§2.2). Subgroup sizes are carried
    on every record because at pilot N this subset is often tiny.
    """
    res = [r for r in rows if r.get("task_success") == 1]
    nfm = _nfiles_map(rows)
    out: dict = {"n_resolved_runs": {arm: len(_arm(res, arm)) for arm in ARMS},
                 "metrics": {}}
    for metric in TOKEN_METRICS:
        if not has_metric(rows, metric):
            continue
        records = []
        for inst in _instances(res):
            sub = _inst(res, inst)
            c = fvals(_arm(sub, "control"), metric)
            t = fvals(_arm(sub, "treatment"), metric)
            if c.size == 0 or t.size == 0:  # need a resolved run in BOTH arms
                continue
            mc, mt = float(np.median(c)), float(np.median(t))
            records.append({
                "instance_id": inst,
                "n_files": nfm.get(inst),
                "n_control": int(c.size), "n_treatment": int(t.size),
                "control": mc, "treatment": mt, "diff": mt - mc,
                "pct_change": (100.0 * (mt - mc) / mc) if mc else None,
            })
        out["metrics"][metric] = {
            "n_paired_instances": len(records),
            "per_instance": records,
        }
    return out


# --------------------------------------------------------------------------- #
# Inferential statistics
# --------------------------------------------------------------------------- #
def size_interaction(rows: list[Row], metric: str) -> dict:
    """Does the per-instance token saving scale with instance size (n_files)?

    The headline hypothesis (PLAN §2.3): qi's symbol navigation should help most
    on large, multi-file instances. For each instance we take the per-arm median
    and the percent change (treatment vs control), pair it with ``n_files``, and
    report the Spearman correlation between size and saving. Negative pct_change
    = treatment saves; a negative rho means bigger instances save more.
    """
    nfm = _nfiles_map(rows)
    records = []
    for r in per_instance_medians(rows, metric):
        pct = (100.0 * r["diff"] / r["control"]) if r["control"] else None
        records.append({"instance_id": r["instance_id"], "n_files": nfm.get(r["instance_id"]),
                        "control": r["control"], "treatment": r["treatment"], "pct_change": pct})
    paired = [(x["n_files"], x["pct_change"]) for x in records
              if x["n_files"] is not None and x["pct_change"] is not None]
    spearman = {"rho": None, "pvalue": None, "n": len(paired), "note": None}
    if len(paired) < 3:
        spearman["note"] = f"n={len(paired)} pairs -- too few for a correlation"
    else:
        xs, ys = zip(*paired)
        if len(set(xs)) < 2:
            spearman["note"] = "n_files has no variation -- correlation undefined"
        else:
            sr = scipy_stats.spearmanr(xs, ys)
            spearman["rho"] = float(sr.statistic)
            spearman["pvalue"] = float(sr.pvalue)
    return {"per_instance": records, "spearman": spearman}


def paired_wilcoxon(diffs: np.ndarray) -> dict:
    """Two-sided Wilcoxon signed-rank on per-instance median differences.

    Operates directly on the per-instance diffs (no resampling) -- the paired
    primary token test (PLAN §1.4). Records a reason when the sample is too
    small / all-zero for the test to be defined.
    """
    d = np.asarray(diffs, dtype=float)
    d = d[~np.isnan(d)]
    res = {"n_pairs": int(d.size), "statistic": None, "pvalue": None, "note": None}
    nonzero = d[d != 0]
    if nonzero.size < 1:
        res["note"] = "all per-instance differences are zero -- test undefined"
        return res
    if d.size < 5:
        # scipy still computes it, but the exact p-value floor at n<5 makes it
        # uninformative; we run it and flag rather than hide the number.
        res["note"] = f"n_pairs={d.size} < 5 -- p-value is not meaningful at this N"
    try:
        w = scipy_stats.wilcoxon(d, zero_method="wilcox", alternative="two-sided")
        res["statistic"] = float(w.statistic)
        res["pvalue"] = float(w.pvalue)
    except ValueError as exc:
        res["note"] = f"wilcoxon undefined: {exc}"
    return res


def clustered_bootstrap_diff(rows: list[Row], metric: str, iters: int,
                             rng: np.random.Generator) -> dict:
    """Clustered bootstrap 95% CI for the difference of pooled medians.

    Each iteration resamples *instances* with replacement (preserving the
    within-instance correlation, PLAN §6.2), pools the resampled instances' runs
    per arm, and recomputes ``median(treatment) - median(control)``. The point
    estimate is the difference of pooled medians on the observed data; the
    ratio is derived from the same resamples (PLAN §1.4).
    """
    instances = np.array(_instances(rows))
    # Pre-bucket each instance's per-arm values once; resampling then just
    # concatenates from these buckets instead of re-filtering the rows.
    buckets: dict[str, dict[str, np.ndarray]] = {}
    for inst in instances:
        sub = _inst(rows, inst)
        buckets[inst] = {arm: fvals(_arm(sub, arm), metric) for arm in ARMS}

    def pooled_diff_ratio(sample_insts) -> tuple[float, float]:
        ctrl = np.concatenate([buckets[i].get("control", np.array([]))
                               for i in sample_insts])
        treat = np.concatenate([buckets[i].get("treatment", np.array([]))
                                for i in sample_insts])
        if ctrl.size == 0 or treat.size == 0:
            return (np.nan, np.nan)
        mc, mt = np.median(ctrl), np.median(treat)
        return (mt - mc, (mt / mc) if mc else np.nan)

    point_diff, point_ratio = pooled_diff_ratio(list(instances))

    n = len(instances)
    boot_diff = np.empty(iters)
    boot_ratio = np.empty(iters)
    for b in range(iters):
        pick = rng.choice(instances, size=n, replace=True)
        boot_diff[b], boot_ratio[b] = pooled_diff_ratio(pick)

    def ci(arr):
        arr = arr[~np.isnan(arr)]
        if arr.size == 0:
            return (None, None)
        lo, hi = np.percentile(arr, [2.5, 97.5])
        return (float(lo), float(hi))

    diff_lo, diff_hi = ci(boot_diff)
    ratio_lo, ratio_hi = ci(boot_ratio)
    return {
        "n_instances": n,
        "point_diff": None if np.isnan(point_diff) else float(point_diff),
        "diff_ci": [diff_lo, diff_hi],
        "point_ratio": None if np.isnan(point_ratio) else float(point_ratio),
        "ratio_ci": [ratio_lo, ratio_hi],
        "excludes_zero": (diff_lo is not None and (diff_lo > 0 or diff_hi < 0)),
    }


def per_instance_ci(c_vals: np.ndarray, t_vals: np.ndarray, iters: int,
                    rng: np.random.Generator) -> list[float | None]:
    """Bootstrap 95% CI for ONE instance's median difference (PLAN §5.2 #8).

    Resamples the reps *within* this instance/arm with replacement -- a separate
    resample from the global clustered bootstrap (which resamples instances).
    Returns ``[lo, hi]`` for ``median(treatment) - median(control)``. At a few
    reps per arm this CI is coarse and lumpy (the median of 3 resampled points
    takes few distinct values); callers should surface the rep count so it is
    read with that caveat.
    """
    if c_vals.size == 0 or t_vals.size == 0:
        return [None, None]
    diffs = np.empty(iters)
    for b in range(iters):
        cs = rng.choice(c_vals, size=c_vals.size, replace=True)
        ts = rng.choice(t_vals, size=t_vals.size, replace=True)
        diffs[b] = np.median(ts) - np.median(cs)
    lo, hi = np.percentile(diffs, [2.5, 97.5])
    return [float(lo), float(hi)]


def clustered_bootstrap_success(rows: list[Row], iters: int,
                                rng: np.random.Generator) -> dict:
    """Clustered bootstrap 95% CI for the success-rate difference (treat-control).

    Success is clustered within instance just like tokens, so its CI must
    cluster too (PLAN §6.2). Resamples instances; per resample computes the
    pooled per-arm success rate and their difference. The non-inferiority check
    (PLAN §1.4) asks whether the CI lower bound clears the -5pp margin.
    """
    instances = np.array(_instances(rows))
    buckets = {inst: {arm: fvals(_arm(_inst(rows, inst), arm), "task_success")
                      for arm in ARMS}
               for inst in instances}

    def pooled_diff(sample_insts) -> float:
        ctrl = np.concatenate([buckets[i].get("control", np.array([])) for i in sample_insts])
        treat = np.concatenate([buckets[i].get("treatment", np.array([])) for i in sample_insts])
        if ctrl.size == 0 or treat.size == 0:
            return np.nan
        return float(treat.mean() - ctrl.mean())

    point = pooled_diff(list(instances))
    n = len(instances)
    boot = np.array([pooled_diff(rng.choice(instances, size=n, replace=True))
                     for _ in range(iters)])
    boot = boot[~np.isnan(boot)]
    lo, hi = (np.percentile(boot, [2.5, 97.5]) if boot.size else (np.nan, np.nan))
    return {
        "point_diff_pp": None if np.isnan(point) else float(point),
        "diff_ci_pp": [None if np.isnan(lo) else float(lo),
                       None if np.isnan(hi) else float(hi)],
        "non_inferior_5pp": bool(not np.isnan(lo) and lo > -0.05),
    }


# --------------------------------------------------------------------------- #
# Per-model analysis
# --------------------------------------------------------------------------- #
def split_rep_calibration(rows: list[Row], metric: str) -> dict | None:
    """Within-control split-rep null calibration.

    Control reps are sorted by run_id and split into two halves (first half →
    'a', remainder → 'b'). The null diff (median_b − median_a) should be near
    zero. Comparing its magnitude and Wilcoxon distribution to the actual
    treatment diff shows whether the treatment effect exceeds within-condition
    run-to-run noise on a per-instance basis.

    Requires at least 2 control reps per instance; instances with fewer are
    skipped and noted in the output.
    """
    if not has_metric(rows, metric):
        return None
    nfm = _nfiles_map(rows)
    per_inst = []
    skipped = []
    for inst in _instances(rows):
        ctrl = sorted(_arm(_inst(rows, inst), "control"),
                      key=lambda r: str(r.get("run_id") or ""))
        if len(ctrl) < 2:
            skipped.append(inst)
            continue
        n_a = max(1, len(ctrl) // 2)
        va = fvals(ctrl[:n_a], metric)
        vb = fvals(ctrl[n_a:], metric)
        if va.size == 0 or vb.size == 0:
            skipped.append(inst)
            continue
        ma, mb = float(np.median(va)), float(np.median(vb))
        null_diff = mb - ma
        null_pct = 100.0 * null_diff / ma if ma else None
        # treatment diff for the same instance
        ctrl_all = fvals(_arm(_inst(rows, inst), "control"), metric)
        trt_all = fvals(_arm(_inst(rows, inst), "treatment"), metric)
        if ctrl_all.size and trt_all.size:
            mc, mt = float(np.median(ctrl_all)), float(np.median(trt_all))
            trt_diff = mt - mc
            trt_pct = 100.0 * trt_diff / mc if mc else None
        else:
            trt_diff = trt_pct = None
        per_inst.append({
            "instance_id": inst,
            "n_files": nfm.get(inst),
            "n_a": n_a, "n_b": len(ctrl) - n_a,
            "null_diff": null_diff, "null_pct": null_pct,
            "trt_diff": trt_diff, "trt_pct": trt_pct,
            "null_exceeds_trt": (abs(null_diff) > abs(trt_diff)
                                 if trt_diff is not None else None),
        })
    if not per_inst:
        return None
    null_diffs = np.array([r["null_diff"] for r in per_inst], dtype=float)
    trt_diffs = np.array([r["trt_diff"] for r in per_inst
                          if r["trt_diff"] is not None], dtype=float)
    n_null_gt_trt = sum(1 for r in per_inst if r["null_exceeds_trt"])
    return {
        "per_instance": per_inst,
        "skipped": skipped,
        "null_wilcoxon": paired_wilcoxon(null_diffs),
        "null_median_diff": float(np.median(null_diffs)),
        "trt_median_diff": float(np.median(trt_diffs)) if trt_diffs.size else None,
        "n_null_exceeds_trt": n_null_gt_trt,
        "n_instances": len(per_inst),
    }


def analyse_model(rows: list[Row], iters: int, rng: np.random.Generator) -> dict:
    """Full statistics block for one model's runs."""
    out: dict = {"n_runs": len(rows), "n_instances": len(_instances(rows)), "arms": {}, "censoring": {}}
    has_exit = any("exit_status" in r for r in rows)

    # Descriptives per arm
    for arm in ARMS:
        arm_rows = _arm(rows, arm)
        block = {"n_runs": len(arm_rows)}
        for m in DESCRIPTIVE_METRICS + MECHANISM_METRICS:
            if has_metric(rows, m):
                block[m] = describe(fvals(arm_rows, m))
        # success rate + Wilson CI (PLAN §1.1)
        succ = sum(1 for r in arm_rows if r.get("task_success") == 1)
        rate, lo, hi = wilson_ci(succ, len(arm_rows))
        block["success"] = {"resolved": succ, "n": len(arm_rows),
                            "rate": rate, "wilson_ci": [lo, hi]}
        # completion / censoring (PLAN §3.3, §4.2)
        if arm_rows and any("submitted" in r for r in arm_rows):
            block["submitted_rate"] = sum(
                1 for r in arm_rows if str(r.get("submitted")).lower() == "true"
            ) / len(arm_rows)
        else:
            block["submitted_rate"] = None
        block["exit_status"] = (dict(Counter(r.get("exit_status") for r in arm_rows
                                              if r.get("exit_status") is not None).most_common())
                                if has_exit else {})
        out["arms"][arm] = block

    # Censoring rate by arm (LimitsExceeded => right-censored tokens, PLAN §4.2)
    if has_exit:
        for arm in ARMS:
            arm_rows = _arm(rows, arm)
            n_cens = sum(1 for r in arm_rows if r.get("exit_status") == "LimitsExceeded")
            out["censoring"][arm] = {"limits_exceeded": n_cens, "n": len(arm_rows),
                                     "rate": (n_cens / len(arm_rows)) if arm_rows else None}

    # Inferential, per token metric
    out["inferential"] = {}
    out["size_interaction"] = {}
    nfm = _nfiles_map(rows)
    # Decoupled stream for the within-instance CIs so the global clustered
    # bootstrap below stays bit-for-bit reproducible regardless of this addition.
    pi_rng = rng.spawn(1)[0]
    for m in TOKEN_METRICS:
        if not has_metric(rows, m):
            continue
        pim = per_instance_medians(rows, m)
        # attach n_files, percent change, rep counts, and a within-instance CI
        records = [dict(r) for r in pim]
        for rec in records:
            sub = _inst(rows, rec["instance_id"])
            c = fvals(_arm(sub, "control"), m)
            t = fvals(_arm(sub, "treatment"), m)
            rec["n_files"] = nfm.get(rec["instance_id"])
            rec["pct_change"] = (100.0 * rec["diff"] / rec["control"]) if rec["control"] else None
            rec["n_control"] = int(c.size)
            rec["n_treatment"] = int(t.size)
            rec["diff_ci"] = per_instance_ci(c, t, iters, pi_rng)
        diffs = np.array([r["diff"] for r in pim], dtype=float)
        out["inferential"][m] = {
            "n_paired_instances": len(pim),
            "wilcoxon": paired_wilcoxon(diffs),
            "bootstrap": clustered_bootstrap_diff(rows, m, iters, rng),
            "per_instance": records,
        }
        out["size_interaction"][m] = size_interaction(rows, m)

    # Split-rep calibration: null check using within-control rep splits
    out["split_rep_calibration"] = {
        m: split_rep_calibration(rows, m)
        for m in TOKEN_METRICS if has_metric(rows, m)
    }

    # Success comparison (clustered)
    out["success_comparison"] = clustered_bootstrap_success(rows, iters, rng)
    out["per_instance_success"] = per_instance_success(rows)

    # Successful-runs lens (descriptive sidebar, PLAN §3.6 / §4.1)
    out["resolved_lens"] = resolved_lens(rows)

    # Mechanism summary (qi adherence, PLAN §3.4)
    out["mechanism"] = {
        arm: {m: describe(fvals(_arm(rows, arm), m))
              for m in MECHANISM_METRICS if has_metric(rows, m)}
        for arm in ARMS
    }
    return out


# --------------------------------------------------------------------------- #
# Text summary
# --------------------------------------------------------------------------- #
def _fmt(x, nd=0):
    if x is None or (isinstance(x, float) and math.isnan(x)):
        return "    n/a"
    return f"{x:,.{nd}f}"


def write_summary(model: str, stats: dict, fh) -> None:
    p = lambda *a: print(*a, file=fh)
    label = model or "(unknown model)"
    p("=" * 78)
    p(f"MODEL: {label}    (n={stats['n_runs']} runs)")
    p("=" * 78)
    n_inst = stats.get("n_instances", 0)
    if n_inst < 10:
        p("\nPILOT -- p-values are EXPLORATORY (underpowered); the headline is the")
        p("effect size (raw median difference) with its clustered bootstrap CI.\n")
    else:
        p(f"\nN={n_inst} instances -- Wilcoxon p-values are interpretable at this sample")
        p("size; headline is the effect size with its clustered bootstrap CI.\n")

    # --- Descriptives ---
    for arm in ARMS:
        b = stats["arms"].get(arm)
        if not b:
            continue
        p(f"--- {arm.upper()}  (n={b['n_runs']} runs) ---")
        p(f"  {'metric':28s} {'median':>12s} {'IQR':>12s} {'mean':>12s} {'SD':>12s}")
        for m in DESCRIPTIVE_METRICS:
            d = b.get(m)
            if not d:
                continue
            p(f"  {m:28s} {_fmt(d['median']):>12s} {_fmt(d['iqr']):>12s} "
              f"{_fmt(d['mean']):>12s} {_fmt(d['sd']):>12s}")
        s = b["success"]
        p(f"  success: {s['resolved']}/{s['n']} = {s['rate']:.1%} "
          f"(95% Wilson CI {s['wilson_ci'][0]:.1%}..{s['wilson_ci'][1]:.1%})")
        if b.get("submitted_rate") is not None:
            p(f"  submitted (completion) rate: {b['submitted_rate']:.1%}")
        p(f"  exit_status: {b['exit_status']}")
        mech = "  ".join(
            f"{m.split('_')[0]}={b[m]['mean']:.1f}" for m in MECHANISM_METRICS
            if b.get(m) and b[m]['mean'] is not None)
        p(f"  mechanism (mean/run): {mech}")
        p("")

    # --- Censoring ---
    p("--- Censoring (LimitsExceeded => right-censored token counts) ---")
    for arm in ARMS:
        c = stats["censoring"].get(arm)
        if c:
            p(f"  {arm:9s} {c['limits_exceeded']}/{c['n']} "
              f"({(c['rate'] or 0):.1%}) hit the turn budget")
    p("")

    # --- Inferential token table (PLAN §5.9 #25) ---
    p("--- Inferential: token metrics (treatment - control) ---")
    p("    paired Wilcoxon on per-instance median diffs; clustered bootstrap CI")
    p("    (resamples instances). Negative diff => treatment uses FEWER tokens.\n")
    p(f"  {'metric':28s} {'med(C)':>10s} {'med(T)':>10s} {'rawΔ':>10s} "
      f"{'ratio':>7s} {'boot 95% CI (Δ)':>24s} {'Wilcoxon p':>12s}")
    for m in TOKEN_METRICS:
        inf = stats["inferential"].get(m)
        if not inf:
            continue
        boot = inf["bootstrap"]
        # Pooled per-arm medians -- the quantities the bootstrap diffs, so that
        # rawΔ == med(T) - med(C) on the displayed row (the per-instance medians
        # feed the Wilcoxon column instead).
        medC = stats["arms"]["control"][m]["median"]
        medT = stats["arms"]["treatment"][m]["median"]
        ci = boot["diff_ci"]
        ci_s = (f"[{_fmt(ci[0])}, {_fmt(ci[1])}]" if ci[0] is not None else "n/a")
        ratio_s = _fmt(boot["point_ratio"], 3) if boot["point_ratio"] is not None else "n/a"
        wp = inf["wilcoxon"]["pvalue"]
        wp_s = f"{wp:.4f}" if wp is not None else "n/a"
        star = " *excl.0" if boot["excludes_zero"] else ""
        p(f"  {m:28s} {_fmt(medC):>10s} {_fmt(medT):>10s} "
          f"{_fmt(boot['point_diff']):>10s} {ratio_s:>7s} {ci_s:>24s} {wp_s:>12s}{star}")
        note = inf["wilcoxon"].get("note")
        if note:
            p(f"      └ Wilcoxon note: {note}")
    p("")

    # --- Split-rep calibration ---
    src = stats.get("split_rep_calibration", {})
    if src:
        p("--- Split-rep calibration (null check) ---")
        p("    Control reps split by run order (first half=a, rest=b); null diff")
        p("    = median(b) - median(a) within control. Should be near zero.")
        p("    If |null| approaches |treatment|, the effect is within noise.")
        p("    Null Wilcoxon p should be large (H0: null diff = 0).\n")
        for m in TOKEN_METRICS:
            block = src.get(m)
            if not block:
                continue
            nw = block["null_wilcoxon"]
            wp_s = f"{nw['pvalue']:.4f}" if nw["pvalue"] is not None else "n/a"
            null_med = block["null_median_diff"]
            trt_med = block["trt_median_diff"]
            ratio_s = (f"{abs(trt_med/null_med):.2f}x" if null_med and trt_med
                       else "n/a")
            n_gt = block["n_null_exceeds_trt"]
            n_tot = block["n_instances"]
            p(f"  [{m}]")
            p(f"    null Wilcoxon p={wp_s}  "
              f"null median Δ={_fmt(null_med)}  trt median Δ={_fmt(trt_med)}  "
              f"(|trt|/|null|={ratio_s})")
            p(f"    instances where |null diff| > |trt diff|: {n_gt}/{n_tot}")
            p(f"    {'instance':18s} {'nf':>3s} {'n_a/b':>6s} "
              f"{'null%':>8s} {'trt%':>8s}  flag")
            for r in sorted(block["per_instance"],
                            key=lambda x: (x["n_files"] is None, x["n_files"] or 0)):
                nf_s = "n/a" if r["n_files"] is None else str(int(r["n_files"]))
                ab_s = f"{r['n_a']}/{r['n_b']}"
                np_s = "n/a" if r["null_pct"] is None else f"{r['null_pct']:+.1f}%"
                tp_s = "n/a" if r["trt_pct"] is None else f"{r['trt_pct']:+.1f}%"
                flag = "!" if r["null_exceeds_trt"] else " "
                p(f"    {r['instance_id'].split('__')[-1]:18s} {nf_s:>3s} "
                  f"{ab_s:>6s} {np_s:>8s} {tp_s:>8s}  {flag}")
            if block["skipped"]:
                p(f"    skipped (<2 control reps): "
                  f"{', '.join(i.split('__')[-1] for i in block['skipped'])}")
            p("")

    # --- Size interaction (PLAN §2.3): does saving scale with n_files? ---
    si = stats.get("size_interaction", {})
    if si:
        p("--- Token saving vs instance size (n_files) ---")
        p("    per-instance % change (treatment vs control); negative = qi saves.")
        p("    Spearman rho<0 => larger instances save more (the qi hypothesis).\n")
        for m in TOKEN_METRICS:
            block = si.get(m)
            if not block:
                continue
            p(f"  [{m}]")
            p(f"    {'instance':18s} {'n_files':>7s} {'pct_change':>11s}")
            for rec in sorted(block["per_instance"],
                             key=lambda r: (r["n_files"] is None, r["n_files"] or 0)):
                nf_s = "n/a" if rec["n_files"] is None else str(rec["n_files"])
                pc_s = "n/a" if rec["pct_change"] is None else f"{rec['pct_change']:+.1f}%"
                p(f"    {rec['instance_id'].split('__')[-1]:18s} {nf_s:>7s} {pc_s:>11s}")
            sp = block["spearman"]
            if sp["rho"] is not None:
                p(f"    Spearman(n_files, pct_change): rho={sp['rho']:+.3f} "
                  f"p={sp['pvalue']:.3f} (n={sp['n']})")
            elif sp.get("note"):
                p(f"    Spearman: {sp['note']}")
            p("")

    # --- Resolved-only paired view (PLAN §3.6 -- DESCRIPTIVE sidebar) ---
    rl = stats.get("resolved_lens")
    if rl:
        nrc, nrt = rl["n_resolved_runs"]["control"], rl["n_resolved_runs"]["treatment"]
        p("--- Resolved-only, paired (DESCRIPTIVE SIDEBAR -- NOT the primary effect) ---")
        p("    Tokens among task_success=1 runs, paired on instances BOTH arms")
        p("    resolved. Filtering on a post-treatment outcome conditions on a")
        p("    collider (§4.1): a cleaner like-for-like read, but it can introduce")
        p(f"    bias -- never the headline. Resolved runs: control={nrc}, treatment={nrt}.\n")
        for m in TOKEN_METRICS:
            block = rl["metrics"].get(m)
            if not block:
                continue
            recs = block["per_instance"]
            p(f"  [{m}]  ({block['n_paired_instances']} instance(s) resolved in both arms)")
            if not recs:
                p("    (none -- no instance was resolved by both arms)")
                p("")
                continue
            p(f"    {'instance':18s} {'n_files':>7s} {'nC/nT':>7s} "
              f"{'control':>11s} {'treatment':>11s} {'pct':>8s}")
            for r in sorted(recs, key=lambda x: (x["n_files"] is None, x["n_files"] or 0)):
                nf_s = "n/a" if r["n_files"] is None else str(r["n_files"])
                pc_s = "n/a" if r["pct_change"] is None else f"{r['pct_change']:+.1f}%"
                p(f"    {r['instance_id'].split('__')[-1]:18s} {nf_s:>7s} "
                  f"{r['n_control']}/{r['n_treatment']:<5d} "
                  f"{_fmt(r['control']):>11s} {_fmt(r['treatment']):>11s} {pc_s:>8s}")
            p("")

    # --- Success comparison ---
    sc = stats["success_comparison"]
    if sc["point_diff_pp"] is not None:
        lo, hi = sc["diff_ci_pp"]
        p("--- Success rate (clustered bootstrap, treatment - control) ---")
        p(f"  Δ = {sc['point_diff_pp']*100:+.1f}pp  "
          f"95% CI [{(lo or 0)*100:+.1f}, {(hi or 0)*100:+.1f}]pp  "
          f"non-inferior @5pp margin: {sc['non_inferior_5pp']}")
    p("")

    # --- Caveats (PLAN §4) ---
    p("--- Caveats (read these alongside the numbers) ---")
    p("  * Unit of analysis is the INSTANCE; reps within an instance are correlated.")
    if n_inst < 10:
        p("  * Pilot N is small -> p-values are exploratory, not confirmatory.")
    else:
        p(f"  * N={n_inst} instances -> Wilcoxon p-values interpretable; bootstrap CIs are the primary summary.")
    p("  * Success confounds token counts: prefer the paired within-instance view.")
    p("  * LimitsExceeded runs are right-censored (see censoring rates above).")
    p("  * tool_output_tokens_approx is ~4 chars/token, descriptive only.")
    p("")


# --------------------------------------------------------------------------- #
# Charts (Priority 1 -- PLAN §5)
# --------------------------------------------------------------------------- #
def _save(fig, path: Path) -> None:
    fig.savefig(path, dpi=130, bbox_inches="tight")
    plt.close(fig)


def make_charts(rows: list[Row], stats: dict, charts_dir: Path) -> list[str]:
    """Render the Priority-1 chart set. Returns the filenames written."""
    charts_dir.mkdir(parents=True, exist_ok=True)
    written: list[str] = []
    colors = {"control": "#b0b0b0", "treatment": "#2a7fb8"}
    _palette = ["#e41a1c", "#377eb8", "#4daf4a", "#984ea3", "#ff7f00", "#a65628"]
    for i, a in enumerate(ARMS):
        if a not in colors:
            colors[a] = _palette[i % len(_palette)]

    def arm_vals(metric):
        return [fvals(_arm(rows, a), metric) for a in ARMS]

    # 1-3: boxplots of the three token metrics
    box_specs = [
        ("01_boxplot_total_input.png", "total_input_tokens", "Total input tokens"),
        ("02_boxplot_peak_prompt.png", "peak_prompt_tokens", "Peak prompt tokens"),
        ("03_boxplot_tool_output.png", "tool_output_tokens_approx", "Tool output tokens (approx)"),
    ]
    for fname, metric, title in box_specs:
        if not has_metric(rows, metric):
            continue
        fig, ax = plt.subplots(figsize=(5, 4))
        data = arm_vals(metric)
        bp = ax.boxplot(data, tick_labels=list(ARMS), patch_artist=True, showmeans=True)
        for patch, a in zip(bp["boxes"], ARMS):
            patch.set_facecolor(colors[a])
        ax.set_title(f"{title} by arm")
        ax.set_ylabel(metric)
        _save(fig, charts_dir / fname)
        written.append(fname)

    # 4: violin of peak_prompt
    if has_metric(rows, "peak_prompt_tokens"):
        fig, ax = plt.subplots(figsize=(5, 4))
        ax.violinplot(arm_vals("peak_prompt_tokens"), showmedians=True)
        ax.set_xticks([1, 2], list(ARMS))
        ax.set_title("Peak prompt tokens (distribution shape)")
        ax.set_ylabel("peak_prompt_tokens")
        _save(fig, charts_dir / "04_violin_peak_prompt.png")
        written.append("04_violin_peak_prompt.png")

    # 5: overlaid histogram of peak_prompt
    if has_metric(rows, "peak_prompt_tokens"):
        fig, ax = plt.subplots(figsize=(5, 4))
        for a in ARMS:
            ax.hist(fvals(_arm(rows, a), "peak_prompt_tokens"),
                    bins=12, alpha=0.55, label=a, color=colors[a])
        ax.set_title("Peak prompt tokens")
        ax.set_xlabel("peak_prompt_tokens")
        ax.set_ylabel("runs")
        ax.legend()
        _save(fig, charts_dir / "05_hist_peak_prompt.png")
        written.append("05_hist_peak_prompt.png")

    # 6: ECDF of peak_prompt
    if has_metric(rows, "peak_prompt_tokens"):
        fig, ax = plt.subplots(figsize=(5, 4))
        for a in ARMS:
            v = np.sort(fvals(_arm(rows, a), "peak_prompt_tokens"))
            if v.size:
                ax.step(v, np.arange(1, v.size + 1) / v.size, where="post",
                        label=a, color=colors[a])
        ax.set_title("Peak prompt tokens (ECDF)")
        ax.set_xlabel("peak_prompt_tokens")
        ax.set_ylabel("cumulative fraction")
        ax.legend()
        _save(fig, charts_dir / "06_ecdf_peak_prompt.png")
        written.append("06_ecdf_peak_prompt.png")

    # 7: paired dot/line of per-instance median peak_prompt
    pim = per_instance_medians(rows, "peak_prompt_tokens")
    if pim:
        fig, ax = plt.subplots(figsize=(6, 4))
        for r in pim:
            better = r["treatment"] < r["control"]
            ax.plot([0, 1], [r["control"], r["treatment"]],
                    color=("#2ca02c" if better else "#d62728"), alpha=0.7, marker="o")
        ax.set_xticks([0, 1], list(ARMS))
        ax.set_title("Per-instance median peak prompt tokens\n(green=treatment lower)")
        ax.set_ylabel("peak_prompt_tokens")
        _save(fig, charts_dir / "07_paired_dot_peak_prompt.png")
        written.append("07_paired_dot_peak_prompt.png")

    # 8: forest plot -- per-instance median diff + within-instance 95% CI
    # (PLAN §5.2 #8). CIs come from the inferential records (rep resampling);
    # rep counts are annotated because at few reps the CI is coarse.
    forest = stats["inferential"].get("peak_prompt_tokens", {}).get("per_instance", [])
    if forest:
        order = sorted(forest, key=lambda r: r["diff"])
        fig, ax = plt.subplots(figsize=(7.5, max(3, 0.5 * len(order) + 1)))
        y = np.arange(len(order))
        diffs = np.array([r["diff"] for r in order])
        # asymmetric xerr from each record's [lo, hi]; fall back to 0 if absent
        lo = np.array([(r.get("diff_ci") or [None, None])[0] for r in order], dtype=float)
        hi = np.array([(r.get("diff_ci") or [None, None])[1] for r in order], dtype=float)
        xerr = np.vstack([np.nan_to_num(diffs - lo, nan=0.0),
                          np.nan_to_num(hi - diffs, nan=0.0)])
        ax.errorbar(diffs, y, xerr=xerr, fmt="o", color="#2a7fb8",
                    ecolor="#9ecae1", elinewidth=2, capsize=4, zorder=3)
        ax.axvline(0, color="k", lw=0.8)
        labels = []
        for r in order:
            name = r['instance_id'].split('__')[-1]
            nf = f"\nn_files={int(r['n_files'])}" if r.get('n_files') is not None else ""
            labels.append(f"{name} ({r['n_control']}/{r['n_treatment']}){nf}")
        ax.set_yticks(y, labels, fontsize=8)
        ax.set_xlabel("median peak_prompt diff (treatment - control)")
        ax.set_title("Per-instance effect with within-instance 95% CI\n"
                     "(negative = treatment saves; label = nC/nT reps)")
        _save(fig, charts_dir / "08_forest_plot.png")
        written.append("08_forest_plot.png")

    # 9: heatmap instances x token metrics, color = treatment/control ratio
    metrics_present = [m for m in TOKEN_METRICS if has_metric(rows, m)]
    if metrics_present:
        # one pim per metric, indexed by instance for O(1) lookup
        pims = {m: {r["instance_id"]: r for r in per_instance_medians(rows, m)}
                for m in metrics_present}
        nf_map = {r["instance_id"]: r.get("n_files") for r in rows}
        # sort: n_files ascending (small at top in imshow), then alpha descending
        # within same n_files (z at top, a at bottom). Stable double-sort achieves this.
        insts_set = set.intersection(*[set(pm) for pm in pims.values()])
        # sort by short name descending first (z at top), then n_files ascending
        # (stable double-sort: second key wins; first key breaks ties within second)
        insts = sorted(insts_set, key=lambda i: i.split("__")[-1], reverse=True)
        insts = sorted(insts, key=lambda i: nf_map.get(i) or 0)
        ratios, labels = [], []
        for inst in insts:
            row = []
            for m in metrics_present:
                rr = pims[m][inst]
                c, t = rr["control"], rr["treatment"]
                row.append(t / c if c else np.nan)
            ratios.append(row)
            nf = nf_map.get(inst)
            nf_str = f"\nn_files={int(nf)}" if nf is not None else ""
            labels.append(inst.split("__")[-1] + nf_str)
        if ratios:
            arr = np.array(ratios)
            fig, ax = plt.subplots(figsize=(1.6 * len(metrics_present) + 2, 0.7 * len(labels) + 2))
            im = ax.imshow(arr, cmap="RdYlGn_r", vmin=0.5, vmax=1.5, aspect="auto")
            ax.set_xticks(range(len(metrics_present)), metrics_present, rotation=30, ha="right")
            ax.set_yticks(range(len(labels)), labels, fontsize=8)
            for i in range(arr.shape[0]):
                for j in range(arr.shape[1]):
                    if not np.isnan(arr[i, j]):
                        ax.text(j, i, f"{arr[i, j]:.2f}", ha="center", va="center", fontsize=8)
            fig.colorbar(im, ax=ax, label="treatment / control ratio")
            ax.set_title("Token ratio (green = treatment saves)")
            _save(fig, charts_dir / "09_heatmap.png")
            written.append("09_heatmap.png")

    # 10: success-rate bar with Wilson CI
    fig, ax = plt.subplots(figsize=(5, 4))
    rates, los, his = [], [], []
    for a in ARMS:
        s = stats["arms"][a]["success"]
        rates.append(s["rate"]); los.append(s["rate"] - s["wilson_ci"][0])
        his.append(s["wilson_ci"][1] - s["rate"])
    ax.bar(ARMS, rates, color=[colors[a] for a in ARMS], yerr=[los, his], capsize=6)
    ax.set_ylim(0, 1)
    ax.set_ylabel("task_success rate")
    ax.set_title("Resolution rate by arm (95% Wilson CI)")
    _save(fig, charts_dir / "10_bar_success_rate.png")
    written.append("10_bar_success_rate.png")

    # 11: stacked exit_status by arm
    if any("exit_status" in r for r in rows):
        statuses = sorted({r["exit_status"] for r in rows if r.get("exit_status")})
        fig, ax = plt.subplots(figsize=(5, 4))
        bottom = np.zeros(len(ARMS))
        cmap = plt.get_cmap("tab10")
        for k, st in enumerate(statuses):
            counts = [sum(1 for r in _arm(rows, a) if r.get("exit_status") == st) for a in ARMS]
            ax.bar(ARMS, counts, bottom=bottom, label=st, color=cmap(k % 10))
            bottom += counts
        ax.set_ylabel("runs")
        ax.set_title("Exit status by arm")
        ax.legend(fontsize=8)
        _save(fig, charts_dir / "11_stacked_exit_status.png")
        written.append("11_stacked_exit_status.png")

    # 12: per-instance success rate, paired
    pis = per_instance_success(rows)
    if pis:
        fig, ax = plt.subplots(figsize=(6, 4))
        for r in pis:
            ax.plot([0, 1], [r["control"], r["treatment"]],
                    color="#2a7fb8", alpha=0.5, marker="o")
        ax.set_xticks([0, 1], list(ARMS))
        ax.set_ylim(-0.05, 1.05)
        ax.set_title("Per-instance success rate")
        ax.set_ylabel("success rate")
        _save(fig, charts_dir / "12_paired_dot_success.png")
        written.append("12_paired_dot_success.png")

    # 16: resolved-only boxplot of peak_prompt (PLAN §3.6 descriptive sidebar).
    # Subgroup sizes are annotated because this subset is tiny at pilot N.
    if has_metric(rows, "peak_prompt_tokens") and has_metric(rows, "task_success"):
        res = [r for r in rows if r.get("task_success") == 1]
        data = [fvals(_arm(res, a), "peak_prompt_tokens") for a in ARMS]
        if any(d.size for d in data):
            fig, ax = plt.subplots(figsize=(5, 4))
            bp = ax.boxplot(data, tick_labels=list(ARMS), patch_artist=True, showmeans=True)
            for patch, a in zip(bp["boxes"], ARMS):
                patch.set_facecolor(colors[a])
            for i, d in enumerate(data, start=1):
                ax.annotate(f"n={d.size}", (i, 0), xytext=(0, -22),
                            textcoords="offset points", ha="center", fontsize=8,
                            xycoords=("data", "axes fraction"))
            ax.set_title("Peak prompt tokens — RESOLVED runs only\n(descriptive sidebar, §3.6)")
            ax.set_ylabel("peak_prompt_tokens")
            _save(fig, charts_dir / "16_boxplot_peak_resolved.png")
            written.append("16_boxplot_peak_resolved.png")

    # 23/24: bootstrap CI plots for the two primary token metrics. This is a
    # display-only resample (membership, like the original); the reported CI is
    # the one from clustered_bootstrap_diff, overlaid here.
    rng = np.random.default_rng(0)
    for fname, metric, title in [
        ("23_bootstrap_ci_peak.png", "peak_prompt_tokens", "peak prompt tokens"),
        ("24_bootstrap_ci_total_input.png", "total_input_tokens", "total input tokens"),
    ]:
        inf = stats["inferential"].get(metric)
        if not inf:
            continue
        instances = np.array(_instances(rows))
        n = len(instances)
        buckets = {inst: {arm: fvals(_arm(_inst(rows, inst), arm), metric) for arm in ARMS}
                   for inst in instances}
        boot = []
        for _ in range(2000):
            pick = rng.choice(instances, size=n, replace=True)
            c = np.concatenate([buckets[i].get("control", np.array([])) for i in pick])
            t = np.concatenate([buckets[i].get("treatment", np.array([])) for i in pick])
            if c.size and t.size:
                boot.append(np.median(t) - np.median(c))
        if not boot:
            continue
        ci = inf["bootstrap"]["diff_ci"]
        fig, ax = plt.subplots(figsize=(6, 4))
        ax.hist(boot, bins=40, color="#9ecae1", edgecolor="white")
        ax.axvline(0, color="k", lw=0.8)
        if ci[0] is not None:
            ax.axvline(ci[0], color="#d62728", ls="--", label="95% CI")
            ax.axvline(ci[1], color="#d62728", ls="--")
        if inf["bootstrap"]["point_diff"] is not None:
            ax.axvline(inf["bootstrap"]["point_diff"], color="#2a7fb8", label="point estimate")
        ax.set_title(f"Bootstrap median difference: {title}\n(treatment - control)")
        ax.set_xlabel(f"Δ {metric}")
        ax.legend(fontsize=8)
        _save(fig, charts_dir / fname)
        written.append(fname)

    # 26: size interaction -- per-instance % token change vs n_files. This is
    # the headline moderator plot (PLAN §2.3): does qi save more on larger,
    # multi-file instances?
    si = stats.get("size_interaction", {})
    panels = [m for m in TOKEN_METRICS if si.get(m) and
              any(r["n_files"] is not None and r["pct_change"] is not None
                  for r in si[m]["per_instance"])]
    for m in panels:
        block = si[m]
        pts = [(r["n_files"], r["pct_change"], r["instance_id"].split("__")[-1])
               for r in block["per_instance"]
               if r["n_files"] is not None and r["pct_change"] is not None]
        xs = [x for x, _, _ in pts]; ys = [y for _, y, _ in pts]
        fig, ax = plt.subplots(figsize=(5, 4))
        ax.axhline(0, color="k", lw=0.8)
        ax.scatter(xs, ys, color="#2a7fb8", zorder=3)
        for x, y, lbl in pts:
            ax.annotate(lbl, (x, y), fontsize=7, xytext=(4, 2),
                        textcoords="offset points")
        sp = block["spearman"]
        sub = (f"Spearman rho={sp['rho']:+.2f} (p={sp['pvalue']:.2f})"
               if sp["rho"] is not None else (sp.get("note") or ""))
        ax.set_title(f"Token saving vs instance size\n{m} — {sub}", fontsize=9)
        ax.set_xlabel("n_files (gold patch)")
        ax.set_ylabel("% change (neg = qi saves)")
        ax.xaxis.set_major_locator(MaxNLocator(integer=True))
        fname = f"26_size_vs_savings_{m}.png"
        _save(fig, charts_dir / fname)
        written.append(fname)

    return written


# --------------------------------------------------------------------------- #
# Main
# --------------------------------------------------------------------------- #
def _default_csv() -> Path | None:
    dirs = sorted((p for p in paths.RUNS_DIR.glob("*") if p.is_dir()))
    for d in reversed(dirs):
        cand = d / "runs_with_success.csv"
        if cand.is_file():
            return cand
    return None


def _json_safe(obj):
    """Recursively coerce numpy scalars/bools to plain Python for json.dump."""
    if isinstance(obj, dict):
        return {k: _json_safe(v) for k, v in obj.items()}
    if isinstance(obj, (list, tuple)):
        return [_json_safe(v) for v in obj]
    if isinstance(obj, (np.integer,)):
        return int(obj)
    if isinstance(obj, (np.floating,)):
        f = float(obj)
        return None if math.isnan(f) else f
    if isinstance(obj, (np.bool_,)):
        return bool(obj)
    if isinstance(obj, float) and math.isnan(obj):
        return None
    return obj


def model_slug(model: str) -> str:
    """Filesystem-safe token for a model dir name."""
    return "".join(c if c.isalnum() or c in "-_" else "-" for c in (model or "unknown"))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--csv", type=Path, default=None,
                    help="runs_with_success.csv (default: <dir>/runs_with_success.csv, "
                         "or newest under results/runs/<ts>/ when --dir is omitted)")
    ap.add_argument("--dir", type=Path, default=None,
                    help="Run directory: reads runs_with_success.csv from it and "
                         "writes outputs there (same meaning as merge_results.py). "
                         "--csv overrides the input file; output still lands here.")
    ap.add_argument("--batch", default=None, metavar="BATCH_ID",
                    help="Resolve the run directory as results/runs/<batch>/ "
                         "(alias for --dir results/runs/<batch>)")
    ap.add_argument("--pool", type=Path, default=paths.DATA_DIR / "pool.csv",
                    help="Fallback source for per-instance n_files when the CSV "
                         "predates the named-batch refactor and lacks the column: "
                         "data/pool.csv or an instances-list file with a trailing "
                         f"count (default: {paths.DATA_DIR / 'pool.csv'})")
    ap.add_argument("--bootstrap-iters", type=int, default=10000,
                    help="Clustered bootstrap resamples (default: 10000)")
    ap.add_argument("--seed", type=int, default=42,
                    help="RNG seed for the bootstrap (default: 42)")
    ap.add_argument("--no-charts", action="store_true",
                    help="Skip chart rendering even if matplotlib is available")
    args = ap.parse_args()

    # Input precedence: explicit --csv wins; else runs_with_success.csv inside
    # --dir or --batch; else the newest run dir's CSV.
    # --dir / --batch always sets the output location.
    run_dir = args.dir or (paths.batch_run_dir(args.batch) if args.batch else None)
    if args.csv is not None:
        csv_path = args.csv
    elif run_dir is not None:
        csv_path = run_dir / "runs_with_success.csv"
    else:
        csv_path = _default_csv()
    if csv_path is None or not csv_path.is_file():
        print(f"ERROR: no runs_with_success.csv found"
              f"{f' at {csv_path}' if csv_path else ''}; pass --csv or --dir",
              file=sys.stderr)
        return 1

    out_dir = (run_dir or csv_path.parent).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    n_files_map = load_n_files(args.pool)
    rows = load(csv_path, n_files_map)
    print(f"Loaded {len(rows)} runs from {csv_path}")
    # Discover arms from the data so arbitrary arm names (e.g. prompt variants)
    # are handled without a hardcoded allow-list.
    global ARMS
    discovered = sorted({r["arm"] for r in rows if r.get("arm")})
    if discovered:
        ARMS = tuple(discovered)
    if n_files_map:
        matched = len({r["instance_id"] for r in rows
                       if not (isinstance(r["n_files"], float) and math.isnan(r["n_files"]))})
        print(f"  n_files joined from {args.pool} ({matched} instances matched)")
    else:
        print(f"  WARNING: no n_files from {args.pool} -- size interaction skipped",
              file=sys.stderr)
    if plt is None and not args.no_charts:
        print("WARNING: matplotlib unavailable -- charts skipped.", file=sys.stderr)

    models = sorted({r["model"] for r in rows})
    multi = len(models) > 1
    rng = np.random.default_rng(args.seed)

    all_stats: dict = {"input_csv": str(csv_path), "models": {}}
    summary_path = out_dir / "stats_summary.txt"
    with summary_path.open("w") as fh:
        for model in models:
            mrows = [r for r in rows if r["model"] == model]
            stats = analyse_model(mrows, args.bootstrap_iters, rng)
            all_stats["models"][model or "(unknown)"] = stats
            write_summary(model, stats, fh)

            if plt is not None and not args.no_charts:
                charts_dir = (out_dir / "charts" / (model_slug(model) if multi else "")).resolve()
                written = make_charts(mrows, stats, charts_dir)
                print(f"  [{model or '(unknown)'}] wrote {len(written)} charts -> {charts_dir}")

    (out_dir / "stats.json").write_text(json.dumps(_json_safe(all_stats), indent=2))
    print(f"Wrote {summary_path}")
    print(f"Wrote {out_dir / 'stats.json'}")
    # Echo the summary to stdout (PLAN §5.9 #25: printed to stdout).
    print()
    print(summary_path.read_text())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
