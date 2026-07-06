#!/usr/bin/env python3
"""Chart: min-max log file size range by arm.

Generates two kinds of chart:
  - Per-instance: two range bars (control vs treatment) showing min→max .log
    file size with a diamond at the median. Written to the batch's charts/ dir.
  - Cross-instance: all instances in one chart, each row showing control and
    treatment range bars side-by-side. Written to _cross/charts/.
"""

from __future__ import annotations

import statistics as st
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker

CONTROL, TREATMENT = "swebp_control", "swebp_treatment"
ARM_COLOR = {CONTROL: "#b0b0b0", TREATMENT: "#2a7fb8"}
ARM_LABEL = {CONTROL: "control", TREATMENT: "treatment"}


def _save(fig, path: Path) -> None:
    fig.savefig(path, dpi=96, bbox_inches="tight")
    plt.close(fig)


def get_log_sizes(log_dir: Path) -> dict[str, list[float]]:
    """Scan *swebp_{arm}_rep*.log files, return {arm: [size_kb, ...]}."""
    arms: dict[str, list[float]] = {CONTROL: [], TREATMENT: []}
    for arm in arms:
        sizes = sorted(
            p.stat().st_size / 1024.0
            for p in log_dir.glob(f"{arm}_rep*.log")
        )
        arms[arm] = sizes
    return arms


def per_instance_chart(
    instance_name: str,
    sizes: dict[str, list[float]],
    output_path: Path,
) -> str:
    """Range-bar chart for one instance: two horizontal bars (control, treatment),
    each spanning min→max with circle endpoints and a diamond at the median.

    Returns the filename written.
    """
    output_path.parent.mkdir(parents=True, exist_ok=True)

    fig, ax = plt.subplots(figsize=(5, 2.2))

    # Treatment on top, control below (visually: treatment above zero-line).
    for y, arm in enumerate([TREATMENT, CONTROL]):
        vals = sizes[arm]
        if not vals:
            continue
        lo, hi = min(vals), max(vals)
        color = ARM_COLOR[arm]
        ax.hlines(y, lo, hi, color=color, linewidth=2.5, zorder=2)
        ax.scatter(
            [lo, hi], [y, y],
            color=color, edgecolor="k", linewidth=0.6, s=55, zorder=3,
        )
        med = st.median(vals)
        ax.scatter(
            [med], [y],
            marker="D", color="black", s=50, zorder=4,
        )

    ax.set_yticks([0, 1])
    ax.set_yticklabels([ARM_LABEL[TREATMENT], ARM_LABEL[CONTROL]])
    ax.set_ylim(-0.6, 1.6)
    ax.set_xlabel("log file size (KB)")
    ax.set_title(f"{instance_name}: log file size range by arm")
    ax.xaxis.set_major_formatter(mticker.FuncFormatter(lambda x, _: f"{x:.0f}"))

    _save(fig, output_path)
    return output_path.name


def cross_instance_chart(
    instances: list[tuple[str, dict[str, list[float]]]],
    output_path: Path,
) -> str:
    """Combined chart: one row per instance with control and treatment range
    bars offset vertically within the row.

    Returns the filename written.
    """
    output_path.parent.mkdir(parents=True, exist_ok=True)
    n = len(instances)
    fig, ax = plt.subplots(figsize=(7, 0.55 * n + 1.8))

    for row, (name, sizes) in enumerate(instances):
        base_y = row
        for offset, arm in [(0.15, CONTROL), (-0.15, TREATMENT)]:
            vals = sizes[arm]
            if not vals:
                continue
            lo, hi = min(vals), max(vals)
            y = base_y + offset
            color = ARM_COLOR[arm]
            ax.hlines(y, lo, hi, color=color, linewidth=2.5, zorder=2)
            ax.scatter(
                [lo, hi], [y, y],
                color=color, edgecolor="k", linewidth=0.5, s=45, zorder=3,
            )
            med = st.median(vals)
            ax.scatter(
                [med], [y],
                marker="D", color="black", s=35, zorder=4,
            )

    ax.set_yticks(range(n))
    ax.set_yticklabels([name for name, _ in instances])
    ax.set_ylim(-0.6, n - 0.4)
    ax.set_xlabel("log file size (KB)")
    ax.set_title("Log file size range by instance and arm")
    ax.xaxis.set_major_formatter(mticker.FuncFormatter(lambda x, _: f"{x:.0f}"))

    handles = [
        plt.Line2D(
            [0], [0], marker="o", linestyle="", color=ARM_COLOR[CONTROL],
            markeredgecolor="k", label=ARM_LABEL[CONTROL], markersize=8,
        ),
        plt.Line2D(
            [0], [0], marker="o", linestyle="", color=ARM_COLOR[TREATMENT],
            markeredgecolor="k", label=ARM_LABEL[TREATMENT], markersize=8,
        ),
    ]
    ax.legend(handles=handles, fontsize=8, loc="lower right")

    _save(fig, output_path)
    return output_path.name


# -- Canonical 5 instances for the cross-instance chart ---------------------- #
INSTANCES = [
    ("webclients",    "deepseek--deepseek-v4-pro",               "pro_pilot_webclients_ds_v4_pro"),
    ("nodebb",        "anthropic--claude-haiku-4-5-20251001",    "pro_pilot_nodebb_haiku"),
    ("openlibrary",   "anthropic--claude-haiku-4-5-20251001",    "pro_pilot_openlibrary_haiku_v2"),
    ("ansible",       "xiaomi_mimo--mimo-v2.5-pro",              "pro_pilot_ansible_mimo_v2.5-pro"),
    ("qutebrowser",   "anthropic--claude-haiku-4-5-20251001",    "pro_pilot_qutebrowser_haiku_v1"),
]


def main() -> None:
    logs_root = Path(__file__).resolve().parent.parent / "logs"
    results_root = Path(__file__).resolve().parent.parent / "results" / "pro_runs"

    cross_data: list[tuple[str, dict[str, list[float]]]] = []

    for name, model_dir, run_dir in INSTANCES:
        log_dir = logs_root / model_dir / run_dir
        if not log_dir.is_dir():
            print(f"SKIP {name}: log dir not found at {log_dir}", file=sys.stderr)
            continue

        sizes = get_log_sizes(log_dir)
        if not any(sizes.values()):
            print(f"SKIP {name}: no .log files found", file=sys.stderr)
            continue

        # Per-instance chart
        chart_path = results_root / run_dir / "charts" / "09_strip_log_size_range.png"
        fname = per_instance_chart(name, sizes, chart_path)
        print(f"  {run_dir}/charts/{fname}")

        cross_data.append((name, sizes))

    # Cross-instance chart
    cross_path = results_root / "_cross" / "charts" / "log_size_range.png"
    fname = cross_instance_chart(cross_data, cross_path)
    print(f"  _cross/charts/{fname}")


if __name__ == "__main__":
    import sys
    main()
