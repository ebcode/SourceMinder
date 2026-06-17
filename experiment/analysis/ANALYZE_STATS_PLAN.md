# analyze_stats.py — Design Plan

Statistical analysis layer that consumes `runs_with_success.csv` and produces
descriptive statistics, inferential tests, and visualizations for the qi context
preservation experiment. The pilot (N=400: 20 instances × 2 arms × 10 reps) is
exploratory; this script estimates variance and effect sizes to power the
confirmatory study.

## 1. Statistics Collected

All are computed per-arm with per-instance disaggregation where noted.

### 1.1 Descriptives (per PREREGISTRATION §9.2)

| Metric | Statistic | Why |
|--------|-----------|-----|
| `total_input_tokens` | median, IQR, mean, SD, min, max | Overall context consumption |
| `peak_prompt_tokens` | median, IQR, mean, SD, min, max | Context-window pressure (primary) |
| `tool_output_tokens_approx` | median, IQR, mean, SD | Exploration overhead (approximate) |
| `task_success` | count, rate, 95% Wilson CI | Non-inferiority check |
| `turn_count` | median, IQR, mean, SD | Censorship awareness |
| `total_completion_tokens` | median, IQR, mean, SD | Output cost |
| `total_reasoning_tokens` | median, IQR, mean, SD | Thinking overhead |
| `total_cached_tokens` | median, IQR, mean, SD | Cache hit rate |
| `qi_invocations` | mean, SD, range (treatment only) | Adherence check |
| `grep_invocations` | mean, SD, by arm | Fallback behavior |
| `file_read_invocations` | mean, SD, by arm | File-dump behavior |

### 1.2 Pilot Evaluation Criteria (per PREREGISTRATION §9.3)

| Criterion | Computation |
|-----------|-------------|
| Completion rate | `count(submitted=True) / total_runs`, by arm |
| Control success rate | `count(arm=control, task_success=1) / count(arm=control)` — must be ≥30% |
| Variance stability | Bootstrap SE of each primary metric's variance; flag if SE > 30% of point estimate |

### 1.3 Success Thresholds (per PREREGISTRATION §10, confirmatory-only)

| Threshold | How measured | Expected now |
|-----------|-------------|--------------|
| Median total_input_tokens reduced ≥20% | Bootstrap CI on ratio of medians | Exploratory only in pilot |
| Median peak_prompt_tokens reduced ≥20% | Bootstrap CI on ratio of medians | Exploratory only in pilot |
| 95% CI for median peak difference excludes zero | Bootstrap percentile CI on raw difference | Exploratory only in pilot |
| Success rate non-inferior (margin 5pp) | Two-proportion CI lower bound > -5pp | Exploratory only in pilot |

### 1.4 Inferential (Exploratory)

| Test | Metric | Notes |
|------|--------|-------|
| Mann-Whitney U (two-sided) | peak_prompt_tokens, total_input_tokens, tool_output_tokens_approx | Ranks-based, doesn't assume normality |
| Two-proportion z-test | task_success | Fisher's exact for small N |
| Bootstrap 95% CI for median difference | All three token metrics | 10,000 resamples, percentile method |
| Bootstrap 95% CI for median ratio | All three token metrics | Treatment / control |

## 2. Grouping and Aggregation Levels

### 2.1 Global (Primary)

All 400 runs pooled. Every metric computed once per arm. This is the headline
comparison but masks per-instance heterogeneity.

### 2.2 Per-Instance (Secondary)

20 paired comparisons (one per instance). Each instance has 10 control + 10
treatment runs. Computed:

- Per-instance median for each metric (both arms)
- Per-instance median difference (treatment - control)
- Per-instance success rate (both arms)
- Per-instance effect direction (+ = treatment costs more, - = treatment saves)

### 2.3 Per-Repo (Exploratory)

Instances grouped by repository (Django, matplotlib, sympy, astropy, pylint,
sphinx, xarray, etc.). Not preregistered but informative for generalization.
Django instances have cross-file permissions/migrations — where qi's symbol
navigation should be strongest.

### 2.4 Per-Outcome (Exploratory)

