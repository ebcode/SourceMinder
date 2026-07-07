#!/usr/bin/env python3
"""Cross-instance meta-analysis for the SWE-bench **Pro** experiment.

Each Pro batch is one instance x N reps per arm (see PRO_ANALYZE.md). A single
batch only describes within-instance variance on one task; the qi hypothesis
("qi saves tokens, never hurts resolve") is a claim *across* instances. The
control-arm token scale spans ~16x across instances, so a naive Mann-Whitney on
pooled raw values is dominated by bug size, not by the treatment. This script
instead works on the per-instance **log-ratio of medians**, which is scale-free,
then pools the instances with inverse-variance weighting.

  effect_i = ln( median_treatment_i / median_control_i )      (negative = cheaper)
  pooled   = Sigma(w_i * effect_i) / Sigma(w_i),  w_i = 1 / se_i^2

It consumes already-computed per-batch artifacts (runs_with_success.csv) -- it
does NOT re-parse trajectories. Stdlib + matplotlib only; shared helpers (fnum,
wilson, arm constants) are imported from analyze_pro_stats.py rather than
copied. See docs/CROSS_INSTANCE.md.

Usage:
  experiment/.venv_pro/bin/python experiment/analysis/cross_batch_compare.py \
      --manifest experiment/analysis/cross_instance_manifest.txt \
      --out experiment/results/pro_runs/_cross/
  # or repeated --batch DIR for an ad-hoc subset.
"""
from __future__ import annotations

import argparse
import csv
import math
import random
import statistics as st
import sys
from pathlib import Path

# Shared helpers live in analyze_pro_stats.py (same directory). The script's own
# dir is on sys.path[0] when invoked by full path, so a plain import resolves.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze_pro_stats import (  # noqa: E402
    CONTROL, TREATMENT, ARM_COLOR, ARM_LABEL, fnum,  # load_wall_times: wall time removed 2026-07-05
    EXPLORE_TOOLS, EXPLORE_TOKEN_TOOLS, EXPLORE_LABEL, EXPLORE_COLORS,
    RADAR_AXES, radar_ratios, _geomean,
)
from lib import cmds  # noqa: E402  (analyze_pro_stats put experiment/ on sys.path)

try:
    import matplotlib
    matplotlib.use("Agg")  # headless: PNG output, no display
    import matplotlib.pyplot as plt
    import matplotlib.ticker as mticker
except ImportError:  # soft dependency -- charts skip, CSV does not
    plt = None
    mticker = None

# Continuous metrics that get a forest plot + a pooled estimate. turns are
# tax-immune; total_tokens/cost are inflated on the format-tax instances (see
# FORMAT_TAX_INSTANCES) and carry the format_tax flag + a chart footnote.
# Wall time (duration_sec) removed from the cross analysis 2026-07-05: the
# ledger-based durations are skewed by the user's spotty wifi, so it isn't a
# reliable cross-instance signal even as a CSV row. The per-batch pipeline
# (wall_time.py / analyze_pro_stats.py) still records it.
DEFAULT_METRICS = ["turn_count", "total_tokens", "cost"]  # was: + "duration_sec"
METRIC_LABEL = {
    "turn_count": "turns",
    "total_tokens": "total tokens",
    "cost": "cost ($)",
    # "duration_sec": "wall time (s)",
}
# Metrics kept in cross_instance.csv / the stdout report but not charted.
# (duration_sec lived here while wall time was CSV-only; now fully removed.)
NO_CHART_METRICS: set[str] = set()
# Metrics whose token/cost counts are inflated by the per-turn format tax (see
# PRO_ANALYZE.md -> Format tax). Only these metrics are flagged, not turns/wall.
# The tax is a DeepSeek/MiMo artifact (wrong tool-call format for the harness);
# Haiku batches pay no tax. webclients left the set when its batch was swapped
# from DeepSeek to Haiku; ansible left when the MiMo batch was replaced by
# ansible2_haiku. Add instance short-labels here if a taxed batch re-enters.
TAX_METRICS = {"total_tokens", "cost"}
FORMAT_TAX_INSTANCES: set[str] = set()

# Model family -> color, so model-driven spread is visible on every forest.
MODEL_COLOR = {
    "haiku": "#4c72b0",
    "deepseek": "#dd8452",
    "mimo": "#55a868",
}
MODEL_OTHER = "#7f7f7f"


def model_family(model: str) -> str:
    """Collapse a model slug to a short family key for coloring/annotation."""
    m = model.lower()
    for fam in MODEL_COLOR:
        if fam in m:
            return fam
    return model


def model_color(model: str) -> str:
    return MODEL_COLOR.get(model_family(model), MODEL_OTHER)


def short_label(instance_id: str) -> str:
    """instance_protonmail__webclients-<sha>... -> 'webclients'.

    Take the segment after '__' (drops the 'instance_<org>' prefix), then the
    part before the first '-' (drops the commit sha / version suffix). Lowercased
    so labels are stable across the CSV and every chart. One helper, one mapping.
    """
    tail = instance_id.split("__", 1)[-1]
    return tail.split("-", 1)[0].lower()


