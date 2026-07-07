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

The unit of analysis is the **instance**, not the run — the 20 instances are
independent, the 400 runs are not (10 correlated reps per arm per instance).
Every inferential test therefore operates on per-instance summaries, never on
pooled runs. (See §4.4.)

| Test | Metric | Notes |
|------|--------|-------|
| Wilcoxon signed-rank (two-sided) | peak_prompt_tokens, total_input_tokens, tool_output_tokens_approx | **Primary token test.** Paired on the 20 per-instance median differences (treatment − control). Replaces a pooled Mann-Whitney U, which would treat 400 correlated runs as independent. |
| Bootstrap 95% CI for median difference | All three token metrics | **Primary effect size.** Clustered: resample the 20 instances, recompute the difference of pooled medians. 10,000 resamples, percentile method. Raw difference is the lead statistic. |
| Bootstrap 95% CI for median ratio | All three token metrics | Secondary framing (treatment / control). Tokens are strictly positive so the ratio is well-defined; report it as derived from the difference, not as the headline. |
| Two-proportion comparison | task_success | Clustered bootstrap on the per-instance success rates (resample instances), or McNemar/paired framing on instance success counts. A plain two-proportion z-test would ignore the same clustering the token tests respect — do not use it as the headline. Label descriptive at pilot N. |

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
Compared with the paired Wilcoxon signed-rank test (on per-instance median
differences) and clustered bootstrap CIs (§1.4).

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

### 3.6 Successful-Runs Lens (Descriptive Only)

"Among runs that actually resolved the issue, how do the token costs compare?"

Filter to `task_success = 1` runs and report token descriptives by arm.

**This is descriptive, not an adjustment.** Filtering on `task_success` conditions
on a *post-treatment outcome* (a collider): treatment can change both the success
rate and which runs succeed, so the successful-only subgroup is not a fair
like-for-like comparison and can *introduce* bias rather than remove it. It does
not "control for" the success confound. The real defense against that confound is
the paired within-instance design (§2.2, §4.1), not outcome-filtering. Report this
lens as a descriptive sidebar, clearly labeled, and never as the primary effect.
At pilot N it is often near-empty (e.g. a handful of successes per arm) — annotate
the subgroup sizes on every chart.

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
inefficient. The real defense is the **paired within-instance design** (§2.2):
compare arms instance-by-instance, where the same task difficulty is held fixed.
The successful-runs lens (§3.6) is a descriptive sidebar only — filtering on
`task_success` conditions on a post-treatment collider and does *not* adjust for
this confound. The safest interpretation: "for the same instance, treatment uses
[more/less] tokens."

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

**Build Priority 1 first.** The pilot's job is variance and effect-size
estimation to power the confirmatory study, so the minimal useful deliverable is
the summary table (#25), the two bootstrap-CI plots (#23/#24), the per-instance
forest plot (#8), and the Priority-1 distribution/success charts. Defer
Priority-2/3 charts until the data justifies them — at current scale (2
instances, treatment unevaluated) most of the 25 charts would render near-empty.
Charts are **PNG** for the pilot (switch to SVG/PDF only at publication).

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

### 5.5 Successful-Runs (Descriptive, Priority 2)

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
| 25 | **Text table** | Per-metric median (both arms), raw difference (lead), ratio (derived), paired Wilcoxon signed-rank statistic + p-value, clustered bootstrap 95% CI for median difference. Printed to stdout. |

## 6. Implementation Notes

### 6.1 Dependencies

- `scipy.stats` — `wilcoxon`, `bootstrap` (Python 3.8+)
- `numpy` — arrays, percentiles
- `matplotlib` — all charts
- `pandas` — read CSV, groupby, aggregation
- Pure stdlib: `csv`, `statistics` (fallback if scipy unavailable)

### 6.2 Clustered Bootstrap

For global statistics, resample **instances** (not individual runs) to preserve
the within-instance correlation structure. Each bootstrap iteration: resample 20
instances with replacement, pool their 10 control + 10 treatment runs, compute
the statistic. 10,000 iterations. This applies to **both** the token-difference
CIs *and* the success-rate comparison (§1.4) — success is clustered within
instance just like tokens, so its CI must be clustered too. The paired Wilcoxon
signed-rank (§1.4) operates on the 20 per-instance median differences directly
and needs no resampling.

### 6.3 Derived `repo` Column

Add `repo = instance_id.split("__", 1)[0]` as a derived column at load time
(e.g. `astropy__astropy-14369` → `astropy`). Drives the per-repo grouping (§2.3)
without requiring any change to the upstream CSVs.

### 6.3a `model` Is a Grouping Dimension

`runs_with_success.csv` now carries a `model` column (logs are stored under
`logs/<model>/<arm>/...`; `analyze_trajectories.py` and `evaluate_patches.py`
derive it from the path, and it is part of the eval-DB primary key and the
merge join key). Arms are only comparable **within** a model. Default to the
single model present in the CSV; if more than one is present, group by `model`
first (separate summary tables / chart sets per model) and never pool runs
across models. The paired within-instance design (§2.2) is per `(model,
instance)`.

### 6.4 Output Organization

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

### 6.5 CLI

```bash
python3 experiment/analysis/analyze_stats.py \
    --dir experiment/analysis/session-01 \
    --bootstrap-iters 10000
```

Charts default to `<dir>/charts/`; `stats_summary.txt` and `stats.json` go in
`<dir>/`. No separate `--charts-dir` — it's derived from `--dir`.

### 6.6 Graceful Degradation

If `scipy` is not installed, skip inferential tests (Wilcoxon signed-rank,
bootstrap CIs) and print a warning. Descriptive statistics and charts should
still work. If `matplotlib` is not installed, skip charts but produce the text
summary. Neither should be a hard dependency.

## 7. Resolved Decisions

1. **Per-repo grouping — YES.** Derive `repo = instance_id.split("__", 1)[0]`
   as a column at load time (§6.3). Drives the per-repo lens (§2.3).
2. **Chart format — PNG for the pilot.** Switch to SVG/PDF only at publication.
3. **Script, not notebook.** Reproducible and CI-friendly, matches the rest of
   the pipeline. Closed.
4. **Confound handling — paired within-instance design, not outcome-filtering.**
   The success confound is addressed by comparing arms instance-by-instance
   (§2.2, §4.1), where task difficulty is held fixed. The successful-runs lens
   (§3.6) is a descriptive sidebar only. No propensity matching — it would itself
   condition on a post-treatment variable and is overkill for the pilot.
