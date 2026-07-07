# compare_models.py — Design Plan

Cross-model analysis layer for the SourceMinder experiment. Where
`analyze_stats.py` analyzes one model in isolation (arms compared *within* a
model), `compare_models.py` compares the **treatment effect across models** —
e.g. Claude Haiku 4.5 vs DeepSeek V4 Flash run on the *same* instance sample.

It is **additive**: it imports `analyze_stats.py` as a library and reuses its
loaders and per-instance machinery rather than re-deriving them. There is no new
CSV parsing, no new `n_files` join, no second copy of the per-instance pivot —
those live in one place to avoid the copy-paste drift this codebase has already
been bitten by (the `infer_arm_instance` → `infer_path_meta` episode).

> **Status:** IMPLEMENTED (2026-06-18) as `analysis/compare_models.py`, tested
> against the combined Haiku + DeepSeek two-model CSV. This doc is the
> authoritative design; it superseded `CROSS_MODEL_PLAN.md` (which compared raw
> token Δ across models — invalid across tokenizers, see §1.3). Build deltas from
> this spec: charts land under `charts/cross_model/`; a fifth chart
> (`05_mechanism_cross_model.png`) and a per-instance %change table with
> direction-agreement counts were folded in from the superseded plan; outputs are
> `model_comparison_summary.txt` + `model_comparison.json`; `--csv` is repeatable
> for the multi-CSV concat fallback (§2.2); §3.5 (model×arm paired test) was kept
> descriptive per open question #2.

---

## 1. Scope and framing

### 1.1 What this script is for

The single most valuable thing a second model buys us is **generalization**: is
"qi helps, and helps more on larger instances" (Haiku gave Spearman
rho ≈ −0.78 for peak/total-input tokens) a property of the **tool × task
structure**, or an artifact of one model? Running the *same* instances on a
second model lets us answer that by pairing per-instance effects across models.

### 1.2 What this script is NOT

- **Not a meta-analysis.** With k=2 models you cannot estimate between-study
  heterogeneity (τ²); formal random-effects pooling is meaningless at k=2. This
  is **side-by-side replication + cross-model consistency**, and the prose/labels
  must say so. (Same discipline as not calling the pooled −2.5% "qi's effect".)
- **Not a head-to-head model benchmark.** We are not claiming "Haiku is better
  than DeepSeek." The experiment isn't designed for that (different APIs,
  pricing, default behaviors).

### 1.3 The load-bearing methodological rule

**Compare effects (ratios / % change), never raw token counts, across models.**
Different tokenizers plus the ~4 chars/token approximation
(`tool_output_tokens_approx`) make absolute token counts non-comparable between
models. The quantity that *travels* is the normalized within-model effect — the
treatment/control **ratio** or **% change** — because the tokenizer cancels.
Every cross-model statistic in this script operates on ratios/percentages, not
on `total_input_tokens` etc. directly. Raw-token comparison across models is
explicitly out of scope (§6).

### 1.4 Unit of analysis

The within-model unit stays the **instance** (as in `analyze_stats.py` §4.4).
The cross-model unit is the **per-(model, instance) effect**: for each model we
compute a per-instance % change (treatment vs control), then pair those across
models on `instance_id`. Cross-model paired analyses use only the instance
intersection (instances run on *both* models); dropped instances are reported.

---

## 2. Input

### 2.1 Preferred: one combined `runs_with_success.csv`

`analyze_trajectories.py` already walks `logs/<model>/<arm>/<instance>/` and
emits a `model` column; `merge_results.py` keys on `(model, arm, instance, rep)`.
So the clean path, once both runs exist, is to regenerate the pipeline pointed
at the **logs root** (which contains both model dirs):

```bash
d=results/runs/<new_ts>
.venv/bin/python analysis/analyze_trajectories.py --logs logs --dir "$d"
.venv/bin/python analysis/evaluate_patches.py --logs logs --dir "$d"   # if not already evaluated
.venv/bin/python analysis/merge_results.py --dir "$d"
```

That produces a single `runs_with_success.csv` carrying *both* models, which is
the natural input. `analyze_stats.py` run on it already yields two independent
per-model analyses (separate charts subdirs); `compare_models.py` adds the
cross-model layer on top.

### 2.2 Fallback: multiple per-run CSVs

Because the Haiku and DeepSeek runs live under different `results/runs/<ts>/`
dirs, also accept several CSVs and concatenate their rows:

```bash
compare_models.py --csv .../haiku/runs_with_success.csv \
                  --csv .../deepseek/runs_with_success.csv
```

Concatenation is a plain list extend (rows already carry `model`). If the same
`(model, instance, arm, rep)` appears in two files, keep the first and warn.

### 2.3 n_files

Same source as `analyze_stats.py`: `--pool data/pool.csv` (or an instances-list
file). Reuse `analyze_stats.load_n_files` verbatim. Needed for the size-
interaction replication (§3.2); if absent, that analysis degrades with a note.

---

## 3. Analyses

All operate on per-model **effects**. Reuse, per model:
`analyze_stats.per_instance_medians`, `.size_interaction`,
`.clustered_bootstrap_diff`, `.per_instance_success`, `.wilson_ci`, and the row
helpers (`_arm`, `_inst`, `_instances`, `fvals`, `_nfiles_map`).

### 3.1 Side-by-side effects table (primary text output)

For each token metric (`peak_prompt_tokens`, `total_input_tokens`,
`tool_output_tokens_approx`), one row per model:

| column | source |
|--------|--------|
| pooled median ratio (T/C) | `clustered_bootstrap_diff(...).point_ratio` |
| ratio 95% CI | `clustered_bootstrap_diff(...).ratio_ci` (clustered) |
| median of per-instance % change | from `size_interaction(...).per_instance` |
| Spearman(n_files, %change) rho, p | `size_interaction(...).spearman` |

This is the at-a-glance "does the effect look the same in both models?" table.
Ratios and % changes only — no raw token columns (§1.3).

### 3.2 Size-interaction replication (the headline chart)

Overlay both models' per-instance `(n_files, %change)` points on **one** scatter
per token metric (different marker/color per model), annotate each model's
Spearman rho in the legend/title. The question it answers: **do both models show
rho < 0?** If yes → the size interaction generalizes; if they diverge → it's
model-specific. This is the chart that justifies the second model.

### 3.3 Cross-model per-instance effect correlation

For each token metric, scatter **model A's per-instance %change (x)** vs
**model B's per-instance %change (y)**, one point per shared instance, with a
Spearman/Pearson correlation and the y=x line. High positive correlation ⇒ the
*same instances* benefit in both models ⇒ the effect is instance-driven and
consistent (strong corroboration of the size hypothesis). Restricted to the
instance intersection; report N and any dropped instances. Generalizes to >2
models as a pairwise correlation matrix (defer the matrix until k>2).

### 3.4 Success-parity replication

Per model, the per-arm success rate (+ Wilson CI) and the clustered
success-rate difference (`clustered_bootstrap_success`), side by side. Haiku was
10/15 both arms (Δ=0pp: qi changes search, not resolution). Does DeepSeek also
show parity? Replicated parity is a cleaner non-inferiority story than either run
alone.

### 3.5 Model × arm interaction (exploratory)

Does qi help one model *more*? Compare the per-instance treatment effect
(% change) distributions between models — descriptively (overlaid, paired on
shared instances) and, if motivated, a paired test on the cross-model difference
of per-instance % changes. Label exploratory; at this N it is descriptive.

---

## 4. Charts (PNG, like analyze_stats.py)

| # | file | content |
|---|------|---------|
| 1 | `01_size_interaction_replication.png` | §3.2 overlay (one panel per token metric), per-model rho in title |
| 2 | `02_crossmodel_effect_correlation.png` | §3.3 A-vs-B %change scatter (one panel per token metric), y=x line, Spearman |
| 3 | `03_effects_sidebyside.png` | §3.1 as a forest-style plot: per metric, each model's ratio + clustered CI, vertical line at ratio=1 |
| 4 | `04_success_parity.png` | §3.4 grouped bars: success rate by (model × arm) with Wilson CIs |