- **Submitted only:** token metrics for runs that produced a patch
- **Successful only:** token metrics for runs with `task_success = 1`
- **By exit_status:** breakdown of where runs end (Submitted, LimitsExceeded, etc.)

## 3. Analytical Lenses

### 3.1 Token Efficiency Lens

"Does the treatment arm consume fewer tokens?"

Primary metrics: `total_input_tokens`, `peak_prompt_tokens`, `tool_output_tokens_approx`.
Compared globally and per-instance with Mann-Whitney U and bootstrap CIs.

### 3.2 Success Rate Lens

"Does the treatment arm resolve as many instances?"

Primary metric: `task_success`. Compared with two-proportion test and
non-inferiority check (lower bound of 95% CI must not cross -5pp).

### 3.3 Completion Lens

"Did runs terminate cleanly or exhaust the budget?"

Metrics: `exit_status` distribution, `turn_count` distribution, completion rate
(≥90% expected). Censorship at 100 turns matters — runs hitting the limit may
have been on track to succeed with more budget.

### 3.4 Mechanism Lens

"*How* did the arms explore — qi, grep, or file dumps?"

Metrics: `qi_invocations`, `grep_invocations`, `file_read_invocations`. For
treatment, correlates qi usage with token savings to ask: does more qi usage
actually reduce token consumption? For control, establishes the baseline
exploration pattern (expect heavy grep/file-dump dominance).

### 3.5 Cost Lens

"What did the runs cost in output and reasoning tokens?"

Metrics: `total_completion_tokens`, `total_reasoning_tokens`. Reasoning tokens
are thinking-mode overhead that doesn't appear in visible output. If treatment
has higher reasoning tokens, it may indicate the qi instruction adds cognitive
overhead.

### 3.6 Success-Adjusted Lens

"After controlling for success, does the token effect persist?"

Filter to `task_success = 1` runs only. A treatment run that succeeds may have
higher token counts simply because successful runs do more work. This lens
isolates exploration efficiency from success bias.

### 3.7 Censorship-Aware Lens

"How do turn-budget truncations affect the measured distributions?"

Flag runs with `exit_status = LimitsExceeded`. Their token counts are
right-censored — they would have consumed more tokens with more turns. If one
arm hits the limit significantly more often, comparisons are biased.

## 4. Potential Misinterpretations

*These should be printed as caveats alongside results, not buried in a footnote.*

### 4.1 Success Confounds Token Counts (MOST CRITICAL)

A successful run (agent finds and fixes the bug) naturally does more work: more
exploration, more edits, more turns. If treatment has a higher success rate, its
median tokens may be higher *because it succeeds more often*, not because qi is
inefficient. The success-adjusted lens (§3.6) and per-protocol analysis
addresses this partially. The safest interpretation: "among comparable outcomes,
treatment uses [more/less] tokens."

### 4.2 Turn Budget Censorship

Runs that hit 100 turns (`LimitsExceeded`) have truncated token counts. The
median of a censored distribution underestimates the true median. If one arm
hits the limit more often, the comparison is confounded. Report the censoring
rate by arm explicitly.

### 4.3 ~4 chars/token Approximation

`tool_output_tokens_approx` divides character counts by 4.0 — not the real
DeepSeek tokenizer. It's adequate for descriptive ranking (which arm had more
tool output?) but NOT for precise token counts. The primary metrics
(`total_input_tokens`, `peak_prompt_tokens`) come from the API's own token
counter and are exact.

### 4.4 N=20 Instances, Not 400 Independent Observations

Each instance contributes 20 correlated runs (10 control, 10 treatment). Pooling
all 400 and treating them as independent inflates degrees of freedom. The
per-instance paired analysis treats instance as the blocking factor. The global
pool should use clustered bootstrap (resample instances, not individual runs).

### 4.5 Pilot is Underpowered — P-Values are Descriptive

This script is for the **pilot**, whose purpose is estimating variance and
effect sizes. Any p-values computed are exploratory and should be labeled as
such. The confirmatory study will be powered to 80-90% based on pilot estimates.

### 4.6 Multiple Comparisons

