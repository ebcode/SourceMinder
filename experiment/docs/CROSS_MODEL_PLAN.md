> **SUPERSEDED (2026-06-18).** This plan was reconciled with
> `COMPARE_MODELS_SCRIPT_DESIGN.md`, which won on methodology and is the design
> actually built as `compare_models.py`. The fatal flaw here: this plan compares
> raw per-instance **Δ (token-count differences)** across models (§2.1–2.3),
> but tokenizers differ between models, so absolute token counts are **not
> comparable across models** — see the other doc's §1.3/§6. The implemented
> script compares **ratios / % change** instead. The compatible *chart* ideas
> from this plan were carried over (per-instance %change table with direction
> agreement → reconciled into the per-instance %change table; success grouped
> bars; size-interaction overlay). Kept for history; do not implement as written.

# analyze_cross_model.py — Design Plan (superseded — see compare_models.py)

Meta-analysis comparing the qi treatment effect **across models**. Consumes the
same `runs_with_success.csv` as `analyze_stats.py` (which may carry rows for one
or more models). Where `analyze_stats.py` answers "did treatment help within
model X?", this script answers "does qi help DeepSeek *more* than it helps
Haiku?"

The design is a **2×2 factorial** (model × arm) evaluated per-instance-class.

---

## 1. Core Question Inventory

| Question | Method | Priority |
|---|---|---|
| Does the treatment effect differ between models? | Per-instance Δ side-by-side; interaction description | 1 |
| On which instances do the models agree/disagree about qi? | Per-instance Δ scatter (model A on x, model B on y) | 1 |
| Does qi shift success rate differently per model? | Per-model, per-arm success rate + Wilson CI | 1 |
| Does the size-interaction pattern replicate across models? | Per-model n_files vs pct_change on same axes | 1 |
| Do models differ in qi adoption rate? | Per-model mechanism summary | 2 |
| Are the models' token budgets comparable? | Per-model cost/cache descriptives | 3 |

**Pilot caveat (same as single-model):** n=5 instances is underpowered for
inferential tests. The cross-model interaction (model × arm) on 5 paired diffs
is doubly underpowered. Everything here is descriptive.

---

## 2. Analysis Primitives

All leverage the per-instance treatment effect Δ, computed per model:

```
instance_id: django__django-11532
  haiku:  Δ = med(treatment) - med(control)  = -1.03M  (qi saves)
  deepseek:  Δ = ...                          = ???
```

For each token metric, this gives a table of K instances × M models.

### 2.1 Per-instance Δ by model (Priority 1 — table + forest plot)

**Table** (printed to stdout):

```
              peak_prompt_tokens        total_input_tokens
instance       Haiku Δ    DeepSeek Δ    Haiku Δ    DeepSeek Δ   Direction agrees?
django-11532   -8,500     ???           -1.03M     ???          ?
xarray-3305    +2,800     ???           +129K      ???          ?
pytest-8399    +3,500     ???           +478K      ???          ?
sphinx-10673  -22,000     ???           -1.64M     ???          ?
sympy-22080   -12,000     ???           -1.22M     ???          ?
```

`Direction agrees?` is Y if both models' Δ have the same sign, N if they differ.
At n=5, a count like "4/5 agree" is a defensible descriptive statement.

**Forest plot** (`01_forest_cross_model.png`): Instances as rows (y-axis). For
each instance, two points on the same row — one per model (different
colors/shapes), with a vertical 0-line. X-axis = Δ. Negative = treatment saves.
Both models' effects visible at a glance per instance.

### 2.2 Δ correlation across models (Priority 1 — scatter)

For each token metric, scatter the per-instance Δ of model A (x) vs model B (y).

```
02_scatter_delta_{metric}.png
```

Each point = one instance. The identity line (y=x) shows where qi helps both
models equally. Points in quadrants II/IV = the models disagree on direction.
Annotate instance labels. Spearman correlation (rho) measures concordance:
positive rho = instances where qi helps one model tend to help the other too.

### 2.3 Cross-model Δ distribution (Priority 1 — side-by-side boxplot)

```
03_boxplot_delta_distribution.png
```

One box per model. Each box represents the distribution of per-instance Δs for
that model (n=5 points per model). Overlaid dots for each instance. The
visual comparison: do the boxes overlap? Is one model's Δ consistently more
negative (qi saves more)?

### 2.4 Success rate by model × arm (Priority 1 — grouped bar)

```
04_bar_success_cross_model.png
```

Grouped bar chart: x-axis = arm (control/treatment), group = model
(Haiku/DeepSeek). Error bars = Wilson 95% CI. Four bars total. The visual
comparison: does the treatment bar shift *relative to its control bar* equally
for both models? (i.e. is the interaction visible?)

### 2.5 Size interaction by model (Priority 1 — scatter with both models)

```
05_scatter_size_cross_model.png
```

