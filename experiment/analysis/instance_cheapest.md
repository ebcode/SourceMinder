# instance_cheapest.py

Find which SWE-bench Pro instances are cheapest to solve, based on actual
results from prior batch runs. Ranks by median turns, tokens, cost ($),
or wall-clock time so you can pick the right smoke-test instance for
your model and budget.

```
experiment/.venv_pro/bin/python experiment/analysis/instance_cheapest.py [flags]
```

**Data source:** all `runs_with_success.csv` files under `results/pro_runs/*/`.
Only rows with `task_success=1` are used.  Median is computed per
`(instance, model, arm)` group.

---

## Five common use cases

### 1. Cheapest smoke test for a specific model

Pick the instance your model solves in the fewest dollars or turns:

```
# Haiku: which instance costs least?
experiment/.venv_pro/bin/python experiment/analysis/instance_cheapest.py \
    --model haiku --metric cost --top 5

# DeepSeek Flash: which instance uses the fewest tokens?
experiment/.venv_pro/bin/python experiment/analysis/instance_cheapest.py \
    --model deepseek-flash --metric tokens --top 5
```

`--model` does a **substring match** on the model column, so `haiku`
matches `claude-haiku-4-5-20251001` and `deepseek` matches both Flash
and Pro (use the full name to pin one).

### 2. Control-only baseline (ignore treatment noise)

When you want a fair baseline — no qi, no prompt change:

```
experiment/.venv_pro/bin/python experiment/analysis/instance_cheapest.py \
    --arm swebp_control --metric turns --min-reps 3
```

Filters to control-arm solves only.  `--min-reps 3` discards one-off
flukes (an instance "solved" once but never replicated).

### 3. Budget-constrained pilot planning

When compute budget matters more than wall time:

```
experiment/.venv_pro/bin/python experiment/analysis/instance_cheapest.py \
    --metric cost --top 10
```

Costs come from `litellm` pricing tracked at runtime, so they reflect
actual spend, not estimates.  Models with very different per-token
pricing (mimo $0.0000004 vs Haiku $0.00125) are directly comparable.

### 4. Broad survey: what's cheapest per repo?

Run without `--model` or `--arm` to see every instance from every
prior batch, with a per-repo summary at the bottom:

```
experiment/.venv_pro/bin/python experiment/analysis/instance_cheapest.py \
    --metric turns --top 20 --min-reps 3
```

The per-repo section answers:  _"I want to test on NodeBB — which
instance is the cheapest to solve there?"_  Each repo gets its own
cheapest entry showing the best model/arm combination.

### 5. Course-correct during a run (wall time)

If a model is slow but not token-heavy, turns/cost understate the
real wait:

```
experiment/.venv_pro/bin/python experiment/analysis/instance_cheapest.py \
    --model mimo --metric wall --top 5
```

Wall time comes from `wall_time.csv` (container start→stop).  Not all
batches have this file; the script silently skips runs without it and
requires `--min-reps 1` (or omit the flag) since coverage is sparser.

---

## Flags

| Flag | Default | What it does |
|---|---|---|
| `--dir` | `results/pro_runs/` | Root of per-batch result dirs |
| `--arm` | *(all)* | Filter to `swebp_control` or `swebp_treatment` |
| `--model` | *(all)* | Substring match on model column |
| `--metric` | `turns` | `turns`, `tokens`, `cost`, or `wall` |
| `--top N` | `20` | Show top N cheapest rows |
| `--min-reps N` | `1` | Require ≥ N successful solves per group |

## Interpretation notes

- **A cheap solve ≠ an easy instance.**  openlibrary costs $0.008
  because mimo is a cheap model, not because the task is trivial.
  Always read the `model` and `n` columns alongside the median.

- **n (number of solves) is an honesty signal.**  n=1 could be a fluke;
  n=5 or n=20 means the solve is reproducible.  Use `--min-reps` to
  raise the bar.

- **The per-repo section picks the best model per instance.**  If
  Haiku solved NodeBB in 103 turns and Flash never solved it, the
  summary shows Haiku.  It answers "what's the best anyone has done
  on this repo."

- **No Go instances yet.**  The vuls run is in progress — once it
  finishes and the pipeline runs, Go instances will appear here.
