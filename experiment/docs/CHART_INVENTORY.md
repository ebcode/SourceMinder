# Chart Inventory

Every chart the Pro analysis pipeline produces — single-instance and
cross-instance — with its generating function, data source, and status. See
[`PRO_ANALYZE.md`](PRO_ANALYZE.md) for the pipeline steps and
[`CROSS_INSTANCE.md`](CROSS_INSTANCE.md) for the cross-instance method.

Update this table whenever a chart is added, renamed, or retired.

## Single-Instance Charts

Produced by `analyze_pro_stats.py::make_charts()`, written to
`results/pro_runs/<batch>/charts/`. Filenames carry a numeric index reflecting
draw order; retired/skipped indices are left vacant rather than renumbered, so
gaps in the sequence are expected, not a bug.

| # | Filename | Function | Data source | Status | Description |
|---|----------|----------|--------------|--------|--------------|
| 01 | `01_box_turn_count.png` | `make_charts()` (METRICS loop) | `runs_with_success.csv` | live | Boxplot of turn count per rep, control vs treatment. |
| 02 | `02_box_total_tokens.png` | `make_charts()` (METRICS loop) | `runs_with_success.csv` | live | Boxplot of total tokens per rep, control vs treatment. |
| 03 | `03_box_cost.png` | `make_charts()` (METRICS loop) | `runs_with_success.csv` | live | Boxplot of cost ($) per rep, control vs treatment. |
| 04 | *(vacant)* | — | `wall_time.csv` | **removed** | Would have been a wall-time boxplot; dropped because wall time is noisy on a spotty connection (reps run sequentially, so spread reflects scheduling/network, not work). Table row (`pro_stats_summary.csv`) still shown via `NO_CHART_METRICS`. |
| 05 | `05_strip_patch_lines.png` | `make_charts()` (METRICS loop, `STRIP_METRICS`) | `runs_with_success.csv` | live | Strip/jitter plot of patch line count per rep (avoids a boxplot dramatizing one larger rep as an "outlier"). |
| 06 | `06_strip_files_touched.png` | `make_charts()` (METRICS loop, `STRIP_METRICS`) | `runs_with_success.csv` | live | Strip/jitter plot of files touched per rep. |
| 07 | `07_bar_resolve_rate.png` | `make_charts()` (inline, Wilson CI) | `runs_with_success.csv` (`task_success`) | live | Bar chart of resolve rate per arm with 95% Wilson confidence interval. |
| 08 | *(vacant)* | `chart_qi_grep()` (kept, unused) | `qi_commands.csv` (via `load_qi_commands()`) | **retired** | Was a dual-axis chart of grep/qi call counts and output tokens; superseded by the explore call/token stacks (#11/#12), whose usage-detection model doesn't ignore cat/sed-read the way this grep-vs-qi-only framing did. Function preserved, call commented out. |
| 09 | `09_line_cumulative_cost.png` | `chart_cumulative_cost()` | `runs_with_success.csv` (`cost`, sorted by `run_id`) | live | Line chart of cumulative cost across reps, per arm. |
| 10 | `10_range_log_size.png` | `chart_log_size_range()` | `runs_with_success.csv` (`log_size_kb`) | live | Horizontal min-max range bars (with median) of agent log file size, per arm. |
| 11 | `11_stack_explore_calls.png` | `chart_explore_stack()` | `qi_commands.csv` (via `load_explore()`) | live | Stacked bar of recognized-tool calls per run (grep/cat/sed-read/mixed/qi) — the "qi displaces manual search" headline visual. |
| 12 | `12_stack_explore_tokens.png` | `chart_explore_tokens()` | `qi_commands.csv` (via `load_explore()`, homogeneous actions only) | live | Stacked bar of output tokens per run, attributed per tool for single-tool (homogeneous) actions only — not a total. |
| 13 | `13_radar_efficiency.png` | `chart_radar()` | `runs_with_success.csv` + `qi_commands.csv` (via `radar_ratios()`) | live | Radar of treatment/control ratios across 5 axes (log size, log variance, turns, patch lines, grep+cat calls); control is the baseline ring, treatment is the polygon inside it. |

**Prior retirement, fully deleted (not vacant, not commented out):**
`09_strip_log_size_range.png` — a stale orphan from a superseded pipeline
version, deleted (not a vacated index) since no live function ever produced
it under that name.

## Cross-Instance Charts

Produced by `cross_batch_compare.py`, written to
`results/pro_runs/_cross/charts/` (or an ad-hoc `--out` dir). Filenames carry
no numeric index — order is whatever `main()`'s chart-writing block calls in.

| Filename | Function | Data source | Status | Description |
|----------|----------|--------------|--------|--------------|
| `forest_turn_count.png` | `forest_chart()` | `cross_instance.csv` rows for `turn_count` | live | Forest plot: per-instance turn-count effect (log-ratio of medians) as a dot + CI whisker, colored by model, with a pooled diamond. |
| `forest_total_tokens.png` | `forest_chart()` | `cross_instance.csv` rows for `total_tokens` | live | Forest plot: per-instance total-token effect, same layout as above. |
| `forest_cost.png` | `forest_chart()` | `cross_instance.csv` rows for `cost` | live | Forest plot: per-instance cost effect, same layout as above. |
| *(none)* | `forest_chart()` for `duration_sec` | `cross_instance.csv` row for `duration_sec` | **removed** | Would have been a wall-time forest plot; dropped for the same spotty-wifi rationale as the single-instance wall-time chart. `NO_CHART_METRICS = {"duration_sec"}` skips the `forest_chart()` call while the CSV row and stdout report line still print. |
| `resolve_dumbbell.png` | `resolve_dumbbell()` | per-batch resolve counts (`b["resolve"]`) | live | Dumbbell plot of control vs treatment resolve rate per instance — the "never hurts" safety panel, kept separate from the efficiency forests. |
| `cumulative_cost.png` | `cumulative_cost_chart()` | per-batch `runs_with_success.csv` (small multiples, normalized to each instance's control total) | live | Small-multiples cumulative-cost curves, one panel per instance, normalized so cheap and expensive models share one scale. |
| `log_size_range.png` | `log_size_range_chart()` | per-batch `runs_with_success.csv` (`log_size_kb`) | live | Per-instance log-file-size min-max range bars (with median), control and treatment offset within each row. |
| `search_output.png` | `search_output_chart()` | per-batch `qi_commands.csv` (treatment arm only, qi vs grep output tokens) | live but **superseded** | Side-by-side boxplot of qi vs grep output tokens per invocation, treatment arm only; the explore call/token stacks below tell the same story with cat/sed-read included. Not yet retired (call still active) — candidate for the same retirement treatment as `qi_grep.png`. |
| *(none)* | `qi_grep_chart()` (kept, unused) | per-batch `qi_commands.csv` | **retired** | Was a cross-instance grep-vs-qi chart; superseded by `explore_calls.png`/`explore_tokens.png`. Function preserved, call commented out. |
| `explore_calls.png` | `explore_stack_chart()` | per-batch `qi_commands.csv` (via shared `_cross_explore_chart()`) | live | Cross-instance stacked bar of recognized-tool calls per run, normalized to control-total = 100% per instance. |
| `explore_tokens.png` | `explore_tokens_chart()` | per-batch `qi_commands.csv` (via shared `_cross_explore_chart()`, homogeneous actions only) | live | Cross-instance stacked bar of homogeneous per-tool output tokens per run. |
| `radar_efficiency.png` | `radar_chart()` | per-batch `runs_with_success.csv` + `qi_commands.csv` (pooled via `radar_ratios()` per instance, geomean) | live | The hero chart: pooled (geometric mean) efficiency radar across instances, nesting an "all instances" polygon and a "qi good-fit" subset polygon (`RADAR_NON_SOURCE` scoped out) inside the control baseline ring. |

## Shared Conventions

- **Control = grey (`#b0b0b0`), treatment = blue (`#2a7fb8`)** across every
  chart in both files (`ARM_COLOR`).
- **Retire by commenting out the call site, not deleting the function** —
  matches the project's comment-out-vs-delete convention. A vacated
  single-instance index (`04`, `08`) is left as a gap rather than
  renumbered, so the index alone signals "something used to be here."
- **`NO_CHART_METRICS`** (defined independently in each file) is the
  mechanism for "keep the number, drop the chart" — currently used only for
  `duration_sec` in both files.
- **Mechanism vs outcome spokes**: the two radar charts (single-instance
  `chart_radar()` and cross-instance `radar_chart()`) draw the `grep+cat
  calls` axis dashed (the *mechanism*) and the four outcome axes solid,
  labeled in each chart's legend.

## See Also

- [`PRO_ANALYZE.md`](PRO_ANALYZE.md) — full pipeline reference (Steps 1-8)
- [`CROSS_INSTANCE.md`](CROSS_INSTANCE.md) — cross-instance meta-analysis method and manifest usage
- `experiment/analysis/analyze_pro_stats.py` — single-instance chart source
- `experiment/analysis/cross_batch_compare.py` — cross-instance chart source