Same axes as `analyze_stats.py` chart 26, but with both models' points on the
same subplot. Each point = one instance, color = model. Instance labels
annotated. Spearman fit line per model (or Spearman rho annotation). Answers:
does the "qi saves more on larger instances" pattern replicate across models?

### 2.6 Mechanism: qi/grep/file_read invocation rates (Priority 2)

```
06_bar_mechanism_cross_model.png
```

Grouped bar chart: x-axis = invocation type (qi/grep/file_read), group = model
× arm. Shows whether both models actually adopted qi at similar rates, and
whether grep/file_read use shifts similarly under treatment.

### 2.7 Summary Table (Priority 1)

```
07_summary_table.txt  (also printed to stdout)
```

Per-model pooled statistics, side-by-side:

```
                     Haiku              DeepSeek
              control  treatment   control  treatment
peak_prompt    XX,XXX    XX,XXX    ???       ???
total_input    XX,XXX    XX,XXX    ???       ???
success rate    60.0%     60.0%    ???       ???
censoring        0.0%      6.7%    ???       ???
qi (mean/run)      0.0      6.2    ???       ???
```

Followed by the per-instance Δ table (§2.1).

---

## 3. Design Decisions

### 3.1 Shared helpers, not duplicate code

`analyze_cross_model.py` imports row-level helpers from `analyze_stats`:

```python
from analysis.analyze_stats import (
    load, load_n_files, fvals, _arm, _inst, _instances,
    _nfiles_map, has_metric, describe, wilson_ci,
    per_instance_medians, per_instance_success,
    TOKEN_METRICS, DESCRIPTIVE_METRICS, MECHANISM_METRICS,
    Row, ARMS, Z95, _json_safe, model_slug, _default_csv,
)
```

The cross-model script adds only:
- `per_instance_deltas(rows, metric, models)` — builds the K×M table of Δs
- `write_cross_summary(models_rows, stats, fh)` — text output
- `make_cross_charts(models_rows, per_model_stats, charts_dir)` — charts
- `main()` — orchestrates per-model analysis then cross-model comparison

### 3.2 Per-model analysis re-used, not re-implemented

`analyse_model()` from `analyze_stats.py` is imported and run for each model.
The cross-model script adds the comparison layer on top. No duplication.

### 3.3 Depends on at least 2 models in the CSV

If the input CSV has only one model, the script prints a note ("only one model
— nothing to compare; use analyze_stats.py") and exits cleanly. Not an error.

### 3.4 Handling of model with different instances

If models have disjoint instance sets (e.g. Haiku on instances A-E, DeepSeek on
instances F-J), per-instance comparison drops to zero rows. The script detects
this and reports "no shared instances between models — comparison empty."

### 3.5 No cross-model inferential tests at pilot N

With M=2 models and K=5 instances, the per-instance Δ table has 5 rows. The
treatment×model interaction on 5 matched triples is comically underpowered.
Report descriptives only; label clearly as pilot/exploratory.

---

## 4. CLI

```bash
python3 experiment/analysis/analyze_cross_model.py \
    --csv experiment/results/runs/<ts>/runs_with_success.csv \
    --bootstrap-iters 10000

# defaults: newest runs_with_success.csv, output beside it
python3 experiment/analysis/analyze_cross_model.py
```

Shared flags with `analyze_stats.py`: `--csv`, `--dir`, `--pool`,
`--bootstrap-iters`, `--seed`, `--no-charts`.

---

## 5. Output Layout

```
<output_dir>/
├── cross_model_summary.txt         # text summary + per-instance Δ table
├── cross_model.json                # machine-readable stats
├── charts/
│   ├── 01_forest_cross_model.png
│   ├── 02_scatter_delta_peak_prompt.png
│   ├── 02_scatter_delta_total_input.png
│   ├── 02_scatter_delta_tool_output.png
│   ├── 03_boxplot_delta_distribution.png
│   ├── 04_bar_success_cross_model.png
│   ├── 05_scatter_size_cross_model.png
│   ├── 06_bar_mechanism_cross_model.png
│   └── 07_summary_table.txt
```

Output goes to the CSV's directory (same as `analyze_stats.py` default).

---

## 6. Implementation Notes

- **~250 lines total.** 80% of the logic is imported from `analyze_stats.py`.
- **`per_instance_deltas(rows, metric, models)`** iterates instances, computes
  `per_instance_medians(model_rows, metric)` for each model, and collects Δs.
  Returns `list[dict]` with keys: instance_id, n_files, and `<model>_delta`.
- **Direction agreement** counted by comparing signs of Δ columns.
- **Cross-model scatter** uses plt.scatter with different markers for models;
  identity line drawn diagonally.
- **Forest plot** reuses the per-instance Δ table; horizontal layout with two
  points per row, offset slightly so they don't overlap.
