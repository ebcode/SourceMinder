# Pro Analysis Pipeline

Reference for running the full SWE-bench Pro analysis pipeline on a completed rep batch.

## Quick Start

```bash
# One-command pipeline (all steps):
bash experiment/scripts/run_pro_pipeline.sh <batch> \
    --logs experiment/logs/deepseek--deepseek-v4-flash/<batch>
```

## Prerequisites

- **Venv**: `experiment/.venv_pro/bin/python` (the Pro venv; has `datasets`, `docker` SDK, `pandas`).
- **Reps complete**: `run_pro_reps.py` has finished and trajectories exist on disk.
- **The `evaluate_pro_patches.py` eval step** is the heavyweight Docker pass — it's often run manually before invoking the rest of the pipeline. The `run_pro_pipeline.sh` script runs it for you.

## Log Path Scheme

Pro reps write trajectories under `experiment/logs/<model_slug>/<batch>/`:

```
logs/deepseek--deepseek-v4-flash/pro_pilot_teleport_v4_flash/
├── swebp_control/
│   ├── <instance_id>/
│   │   └── <instance_id>.rep01.traj.json
│   │   └── ...
│   └── ...
└── swebp_treatment/
    └── ...
```

The `<model_slug>` is produced by `experiment/lib/model.py:model_dir()`, which replaces `/` with `--` (e.g. `deepseek/deepseek-v4-flash` → `deepseek--deepseek-v4-flash`).

When using `run_pro_pipeline.sh`, the logs path can be set three ways (in precedence order):

1. `--logs <dir>` — explicit path
2. `--batch-id <id> [--model <litellm_id>]` — reconstructs `logs/<model_slug>/<id>/`
3. Default: `experiment/logs/pro_pilot/` (legacy)

When running the analysis scripts directly, always pass `--logs`:

```bash
--logs experiment/logs/deepseek--deepseek-v4-flash/pro_pilot_<batch>
```

## Full Pipeline

```
Step 1: analyze_pro_trajectories.py ──→ runs.csv
Step 2: evaluate_pro_patches.py      ──→ eval_results.csv, eval_test_failures.csv
Step 3: merge_results.py             ──→ runs_with_success.csv
Step 4: wall_time.py                 ──→ wall_time.csv + report (ledger-based)
Step 5: extract_qi_commands.py       ──→ qi_commands.csv
Step 6: report_qi_commands.py        ──→ qi usage report (stdout)
Step 7: analyze_pro_eval_granular.py ──→ granular eval comparison (stdout)
Step 8: analyze_pro_stats.py         ──→ pro_stats_summary.csv + report + charts
```

Step 4 (`wall_time.py`) reads the run ledger, not the prior CSVs, so it's
independent but runs before `analyze_pro_stats.py` so its `wall_time.csv` can
feed the wall-time metric and chart. It stays non-fatal: a missing ledger entry
just drops the wall-time row/chart. Steps 1-3 are strictly sequential (each
depends on the prior output). Steps 5-7 are non-fatal reporting steps that can
run any time after Step 2: `extract_qi_commands.py` (Step 5) runs before
`report_qi_commands.py` (Step 6) so its `qi_commands.csv` feeds the report.
`analyze_pro_stats.py` (Step 8) runs last as the final summary — it consumes
`wall_time.csv` (Step 4) when present. All steps are in `run_pro_pipeline.sh`.

## Step 1: Trajectory Analysis

Extracts per-run metrics (turns, tokens, cost, qi/grep/read invocation counts, qi flag adoption, patch size) from trajectory JSON files.

```bash
experiment/.venv_pro/bin/python experiment/analysis/analyze_pro_trajectories.py \
    --logs experiment/logs/deepseek--deepseek-v4-flash/<batch> \
    --dir experiment/results/pro_runs/<batch>
```

Produces: `results/pro_runs/<batch>/runs.csv` (one row per run).

Columns include: `model`, `arm`, `instance_id`, `run_id`, `turn_count`, `total_input_tokens`, `total_tokens`, `peak_prompt_tokens`, `cost`, `qi_invocations`, `grep_invocations`, `file_read_invocations`, `qi_parent_calls`, `qi_verbose_calls`, `patch_chars`, `patch_lines`, `files_touched`, `submitted`, plus the format-tax columns `empty_content_turns`, `reasoning_recovered_turns`, `reasoning_recovered_rate` (see the *Format tax* gotcha below).

Options:
- `--arms swebp_control swebp_treatment` — limit to specific arms (default: both)
- `--run-prefix oldprompt_` — scope to runs with a given run-id prefix

