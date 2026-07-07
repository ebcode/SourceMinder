# Pre-Publication Draft: qi Efficiency Claims

**Date:** 2026-07-05
**Status:** DRAFT — claims verified against the data as of this date; re-derive before publishing.
**Scope:** SWE-bench Pro cross-instance results, Haiku-4.5/Sonnet-5 batches, k=8 manifest.

---

## The Claims

### Version A — the full pool (lead with this)

> Across 8 instances spanning 8 repos and 4 languages (JS, TS, Python, Go) —
> excluding only two tasks outside qi's indexing scope (a majority
> machine-generated patch; a patch that creates its own key symbol) — and with
> a model that uses qi competently, qi reduced median token cost on every
> instance (8/8, one-sided sign test p ≈ 0.004), pooling to ~15% tokens and
> ~18% dollar cost (95% CIs exclude zero), with point estimates ranging from
> −7% to −46%.

### Version B — the mechanism-scoped subset (follow with this)

> On instances selected for frontier-resolvability and non-trivial regression
> breadth (p2p > 0), and with a model that uses qi competently, qi reduced
> token cost on the order of 10–30% (5/5 negative; pooled −21% tokens,
> −20% cost; 95% CIs exclude zero).

### Mandatory footnotes (attach to both)

1. **Resolve safety:** Resolve was at ceiling (5/5 both arms) on 7 of 8
   instances. On the eighth (nodebb), *both* arms sat below ceiling — control
   4/5, treatment 3/5 (n.s.; Fisher exact p = 1.0) — with mirrored failure
   modes (one `regression` in each arm at pass_rate 0.9966 on a 291-test
   surface, plus one treatment `bug_not_fixed`). The honest safety claim is
   "no *detected* harm at n=5/arm," not "proven harmless."
2. **Model qualifier is load-bearing:** on the same qutebrowser instance where
   Haiku 4.5 saved 46%, GLM-5.2 *increased* median tokens by +37%
   (398,252 vs 291,455). "Uses qi competently" is a real precondition, not
   hedging. Evidence base: Haiku 4.5 (7 instances) + Sonnet 5 (1: tutanota).
3. **Heterogeneity caveat:** I² = 0% on all pooled metrics; the −7%…−46%
   spread in point estimates is not formally distinguishable from bootstrap
   noise at n=5/arm. Do not claim "substantial per-instance variation" *and*
   cite I²=0% — pick the honest framing: wide-looking spread, undetectable
   formal heterogeneity, low power.

---

## The Instance Set

Manifest: `experiment/analysis/cross_instance_manifest.txt` (k=8; exclusions
and their rationale recorded in `experiment/docs/SELECTION_RULES.md`).
Per-batch inputs live in `experiment/results/pro_runs/<batch>/`.

| label | batch dir (`experiment/results/pro_runs/…`) | repo | lang | model | n_p2p_files | frontier resolve | tokens Δ [95% CI] | cost Δ [95% CI] |
|---|---|---|---|---|---|---|---|---|
| qutebrowser | `pro_pilot_qutebrowser_haiku_v1` | qutebrowser/qutebrowser | python | haiku-4.5 | 2 | 1.000 (9/9) | −46% [−78, +21] | −49% [−83, −32] |
| element | `pro_pilot_element3_haiku` | element-hq/element-web | js | haiku-4.5 | 7 | 0.222 (2/9) | −26% [−48, +1] | −22% [−40, −1] |
| openlibrary | `pro_pilot_openlibrary_haiku_v2` | internetarchive/openlibrary | python | haiku-4.5 | 1 | 0.000 (0/9) | −22% [−46, +53] | −28% [−46, +12] |
| nodebb | `pro_pilot_nodebb_haiku` | NodeBB/NodeBB | js | haiku-4.5 | 2 | 0.111 (1/9) | −16% [−75, +117] | −13% [−69, +87] |
| teleport | `pro_pilot_teleport3_haiku` | gravitational/teleport | go | haiku-4.5 | 10 | 0.500 (4/8) | −12% [−29, +25] | −14% [−30, +8] |
| tutanota | `pro_pilot_tutanota2_sonnet5` | tutao/tutanota | ts | sonnet-5 | 0 | 0.222 (2/9) | −11% [−38, +20] | −3% [−43, +32] |
| ansible | `pro_pilot_ansible2_haiku` | ansible/ansible | python | haiku-4.5 | 0 | 0.000 (0/9) | −8% [−33, +44] | −15% [−43, +15] |
| vuls | `pro_pilot_vuls2_haiku` | future-architect/vuls | go | haiku-4.5 | 0 | 0.000 (0/9) | −7% [−44, +6] | −16% [−43, −1] |

