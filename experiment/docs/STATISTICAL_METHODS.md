# Statistical Methods for the qi Experiment

The canonical reference for how this experiment turns raw run data into claims.
It exists because the rationale behind each test was being re-derived
interactively every analysis session. Read this once; cite it instead of
re-arguing.

The qi hypothesis has two parts, and they need two different statistics:

1. **qi saves tokens/turns/cost** — a claim about a continuous metric's central
   tendency. Tested per-instance (Mann-Whitney) and across instances (meta).
2. **qi never hurts resolve** — a safety claim about a near-degenerate binary
   rate. Tested descriptively (Wilson CI + dumbbell), never over-tested.

## TL;DR decision tree

```
Are you analyzing ONE instance (one batch = one task × N reps/arm)?
│
├─ continuous metric (tokens, turns, cost, wall time)?
│     → median + IQR per arm
│     → Mann-Whitney U (mannwhitney_u)          ... is the shift real?
│     → bootstrap 95% CI on median diff (boot_median_diff) ... how big?
│     → DO NOT read the p-value as proof. n=5/arm is descriptive.
│
├─ resolve / pass rate (binary)?
│     → Wilson CI (wilson). Report k/n + interval. Don't run a test on 5/5.
│
└─ tail behavior (crashes, empty patches, limit-exceeded)?
      → blow-up RATE (is_blowup). The median can't see one tail event;
        report the rate explicitly.

Are you combining MULTIPLE instances into one qi claim?
│
└─ → meta-analysis on per-instance LOG-RATIOS (cross_batch_compare.py)
     → NEVER pool raw values across instances (bug-size confound → ~0% power)
     → inverse-variance pooled diamond + Cochran's Q / I² for heterogeneity
     → see CROSS_INSTANCE.md for the usage reference
```

---

## The unit-of-analysis problem (read this first)

Every batch is **one instance × N reps per arm**. A single batch can only
describe *within-instance* variance on one task. But the qi hypothesis is a
claim *across* tasks and models. So there are two regimes, and the most common
mistake is using the wrong one:

| Regime | Question | Unit | Test |
|---|---|---|---|
| Single-instance | "Did qi help *on this task*?" | the rep | Mann-Whitney + bootstrap CI |
| Cross-instance | "Does qi help *in general*?" | the instance | log-ratio meta-analysis |

You cannot answer the cross-instance question by pooling reps from all
instances into one big Mann-Whitney. That is the single biggest statistical
trap in this project, and the next section is why.

---

## Why you can't pool raw values across instances

The control-arm token scale spans **16×** across the five canonical instances:

| instance | model | control median total-tokens |
|---|---|---|
| openlibrary | haiku-4.5 | 463K |
| webclients | deepseek-v4-pro | 975K |
| qutebrowser | haiku-4.5 | 1.55M |
| nodebb | haiku-4.5 | 4.31M |
| ansible | mimo-v2.5-pro | 7.64M |

A naive Mann-Whitney on pooled raw token values is dominated by **bug size, not
treatment**: the 7.6M ansible reps drown out the 463K openlibrary reps. The
treatment effect (a ~30% within-instance shift) is invisible next to a 16×
between-instance spread.

`tmp/meta_power_demo.py` quantifies this with 10,000 simulations of a *true* 30%
savings effect (TRUE_RATIO = 0.70):

| Method | Power (one-sided, 5%) |
|---|---|
| Naive Mann-Whitney on pooled raw values | **~0%** |
| Per-instance log-ratio → inverse-variance meta | **~78–86%** at n=5/arm |

The naive test has essentially no power to detect a real effect, because the
between-instance variance swamps the signal. The fix is to **normalize per
instance first**, then pool the normalized effects.

---

## Single-instance methods (per batch)