## Step 2: Patch Evaluation

Runs the SWE-bench Pro Docker harness against submitted patches. This is the heavyweight step (Docker builds + test suites). Often run manually before the rest of the pipeline.

```bash
experiment/.venv_pro/bin/python experiment/analysis/evaluate_pro_patches.py \
    --logs experiment/logs/deepseek--deepseek-v4-flash/<batch> \
    --dir experiment/results/pro_runs/<batch> \
    --workers 1 --redo
```

Produces: `eval_results.csv`, `eval_test_failures.csv`.

Options:
- `--workers N` — parallel Docker workers (default: 5; use `--workers 1` to match CPU count safely)
- `--redo` — re-evaluate completed reps
- `--arms swebp_control swebp_treatment` — limit to specific arms

### Critical Gotcha: eval_results.csv Overwrite

The script opens `eval_results.csv` with `"w"` mode and only writes rows for the arms passed via `--arms`. **Running with a single arm (`--arms swebp_treatment`) then again with the other arm (`--arms swebp_control`) silently overwrites the first arm's results.** Always pass both arms in a single invocation:

```bash
# CORRECT — both arms in one invocation:
--arms swebp_control swebp_treatment

# WRONG — will clobber the first invocation:
--arms swebp_treatment   # overwrites previous eval_results.csv
--arms swebp_control     # overwrites treatment rows
```

## Step 3: Merge Results

Left-joins `eval_results.csv` onto `runs.csv` by run identity.

```bash
experiment/.venv_pro/bin/python experiment/analysis/merge_results.py \
    --dir experiment/results/pro_runs/<batch>
```

Produces: `runs_with_success.csv`. Runs present in `runs.csv` but absent from `eval_results.csv` get `outcome = not_evaluated` (not silently dropped).

## Step 4: Wall Time

Per-rep and per-arm wall-clock durations from the run ledger (`logs/run_pro_ledger.jsonl`) — the only source with timing, since trajectories carry only `instance_cost`/`api_calls`. Reports per-rep durations, per-arm aggregates, the true batch wall clock (max finished − min started, accounting for parallel workers), and a parallelism factor. Retries are de-duplicated to the latest attempt per `(arm, rep)`.

Runs **before** `analyze_pro_stats.py` (Step 5) so its `wall_time.csv` can feed the wall-time metric and chart. It remains non-fatal — a missing ledger entry just means Step 5 drops the wall-time row/chart.

```bash
experiment/.venv_pro/bin/python experiment/analysis/wall_time.py \
    --batch <batch_id> \
    --dir experiment/results/pro_runs/<batch>
```

Produces: `wall_time.csv` (one row per rep) when `--dir` is given; aggregates always print to stdout.

Options:
- `--batch <batch_id>` — **required**; the ledger `batch_id` (i.e. `run_pro_reps.py --batch-id`)
- `--dir <path>` — also write `wall_time.csv` here (omit for stdout-only)
- `--ledger <path>` — ledger location (default: `logs/run_pro_ledger.jsonl`)

Note: `--batch` is the **ledger** key, not the `--logs` path. If a batch isn't in the ledger (older/renamed runs), the script warns and exits 0 — it never fails the pipeline.

## Step 5: Extract qi Commands

Parses Pro trajectories and emits one row per shell command, with qi flag decomposition, output size, and error tracking.

```bash
experiment/.venv_pro/bin/python experiment/analysis/extract_qi_commands.py \
    --logs experiment/logs/deepseek--deepseek-v4-flash/<batch> \
    --out experiment/results/pro_runs/<batch>/qi_commands.csv
```

Produces: `qi_commands.csv` (one row per shell command across all runs; qi-pure commands carry parsed flag columns).

## Step 6: Report qi Commands

Per-arm qi usage report: tool share, flag vocabulary, output sizes, limit adoption, antipattern rates, tool timing (onset/abandonment), streak dynamics, grep sophistication.

```bash
experiment/.venv_pro/bin/python experiment/analysis/report_qi_commands.py \
    --csv experiment/results/pro_runs/<batch>/qi_commands.csv
```

Options:
- `--cross-model` — arm-by-model matrix instead of per-model detail
- `--model <substr>` — filter to models whose ID contains the substring

## Step 7: Granular Eval Comparison

Compares arms on pass_rate, failure mode mix, F2P/P2P breakdown, failing-test breadth (distinct tests failed), Mann-Whitney U on pass_rate, and bootstrap CI. Goes beyond the binary `resolved` column.

