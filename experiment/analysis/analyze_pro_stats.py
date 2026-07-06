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

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # -> experiment/
from lib import cmds  # noqa: E402

try:
    import matplotlib
    matplotlib.use("Agg")  # headless: no display needed for PNG output
    import matplotlib.patches as mpatches
    import matplotlib.pyplot as plt
except ImportError:  # soft dependency -- charts skip, text/CSV do not
    mpatches = None
    plt = None

METRICS = [
    ("turn_count", "turns", False),
    ("total_tokens", "total tokens", False),
    ("cost", "cost ($)", True),
    # Wall time is joined from wall_time.csv (the ledger, not the trajectory) and
    # is simply skipped — table row and chart both — when that file is absent.
    ("duration_sec", "wall time (s)", False),
    ("patch_lines", "patch lines", False),
    ("files_touched", "patch files", False),
]
CONTROL, TREATMENT = "swebp_control", "swebp_treatment"
ARM_ORDER = [CONTROL, TREATMENT]
ARM_LABEL = {CONTROL: "control", TREATMENT: "treatment"}
# Match analyze_stats.py: control grey, treatment blue.
ARM_COLOR = {CONTROL: "#b0b0b0", TREATMENT: "#2a7fb8"}
# Metrics drawn as strip/jitter instead of boxplots: low-variance integer
# counts where a boxplot dramatizes a single rep as a false "outlier".
STRIP_METRICS = {"patch_lines", "files_touched"}
# Metrics kept in the stats table but NOT charted: wall time is noisy/misleading
# here (reps run sequentially across days, so the spread is scheduling, not work)
# -- already dropped from the cross-instance charts. Its index (04) is left vacant.
NO_CHART_METRICS = {"duration_sec"}

# Recognized exploration tools for the "qi displaces grep+cat+sed-read" charts.
# Call-count stack order is bottom -> top: the manual-search block (grep, cat,
# sed_read), then mixed, then qi set apart on top. Token charts drop 'mixed'.
EXPLORE_TOOLS = ["grep", "cat", "sed_read", "mixed", "qi"]
EXPLORE_TOKEN_TOOLS = ["grep", "cat", "sed_read", "qi"]
EXPLORE_LABEL = {"qi": "qi", "grep": "grep", "cat": "cat",
                 "sed_read": "sed -n", "mixed": "mixed"}
EXPLORE_COLORS = {"qi": "#2a7fb8", "grep": "#dd8452", "cat": "#8c8c8c",
                  "sed_read": "#c4b07a", "mixed": "#cfcfcf"}

# Efficiency-radar axes (all "smaller = leaner", so control encloses treatment).
# Four outcome axes + one mechanism axis (grep+cat calls) that shows *why* the
# outcomes drop. See NEW_QI_VS_GREP_CAT_STORY_IN_CHARTS.md §4.4.
RADAR_AXES = ["log size", "log variance", "turns", "patch lines", "grep+cat calls"]


def _geomean(vals) -> float:
    """Geometric mean of positive ratios -- the correct average for ratios (a 2x
    and a 0.5x cancel to 1.0) and outlier-resistant. NaN if no positive values."""
    vals = [v for v in vals if v is not None and v > 0]
    return math.exp(sum(math.log(v) for v in vals) / len(vals)) if vals else float("nan")