Implemented in `experiment/analysis/analyze_pro_stats.py`. These describe one
task; treat the p-values as descriptive flags, not proof — n=5/arm is small by
design (instances buy generalizability, reps don't — see power note below).

### Central tendency: median + IQR

Use the **median**, not the mean. Token/turn distributions are right-skewed
(one thrashing run can 3× the mean) and a single tail event shouldn't move the
reported center. `iqr()` reports the 25th/75th percentiles as the spread.

### Mann-Whitney U — `mannwhitney_u(a, b)`

Two-sided rank-sum test with a normal approximation and **tie correction**
(important: token counts and especially turn counts have ties). Returns
`(U, z, p)`. It asks *"is treatment shifted vs control?"* without assuming
normality — correct for skewed, small samples.

What it is **not**: proof. At n=5/arm the test is underpowered for anything but
a large within-instance shift. Report it as a flag ("p=0.03, treatment lower"),
and let the cross-instance meta carry the inferential weight.

### Effect size: bootstrap CI — `boot_median_diff(a, b)`

The p-value says *whether*; the bootstrap says *how much*. Resample reps with
replacement within each arm (default 10,000 iters, seed 42), recompute
`median(T*) − median(C*)`, take the 2.5/97.5 percentiles. This is the honest
effect-size statement: "treatment used X fewer tokens (95% CI [lo, hi])."

> **n=5 caveat.** With 5 reps the median bootstrap is **coarse** — only a few
> distinct resample medians exist, so CIs are wide and lumpy. They are honest,
> not precise. Precision comes from pooling across instances, not from any one
> batch.

### Resolve rate: Wilson interval — `wilson(k, n)`

For the binary "did the patch resolve" rate, use the **Wilson** score interval,
not the normal (Wald) approximation — Wald is badly behaved near 0/1 and n=5,
which is exactly our regime (most instances sit at 5/5 or 4/5). Report `k/n` and
the interval. **Do not run a significance test** on a near-ceiling binary with
n=5; it is descriptive. This is the "never hurts" safety panel.

### Tail events: blow-up rate — `is_blowup(row)`

The median is blind to a single catastrophic run. A blow-up is the failure mode
qi is *meant to curb*: a crash (non-`Submitted`/`Completed` exit), a
limit-exceeded run, or a run that thrashed and submitted an empty patch. The
median of 5 reps can't show one such tail event, so report its **rate**
explicitly (`CLEAN_EXITS = {"Submitted", "Completed"}`; empty patch also counts
as a blow-up). A treatment that trades a hair more median tokens for fewer
blow-ups is still a qi win — and only the blow-up rate makes that visible.

---

## Cross-instance method (meta-analysis)

Implemented in `experiment/analysis/cross_batch_compare.py`; full usage and
chart reference in **`CROSS_INSTANCE.md`**. Summarized here so the *method*
lives with the other methods.

### Effect measure: log-ratio of medians

For each instance × metric:

```
effect_i = ln( median_treatment_i / median_control_i )
```

- **Scale-free** — removes the bug-size confound; every instance contributes on
  the same axis regardless of native token magnitude.
- **Symmetric** — a 2× increase (+0.69) and a 2× decrease (−0.69) are mirror
  images, unlike raw percent change.
- **Sign convention** — *negative* effect = treatment used **less** (the
  hoped-for direction for tokens/cost/turns/wall time).
- Charts relabel the axis to percent (`exp(effect) − 1`) for reading; the math
  stays on the log scale.

### Per-instance SE: bootstrap on the log-ratio

Same bootstrap machinery as `boot_median_diff`, adapted to the statistic
`ln(median_T* / median_C*)`. Take the percentile CI **and** the bootstrap
standard deviation as the per-instance standard error `se_i` (needed for
pooling). The n=5 lumpiness caveat applies here too — per-instance whiskers look
coarse; that's expected.

### Pooling: inverse-variance

```
w_i      = 1 / se_i²
pooled   = Σ(w_i · effect_i) / Σ(w_i)
se_pool  = sqrt( 1 / Σ(w_i) )
95% CI   = pooled ± 1.96 · se_pool        (back-transform with exp for %)
```

Each instance is weighted by its precision; noisy instances contribute less.
The pooled SE shrinks as ~`1/√k`, which is the whole source of the meta's power
(see below). This is the **pooled diamond** at the bottom of each forest plot.

### Heterogeneity: Cochran's Q and I²

```
Q   = Σ w_i (effect_i − pooled)²          df = k − 1
I²  = max(0, (Q − df) / Q)
```

High **I²** (say >50%) is itself a finding: the qi effect genuinely *varies* by
instance/model, and the single pooled number understates the spread. Don't hide
heterogeneity — report it, and color forests by model so model-driven spread is
visible at a glance.

---

## Why instances beat reps (power)

From `meta_power_demo.py`, the three mechanisms that give 5 instances × 5 reps
its ~78% power:

1. **Normalizing per-instance removes the bug-size confound.** Without it, a
   500K-token instance's savings get drowned by a 10M-token instance's noise.
2. **Meta SE scales as `1/√k`.** Five instances cut noise ~2.2× vs a single
   instance. Adding 2 more instances (→7) or bumping reps to 10 lifts power to
   ~91%.
3. **Treatment has a smaller CV than control** (qi constrains the search
   pattern), so the per-instance ratio estimate is cleaner than raw values.

**Practical consequence: spend your budget on more instances, not more reps.**
Going from 5→7 instances helps more than 5→10 reps, because power is governed by
`k` (the meta SE) far more than by within-instance n. A rough planning table:

| Target | Design |
|---|---|
| 20% savings, 80% power | ~5 instances × ~10 reps |
| 30% savings, 90% power | ~5 instances × ~15 reps, *or* ~7 instances × 5 reps |

Re-run `tmp/meta_power_demo.py` to regenerate these for a specific effect size;
the table above is the simulation's ballpark, not a guarantee.

---

## Format tax (a confound to flag, not drop)

Two instances paid a per-turn **format tax** (wasted turns on
`reasoning_content`/XML parsing — see `PRO_ANALYZE.md` → *Format tax*):

- **ansible** (mimo-v2.5-pro) and **webclients** (deepseek-v4-pro).

Their **token/cost** efficiency is inflated relative to the haiku instances, so
on the token and cost forests they carry a `format_tax` flag and a chart
footnote. They keep full inverse-variance weight (flagged, not excluded). Their
**wall-time and resolve** are tax-immune and need no flag. Do **not** read
ansible/webclients token savings as like-for-like with the haiku instances.

---

## When to filter to resolved-only

Token/turn counts mix two populations: runs that **solved** the task and runs
that **gave up or thrashed**. A run that bailed at turn 3 looks "cheap" but
didn't do the job. For an efficiency claim, filter to **resolved runs** so
you're comparing the cost of *success*, not the cost of *quitting*. For the
safety claim (resolve rate, blow-up rate) you obviously keep everyone. State
which population a number describes — "median tokens among resolved runs" is a
different claim than "median tokens over all runs."

---

## Common mistakes (the checklist)

- **Pooling raw values across instances.** ~0% power. Use log-ratio meta.
- **Reading a single-instance p-value as proof.** n=5/arm is descriptive; the
  meta carries the inference.
- **Mean instead of median** on skewed token/turn data — one tail run distorts
  it.
- **Wald CI on a near-ceiling resolve rate.** Use Wilson.
- **Significance-testing a 5/5 vs 5/5 resolve column.** It's near-degenerate;
  report it descriptively (dumbbell).
- **Ignoring blow-ups** because the median looks fine — report the rate.
- **Treating ansible/webclients token savings as like-for-like** — format tax.
- **Comparing all-runs tokens to resolved-only tokens** — state the population.
- **Adding reps to gain power** when adding instances is far more efficient.

---

## See Also

- **`CROSS_INSTANCE.md`** — usage + chart reference for `cross_batch_compare.py`
  (the cross-instance side of this doc).
- **`PRO_ANALYZE.md`** — per-batch pipeline producing the inputs; the *Format
  tax* gotcha.
- **`analyze_pro_stats.py`** — `mannwhitney_u`, `boot_median_diff`, `wilson`,
  `is_blowup`, `iqr`, shared label/color helpers.
- **`cross_batch_compare.py`** — `log_ratio_effect()` + inverse-variance pooling.
- **`tmp/meta_power_demo.py`** — the simulation behind the power claims.
</content>
</invoke>