Testing 20 instances × 3 primary metrics × multiple tests inflates the
family-wise error rate. Do not cherry-pick the instance with the best
treatment-vs-control delta. If per-instance forest plots are shown, they
should not be individually annotated with p-values.

### 4.7 Control Arm Can't Use qi (By Design)

The control arm has qi *not mounted*. Any qi invocations in control would be
"command not found" errors. This means contamination isn't a concern, but it
also means we can't compare "qi users vs. non-qi users" within the control arm
— there are no qi users in control.

### 4.8 Treatment Arm qi Usage Varies

Not all treatment runs use qi heavily. Some rep/instance combinations may fall
back to grep after a failed qi attempt. A per-protocol analysis (qi-heavy vs.
qi-light treatment runs) is informative but not preregistered — report it
separately from the primary ITT analysis.

## 5. Comprehensive Chart and Graph Inventory

All charts assume `runs_with_success.csv` as input. Implementation priority
from 1 (headline) to 3 (supplementary).

### 5.1 Token Metrics — Global Distributions (Priority 1)

| # | Chart | Variables | Description |
|---|-------|-----------|-------------|
| 1 | **Boxplot** | `total_input_tokens` × `arm` | Median, IQR, whiskers, outliers. The workhorse visualization. |
| 2 | **Boxplot** | `peak_prompt_tokens` × `arm` | Context-window pressure. Primary metric. |
| 3 | **Boxplot** | `tool_output_tokens_approx` × `arm` | Exploration overhead (approximate). |
| 4 | **Violin plot** | `peak_prompt_tokens` × `arm` | Shows distribution shape, not just quartiles. |
| 5 | **Overlaid histogram** | `peak_prompt_tokens`, fill = `arm` | Shows multimodality, skew, overlap. |
| 6 | **ECDF** | `peak_prompt_tokens`, color = `arm` | Cumulative distribution. Stochastic dominance test. |

### 5.2 Per-Instance Effects (Priority 1)

| # | Chart | Variables | Description |
|---|-------|-----------|-------------|
| 7 | **Paired dot/line plot** | median `peak_prompt_tokens` per instance, arm pairs connected | Shows per-instance effect direction at a glance. Red line = treatment worse, green = better. |
| 8 | **Forest plot** | per-instance median difference + bootstrap 95% CI | Which instances favor which arm, with uncertainty. Ordered by effect size. |
| 9 | **Heatmap** | instances × metrics, color = treatment/control ratio | Compact overview. Red cells = treatment costs more, green = saves. |

### 5.3 Success Rate (Priority 1)

| # | Chart | Variables | Description |
|---|-------|-----------|-------------|
| 10 | **Bar chart** | `task_success` rate × `arm`, error bars = 95% Wilson CI | Binary outcome comparison. |
| 11 | **Stacked bar** | `exit_status` count × `arm` | Where runs terminate: Submitted, LimitsExceeded, others. |
| 12 | **Per-instance dot plot** | success rate per instance, arm pairs connected | Same idea as #7 but for binary outcome. |

### 5.4 Mechanism — How Arms Explore (Priority 2)

| # | Chart | Variables | Description |
|---|-------|-----------|-------------|
| 13 | **Grouped bar** | mean invocations per run: `qi`, `grep`, `file_read` × `arm` | Qualitative difference in exploration behavior. |
| 14 | **Scatter** | `qi_invocations` vs `peak_prompt_tokens`, color = `task_success` | Treatment only: does more qi correlate with fewer tokens? |
| 15 | **Scatter** | `grep_invocations` vs `peak_prompt_tokens`, color = `arm` | Both arms: does grep-heavy exploration drive up context? |

### 5.5 Success-Adjusted (Priority 2)

| # | Chart | Variables | Description |
|---|-------|-----------|-------------|
| 16 | **Boxplot** | `peak_prompt_tokens` × `arm`, filtered to `task_success = 1` | Token comparison among runs that actually resolved the issue. |
| 17 | **Scatter** | `turn_count` vs `peak_prompt_tokens`, color = `arm`, shape = `task_success` | Shows relationship between duration and context bloat, separated by outcome. |

### 5.6 Censorship / Completion (Priority 2)