Reuse `analyze_stats._save`, the color conventions, and `MaxNLocator(integer=True)`
on any `n_files` axis (it is integer-valued — same fix as chart #26 there).

---

## 5. Outputs

```
<dir>/
├── model_comparison_summary.txt   # §3.1 table + §3.2/§3.3 rho/correlation + §3.4 + caveats
├── model_comparison.json          # machine-readable, via analyze_stats._json_safe
└── charts/
    ├── 01_size_interaction_replication.png
    ├── 02_crossmodel_effect_correlation.png
    ├── 03_effects_sidebyside.png
    └── 04_success_parity.png
```

Default `--dir`: the input CSV's directory when a single combined CSV is given;
otherwise a new `results/runs/<ts>/` (or require `--dir` for the multi-CSV case).
`stats.json` reuse: emit the same nested `{"models": {...}}` shape plus a
top-level `"cross_model"` block so downstream tooling can read both.

---

## 6. Explicitly out of scope

- **Absolute cross-model token/cost comparison** (§1.3 tokenizer caveat). If ever
  wanted, it needs a real per-model tokenizer, not the 4 chars/token proxy, and a
  cost model per provider — a separate effort.
- **Formal meta-analysis / random-effects pooling** (§1.2, k=2).
- **Pairwise correlation matrix** for §3.3 — only meaningful at k>2; add then.

---

## 7. CLI (proposed)

```bash
python3 experiment/analysis/compare_models.py \
    --csv experiment/results/runs/<combined_ts>/runs_with_success.csv \
    --pool experiment/data/pool.csv \
    --bootstrap-iters 10000
```

| flag | default | notes |
|------|---------|-------|
| `--csv` | newest combined CSV under `results/runs/` | repeatable; multiple → concat (§2.2) |
| `--pool` | `data/pool.csv` | n_files source (reuses `load_n_files`) |
| `--dir` | input CSV's dir (single) / required (multi) | output location |
| `--bootstrap-iters` | 10000 | clustered bootstrap resamples |
| `--seed` | 42 | RNG seed |
| `--models` | all present | optional filter to a subset of model names |
| `--no-charts` | off | skip charts (matplotlib stays the one soft dep) |

---

## 8. Edge cases / degradation

- **< 2 models present.** Print "need ≥2 models to compare (found: …)" and exit 0
  — nothing to compare, not an error.
- **Non-overlapping instances.** Cross-model paired analyses (§3.3, §3.5) use the
  instance intersection; print the count used and any instances dropped per model.
- **n_files missing.** §3.2 replication degrades to "no n_files — size
  interaction skipped" (mirror `analyze_stats` behavior); other analyses unaffected.
- **An arm missing for a model/instance.** `per_instance_medians` already drops
  unpaired instances within a model; cross-model layer inherits that.
- **matplotlib absent / `--no-charts`.** Text + JSON still produced (soft dep).
- **scipy** is a hard dependency (Spearman) — inherited from `analyze_stats.py`.

---

## 9. Reuse contract with analyze_stats.py

`compare_models.py` imports and MUST NOT duplicate:

- `load(csv_path, n_files_map)` and `load_n_files(path)` — loading + n_files join
- `per_instance_medians`, `per_instance_success`, `size_interaction` — per-instance effects
- `clustered_bootstrap_diff`, `clustered_bootstrap_success`, `wilson_ci` — inference
- `_arm`, `_inst`, `_instances`, `fvals`, `_nfiles_map`, `has_metric` — row helpers
- `_save`, `_json_safe`, `model_slug`, `TOKEN_METRICS`, `ARMS` — output/constants

If a needed helper is currently "private" (`_`-prefixed) but stable, promote it
rather than copy it. The rule: **one definition of each computation, period.**

---

## 10. Resolved decisions

1. **Import, don't fork.** `compare_models.py` is a thin consumer of
   `analyze_stats.py` (§9). No parallel CSV/pivot logic.
2. **Effects, not raw tokens, across models** (§1.3). Ratios/% only.
3. **Replication, not meta-analysis** (§1.2). Honest framing in all output.
4. **Same instances, paired across models** (§1.4). Cross-model analyses use the
   instance intersection.
5. **Charts are PNG**, matplotlib soft, scipy hard — same posture as
   `analyze_stats.py`.

## 11. Open questions (decide at build time)

1. **Single combined CSV vs multi-CSV as the primary path.** Leaning combined
   (§2.1) because the pipeline already emits `model`; the multi-CSV concat (§2.2)
   is the fallback. Confirm which the run bookkeeping will actually produce.
2. **§3.5 paired test** — worth a formal test on the cross-model difference of
   per-instance % changes, or keep §3.5 purely descriptive at k=2? Default:
   descriptive.
3. **Pearson vs Spearman for §3.3.** Spearman is safer (monotone, outlier-robust,
   small N); report Spearman, optionally Pearson alongside.