def fnum(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


def load_wall_times(run_dir: Path) -> dict[tuple[str, str], str]:
    """Map (arm, rep) -> duration_sec from wall_time.csv, if it exists.

    wall_time.py (Step 4) writes this from the run ledger; the trajectory CSVs
    carry no wall clock. Returns {} when the file is absent (older batches, no
    ledger entry) so the duration metric simply drops out of the report/charts.
    """
    path = run_dir / "wall_time.csv"
    if not path.is_file():
        return {}
    out: dict[tuple[str, str], str] = {}
    with path.open(newline="") as f:
        for r in csv.DictReader(f):
            out[(r["arm"], r["rep"])] = r.get("duration_sec", "")
    return out


def load_qi_commands(run_dir: Path) -> dict[tuple[str, str], dict[str, int]]:
    """Per-run grep/qi call counts and token output from qi_commands.csv.

    extract_qi_commands.py (Step 5) writes this. Returns {} when absent so the
    grep/qi chart simply self-skips. All calls included (no is_error filter):
    errored and timed-out commands still consume the agent's context budget.
    """
    path = run_dir / "qi_commands.csv"
    if not path.is_file():
        return {}
    agg: dict[tuple[str, str], dict[str, int]] = {}
    with path.open(newline="") as f:
        for r in csv.DictReader(f):
            key = (r["arm"], r["run_id"])
            if key not in agg:
                agg[key] = {"grep_calls": 0, "qi_calls": 0,
                            "grep_tokens": 0, "qi_tokens": 0}
            tool = r.get("tool", "")
            tokens = int(r.get("output_tokens_approx") or 0)
            if tool == "grep":
                agg[key]["grep_calls"] += 1
                agg[key]["grep_tokens"] += tokens
            elif tool == "qi":
                agg[key]["qi_calls"] += 1
                agg[key]["qi_tokens"] += tokens
    return agg


def load_explore(run_dir: Path) -> dict[tuple[str, str], dict]:
    """Per-run recognized-tool usage from qi_commands.csv, two signals:

    - ``calls``: every successful action classified by cmds.action_tool
      (qi/grep/cat/sed_read, or 'mixed' for >=2 recognized tools). Actions using
      none of the four (pytest, edits, git) are not counted.
    - ``tokens``: output attributed to a tool only for *homogeneous* actions
      (cmds.only_tool_and_echo) -- where that tool is the sole content source.
      Mixed/heterogeneous actions contribute no tokens (the honest gap the
      'mixed' call segment already surfaces).

    Errored actions are excluded (matching the ESSENTIALS totals). Returns {}
    when the CSV is absent so the charts self-skip.
    """
    path = run_dir / "qi_commands.csv"
    if not path.is_file():
        return {}
    agg: dict[tuple[str, str], dict] = {}
    with path.open(newline="") as f:
        for r in csv.DictReader(f):
            if (r.get("is_error") or "0") == "1":
                continue
            key = (r["arm"], r["run_id"])
            d = agg.setdefault(key, {
                "calls": {s: 0 for s in EXPLORE_TOOLS},
                "tokens": {s: 0 for s in EXPLORE_TOKEN_TOOLS},
            })
            cmd = r.get("command", "")
            seg = cmds.action_tool(cmd)
            if seg is not None:
                d["calls"][seg] += 1
            tok = int(r.get("output_tokens_approx") or 0)
            for t in EXPLORE_TOKEN_TOOLS:
                if cmds.only_tool_and_echo(cmd, t):
                    d["tokens"][t] += tok
                    break  # homogeneous for at most one tool
    return agg


def _explore_per_arm(explore: dict, field: str, segs: list[str]) -> dict:
    """Mean per-run value of each segment, per arm, for 'calls' or 'tokens'."""
    out: dict[str, dict[str, float]] = {}
    for arm in ARM_ORDER:
        runs = [v for (a, _rid), v in explore.items() if a == arm]
        n = len(runs)
        out[arm] = {s: (sum(v[field][s] for v in runs) / n if n else 0.0)
                    for s in segs}
    return out


def _explore_stack(explore: dict, field: str, segs: list[str], ylabel: str,
                   title: str, subtitle: str, fname: str, charts_dir: Path,
                   mark_manual: bool) -> str | None:
    """Shared stacked-bar renderer for the call-count and token explore charts."""
    if not explore:
        return None
    per = _explore_per_arm(explore, field, segs)
    xs = list(range(len(ARM_ORDER)))
    fig, ax = plt.subplots(figsize=(5.2, 4.2))
    bottoms = [0.0] * len(ARM_ORDER)
    for s in segs:
        vals = [per[a][s] for a in ARM_ORDER]
        ax.bar(xs, vals, bottom=bottoms, width=0.6, color=EXPLORE_COLORS[s],
               edgecolor="white", linewidth=0.6, label=EXPLORE_LABEL[s],
               hatch="//" if s == "mixed" else None, zorder=3)
        bottoms = [b + v for b, v in zip(bottoms, vals)]
    if mark_manual:  # dashed line + label at the grep+cat+sed_read block top
        for i, a in enumerate(ARM_ORDER):
            man = sum(per[a][s] for s in ("grep", "cat", "sed_read"))
            ax.hlines(man, i - 0.32, i + 0.32, color="black",
                      linestyle="--", linewidth=1.1, zorder=4)
            ax.text(i, man, f"manual {man:.0f} ", ha="right", va="bottom",
                    fontsize=8, color="black", zorder=5)
    ax.set_xticks(xs)
    ax.set_xticklabels([ARM_LABEL[a] for a in ARM_ORDER])
    ax.set_ylabel(ylabel)
    ax.set_title(title, pad=20)
    if subtitle:
        ax.text(0.5, 1.012, subtitle, transform=ax.transAxes, ha="center",
                va="bottom", fontsize=7.5, color="#666")
    ax.grid(True, axis="y", color="0.85", linewidth=0.7, zorder=0)
    ax.set_axisbelow(True)
    ax.legend(fontsize=8, loc="upper right")
    _save(fig, charts_dir / fname)
    return fname


def chart_explore_stack(explore: dict, charts_dir: Path,
                        chart_idx: int) -> str | None:
    """Recognized-tool calls/run, stacked per arm: the manual-search block
    (grep+cat+sed-read) shrinks control->treatment while a qi block appears.
    The headline 'qi displaces manual search' visual. Cross analog:
    cross_batch_compare.explore_stack_chart."""
    fname = f"{chart_idx:02d}_stack_explore_calls.png"
    return _explore_stack(
        explore, "calls", EXPLORE_TOOLS,
        ylabel="recognized-tool calls / run",
        title="Recognized-tool calls by arm",
        subtitle="manual-search block (grep+cat+sed -n) vs qi; 'mixed' = ≥2 tools",
        fname=fname, charts_dir=charts_dir, mark_manual=True)


def chart_explore_tokens(explore: dict, charts_dir: Path,
                         chart_idx: int) -> str | None:
    """Per-tool output tokens/run from homogeneous single-tool actions only (no
    'mixed' -- its output isn't cleanly attributable). A secondary 'how heavy is
    each tool's output' view; the stack is NOT a total. Cross analog:
    cross_batch_compare.explore_tokens_chart."""
    fname = f"{chart_idx:02d}_stack_explore_tokens.png"
    return _explore_stack(
        explore, "tokens", EXPLORE_TOKEN_TOOLS,
        ylabel="output tokens / run",
        title="Output tokens by tool, per arm",
        subtitle="homogeneous single-tool actions only — not a total",
        fname=fname, charts_dir=charts_dir, mark_manual=False)


def _grepcat_calls_per_arm(run_dir: Path) -> dict[str, float]:
    """Mean grep+cat *invocations* per run, per arm, from qi_commands.csv. Pure
    usage detection (count grep + count cat per action) -- not the discredited
    'read' partition, so pytest|head etc. are never counted. Errored actions
    excluded (matching the explore totals)."""
    path = run_dir / "qi_commands.csv"
    if not path.is_file():
        return {}
    per: dict[tuple[str, str], float] = {}
    with path.open(newline="") as f:
        for r in csv.DictReader(f):
            arm = r.get("arm", "")
            if arm not in ARM_ORDER or r.get("is_error") == "1":
                continue
            cmd = r.get("command", "")
            key = (arm, r.get("run_id", ""))
            per[key] = per.get(key, 0) + cmds.count_tools(cmd)[1] + cmds.count_cat(cmd)
    out: dict[str, float] = {}
    for arm in ARM_ORDER:
        v = [n for (a, _rid), n in per.items() if a == arm]
        if v:
            out[arm] = st.fmean(v)
    return out


def radar_metrics(run_dir: Path) -> dict[str, dict[str, float]]:
    """Per-arm value of each RADAR_AXES metric (all 'smaller = leaner'), from
    runs_with_success.csv + qi_commands.csv. Read directly (not via the --metrics
    selection) so the radar is self-contained. Returns {arm: {axis: value}} or {}.
    """
    csv_path = run_dir / "runs_with_success.csv"
    if not csv_path.is_file():
        return {}
    with csv_path.open(newline="") as f:
        rows = [r for r in csv.DictReader(f) if r.get("arm") in ARM_ORDER]
    if not rows:
        return {}
    gc = _grepcat_calls_per_arm(run_dir)
    out: dict[str, dict[str, float]] = {}
    for arm in ARM_ORDER:
        ls = [v for v in (fnum(r.get("log_size_kb")) for r in rows if r["arm"] == arm) if v is not None]
        tn = [v for v in (fnum(r.get("turn_count")) for r in rows if r["arm"] == arm) if v is not None]
        pl = [v for v in (fnum(r.get("patch_lines")) for r in rows if r["arm"] == arm) if v is not None]
        out[arm] = {
            "log size": st.fmean(ls) if ls else None,
            "log variance": (st.pstdev(ls) if len(ls) > 1 else 0.0) if ls else None,
            "turns": st.fmean(tn) if tn else None,
            "patch lines": st.fmean(pl) if pl else None,
            "grep+cat calls": gc.get(arm),
        }
    return out


def radar_ratios(run_dir: Path) -> dict[str, float] | None:
    """Treatment/control ratio per radar axis for one batch (<1 = treatment
    leaner). None if any axis is missing/degenerate so the polygon can't close."""
    m = radar_metrics(run_dir)
    if not m or CONTROL not in m or TREATMENT not in m:
        return None
    r = {}
    for ax in RADAR_AXES:
        c, t = m[CONTROL].get(ax), m[TREATMENT].get(ax)
        r[ax] = (t / c) if (c and t is not None and c > 0) else None
    return r if all(v is not None for v in r.values()) else None


def _radar_on_ax(ax, ratios: dict[str, float]) -> None:
    """Draw one efficiency radar onto a polar axis: grey control baseline (1.0 on
    every axis) enclosing the blue treatment polygon. Axis labels are pushed clear
    of the ring (un-clipped) so they never intersect the circle."""
    vals = [ratios[a] for a in RADAR_AXES]
    n = len(RADAR_AXES)
    ang = [2 * math.pi * i / n for i in range(n)]
    angc = ang + ang[:1]
    def close(v): return v + v[:1]
    ax.plot(angc, close([1.0] * n), color="#8a8a8a", lw=2, zorder=2)
    ax.fill(angc, close([1.0] * n), color="#9a9a9a", alpha=0.24, zorder=1)
    ax.plot(angc, close(vals), color=ARM_COLOR[TREATMENT], lw=2, zorder=3)
    ax.fill(angc, close(vals), color=ARM_COLOR[TREATMENT], alpha=0.45, zorder=3)
    rmax = max(1.08, max(vals) + 0.08)
    ax.set_ylim(0, rmax)
    ax.set_xticks(ang)
    ax.set_xticklabels([])
    ax.set_yticks([0.5, 1.0]); ax.set_yticklabels(["50%", "100%"], fontsize=7, color="#999")
    # Spokes carry the mechanism/outcome distinction in their line style: the
    # grep+cat axis (the *cause*) is dashed, the four outcome axes are solid.
    ax.xaxis.grid(False)
    for a, th in zip(RADAR_AXES, ang):
        ax.plot([th, th], [0, rmax], color="black", lw=1.0, alpha=0.85, zorder=4,
                ls=(0, (5, 4)) if a == "grep+cat calls" else "-")
    rlab = rmax * 1.108
    for a, th in zip(RADAR_AXES, ang):
        ha = "left" if math.cos(th) > 0.25 else "right" if math.cos(th) < -0.25 else "center"
        va = "bottom" if math.sin(th) > 0.25 else "top" if math.sin(th) < -0.25 else "center"
        ax.text(th, rlab, f"{a}\n{(ratios[a] - 1) * 100:+.0f}%", ha=ha, va=va,
                fontsize=11, linespacing=1.5, clip_on=False, zorder=5)


def _radar_legend(fig) -> None:
    handles = [mpatches.Patch(facecolor="#9a9a9a", alpha=0.5, label="control (baseline)"),
               mpatches.Patch(facecolor=ARM_COLOR[TREATMENT], alpha=0.6, label="treatment (qi)"),
               plt.Line2D([0], [0], color="black", lw=1.3, ls="-", label="outcome axis"),
               plt.Line2D([0], [0], color="black", lw=1.3, ls=(0, (5, 4)), label="mechanism axis")]
    fig.legend(handles=handles, loc="lower left", bbox_to_anchor=(0.02, 0.02),
               fontsize=10, frameon=False)


def chart_radar(run_dir: Path, charts_dir: Path, chart_idx: int) -> str | None:
    """Single-instance efficiency radar: treatment vs control across the 5 axes.
    Cross analog: cross_batch_compare.radar_chart (pooled, geomean)."""
    ratios = radar_ratios(run_dir)
    if ratios is None:
        return None
    fig, ax = plt.subplots(figsize=(7, 7), subplot_kw=dict(polar=True))
    ax.set_position([0.20, 0.12, 0.66, 0.66])  # room for outward labels + legend
    _radar_on_ax(ax, ratios)
    fig.suptitle("Efficiency radar", y=0.965, fontsize=16, fontweight="bold")
    fig.text(0.5, 0.915, "control = 100% · smaller = leaner", ha="center",
             fontsize=10, color="#555")
    _radar_legend(fig)
    fname = f"{chart_idx:02d}_radar_efficiency.png"
    _save(fig, charts_dir / fname)
    return fname


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


# --------------------------------------------------------------------------- #
# Charts -- mirrors analyze_stats.py's style (per-metric PNGs in charts/,
# control grey / treatment blue, patch_artist boxplots with means, Wilson-CI
# resolve bar), adapted to the Pro single-instance / N-rep shape.
# --------------------------------------------------------------------------- #
def _save(fig, path: Path) -> None:
    fig.savefig(path, dpi=96, bbox_inches="tight")
    plt.close(fig)


def chart_qi_grep(qi_data: dict, charts_dir: Path, chart_idx: int) -> str | None:
    """Dual-axis chart: grep/qi call counts (left) and output tokens (right, log)."""
    if not qi_data:
        return None

    _CALL_COLS  = ["grep_calls", "qi_calls"]
    _TOKEN_COLS = ["grep_tokens", "qi_tokens"]
    _METRIC_LABEL = {
        "grep_calls": "grep\ncalls", "qi_calls": "qi\ncalls",
        "grep_tokens": "grep\ntokens", "qi_tokens": "qi\ntokens",
    }
    _ARM_X = {
        CONTROL:   {"grep_calls": 1.0, "qi_calls": 2.0, "grep_tokens": 3.5, "qi_tokens": 4.5},
        TREATMENT: {"grep_calls": 7.0, "qi_calls": 8.0, "grep_tokens": 9.5, "qi_tokens": 10.5},
    }
    _ARM_CENTER_X = {CONTROL: 2.75, TREATMENT: 8.75}

    series = {col: {a: [] for a in ARM_ORDER}
              for col in _CALL_COLS + _TOKEN_COLS}
    for (arm, _run_id), vals in qi_data.items():
        if arm not in series["grep_calls"]:
            continue
        for col in series:
            series[col][arm].append(vals[col])

    def _draw(ax, cols, arm):
        for col in cols:
            data = series[col][arm]
            if not data:
                continue
            bp = ax.boxplot(
                [data], positions=[_ARM_X[arm][col]], widths=0.7,
                patch_artist=True, showmeans=True,
                medianprops=dict(color="black", linewidth=1.5, linestyle="--"),
                meanprops=dict(marker="D", markerfacecolor="black",
                               markeredgecolor="black", markersize=5),
                manage_ticks=False,
            )
            for patch in bp["boxes"]:
                patch.set_facecolor(ARM_COLOR[arm])
                patch.set_alpha(0.85)

    fig, ax = plt.subplots(figsize=(8.5, 4))
    ax2 = ax.twinx()

    for arm in ARM_ORDER:
        _draw(ax,  _CALL_COLS,  arm)
        _draw(ax2, _TOKEN_COLS, arm)

    tick_positions = [_ARM_X[arm][col] for arm in ARM_ORDER for col in _CALL_COLS + _TOKEN_COLS]
    tick_labels    = [_METRIC_LABEL[col] for _ in ARM_ORDER for col in _CALL_COLS + _TOKEN_COLS]
    ax.set_xticks(tick_positions)
    ax.set_xticklabels(tick_labels, fontsize=8)
    ax.set_xlim(0.0, 12.0)
    ax.axvline(x=5.75, color="0.5", linestyle="--", linewidth=1)

    for arm in ARM_ORDER:
        ax.text(_ARM_CENTER_X[arm], -0.22, ARM_LABEL[arm].upper(),
                transform=ax.get_xaxis_transform(),
                ha="center", va="top", fontsize=10, fontweight="bold",
                color=ARM_COLOR[arm] if arm == TREATMENT else "#555555")

    ax.set_ylabel("# calls")
    ax2.set_yscale("log")
    ax2.set_ylabel("# output tokens (log scale)")
    ax.set_title("grep / qi: calls and output tokens by arm")

    fig.tight_layout()
    fname = f"{chart_idx:02d}_box_qi_grep.png"
    _save(fig, charts_dir / fname)
    return fname


def _data_cumulative_cost(arms: dict) -> dict[str, list[float]]:
    """Per-arm running-total cost across reps (sorted by run_id) -- the series
    chart_cumulative_cost() plots (and chart_data.py dumps)."""
    def _cumsum(arm):
        rows = sorted(arms[arm], key=lambda r: r.get("run_id", ""))
        costs = [c for c in (fnum(r.get("cost")) for r in rows) if c is not None]
        running, out = 0.0, []
        for c in costs:
            running += c
            out.append(running)
        return out
    return {a: _cumsum(a) for a in ARM_ORDER}


def chart_cumulative_cost(arms: dict, charts_dir: Path, chart_idx: int) -> str | None:
    """Cumulative cost across reps (control grey, treatment blue): the running
    spend each arm accrues over the rep batch. The gap that widens (or doesn't)
    is the per-rep cost story the cost boxplot compresses to a single summary.
    Single-instance analog of cross_batch_compare.cumulative_cost_chart."""
    series = _data_cumulative_cost(arms)
    if not any(series.values()):
        return None

    fig, ax = plt.subplots(figsize=(5, 4))
    for arm in ARM_ORDER:
        cum = series[arm]
        if not cum:
            continue
        ax.plot(range(1, len(cum) + 1), cum, marker="o", linewidth=2,
                markersize=6, color=ARM_COLOR[arm], label=ARM_LABEL[arm])
    ax.set_xlabel("rep")
    ax.set_ylabel("cumulative cost ($)")
    ax.set_title("Cumulative cost across reps")
    ax.xaxis.set_major_locator(plt.MaxNLocator(integer=True))
    ax.grid(True, color="0.85", linewidth=0.7, zorder=0)
    ax.set_axisbelow(True)
    ax.legend()
    fname = f"{chart_idx:02d}_line_cumulative_cost.png"
    _save(fig, charts_dir / fname)
    return fname


def _data_log_size_range(arms: dict) -> dict[str, list[float]]:
    """Per-arm sorted log_size_kb values -- the series chart_log_size_range()
    plots (and chart_data.py dumps). Shared so the two never drift apart."""
    return {a: sorted(v for v in (fnum(r.get("log_size_kb")) for r in arms[a])
                      if v is not None)
            for a in ARM_ORDER}


def chart_log_size_range(arms: dict, charts_dir: Path, chart_idx: int) -> str | None:
    """Per-arm .log file size range: two horizontal bars (treatment on top,
    control below), each spanning min->max with circle endpoints and a median
    diamond. Reads the log_size_kb column analyze_pro_trajectories.py records.
    Single-instance analog of cross_batch_compare.log_size_range_chart."""
    sizes = _data_log_size_range(arms)
    if not any(sizes.values()):
        return None

    fig, ax = plt.subplots(figsize=(5, 2.2))
    for y, arm in enumerate([TREATMENT, CONTROL]):
        vals = sizes[arm]
        if not vals:
            continue
        lo, hi = min(vals), max(vals)
        color = ARM_COLOR[arm]
        ax.hlines(y, lo, hi, color=color, linewidth=2.5, zorder=2)
        ax.scatter([lo, hi], [y, y], color=color, edgecolor="k",
                   linewidth=0.6, s=55, zorder=3)
        ax.scatter([st.median(vals)], [y], marker="D", color="black",
                   s=50, zorder=4)

    ax.set_yticks([0, 1])
    ax.set_yticklabels([ARM_LABEL[TREATMENT], ARM_LABEL[CONTROL]])
    ax.set_ylim(-0.6, 1.6)
    ax.set_xlabel("log file size (KB)")
    ax.set_title("log file size range by arm")
    fname = f"{chart_idx:02d}_range_log_size.png"
    _save(fig, charts_dir / fname)
    return fname


def _data_resolve_rate(arms: dict) -> dict[str, tuple[int, int, float, float, float]]:
    """Per-arm (k, n, rate, wilson_lo, wilson_hi) -- the values the resolve-rate
    bar plots (and chart_data.py dumps)."""
    out = {}
    for a in ARM_ORDER:
        k = sum(1 for r in arms[a] if r.get("task_success") == "1")
        n = len(arms[a])
        p, lo, hi = wilson(k, n)
        out[a] = (k, n, p, lo, hi)
    return out


def make_charts(arms: dict, charts_dir: Path,
                qi_data: dict | None = None,
                explore: dict | None = None,
                run_dir: Path | None = None) -> list[str]:
    """Render the per-arm chart set. Returns the filenames written."""
    charts_dir.mkdir(parents=True, exist_ok=True)
    written: list[str] = []
    labels = [ARM_LABEL[a] for a in ARM_ORDER]
    facecolors = [ARM_COLOR[a] for a in ARM_ORDER]
    rng = random.Random(0)  # reproducible jitter

    # Per-metric panels (one PNG each), styled like analyze_stats.py. Most are
    # boxplots; low-variance integer counts (STRIP_METRICS) use strip/jitter so
    # a single larger rep reads as a point, not a dramatized boxplot "outlier".
    for i, (col, label, _) in enumerate(METRICS, 1):
        if col in NO_CHART_METRICS:  # keep the index vacant; table row still shown
            continue
        data = [[fnum(r.get(col)) for r in arms[a] if fnum(r.get(col)) is not None]
                for a in ARM_ORDER]
        if not any(data):
            continue
        fig, ax = plt.subplots(figsize=(5, 4))
        if col in STRIP_METRICS:
            for pos, (d, fc) in enumerate(zip(data, facecolors), 1):
                xs = [pos + rng.uniform(-0.08, 0.08) for _ in d]
                ax.scatter(xs, d, color=fc, edgecolor="k", linewidth=0.5,
                           s=60, alpha=0.85, zorder=3)
                if d:  # mean as a short horizontal bar
                    ax.hlines(st.fmean(d), pos - 0.18, pos + 0.18,
                              color="k", lw=2, zorder=4)
            ax.set_xlim(0.5, 2.5)
            kind = "strip"
        else:
            bp = ax.boxplot(data, patch_artist=True, showmeans=True,
                            medianprops=dict(color="black", linewidth=1.5, linestyle="--"),
                            meanprops=dict(marker="D", markerfacecolor="black",
                                           markeredgecolor="black", markersize=5))
            for patch, fc in zip(bp["boxes"], facecolors):
                patch.set_facecolor(fc)
            kind = "box"
        # Label ticks separately: boxplot's `labels=` became `tick_labels=` in
        # mpl 3.9 and was removed in 3.11; this is version-agnostic.
        ax.set_xticks([1, 2])
        ax.set_xticklabels(labels)
        ax.set_title(f"{label} by arm")
        ax.set_ylabel(col)
        fname = f"{i:02d}_{kind}_{col}.png"
        _save(fig, charts_dir / fname)
        written.append(fname)

    # Resolve-rate bar with 95% Wilson CI (analyze_stats.py chart #10 analog).
    resolve = _data_resolve_rate(arms)
    rates = [resolve[a][2] for a in ARM_ORDER]
    los = [resolve[a][2] - resolve[a][3] for a in ARM_ORDER]
    his = [resolve[a][4] - resolve[a][2] for a in ARM_ORDER]
    fig, ax = plt.subplots(figsize=(5, 4))
    ax.bar(labels, rates, color=facecolors, yerr=[los, his], capsize=6)
    ax.set_ylim(0, 1)
    ax.set_ylabel("resolve rate")
    ax.set_title("Resolution rate by arm (95% Wilson CI)")
    fname = f"{len(METRICS) + 1:02d}_bar_resolve_rate.png"
    _save(fig, charts_dir / fname)
    written.append(fname)

    # 08_box_qi_grep retired: superseded by the explore call/token stacks, and its
    # grep-vs-qi-only framing ignored cat/sed-read. Function kept (unused) pending
    # a decision to delete it; index 08 is intentionally left vacant.
    # if qi_data:
    #     fname = chart_qi_grep(qi_data, charts_dir, len(METRICS) + 2)
    #     if fname:
    #         written.append(fname)

    fname = chart_cumulative_cost(arms, charts_dir, len(METRICS) + 3)
    if fname:
        written.append(fname)

    fname = chart_log_size_range(arms, charts_dir, len(METRICS) + 4)
    if fname:
        written.append(fname)

    if explore:
        fname = chart_explore_stack(explore, charts_dir, len(METRICS) + 5)
        if fname:
            written.append(fname)
        fname = chart_explore_tokens(explore, charts_dir, len(METRICS) + 6)
        if fname:
            written.append(fname)

    if run_dir is not None:
        fname = chart_radar(run_dir, charts_dir, len(METRICS) + 7)
        if fname:
            written.append(fname)

    return written


def load_arms(run_dir: Path, quiet: bool = False) -> dict[str, list[dict]]:
    """Load runs_with_success.csv into {arm: [row, ...]}, joining wall_time.csv's
    duration_sec by (arm, run_id) exactly as main() does. Shared so chart_data.py
    reads the identical rows the charts/report are built from."""
    csv_path = run_dir / "runs_with_success.csv"
    if not csv_path.is_file():
        raise FileNotFoundError(csv_path)
    with csv_path.open(newline="") as f:
        rows = list(csv.DictReader(f))

    wall = load_wall_times(run_dir)
    for r in rows:
        r["duration_sec"] = wall.get((r["arm"], r["run_id"]), "")
    if not quiet:
        if wall:
            print(f"(joined wall_time.csv: {len(wall)} rep duration(s))")
        else:
            print("(no wall_time.csv — wall-time metric/chart skipped; "
                  "run wall_time.py first to include it)")

    arms: dict[str, list[dict]] = {CONTROL: [], TREATMENT: []}
    for r in rows:
        if r["arm"] in arms:
            arms[r["arm"]].append(r)
    return arms


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
    arms = load_arms(args.dir)

    qi_data = load_qi_commands(args.dir)
    explore = load_explore(args.dir)
    if qi_data:
        print(f"(loaded qi_commands.csv: {len(qi_data)} run(s))")
    else:
        print("(no qi_commands.csv — grep/qi + explore charts skipped; "
              "run extract_qi_commands.py first to include them)")

    nC, nT = len(arms[CONTROL]), len(arms[TREATMENT])
    print(f"=== Pro rep-level stats ===")
    print(f"instance(s): {sorted({r['instance_id'] for a in arms.values() for r in a})}")
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

    # format-tax turns per arm -- blank-content ("lost action") turns, a
    # reasoning-model harness artifact (DeepSeek routes the command into
    # reasoning_content, leaving content empty -> "0 actions"). It inflates
    # turns/tokens/cost and is drawn asymmetrically across arms, so it sets a
    # variance floor that can masquerade as a treatment effect. CSV-only for now
    # (no stdout line / chart, unlike the resolve/blow-up rows above) by request;
    # schema mirrors blowup_rate: count in control_median, denom (total turns) in
    # treatment_median, rate in delta_median, plus per-run mean in control_mean.
    for arm in (CONTROL, TREATMENT):
        n = len(arms[arm])
        ev = [int(fnum(r.get("empty_content_turns")) or 0) for r in arms[arm]]
        tv = [int(fnum(r.get("turn_count")) or 0) for r in arms[arm]]
        tax, tot = sum(ev), sum(tv)
        summary_rows.append(dict(
            metric=f"format_tax_turns[{arm}]", control_n=n,
            control_median=tax, control_mean=(st.fmean(ev) if ev else 0.0),
            treatment_median=tot, delta_median=(tax / tot if tot else 0.0),
            boot_ci_lo="", boot_ci_hi="", mwu_p=""))

    out_path = args.dir / "pro_stats_summary.csv"
    keys = sorted({k for r in summary_rows for k in r})
    with out_path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=keys)
        w.writeheader()
        w.writerows(summary_rows)
    print(f"\nWrote {out_path}")

    if not args.no_charts:
        if plt is None:
            print("(charts skipped: matplotlib not available)", file=sys.stderr)
        else:
            charts_dir = args.dir / "charts"
            written = make_charts(arms, charts_dir, qi_data=qi_data,
                                  explore=explore, run_dir=args.dir)
            print(f"Wrote {len(written)} chart(s) -> {charts_dir}/")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