```bash
experiment/.venv_pro/bin/python experiment/analysis/analyze_pro_eval_granular.py \
    --csv experiment/results/pro_runs/<batch>/eval_results.csv
```

Options:
- `--arms swebp_control swebp_treatment` — control first, treatment second
- `--boot N` — bootstrap resamples (default: 10000)
- `--failures-csv <path>` — pointer to `eval_test_failures.csv` (default: sibling of `--csv`)

## Step 8: Pro Stats

Rep-level arm comparison: per-metric median/mean/IQR, Mann-Whitney U, bootstrap CI for difference in medians, resolve rate with Wilson interval, blow-up rate.

If `wall_time.csv` (Step 4) is present in `--dir`, the script joins per-rep `duration_sec` by `(arm, rep)` and treats **wall time** as a full metric — a table row, a `pro_stats_summary.csv` entry, and a boxplot chart, alongside turns/tokens/cost. When `wall_time.csv` is absent the wall-time metric self-skips (no row, no chart) and nothing else changes.

```bash
experiment/.venv_pro/bin/python experiment/analysis/analyze_pro_stats.py \
    --dir experiment/results/pro_runs/<batch>
```

Produces: `pro_stats_summary.csv` + report (stdout) + charts. Use `--no-charts` to skip matplotlib.

## Output Artifacts

All outputs land under `experiment/results/pro_runs/<batch>/`:

| File | Produced By | Contents |
|------|-------------|----------|
| `runs.csv` | Step 1 | Per-run token/turn/cost/qi metrics |
| `eval_results.csv` | Step 2 | Per-rep harness outcome (resolved, pass_rate, F2P/P2P) |
| `eval_test_failures.csv` | Step 2 | Individual failing tests per rep |
| `runs_with_success.csv` | Step 3 | Merged metrics + outcomes table |
| `wall_time.csv` | Step 4 | Per-rep wall-clock durations (from the ledger) |
| `qi_commands.csv` | Step 5 | Per-shell-command rows with qi flag decomposition |
| `pro_stats_summary.csv` | Step 8 | Arm-level statistical summary (incl. wall time when `wall_time.csv` present) |

## The run_pro_pipeline.sh Script

The one-command script at `experiment/scripts/run_pro_pipeline.sh` covers all Steps 1-8. Steps 4-7 are non-fatal (`|| true`): wall_time (Step 4) runs before analyze_pro_stats so its `wall_time.csv` feeds the final summary; extract_qi_commands (Step 5) runs before report_qi_commands (Step 6); analyze_pro_eval_granular (Step 7) runs before the final analyze_pro_stats (Step 8).

```bash
bash experiment/scripts/run_pro_pipeline.sh <batch> \
    --logs experiment/logs/deepseek--deepseek-v4-flash/<batch> \
    --workers 1
```

## Gotchas

### eval_results.csv Overwrite (Step 2)

See Step 2 above. Always pass both arms in a single `evaluate_pro_patches.py` invocation. The script uses `"w"` mode and only writes the arms it processes.

### Resource Oversubscription → Empty Patches

Running `--workers N` with N too high for the host's CPU count causes command timeouts inside containers → agent bash commands fail silently → agent submits an empty diff → `empty_patch` outcome. The agent may have navigated correctly internally but the edits never landed on disk.

**Prevention**: match `--workers` to host core count. For a 6-core box, use `--workers 1` or `--workers 2`. The `evaluate_pro_patches.py` script also benefits from `--workers 1` on constrained hosts.

**Detection**: empty-patch reps appear as `failure_mode=empty_patch` in `eval_results.csv`. Re-run with `--redo` and fewer workers.

### Format tax in pre-fix runs (efficiency is quarantined, resolve is valid)