| # | Chart | Variables | Description |
|---|-------|-----------|-------------|
| 18 | **Survival curve** | step function: runs remaining vs `turn_count`, by `arm` | How many runs are still active at each turn? Crossing curves suggest censorship bias. |
| 19 | **Bar chart** | completion rate (% submitted) × `arm` | Pilot criterion: ≥90% expected. |

### 5.7 Cost / Overhead (Priority 3)

| # | Chart | Variables | Description |
|---|-------|-----------|-------------|
| 20 | **Boxplot** | `total_completion_tokens` × `arm` | Model output cost. |
| 21 | **Boxplot** | `total_reasoning_tokens` × `arm` | Thinking-mode overhead (not visible to agent). |
| 22 | **Scatter** | `total_cached_tokens` vs `total_input_tokens`, color = `arm` | Cache hit rate. Points above diagonal = high cache utilization. |

### 5.8 Inferential Visualizations (Priority 2)

| # | Chart | Variables | Description |
|---|-------|-----------|-------------|
| 23 | **Bootstrap CI plot** | median difference in `peak_prompt_tokens` (treatment - control), histogram of bootstrap distribution + 95% CI | Primary inferential result. |
| 24 | **Bootstrap CI plot** | median difference in `total_input_tokens` | Secondary token metric. |

### 5.9 Summary Table (Priority 1)

| # | Output | Description |
|---|--------|-------------|
| 25 | **Text table** | Per-metric median (both arms), raw difference, ratio, Mann-Whitney U statistic + p-value, bootstrap 95% CI for median difference. Printed to stdout. |

## 6. Implementation Notes

### 6.1 Dependencies

- `scipy.stats` — `mannwhitneyu`, `bootstrap` (Python 3.8+)
- `numpy` — arrays, percentiles
- `matplotlib` — all charts
- `pandas` — read CSV, groupby, aggregation
- Pure stdlib: `csv`, `statistics` (fallback if scipy unavailable)

### 6.2 Clustered Bootstrap

For global statistics, resample **instances** (not individual runs) to preserve
the within-instance correlation structure. Each bootstrap iteration: resample 20
instances with replacement, pool their 10 control + 10 treatment runs, compute
the statistic. 10,000 iterations.

### 6.3 Output Organization

```
analysis/<timestamp>/
├── runs_with_success.csv       # input (from merge_results.py)
├── stats_summary.txt           # text table of all statistics
├── charts/
│   ├── 01_boxplot_total_input.png
│   ├── 02_boxplot_peak_prompt.png
│   ├── 03_boxplot_tool_output.png
│   ├── 04_violin_peak_prompt.png
│   ├── 05_hist_peak_prompt.png
│   ├── 06_ecdf_peak_prompt.png
│   ├── 07_paired_dot_peak_prompt.png
│   ├── 08_forest_plot.png
│   ├── 09_heatmap.png
│   ├── 10_bar_success_rate.png
│   ├── 11_stacked_exit_status.png
│   ├── ...
│   └── 25_summary_table.txt
└── stats.json                  # machine-readable stats for downstream use
```

### 6.4 CLI

```bash
python3 experiment/analysis/analyze_stats.py \
    --dir experiment/analysis/session-01 \
    --charts-dir experiment/analysis/session-01/charts \
    --bootstrap-iters 10000
```

### 6.5 Graceful Degradation

If `scipy` is not installed, skip inferential tests (Mann-Whitney, bootstrap
CIs) and print a warning. Descriptive statistics and charts should still work.
If `matplotlib` is not installed, skip charts but produce the text summary.
Neither should be a hard dependency.

## 7. Open Questions

1. **Per-repo grouping** — The repo isn't directly in the CSV but is derivable
   from `instance_id` (everything before the first `__`). Worth adding as a
   derived column?
2. **Chart format** — PNG vs SVG? SVG scales better for publications. PDF for
   LaTeX inclusion?
3. **Single script vs. notebook** — A script is reproducible and CI-friendly.
   A notebook is interactive for exploration. The plan assumes a script.
4. **Success-adjusted analysis** — How to handle the "treatment succeeds more
   → longer runs → more tokens" confound beyond filtering to successful-only?
   Propensity score matching on turn count? Probably overkill for the pilot.
