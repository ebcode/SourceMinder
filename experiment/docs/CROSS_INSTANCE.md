# Cross-Instance Comparison

`cross_batch_compare.py` pools per-batch Pro results across instances into a
single cross-instance view (forest plots + a pooled meta-estimate). **Status:
built** (`experiment/analysis/cross_batch_compare.py`). This doc is both the
rationale and the usage reference.

Each Pro batch is one instance × N reps per arm (see `PRO_ANALYZE.md`). A single
batch can only describe within-instance variance on one task. The qi hypothesis
("qi saves tokens, never hurts resolve") is a claim *across* instances, so the
evidence has to be assembled at the instance level. This doc fixes how.

## Why not just pool the raw numbers

The control-arm token scale spans **~11×** across our seven instances:

| instance | model | control median total-tokens |
|---|---|---|
| openlibrary | haiku-4.5 | 463K |
| webclients | haiku-4.5 | 981K |
| qutebrowser | haiku-4.5 | 1.55M |
| ansible | haiku-4.5 | 2.04M |
| flipt | haiku-4.5 | 3.19M |
| nodebb | haiku-4.5 | 4.31M |
| tutanota | sonnet-5 | 5.04M |

A naive Mann-Whitney on pooled raw values is dominated by bug size, not by the
treatment (the 4.3M nodebb reps drown the 463K openlibrary reps). The prior
power simulation (`tmp/meta_power_demo.py`) showed this gets ~0% power, while a
meta-analysis on per-instance **log-ratios** reaches ~86% at n=5/arm. So the
unit of analysis is the per-instance effect, normalized to remove bug size.

## Effect measure: log-ratio of medians

For each instance and each continuous metric (turns, input tokens, total tokens,
cost, wall time):

```
effect_i = ln( median_treatment_i / median_control_i )
```

- Symmetric: a 2× increase (+0.69) and a 2× decrease (−0.69) are mirror images,
  unlike raw percent change.
- Scale-free: removes the bug-size confound — every instance contributes on the
  same axis regardless of its native token magnitude.
- Negative effect = treatment used **less** (the hoped-for direction for
  tokens/cost/turns/wall time).
- Charts relabel the axis to percent (`exp(effect) − 1`) for readability; the
  math stays on the log scale.

### Per-instance CI (bootstrap)

Reuse the bootstrap already in `analyze_pro_stats.py` (`boot_median_diff`),
adapted to the log-ratio: for `B` iterations, resample reps with replacement
within each arm, compute `ln(median_T* / median_C*)`, and take the 2.5/97.5
percentiles for the CI and the bootstrap **sd** as the per-instance standard
error `se_i` (needed for pooling).

> **n=5 caveat.** With 5 reps/arm the median bootstrap is coarse — only a few
> distinct resample medians exist, so per-instance CIs are wide and lumpy. They
> are honest, not precise; the pooled estimate is where precision comes from.

### Pooled meta-estimate (inverse-variance)

```
w_i      = 1 / se_i^2
pooled   = Σ(w_i · effect_i) / Σ(w_i)
se_pool  = sqrt( 1 / Σ(w_i) )
95% CI   = pooled ± 1.96 · se_pool         (back-transform with exp for %)
```