**Pooled (k=8):** tokens **−15% [−26, −3]**, cost **−18% [−27, −8]**, turns
−9% [−17, −0]; I² = 0% on all three.

**Pooled (k=5, p2p>0 subset** = qutebrowser, element, openlibrary, nodebb,
teleport**):** tokens **−21% [−34, −4]**, cost **−20% [−31, −8]**, turns
−7% [−18, +5]; I² = 0%.

**Excluded** (rationale in `SELECTION_RULES.md`):
- flipt (`pro_pilot_flipt_haiku`, +27% tokens): gold patch majority
  machine-generated (1/5 source; protobuf/swagger/CHANGELOG).
- webclients (`pro_pilot_webclients_haiku`, +6% tokens): 47-line patch,
  2 gold files — one *created by the patch* (`AddressesAutocomplete.helper.ts`
  / `splitBySeparator` absent at base commit), the other tsx-degraded — and
  n_p2p_files = 0.
- Version B's "selected for" phrasing is honest **only for element and
  teleport** (chosen under `pro_select.py --screen`); the other three predate
  the tooling and merely *pass* the screens retroactively. Say "selected or
  retroactively qualifying" if a reviewer pushes.

---

## Exactly How to Re-Derive Every Number

All commands run from the repo root. Python = `experiment/.venv_pro/bin/python`.

### 1. The k=8 pooled estimates and per-instance rows (Version A)

```bash
experiment/.venv_pro/bin/python experiment/analysis/cross_batch_compare.py \
    --manifest experiment/analysis/cross_instance_manifest.txt \
    --out experiment/results/pro_runs/_cross/
```

- Reads each batch's `experiment/results/pro_runs/<batch>/runs_with_success.csv`
  (per-rep `total_tokens`, `cost`, `turn_count`) and `eval_results.csv`
  (per-rep `resolved`).
- Writes `experiment/results/pro_runs/_cross/cross_instance.csv`: one row per
  (instance, metric) with `effect_logratio`, `ci_lo`, `ci_hi`, `se`,
  `pct_change`, plus one `instance = __POOLED__` row per metric carrying the
  pooled estimate, `k`, `Q`, `I2`.
- **Method** (full derivation in `experiment/docs/STATISTICAL_METHODS.md` and
  `experiment/docs/CROSS_INSTANCE.md`): per-instance effect =
  `ln(median_treatment / median_control)`; CI/SE from bootstrap resampling of
  reps within arm (10,000 iters, seed 42 — the script defaults; pass
  `--bootstrap-iters/--seed` to vary); pooling is inverse-variance
  (`w_i = 1/se_i²`); `pct_change = exp(effect) − 1`.
- The headline numbers are the `__POOLED__` rows for `total_tokens` (−15%
  [−26, −3]) and `cost` (−18% [−27, −8]). Note: wall time (`duration_sec`) was
  removed from this analysis 2026-07-05 (unreliable ledger durations); the
  per-batch pipeline still records it.

### 2. The sign test (Version A's "8/8, p ≈ 0.004")

Count negative `effect_logratio` values among the 8 non-pooled `total_tokens`
rows of `cross_instance.csv`, then compute the one-sided binomial:

```bash
python3 - <<'EOF'
import csv
rows = [r for r in csv.DictReader(open('experiment/results/pro_runs/_cross/cross_instance.csv'))
        if r['metric'] == 'total_tokens' and r['instance'] != '__POOLED__']
neg = sum(float(r['effect_logratio']) < 0 for r in rows)
print(f"{neg}/{len(rows)} negative; one-sided sign test p = {0.5**len(rows):.4f}")
EOF
```

Expected: `8/8 negative; one-sided sign test p = 0.0039`. (For the k=5
subset the same test gives 5/5, p = 0.03125.)

### 3. The k=5 subset pooled estimates (Version B)

Same script, ad-hoc batch list, scratch output dir (do **not** overwrite
`_cross/`, which is the canonical k=8 artifact):

```bash
experiment/.venv_pro/bin/python experiment/analysis/cross_batch_compare.py \
    --batch experiment/results/pro_runs/pro_pilot_qutebrowser_haiku_v1 \
    --batch experiment/results/pro_runs/pro_pilot_element3_haiku \
    --batch experiment/results/pro_runs/pro_pilot_openlibrary_haiku_v2 \
    --batch experiment/results/pro_runs/pro_pilot_nodebb_haiku \
    --batch experiment/results/pro_runs/pro_pilot_teleport3_haiku \
    --out /tmp/cross_p2p_only --no-charts
```