def load_batch(run_dir: Path, metrics: list[str]) -> dict | None:
    """Read one batch's per-rep metric values + resolve counts.

    Returns None when the batch has no runs_with_success.csv or no reps in
    both arms.
    """
    csv_path = run_dir / "runs_with_success.csv"
    if not csv_path.is_file():
        print(f"  skip {run_dir.name}: no runs_with_success.csv", file=sys.stderr)
        return None
    with csv_path.open(newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        return None

    # Wall time removed from the cross analysis 2026-07-05 (see DEFAULT_METRICS).
    # wall = load_wall_times(run_dir)
    # for r in rows:
    #     r["duration_sec"] = wall.get((r["arm"], r["run_id"]), "")

    instance_id = rows[0]["instance_id"]
    model = rows[0].get("model", "")
    vals = {col: {CONTROL: [], TREATMENT: []} for col in metrics}
    resolve = {CONTROL: [0, 0], TREATMENT: [0, 0]}
    log_size = {CONTROL: [], TREATMENT: []}
    for r in rows:
        arm = r["arm"]
        if arm not in (CONTROL, TREATMENT):
            continue
        resolve[arm][1] += 1
        if r.get("task_success") == "1":
            resolve[arm][0] += 1
        for col in metrics:
            v = fnum(r.get(col))
            if v is not None:
                vals[col][arm].append(v)
        ls = fnum(r.get("log_size_kb"))
        if ls is not None:
            log_size[arm].append(ls)

    if not resolve[CONTROL][1] or not resolve[TREATMENT][1]:
        print(f"  skip {run_dir.name}: needs reps in both arms", file=sys.stderr)
        return None

    return {
        "dir": run_dir.name,
        "_run_dir": run_dir,
        "label": short_label(instance_id),
        "model": model,
        "vals": vals,
        "resolve": resolve,
        "log_size": log_size,
    }


def log_ratio_effect(cv, tv, iters, seed):
    """Per-instance effect = ln(median_T / median_C) with a bootstrap CI + se.

    Resamples reps with replacement within each arm; the bootstrap sd of the
    resampled log-ratios is the standard error fed to inverse-variance pooling.
    Medians must be positive (tokens/cost/turns/wall always are); guards skip any
    degenerate resample. Returns (effect, ci_lo, ci_hi, se) or None.
    """
    cv = [x for x in cv if x is not None and x > 0]
    tv = [x for x in tv if x is not None and x > 0]
    if not cv or not tv:
        return None
    mc, mt = st.median(cv), st.median(tv)
    if mc <= 0 or mt <= 0:
        return None
    effect = math.log(mt / mc)

    rng = random.Random(seed)
    samples = []
    for _ in range(iters):
        rc = st.median([rng.choice(cv) for _ in cv])
        rt = st.median([rng.choice(tv) for _ in tv])
        if rc > 0 and rt > 0:
            samples.append(math.log(rt / rc))
    if len(samples) < 2:
        return effect, effect, effect, 0.0
    samples.sort()
    lo = samples[int(0.025 * len(samples))]
    hi = samples[int(0.975 * len(samples)) - 1]
    se = st.stdev(samples)
    return effect, lo, hi, se


def pool(effects, ses):
    """Inverse-variance pool: (pooled, se_pool, ci_lo, ci_hi, k, Q, I2) or None.

    Instances with a zero/undefined se (degenerate bootstrap) are dropped from
    the pool. High I2 means the qi effect genuinely varies by instance/model --
    a finding in itself, not an error.
    """
    pairs = [(e, s) for e, s in zip(effects, ses) if s and s > 0]
    if not pairs:
        return None
    ws = [1.0 / s ** 2 for _, s in pairs]
    W = sum(ws)
    pooled = sum(w * e for w, (e, _) in zip(ws, pairs)) / W
    se_pool = math.sqrt(1.0 / W)
    lo, hi = pooled - 1.96 * se_pool, pooled + 1.96 * se_pool
    k = len(pairs)
    Q = sum(w * (e - pooled) ** 2 for w, (e, _) in zip(ws, pairs))
    I2 = max(0.0, (Q - (k - 1)) / Q) if Q > 0 else 0.0
    return pooled, se_pool, lo, hi, k, Q, I2


def pct(effect: float) -> float:
    """Log-ratio -> percent change for human reading: exp(effect) - 1, in %."""
    return (math.exp(effect) - 1.0) * 100.0


# --------------------------------------------------------------------------- #
# Charts
# --------------------------------------------------------------------------- #
def _save(fig, path: Path) -> None:
    fig.savefig(path, dpi=96, bbox_inches="tight")
    plt.close(fig)


def forest_chart(metric, entries, pooled, charts_dir: Path) -> str:
    """One forest plot for a metric: per-instance effect dot + CI whisker
    (colored by model), sorted alphabetically by label (consistent across all
    charts), with a pooled diamond at the bottom. x-axis is percent change;
    0 = no effect. Returns the filename written."""
    rows = sorted(entries, key=lambda e: e["label"])  # fixed alpha order across all charts
    labels = [e["label"] for e in rows]
    fig, ax = plt.subplots(figsize=(7, 0.55 * (len(rows) + 2) + 1.2))

    for y, e in enumerate(reversed(rows)):  # reversed so alphabetically first label at top
        x = pct(e["effect"])
        xlo, xhi = pct(e["ci_lo"]), pct(e["ci_hi"])
        ax.errorbar(x, y, xerr=[[x - xlo], [xhi - x]], fmt="o", ms=8,
                    color=model_color(e["model"]), ecolor=model_color(e["model"]),
                    elinewidth=1.5, capsize=4, zorder=3)

    yticks = list(range(len(rows)))
    yticklabels = list(reversed(labels))

    if pooled is not None:
        # I² is deliberately NOT shown here: at k=5 with wide bootstrap SEs it
        # collapses to ~0 regardless of true heterogeneity, so on the diamond it
        # reads as false "consistency". The per-instance dot scatter already
        # shows the spread; Q/I² stay in cross_instance.csv for context.
        p, _se, plo, phi, k, _Q, _i2 = pooled
        yp = -1.2
        xp = pct(p)
        ax.errorbar(xp, yp, xerr=[[xp - pct(plo)], [pct(phi) - xp]], fmt="D",
                    ms=11, color="black", ecolor="black", elinewidth=2,
                    capsize=5, zorder=4)
        yticks = [yp] + yticks
        yticklabels = [f"POOLED (k={k})"] + yticklabels

    ax.axvline(0, color="0.4", linestyle="--", linewidth=1, zorder=1)
    ax.set_yticks(yticks)
    ax.set_yticklabels(yticklabels)
    ax.set_ylim(-2.0, len(rows) - 0.3)
    ax.set_xlabel("% change, treatment vs control  (negative = treatment cheaper)")
    ax.set_title(f"{METRIC_LABEL.get(metric, metric)}: per-instance effect (log-ratio of medians)")

    # Model legend (only the families actually present).
    fams = sorted({model_family(e["model"]) for e in rows})
    handles = [plt.Line2D([0], [0], marker="o", linestyle="", color=MODEL_COLOR.get(f, MODEL_OTHER),
                          label=f, markersize=8) for f in fams]
    ax.legend(handles=handles, fontsize=8, loc="lower right", title="model")

    taxed = sorted({e["label"] for e in entries if e["label"] in FORMAT_TAX_INSTANCES})
    if metric in TAX_METRICS and taxed:
        fig.text(0.5, -0.02,
                 f"{', '.join(taxed)}: token/cost inflated by per-turn format tax "
                 "(flagged, not dropped)",
                 ha="center", fontsize=7, style="italic", color="0.4")

    fig.tight_layout()
    fname = f"forest_{metric}.png"
    _save(fig, charts_dir / fname)
    return fname


def resolve_dumbbell(batches, charts_dir: Path) -> str:
    """Control vs treatment resolve rate per instance as a dumbbell (two dots +
    connector). The 'never hurts' safety panel -- kept separate from the
    efficiency forests so a regression shows without distorting the token story."""
    rows = sorted(batches, key=lambda b: b["label"])
    fig, ax = plt.subplots(figsize=(7, 0.55 * len(rows) + 1.5))
    for y, b in enumerate(reversed(rows)):  # reversed so alphabetically first label at top
        kc, nc = b["resolve"][CONTROL]
        kt, nt = b["resolve"][TREATMENT]
        rc = 100.0 * kc / nc if nc else 0.0
        rt = 100.0 * kt / nt if nt else 0.0
        ax.plot([rc, rt], [y, y], color="0.7", linewidth=2, zorder=1)
        ax.scatter([rc], [y], color=ARM_COLOR[CONTROL], edgecolor="k",
                   linewidth=0.5, s=90, zorder=3)
        ax.scatter([rt], [y], color=ARM_COLOR[TREATMENT], edgecolor="k",
                   linewidth=0.5, s=90, zorder=3)
    ax.set_yticks(range(len(rows)))
    ax.set_yticklabels([b["label"] for b in reversed(rows)])
    ax.set_ylim(-0.6, len(rows) - 0.4)
    ax.set_xlim(-3, 105)
    ax.set_xlabel("resolve rate (%)")
    ax.set_title("Resolve rate by arm (safety panel)")
    handles = [
        plt.Line2D([0], [0], marker="o", linestyle="", color=ARM_COLOR[CONTROL],
                   markeredgecolor="k", label=ARM_LABEL[CONTROL], markersize=9),
        plt.Line2D([0], [0], marker="o", linestyle="", color=ARM_COLOR[TREATMENT],
                   markeredgecolor="k", label=ARM_LABEL[TREATMENT], markersize=9),
    ]
    ax.legend(handles=handles, fontsize=8, loc="lower left")
    fig.tight_layout()
    fname = "resolve_dumbbell.png"
    _save(fig, charts_dir / fname)
    return fname


def search_output_chart(batches, charts_dir: Path) -> str | None:
    """Side-by-side boxplot: qi vs grep output tokens per invocation (treatment
    arm only, log scale). One row per instance; qi (left, blue) and grep (right,
    grey) share the same x-axis so the volume-difference is directly visible."""
    qi_color = ARM_COLOR[TREATMENT]  # blue
    grep_color = ARM_COLOR[CONTROL]  # grey

    qi_data: dict[str, list[float]] = {}
    grep_data: dict[str, list[float]] = {}
    labels: list[str] = []

    for b in sorted(batches, key=lambda b: b["label"]):
        name = b["label"]
        run_dir = b.get("_run_dir")
        if run_dir is None:
            continue
        qcsv = run_dir / "qi_commands.csv"
        qi_vals: list[float] = []
        grep_vals: list[float] = []
        with qcsv.open(newline="") as f:
            for row in csv.DictReader(f):
                if row.get("arm") != TREATMENT:
                    continue
                try:
                    val = float(row.get("output_tokens_approx", 0))
                except (ValueError, TypeError):
                    continue
                if val <= 0:
                    continue
                tool = row.get("tool", "")
                if tool == "qi":
                    qi_vals.append(val)
                elif tool == "grep":
                    grep_vals.append(val)
        if qi_vals or grep_vals:
            qi_data[name] = qi_vals
            grep_data[name] = grep_vals
            labels.append(name)

    if not labels:
        return None

    n = len(labels)
    fig, (ax_left, ax_right) = plt.subplots(1, 2, figsize=(9, 0.65 * n + 1.5))

    common_box = dict(
        orientation="horizontal",
        patch_artist=True,
        showmeans=True,
        medianprops=dict(color="black", linewidth=1.5, linestyle="--"),
        meanprops=dict(
            marker="D", markerfacecolor="black", markeredgecolor="black", markersize=5
        ),
    )

    # Left: qi
    qi_series = [qi_data.get(name, []) for name in labels]
    bp_l = ax_left.boxplot(qi_series, **common_box)
    for patch in bp_l["boxes"]:
        patch.set_facecolor(qi_color)
        patch.set_alpha(0.85)
    ax_left.set_xscale("log")
    ax_left.set_yticks(range(1, n + 1))
    ax_left.set_yticklabels(labels)
    ax_left.set_xlabel("output tokens per call (log scale)")
    ax_left.set_title("qi")

    # Right: grep
    grep_series = [grep_data.get(name, []) for name in labels]
    bp_r = ax_right.boxplot(grep_series, **common_box)
    for patch in bp_r["boxes"]:
        patch.set_facecolor(grep_color)
        patch.set_alpha(0.85)
    ax_right.set_xscale("log")
    ax_right.set_yticks(range(1, n + 1))
    ax_right.set_yticklabels([])
    ax_right.set_xlabel("output tokens per call (log scale)")
    ax_right.set_title("grep")

    # Shared x-axis range, extended to 10^4
    all_vals = [
        v
        for name in labels
        for v in qi_data.get(name, []) + grep_data.get(name, [])
    ]
    if all_vals:
        lo = max(min(all_vals) * 0.8, 0.5)
        hi = 10000
        ax_left.set_xlim(lo, hi)
        ax_right.set_xlim(lo, hi)

    fig.suptitle(
        "Search output volume: qi returns compact results; grep returns noisy output",
        fontsize=11,
        fontweight="bold",
        y=1.02,
    )
    fig.tight_layout()
    fname = "search_output.png"
    _save(fig, charts_dir / fname)
    return fname


def qi_grep_chart(batches, charts_dir: Path) -> str | None:
    """Single-panel chart: per instance, total search output tokens by arm.

    Control bar  = grep tokens / run       (grep only — no qi)
    Treatment bar = grep tokens + qi tokens / run  (combined search output)

    Comparing the two bars directly answers: did treatment's total search noise
    go up or down? If the blue bar is shorter than grey, treatment found more
    with less context-window pollution. Instance order matches the forest plots
    (alphabetical top-to-bottom). Log x-axis handles the wide cross-instance
    token range.
    """
    # Per-rep totals: {label: {arm: [grep_tok + qi_tok per rep]}}
    ctrl_vals:  dict[str, list[float]] = {}
    treat_vals: dict[str, list[float]] = {}
    labels: list[str] = []

    for b in sorted(batches, key=lambda b: b["label"]):
        name = b["label"]
        run_dir = b.get("_run_dir")
        if run_dir is None:
            continue
        qcsv = run_dir / "qi_commands.csv"
        if not qcsv.exists():
            continue
        runs: dict[tuple, dict[str, float]] = {}
        with qcsv.open(newline="") as f:
            for row in csv.DictReader(f):
                arm = row.get("arm", "")
                if arm not in (CONTROL, TREATMENT):
                    continue
                key = (arm, row.get("run_id", ""))
                if key not in runs:
                    runs[key] = {"grep_tokens": 0.0, "qi_tokens": 0.0}
                try:
                    tok = float(row.get("output_tokens_approx") or 0)
                except (ValueError, TypeError):
                    tok = 0.0
                tool = row.get("tool", "")
                if tool == "grep":
                    runs[key]["grep_tokens"] += tok
                elif tool == "qi":
                    runs[key]["qi_tokens"] += tok

        c_vals, t_vals = [], []
        for (arm, _), v in runs.items():
            total = v["grep_tokens"] + v["qi_tokens"]
            if arm == CONTROL:
                c_vals.append(total)
            elif arm == TREATMENT:
                t_vals.append(total)

        if c_vals or t_vals:
            ctrl_vals[name]  = c_vals
            treat_vals[name] = t_vals
            labels.append(name)

    if not labels:
        return None

    n = len(labels)
    OFFSET = 0.22
    fig, ax = plt.subplots(figsize=(7, max(3.0, 0.65 * n + 1.5)))

    for i, name in enumerate(labels):
        y_base = n - 1 - i  # alphabetically first at top
        for arm, y_off, vals in (
            (CONTROL,   +OFFSET, ctrl_vals.get(name, [])),
            (TREATMENT, -OFFSET, treat_vals.get(name, [])),
        ):
            if not vals:
                continue
            bp = ax.boxplot(
                [vals], positions=[y_base + y_off], widths=0.35,
                orientation="horizontal", patch_artist=True, showmeans=True,
                medianprops=dict(color="black", linewidth=1.5, linestyle="--"),
                meanprops=dict(marker="D", markerfacecolor="black",
                               markeredgecolor="black", markersize=4),
                manage_ticks=False,
            )
            for patch in bp["boxes"]:
                patch.set_facecolor(ARM_COLOR[arm])
                patch.set_alpha(0.85)

    ax.set_yticks(range(n))
    ax.set_yticklabels(list(reversed(labels)))
    ax.set_ylim(-0.6, n - 0.4)
    ax.set_xscale("log")
    ax.set_xlabel("total search output tokens / run  (grep only  vs  grep + qi)", fontsize=9)
    ax.set_title("Search output volume: grep-only (control) vs grep+qi (treatment)",
                 fontsize=10, fontweight="bold")

    handles = [
        plt.Rectangle((0, 0), 1, 1, facecolor=ARM_COLOR[CONTROL],   alpha=0.85,
                       label=f"grep only  ({ARM_LABEL[CONTROL]})"),
        plt.Rectangle((0, 0), 1, 1, facecolor=ARM_COLOR[TREATMENT], alpha=0.85,
                       label=f"grep + qi  ({ARM_LABEL[TREATMENT]})"),
    ]
    ax.legend(handles=handles, fontsize=8, loc="lower right")

    fig.tight_layout()
    fname = "qi_grep.png"
    _save(fig, charts_dir / fname)
    return fname


def _collect_explore(batches, field: str, segs: list[str]):
    """Per instance, mean-per-run recognized-tool value by arm and segment.

    field 'calls' -> action counts via cmds.action_tool (incl. 'mixed');
    field 'tokens' -> output from homogeneous actions via cmds.only_tool_and_echo.
    Returns ({label: {arm: {seg: mean_per_run}}}, alphabetical labels). Errored
    actions excluded, matching the single-instance loader and ESSENTIALS."""
    data: dict[str, dict] = {}
    labels: list[str] = []
    for b in sorted(batches, key=lambda b: b["label"]):
        run_dir = b.get("_run_dir")
        if run_dir is None:
            continue
        qcsv = run_dir / "qi_commands.csv"
        if not qcsv.exists():
            continue
        runs: dict[tuple, dict[str, float]] = {}
        with qcsv.open(newline="") as f:
            for row in csv.DictReader(f):
                arm = row.get("arm", "")
                if arm not in (CONTROL, TREATMENT):
                    continue
                if (row.get("is_error") or "0") == "1":
                    continue
                key = (arm, row.get("run_id", ""))
                d = runs.setdefault(key, {s: 0.0 for s in segs})
                cmd = row.get("command", "")
                if field == "calls":
                    seg = cmds.action_tool(cmd)
                    if seg in d:
                        d[seg] += 1
                else:
                    try:
                        tok = float(row.get("output_tokens_approx") or 0)
                    except (ValueError, TypeError):
                        tok = 0.0
                    for t in segs:
                        if cmds.only_tool_and_echo(cmd, t):
                            d[t] += tok
                            break
        per = {}
        for arm in (CONTROL, TREATMENT):
            arm_runs = [v for (a, _rid), v in runs.items() if a == arm]
            nrun = len(arm_runs)
            per[arm] = {s: (sum(v[s] for v in arm_runs) / nrun if nrun else 0.0)
                        for s in segs}
        if any(per[CONTROL].values()) or any(per[TREATMENT].values()):
            data[b["label"]] = per
            labels.append(b["label"])
    return data, labels


def _cross_explore_chart(batches, charts_dir: Path, field: str, segs: list[str],
                         title: str, xlabel: str, fname: str) -> str | None:
    """Per-instance horizontal stacked bars (upper=control, lower=treatment),
    normalized to each instance's control total = 100% so the wide cross-instance
    scale doesn't dominate. Shared by the call-count and token cross charts."""
    data, labels = _collect_explore(batches, field, segs)
    if not labels:
        return None
    n = len(labels)
    OFFSET = 0.2
    fig, ax = plt.subplots(figsize=(8, max(3.0, 0.72 * n + 1.6)))
    for i, name in enumerate(labels):
        y_base = n - 1 - i  # alphabetically first at top
        per = data[name]
        denom = (sum(per[CONTROL][s] for s in segs)
                 or sum(per[TREATMENT][s] for s in segs))
        if not denom:
            continue
        for arm, y_off in ((CONTROL, +OFFSET), (TREATMENT, -OFFSET)):
            left = 0.0
            for s in segs:
                w = 100.0 * per[arm][s] / denom
                ax.barh(y_base + y_off, w, left=left, height=0.34,
                        color=EXPLORE_COLORS[s], edgecolor="white", linewidth=0.5,
                        hatch="//" if s == "mixed" else None, zorder=3)
                left += w
            ax.text(-1.5, y_base + y_off, ARM_LABEL[arm][:3], ha="right",
                    va="center", fontsize=6.5, color="#555")

    ax.axvline(100, color="0.7", linewidth=0.8, linestyle=":", zorder=0)
    ax.set_yticks(range(n))
    ax.set_yticklabels(list(reversed(labels)))
    ax.set_ylim(-0.6, n - 0.4)
    ax.set_xlim(left=-12)
    ax.set_xlabel(xlabel, fontsize=9)
    ax.set_title(title, fontsize=10, fontweight="bold")
    handles = [plt.Rectangle((0, 0), 1, 1, facecolor=EXPLORE_COLORS[s],
                             hatch="//" if s == "mixed" else None,
                             label=EXPLORE_LABEL[s]) for s in segs]
    ax.legend(handles=handles, fontsize=8, loc="lower right", ncol=len(segs))
    ax.grid(True, axis="x", color="0.85", linewidth=0.7, zorder=0)
    ax.set_axisbelow(True)
    fig.tight_layout()
    _save(fig, charts_dir / fname)
    return fname


def explore_stack_chart(batches, charts_dir: Path) -> str | None:
    """Cross-instance headline: recognized-tool calls, normalized per instance.
    The manual-search block (grep+cat+sed-read) shrinks on responders while a qi
    block appears; non-responders (qutebrowser/flipt) stack rather than displace.
    Single-instance analog: analyze_pro_stats.chart_explore_stack."""
    return _cross_explore_chart(
        batches, charts_dir, "calls", EXPLORE_TOOLS,
        title="Recognized-tool calls (per instance, % of control total)",
        xlabel="recognized-tool calls, % of control total  (upper=control, lower=treatment)",
        fname="explore_calls.png")


def explore_tokens_chart(batches, charts_dir: Path) -> str | None:
    """Cross-instance per-tool output tokens from homogeneous actions only,
    normalized per instance. Not a total. Single-instance analog:
    analyze_pro_stats.chart_explore_tokens."""
    return _cross_explore_chart(
        batches, charts_dir, "tokens", EXPLORE_TOKEN_TOOLS,
        title="Output tokens by tool (per instance, % of control total)",
        xlabel="homogeneous-action tokens, % of control total  (upper=control, lower=treatment)",
        fname="explore_tokens.png")


# Instances whose SWE-bench Pro gold patch is majority machine-GENERATED content
# (protobuf/swagger/CHANGELOG). qi indexes source code, so it can't help navigate
# these; the 2nd radar panel scopes them out. Verified from gold-patch
# composition -- see NEW_QI_VS_GREP_CAT_STORY_IN_CHARTS.md §1b. openlibrary
# (majority non-source, but hand-written templates/CSS) left this set 2026-07-05:
# it won under qi, falsifying the broader "majority non-source" scope the set
# once encoded (openlibrary 2/8 source files; flipt 1/5).
RADAR_NON_SOURCE = {"flipt"}


def radar_chart(batches, charts_dir: Path) -> str | None:
    """Pooled efficiency-radar hero (geometric mean of per-instance ratios) on a
    SINGLE axis: grey control baseline (100%) enclosing the blue treatment
    pentagon, with the source-navigation subset (non-source tasks scoped out,
    RADAR_NON_SOURCE) drawn as a darker polygon *inside* it. Axis labels carry
    both values ('all / source-nav'). Single-instance analog:
    analyze_pro_stats.chart_radar."""
    ratios = [(b["label"], r) for b in batches
              if (r := radar_ratios(b["_run_dir"])) is not None]
    if not ratios:
        return None

    def geo(subset):
        return {ax: _geomean([r[ax] for _lbl, r in subset]) for ax in RADAR_AXES}

    # Layers drawn outer→inner: all instances (light), then source-nav (dark) on
    # top. Each is (legend label, line color, fill color, fill alpha, values).
    # all-instances = dark blue (larger, drawn first); source-nav = light blue
    # (smaller, drawn on top so it stays visible inside the dark polygon).
    src = [(l, r) for l, r in ratios if l not in RADAR_NON_SOURCE]
    layers = [(f"treatment — all {len(ratios)}",
               ARM_COLOR[TREATMENT], ARM_COLOR[TREATMENT], 0.32, geo(ratios))]
    if src and len(src) < len(ratios):
        layers.append(("treatment — qi good-fit",
                       "#41ab5d", "#c7e9c0", 0.60, geo(src)))

    n = len(RADAR_AXES)
    ang = [2 * math.pi * i / n for i in range(n)]
    angc = ang + ang[:1]
    def close(v): return v + v[:1]

    fig, ax = plt.subplots(figsize=(8, 8), subplot_kw=dict(polar=True))
    # Shrink the plotting circle and nudge it up-right so the outward labels clear
    # the title block above and leave the bottom-left corner free for the (taller)
    # 5-entry legend.
    ax.set_position([0.28, 0.15, 0.60, 0.60])
    ax.plot(angc, close([1.0] * n), color="#8a8a8a", lw=2, zorder=2)
    ax.fill(angc, close([1.0] * n), color="#9a9a9a", alpha=0.22, zorder=1)
    for _lbl, lc, fc, fa, g in layers:
        vals = [g[a] for a in RADAR_AXES]
        ax.plot(angc, close(vals), color=lc, lw=2, zorder=3)
        ax.fill(angc, close(vals), color=fc, alpha=fa, zorder=3)

    allvals = [g[a] for *_x, g in layers for a in RADAR_AXES]
    rmax = max(1.08, max(allvals) + 0.08)
    ax.set_ylim(0, rmax)
    ax.set_xticks(ang)
    ax.set_xticklabels([])  # replaced by manual labels pushed clear of the ring
    ax.set_yticks([0.5, 1.0]); ax.set_yticklabels(["50%", "100%"], fontsize=7, color="#999")

    # Spokes carry the mechanism/outcome distinction in their line style: the
    # grep+cat axis (the *cause*) is dashed, the four outcome axes are solid.
    ax.xaxis.grid(False)
    for a, th in zip(RADAR_AXES, ang):
        ax.plot([th, th], [0, rmax], color="black", lw=1.0, alpha=0.85, zorder=4,
                ls=(0, (5, 4)) if a == "grep+cat calls" else "-")

    # Axis labels carry every layer's delta ("log size\n-10% / -14%"), drawn 15%
    # beyond the outer ring (clip_on=False) so they never intersect the circle.
    def deltas(a):
        return " / ".join(f"{(g[a] - 1) * 100:+.0f}%" for *_x, g in layers)
    rlab = rmax * 1.108  # label gap, snug to the ring
    for a, th in zip(RADAR_AXES, ang):
        ha = "left" if math.cos(th) > 0.25 else "right" if math.cos(th) < -0.25 else "center"
        va = "bottom" if math.sin(th) > 0.25 else "top" if math.sin(th) < -0.25 else "center"
        ax.text(th, rlab, f"{a}\n{deltas(a)}", ha=ha, va=va, fontsize=12,
                linespacing=1.6, clip_on=False, zorder=5)

    handles = [plt.Rectangle((0, 0), 1, 1, facecolor="#9a9a9a", alpha=0.5,
                             label="control (baseline)")]
    handles += [plt.Rectangle((0, 0), 1, 1, facecolor=fc, alpha=min(fa + 0.2, 1.0),
                              label=lbl) for lbl, _lc, fc, fa, _g in layers]
    handles += [plt.Line2D([0], [0], color="black", lw=1.3, ls="-",
                           label="outcome axis"),
                plt.Line2D([0], [0], color="black", lw=1.3, ls=(0, (5, 4)),
                           label="mechanism axis")]
    fig.legend(handles=handles, loc="lower left", bbox_to_anchor=(-0.04, 0.02),
               fontsize=12, frameon=False, borderaxespad=0)
    # Title block anchored to the figure top, independent of the (lowered) axes.
    fig.suptitle("qi keeps agent runs leaner", y=0.975, fontsize=28,
                 fontweight="bold")
    subtitle = ("geometric mean · control = 100% · smaller = leaner"
                + (" · labels: all / qi good-fit" if len(layers) > 1 else ""))
    fig.text(0.5, 0.875, subtitle, ha="center", fontsize=18, color="#555")
    fname = "radar_efficiency.png"
    _save(fig, charts_dir / fname)
    return fname


def cumulative_cost_chart(batches, charts_dir: Path) -> str | None:
    """Small-multiples cumulative-cost curve: one panel per instance, cost summed
    across reps (control grey, treatment blue). Each panel is normalized to its
    control arm's total cumulative cost (=100%) so cheap and expensive-model
    instances share one scale -- without normalization an expensive model (e.g.
    Sonnet) flattens every other panel. The treatment curve's endpoint below (or
    above) 100% is the per-instance cost saving (overrun) the forest compresses.

    Reuses the per-rep cost values already loaded into b["vals"]["cost"]; returns
    None when no batch carries cost data (e.g. a custom --metrics without cost)."""
    rows = sorted(batches, key=lambda b: b["label"])
    have_cost = [b for b in rows if b["vals"].get("cost")
                 and (b["vals"]["cost"][CONTROL] or b["vals"]["cost"][TREATMENT])]
    if not have_cost:
        return None

    n = len(have_cost)
    fig, axes = plt.subplots(1, n, figsize=(2.8 * n, 3.2), sharey=True)
    if n == 1:
        axes = [axes]

    for ax, b in zip(axes, have_cost):
        # Denominator: control's total cumulative cost (fall back to treatment's
        # when an instance has no control costs), so control ends at 100%.
        ctrl_total = sum(c for c in b["vals"]["cost"][CONTROL] if c is not None)
        trt_total = sum(c for c in b["vals"]["cost"][TREATMENT] if c is not None)
        denom = ctrl_total or trt_total
        if not denom:
            continue
        for arm in (CONTROL, TREATMENT):
            costs = [c for c in b["vals"]["cost"][arm] if c is not None]
            if not costs:
                continue
            running, cumsum = 0.0, []
            for c in costs:
                running += c
                cumsum.append(100.0 * running / denom)
            ax.plot(range(1, len(cumsum) + 1), cumsum, marker="o", linewidth=2,
                    markersize=6, color=ARM_COLOR[arm], label=ARM_LABEL[arm])
        ax.axhline(100, color="0.7", linewidth=0.8, linestyle=":", zorder=0)
        ax.grid(True, color="0.85", linewidth=0.7, zorder=0)
        ax.set_axisbelow(True)
        ax.set_title(b["label"])
        ax.set_xlabel("rep")
        if ax is axes[0]:
            ax.set_ylabel("cumulative cost (% of control total)")
        ax.xaxis.set_major_locator(plt.MaxNLocator(integer=True))

    handles = [
        plt.Line2D([0], [0], color=ARM_COLOR[arm], linewidth=2, marker="o",
                   markersize=6, label=ARM_LABEL[arm])
        for arm in (CONTROL, TREATMENT)
    ]
    fig.legend(handles=handles, loc="lower center", ncol=2, fontsize=9,
               frameon=False)
    fig.suptitle("Cumulative cost across reps", fontsize=11, fontweight="bold",
                 y=1.02)
    fig.tight_layout()
    fname = "cumulative_cost.png"
    _save(fig, charts_dir / fname)
    return fname


def log_size_range_chart(batches, charts_dir: Path) -> str | None:
    """Per-instance log-file-size range by arm: one row per instance, control and
    treatment min->max range bars (circle endpoints, median diamond) offset within
    the row. Reads the log_size_kb column analyze_pro_trajectories.py records per
    run. Returns None when no batch carries log-size data."""
    rows = sorted(batches, key=lambda b: b["label"])
    have = [b for b in rows if b.get("log_size")
            and (b["log_size"][CONTROL] or b["log_size"][TREATMENT])]
    if not have:
        return None

    n = len(have)
    fig, ax = plt.subplots(figsize=(7, 0.55 * n + 1.8))
    for row, b in enumerate(reversed(have)):  # reversed so alphabetically first label at top
        for offset, arm in [(0.15, CONTROL), (-0.15, TREATMENT)]:
            vals = b["log_size"][arm]
            if not vals:
                continue
            lo, hi = min(vals), max(vals)
            y = row + offset
            color = ARM_COLOR[arm]
            ax.hlines(y, lo, hi, color=color, linewidth=2.5, zorder=2)
            ax.scatter([lo, hi], [y, y], color=color, edgecolor="k",
                       linewidth=0.5, s=45, zorder=3)
            ax.scatter([st.median(vals)], [y], marker="D", color="black",
                       s=35, zorder=4)

    ax.set_yticks(range(n))
    ax.set_yticklabels([b["label"] for b in reversed(have)])
    ax.set_ylim(-0.6, n - 0.4)
    ax.set_xlabel("log file size (KB)")
    ax.set_title("Log file size range by instance and arm")
    if mticker is not None:
        ax.xaxis.set_major_formatter(mticker.FuncFormatter(lambda x, _: f"{x:.0f}"))

    handles = [
        plt.Line2D([0], [0], marker="o", linestyle="", color=ARM_COLOR[CONTROL],
                   markeredgecolor="k", label=ARM_LABEL[CONTROL], markersize=8),
        plt.Line2D([0], [0], marker="o", linestyle="", color=ARM_COLOR[TREATMENT],
                   markeredgecolor="k", label=ARM_LABEL[TREATMENT], markersize=8),
    ]
    ax.legend(handles=handles, fontsize=8, loc="lower right")
    fig.tight_layout()
    fname = "log_size_range.png"
    _save(fig, charts_dir / fname)
    return fname


# --------------------------------------------------------------------------- #
def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--batch", action="append", default=[], type=Path,
                    help="A batch result dir (repeatable).")
    ap.add_argument("--manifest", type=Path,
                    help="File listing batch dirs, one per line (# comments ok).")
    ap.add_argument("--out", type=Path,
                    default=Path("experiment/results/pro_runs/_cross"),
                    help="Output dir for cross_instance.csv and charts/.")
    ap.add_argument("--metrics", nargs="+", default=DEFAULT_METRICS,
                    help=f"Metric set (default: {' '.join(DEFAULT_METRICS)}).")
    ap.add_argument("--bootstrap-iters", type=int, default=10000)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--no-charts", action="store_true")
    args = ap.parse_args()

    batch_dirs: list[Path] = list(args.batch)
    if args.manifest:
        if not args.manifest.is_file():
            print(f"ERROR: manifest not found: {args.manifest}", file=sys.stderr)
            return 1
        base = args.manifest.resolve().parent
        for line in args.manifest.read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            p = Path(line)
            batch_dirs.append(p if p.is_absolute() else base / p)
    if not batch_dirs:
        print("ERROR: pass --batch DIR (repeatable) and/or --manifest FILE",
              file=sys.stderr)
        return 1

    batches = []
    for d in batch_dirs:
        b = load_batch(d, args.metrics)
        if b is not None:
            batches.append(b)
    if len(batches) < 2:
        print("ERROR: need >=2 usable batches to pool.", file=sys.stderr)
        return 1
    print(f"Loaded {len(batches)} batch(es): "
          f"{', '.join(b['label'] for b in batches)}\n")

    # Per (instance, metric) effects + per-metric pooled estimate.
    summary_rows = []
    pooled_by_metric: dict[str, tuple] = {}
    for metric in args.metrics:
        entries = []
        for b in batches:
            cv = b["vals"][metric][CONTROL]
            tv = b["vals"][metric][TREATMENT]
            res = log_ratio_effect(cv, tv, args.bootstrap_iters, args.seed)
            if res is None:
                print(f"  ({b['label']}/{metric}: no data, skipped)", file=sys.stderr)
                continue
            effect, ci_lo, ci_hi, se = res
            tax = (b["label"] in FORMAT_TAX_INSTANCES) and (metric in TAX_METRICS)
            entries.append({
                "label": b["label"], "model": b["model"], "metric": metric,
                "effect": effect, "ci_lo": ci_lo, "ci_hi": ci_hi, "se": se,
                "control_median": st.median([x for x in cv if x is not None]),
                "treatment_median": st.median([x for x in tv if x is not None]),
                "n_control": len(cv), "n_treatment": len(tv), "format_tax": tax,
            })
            summary_rows.append(dict(
                instance=b["label"], model=b["model"], metric=metric,
                n_control=len(cv), n_treatment=len(tv),
                control_median=entries[-1]["control_median"],
                treatment_median=entries[-1]["treatment_median"],
                effect_logratio=effect, ci_lo=ci_lo, ci_hi=ci_hi, se=se,
                pct_change=pct(effect), format_tax=tax))

        pooled = pool([e["effect"] for e in entries], [e["se"] for e in entries])
        pooled_by_metric[metric] = (entries, pooled)
        if pooled is not None:
            p, se_pool, plo, phi, k, Q, I2 = pooled
            summary_rows.append(dict(
                instance="__POOLED__", model="", metric=metric,
                n_control="", n_treatment="", control_median="",
                treatment_median="", effect_logratio=p, ci_lo=plo, ci_hi=phi,
                se=se_pool, pct_change=pct(p), format_tax="",
                k=k, Q=Q, I2=I2))

    # ----- console report -----
    print(f"{'metric':<14}{'instance':<14}{'pct change':>12}{'95% CI %':>20}")
    print("-" * 60)
    for metric in args.metrics:
        entries, pooled = pooled_by_metric[metric]
        for e in sorted(entries, key=lambda e: e["effect"]):
            ci = f"[{pct(e['ci_lo']):+.0f}, {pct(e['ci_hi']):+.0f}]"
            tax = " *" if e["format_tax"] else ""
            print(f"{metric:<14}{e['label']:<14}{pct(e['effect']):>+11.0f}%{ci:>20}{tax}")
        if pooled is not None:
            p, _se, plo, phi, k, _Q, I2 = pooled
            ci = f"[{pct(plo):+.0f}, {pct(phi):+.0f}]"
            print(f"{metric:<14}{'POOLED':<14}{pct(p):>+11.0f}%{ci:>20}  "
                  f"(k={k}, I²={I2:.0%})")
        print()

    # ----- write CSV -----
    args.out.mkdir(parents=True, exist_ok=True)
    out_csv = args.out / "cross_instance.csv"
    keys = ["instance", "model", "metric", "n_control", "n_treatment",
            "control_median", "treatment_median", "effect_logratio",
            "ci_lo", "ci_hi", "se", "pct_change", "format_tax", "k", "Q", "I2"]
    with out_csv.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=keys)
        w.writeheader()
        for r in summary_rows:
            w.writerow({k: r.get(k, "") for k in keys})
    print(f"Wrote {out_csv}")

    # ----- charts -----
    if not args.no_charts:
        if plt is None:
            print("(charts skipped: matplotlib not available)", file=sys.stderr)
        else:
            charts_dir = args.out / "charts"
            charts_dir.mkdir(parents=True, exist_ok=True)
            written = []
            for metric in args.metrics:
                if metric in NO_CHART_METRICS:  # keep the CSV row; skip the chart
                    continue
                entries, pooled = pooled_by_metric[metric]
                if entries:
                    written.append(forest_chart(metric, entries, pooled, charts_dir))
            written.append(resolve_dumbbell(batches, charts_dir))
            cc = cumulative_cost_chart(batches, charts_dir)
            if cc:
                written.append(cc)
            ls = log_size_range_chart(batches, charts_dir)
            if ls:
                written.append(ls)
            # search_output.png retired 2026-07-05: dropped from the chart set.
            # Function kept (unused) pending a decision to delete it outright.
            # sr = search_output_chart(batches, charts_dir)
            # if sr:
            #     written.append(sr)
            # qi_grep.png retired: superseded by explore_calls/explore_tokens,
            # and its grep-only-vs-grep+qi framing ignored cat/sed-read. Function
            # kept (unused) pending a decision to delete it outright.
            # qg = qi_grep_chart(batches, charts_dir)
            # if qg:
            #     written.append(qg)
            ec = explore_stack_chart(batches, charts_dir)
            if ec:
                written.append(ec)
            et = explore_tokens_chart(batches, charts_dir)
            if et:
                written.append(et)
            rc = radar_chart(batches, charts_dir)
            if rc:
                written.append(rc)
            print(f"Wrote {len(written)} chart(s) -> {charts_dir}/")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