Two model families paid a per-turn "format tax" before the mid-2026-06 harness work: the SWE-bench Pro scaffold required actions in ```` ```bash ```` fences and read only the model `content` field. **DeepSeek-v4-flash** (a reasoning model) routed ~13% of turns' commands into `reasoning_content`, leaving `content` blank → `found 0 actions`; **MiMo-v2.5-pro** emitted Qwen `<tool_call>`/`<parameter=command>` XML → ~14-18% of turns rejected. Wasted turns ran ~13-18% across both arms (noisy; do **not** claim either arm was taxed more).

`analyze_pro_trajectories.py` surfaces this per run via `empty_content_turns`, `reasoning_recovered_turns`, `reasoning_recovered_rate`. The MiMo `parse_action` fix is live in the vendored fork; the DeepSeek `reasoning_content` fold was **reverted 2026-06-26** (it distorted token counts), so future DeepSeek runs again lose blank-content turns — the columns now infer the tax from blank `content` + fenced `reasoning_content`.

**How it affects metrics:** every batch that completed (`Submitted`, no step/cost-limit hit) has a **valid `resolved`/`pass_rate`** — wasted turns never changed an outcome. But **efficiency (turns/tokens/cost) is inflated**: never pool pre-fix and post-fix turn/token/cost numbers. See each batch's `FORMAT_TAX.md` provenance note (e.g. `results/pro_runs/pro_pilot_ansible_ds_v4_flash_v2/FORMAT_TAX.md`).

### Single-Instance Generalization

Every Pro batch is N reps of a single instance — provider nondeterminism on one task. Per-rep statistics (MWU, bootstrap CI) describe within-instance variance, not cross-instance generalizability. Read effects as direction, not proof. Generalization is recovered by pooling several single-instance batches — see [Cross-Instance Analysis](#cross-instance-analysis) below.

### Vendored Code Overwrites

The `GOMAXPROCS` cap and some eval patches live in `experiment/vendor/swebench_pro_os/swe_bench_pro_eval.py`. Re-pulling upstream will overwrite them.

### Containers Left Behind

If `index_instance_pro.sh` crashes mid-run, it can leak a detached `sleep 3600` container. Clean up with:

```bash
docker ps -a --filter "ancestor=sweb.eval.x86_64.*" --format '{{.ID}}' | xargs -r docker rm -f
```

## Cross-Instance Analysis

A single batch can only show whether qi helped on *one* task; the hypothesis ("qi saves tokens, never hurts resolve") is a claim *across* tasks, so the evidence has to be assembled at the instance level. The naive move — pool every rep from every batch and run one Mann-Whitney — fails, because the control-arm token scale spans ~16× across instances (463K openlibrary → 7.6M ansible): the test is dominated by bug size, not by the treatment. The fix is to make each instance contribute on the same scale-free axis. For every instance and metric we take the **log-ratio of medians**, `ln(median_treatment / median_control)` (negative = treatment cheaper), bootstrap a per-instance CI, then **inverse-variance pool** the instances into a single meta-estimate with a Cochran's Q / I² heterogeneity readout. The pooled estimate is direction + rough magnitude across tasks, not a hypothesis test — k≈5 instances is still small.

`cross_batch_compare.py` implements this: it reads each batch's `runs_with_success.csv` (+ optional `wall_time.csv`) and writes `cross_instance.csv` (one row per instance×metric plus a `__POOLED__` row carrying k/Q/I²) and a `charts/` set — forest plots for turns/tokens/cost/wall (per-instance effect dots colored by model, with a pooled diamond) and a resolve dumbbell as the "never hurts" safety panel. Run it on the canonical 5 via `experiment/analysis/cross_instance_manifest.txt`. The format-tax instances (ansible, webclients) stay in the pool, flagged via a `format_tax` column + chart footnote, not dropped. **See [`CROSS_INSTANCE.md`](CROSS_INSTANCE.md) for usage, the effect-measure math, and the design rationale.** For a quick per-batch status table across all batch dirs, use `pro_batch_status.py` (see [`pro_batch_status.py`](../analysis/pro_batch_status.py)).

## See Also

- `experiment/analysis/pro_select.py` (see `experiment/docs/PRO_SELECT.md`) — choose/rank instances *before* a run, the front of the select→run→analyze chain
- `experiment/analysis/pro_batch_status.py` — one-command status dashboard over all batch dirs (which batches are complete, resolve/token/cost deltas at a glance)
- `experiment/docs/STATISTICAL_METHODS.md` — canonical methods reference (single-instance MWU/bootstrap + cross-instance log-ratio meta) behind Steps 7–8
- `experiment/docs/CROSS_INSTANCE.md` — `cross_batch_compare.py`, which pools these per-batch artifacts across instances (forest plots + meta-estimate)
- `experiment/docs/RUN_BOOKKEEPING.md` — manifest, ledger, and resume system for `run_pro_reps.py`
- `experiment/docs/SETUP.md` — Verified experiment setup (different pipeline)
- `experiment/docs/PREREGISTRATION.md` — frozen experimental design for the Verified experiment
- `experiment/docs/TROUBLESHOOTING.md` — common failure modes
- `experiment/docs/CONTAINER_CLEANUP.md` — Docker container management