Expected `__POOLED__` rows: tokens −21% [−34, −4], cost −20% [−31, −8],
turns −7% [−18, +5], each with k=5, I²=0%.

### 4. The selection signals (p2p breadth, frontier resolve)

- **n_p2p_files** per instance: `experiment/analysis/pro_select.py --csv
  --limit 0` emits the full pool with `n_p2p`/`n_p2p_files` columns (parsed
  from the SWE-bench Pro `pass_to_pass` field of
  `experiment/data/test.parquet` via the language-aware file parser; see
  `experiment/docs/PRO_SELECT.md`).
- **frontier resolve_rate**: `experiment/data/pro_resolve_rates.csv`, columns
  `n_resolved`/`n_scored`/`resolve_rate` (per-instance pass/fail of 9 frontier
  models, sourced from `scaleapi/SWE-bench_Pro-os` `traj/<model>/eval_results.json`).
  Caveat on file: our harness has resolved instances Scale scores 0/9
  (vuls2 control reps) — scoring methodologies differ; treat these rates as a
  selection signal, not ground truth.
- **Gold-patch composition** (flipt/webclients exclusions):
  `experiment/tmp/qi_fit_signals.py --explain <sha-substring>` prints the
  per-gold-file class (source/generated, leaked/hidden, graded) plus all fit
  signals for any instance.

### 5. The nodebb resolve footnote

```bash
python3 -c "
import csv
for r in csv.DictReader(open('experiment/results/pro_runs/pro_pilot_nodebb_haiku/eval_results.csv')):
    if r['resolved'] != '1':
        print(r['arm'], r['failure_mode'], r['pass_rate'], r['f2p_total'], r['p2p_total'])"
```

Expected: one control `regression`, one treatment `regression`, one treatment
`bug_not_fixed`, all at pass_rate 0.9966 (3 F2P + 288 P2P tests). Fisher exact
on 4/5 vs 3/5 → p = 1.0.

### 6. The GLM-5.2 reversal (footnote 2)

```bash
python3 -c "
import csv, statistics as st
rows = list(csv.DictReader(open('experiment/results/pro_runs/pro_pilot_qutebrowser_glm5.2/runs_with_success.csv')))
for arm in ('swebp_control', 'swebp_treatment'):
    v = [int(r['total_tokens']) for r in rows if r['arm'] == arm]
    print(arm, st.median(v))"
```

Expected: control 291,455 / treatment 398,252 → +36.6%. Same instance
(`qutebrowser-f91ace9622…`) as the Haiku −46% row above.

### 7. Charts

`experiment/results/pro_runs/_cross/charts/` (regenerated by the step-1
command): `forest_total_tokens.png` / `forest_cost.png` / `forest_turn_count.png`
(per-instance dots + pooled diamond), `resolve_dumbbell.png` (the safety
panel — shows the nodebb gap), `radar_efficiency.png` (single treatment
polygon, k=8), `cumulative_cost.png`, `log_size_range.png`,
`explore_calls.png`, `explore_tokens.png`.

---

## Standing Caveats (do not publish without)

- **Every batch is one instance × 5 reps/arm** — per-instance CIs are lumpy
  bootstrap-of-medians at n=5; the pooled diamond carries the inference
  (`experiment/docs/STATISTICAL_METHODS.md`).
- **k=8 is small**; direction + rough magnitude, not a calibrated effect size.
- **Model mix**: 7× Haiku 4.5, 1× Sonnet 5. The claim is scoped to models
  fluent with qi; GLM-5.2 demonstrates the reversal is real.
- **No retroactive exclusions beyond the two recorded**: vuls2 and ansible2
  fail the same forward screens as webclients and deliberately remain
  (`experiment/docs/SELECTION_RULES.md`, two-pools policy) — dropping them
  would trim exactly the predicted-weak half and inflate the pooled effect.
- **Token counts are total API tokens** (prompts + tool output), not log
  size. Log size is a separate outcome axis (radar): treatment logs ~11%
  smaller, log *variance* ~30% smaller.
- **Format tax:** none of the k=8 batches is tax-affected (all Haiku/Sonnet);
  the `format_tax` column in `cross_instance.csv` records this per row.