Optional heterogeneity readout (Cochran's Q, I²):

```
Q   = Σ w_i (effect_i − pooled)^2          df = k − 1
I²  = max(0, (Q − df) / Q)
```

High I² (say >50%) is itself a finding — it means the qi effect size genuinely
varies by instance/model, and the single pooled number understates the spread.

## Inputs

The script consumes already-computed per-batch artifacts — it does **not**
re-parse trajectories. Per batch dir (`results/pro_runs/<batch>/`):

| File | Used for |
|---|---|
| `runs_with_success.csv` | per-rep metric values (the bootstrap operates on these) |
| `pro_stats_summary.csv` | sanity cross-check + resolve/blowup rates |
| `wall_time.csv` *(optional)* | per-rep `duration_sec`, joined by `(arm, rep)` |
| `eval_results.csv` | per-rep `resolved` for the safety panel |

Model and instance id are read from `runs_with_success.csv`. wall_time is
present on all 7 canonical batches, so the duration forest is 7/7; the join
self-skips any rep with no `wall_time.csv` entry, so a future batch missing it
simply drops from that one forest.

## The script: `cross_batch_compare.py`

```bash
# Canonical 5-instance run via the manifest:
experiment/.venv_pro/bin/python experiment/analysis/cross_batch_compare.py \
    --manifest experiment/analysis/cross_instance_manifest.txt \
    --out experiment/results/pro_runs/_cross/

# Ad-hoc subset via repeated --batch:
experiment/.venv_pro/bin/python experiment/analysis/cross_batch_compare.py \
    --batch experiment/results/pro_runs/pro_pilot_webclients_ds_v4_pro \
    --batch experiment/results/pro_runs/pro_pilot_qutebrowser_haiku_v1 \
    --out experiment/results/pro_runs/_cross/
```

CLI:
- `--batch DIR` (repeatable) — a batch result dir, for ad-hoc subsets.
- `--manifest FILE` — file listing dirs, one per line (`#` comments allowed),
  paths relative to the manifest. The standing 5 live in
  `experiment/analysis/cross_instance_manifest.txt`. `--batch` and `--manifest`
  combine; at least one is required.
- `--out DIR` — where `cross_instance.csv` and `charts/` land (default
  `experiment/results/pro_runs/_cross/`).
- `--metrics turn_count total_tokens cost duration_sec` — override the metric set
  (this is the default).
- `--bootstrap-iters` / `--seed` — mirror `analyze_pro_stats.py` defaults
  (10000 / 42) for reproducibility.
- `--no-charts` — CSV only.

Stdlib + matplotlib only, matching the other analysis scripts (no scipy/numpy).
Shared helpers (`fnum`, `wilson`, `load_wall_times`, arm constants) are imported
from `analyze_pro_stats.py` rather than copied. The log-ratio bootstrap is a
different statistic than `boot_median_diff`, so `log_ratio_effect()` is local.

## Outputs

### `cross_instance.csv`

One row per (instance, metric):

| column | meaning |
|---|---|
| `instance` | short instance label (e.g. `qutebrowser`) |
| `model` | model slug (annotation / color key) |
| `metric` | `total_tokens`, `cost`, `duration_sec`, … |
| `n_control`, `n_treatment` | reps per arm |
| `control_median`, `treatment_median` | native-unit medians |
| `effect_logratio` | `ln(med_T/med_C)` |
| `ci_lo`, `ci_hi` | bootstrap 95% CI on the log scale |
| `se` | bootstrap sd (pooling weight input) |
| `pct_change` | `exp(effect) − 1`, for human reading |
| `format_tax` | bool — true for instances with a per-turn format tax (none currently in the canonical manifest) |

Plus a pooled row per metric (`instance = __POOLED__`) carrying `pooled`,
`se_pool`, the back-transformed % and CI, `k` (instances), `Q`, and `I2`.

### Charts (`charts/`)

1. **`forest_total_tokens.png`** *(primary)* — one row per instance: effect dot
   + bootstrap CI whisker, sorted alphabetically. A pooled diamond at the bottom
   (inverse-variance) labeled with `k` and `I²`. Vertical line at 0 (no effect);
   x-axis labeled in %. The format-tax footnote is conditional — it only appears
   when the manifest contains taxed instances (currently none).
2. **`forest_cost.png`** — same, for cost.
3. **`forest_duration_sec.png`** — same, for wall time (all 7 instances).
4. **`forest_turn_count.png`** — same, for turns (tax-immune, no footnote).
5. **`resolve_dumbbell.png`** — one row per instance, control vs treatment
   resolve rate as a dumbbell (two dots + connector), sorted alphabetically by
   instance label. This is the "never hurts" safety panel; kept separate from
   the efficiency forests.
6. **`cumulative_cost.png`** — small-multiples (one panel per instance, sorted by
   label, shared y-axis): cost summed across reps, control grey vs treatment
   blue. Shows the running spend each arm accrues over a rep batch — the widening
   (or non-widening) gap is the per-instance cost story the forest compresses to
   a single ratio. Skipped if no batch carries cost data.
7. **`search_output.png`** — treatment-arm qi vs grep output tokens per call
   (side-by-side boxplots, log x-axis), one row per instance: qi returns compact
   results, grep returns noisy output.
8. **`log_size_range.png`** — per-instance .log file size range bars (control vs
   treatment min–max span), showing treatment's consistently tighter log output.
9. **`explore_calls.png`** — normalized cross-instance stack (control total =
   100%) of exploration *call counts* by tool (qi / grep / cat / sed-read /
   mixed), usage-detected (not partitioned — see `NEW_QI_VS_GREP_CAT_STORY_IN_CHARTS.md`
   §11 for why partitioning broke on test-execution output).
10. **`explore_tokens.png`** — same stack, on homogeneous-action token totals
    instead of call counts: which tool's *output* dominates context, not just
    which tool got called.
11. **`radar_efficiency.png`** *(hero)* — pooled efficiency radar (geometric
    mean of per-instance treatment/control ratios): grey control baseline (100%)
    enclosing the treatment pentagon across log size, log variance, turns, patch
    lines, and grep+cat calls. A second, tighter polygon scopes to the
    "qi good-fit" subset (`RADAR_NON_SOURCE` excludes instances whose gold patch
    is mostly non-source content qi can't index). Solid spokes = outcome axes;
    the dashed grep+cat spoke = the mechanism axis. Single-instance analog:
    `analyze_pro_stats.chart_radar`.

`qi_grep.png` (grep-only-vs-grep+qi framing) is **retired**: `qi_grep_chart`'s
call is commented out in `cross_batch_compare.py`, superseded by
`explore_calls.png`/`explore_tokens.png`, which account for cat/sed-read too.

## Heterogeneity handling (decided)

- **Pool all 7 instances** on every efficiency forest. Six run Haiku-4.5; the
  seventh (tutanota) runs Sonnet-5, deliberately folded into the canonical
  manifest — the prior single-model discipline was about avoiding *format-tax*
  contamination (DeepSeek/MiMo), not about model family per se, and I² stays
  ~0% with Sonnet-5 included (see below), so the pooled estimate isn't being
  distorted by the mix.
- **Format-tax flagging is now self-managing.** `FORMAT_TAX_INSTANCES` in
  `cross_batch_compare.py` is `set()`; the chart footnote reads taxed labels from
  the batch set and self-hides when empty. If a format-tax batch is added to the
  manifest in the future, the footnote will re-appear automatically with the
  correct instance names.
- **I² is 0% on turns/tokens/wall-time and 1% on cost** with the current
  6-Haiku + 1-Sonnet-5 manifest — no meaningful heterogeneity, so a
  model-stratified diamond is not needed. Re-check this if a second non-Haiku
  instance is added; one mixed-model point passing this test doesn't guarantee
  the next one will.

## Gotchas

- **Single-instance origin.** Every input batch is one task. Cross-instance
  pooling buys generalizability across *tasks/repos*, but k=7 instances is still
  small — read the pooled estimate as direction + rough magnitude, not proof.
- **wall_time coverage is 7/7.** All canonical batches include `wall_time.csv`.
  A *future* batch with no `wall_time.csv` simply drops from that one forest
  (the join self-skips) — not an error.
- **No format tax in the current canonical set.** The prior 2/5 tax-contaminated
  instances (MiMo ansible, DeepSeek webclients) were replaced with Haiku batches
  in `20260630_032733`. If a taxed batch is ever re-added to the manifest, the
  footnote will re-appear automatically.
- **n=5 bootstrap medians are lumpy.** Per-instance CIs will look coarse; this is
  expected and is why the pooled diamond carries the inferential weight.
- **Resolve is near-ceiling but not universally.** Five of seven instances sit at
  5/5 in both arms. `nodebb` (4/5 control, 3/5 treatment) and `flipt` (4/5
  control, 3/5 treatment) sit below ceiling in *both* arms — a pre-existing task
  difficulty, not a treatment-caused regression. The dumbbell is descriptive;
  don't over-test a near-degenerate column.
- **Mixed model family.** `tutanota` runs Sonnet-5 while the other six run
  Haiku-4.5; the `model` column in `cross_instance.csv` records this per row so
  a future model-stratified cut is possible without re-running.
- **Instance label derivation.** Instance ids look like
  `instance_protonmail__webclients-<sha>`; the short label strips the prefix,
  the org, and the sha. Keep the mapping in one helper so labels are stable
  across charts and CSV.

## Decisions (formerly open questions)

1. **Manifest + repeated `--batch` (both).** The canonical 7 live in
   `experiment/analysis/cross_instance_manifest.txt`; `--batch` is still accepted
   and combines with `--manifest` for ad-hoc subsets.
2. **Outputs live in `results/pro_runs/_cross/`.** Nested under `pro_runs/` so all
   Pro outputs share one tree (chosen over a separate `results/cross_instance/`).
3. **Metrics = turns + tokens + cost + wall.** Four forests; turns added (cheap,
   tax-immune). patch-size left out — noisier and off-thesis.
4. **Single pooled estimate; format-tax flagging is self-updating.** The
   `format_tax` column + conditional chart footnote carry the caveat when needed;
   currently the canonical set has no taxed instances. No separate sensitivity
   diamond (the CSV carries `model` + `format_tax`, so one can be added later
   without re-running).

## See Also

- `STATISTICAL_METHODS.md` — the canonical methods reference: single-instance
  vs cross-instance regimes, why raw pooling fails, the power rationale
- `pro_batch_status.py` — one-command status dashboard over all `pro_runs/*`
  batches (model, instance, reps, resolve, token/cost Δ, wall, tax, charts);
  marks this manifest's batches with `*`, or `--manifest` to restrict to them.
  Use it to keep this manifest honest as batches are added/renamed.
- `PRO_ANALYZE.md` — per-batch pipeline that produces the inputs (esp. the
  *Format tax* gotcha)
- `tmp/meta_power_demo.py` — the simulation behind the meta-analysis power claim
- `analyze_pro_stats.py` — source of the shared bootstrap/label/color helpers
