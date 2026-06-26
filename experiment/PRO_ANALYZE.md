# Pro Analysis Pipeline

Reference for running the full SWE-bench Pro analysis pipeline on a completed rep batch.

## Quick Start

```bash
# One-command pipeline (trajectories → eval → merge → stats):
bash experiment/scripts/run_pro_pipeline.sh <batch> \
    --logs experiment/logs/deepseek--deepseek-v4-flash/<batch>

# Then run the supplementary steps:
experiment/.venv_pro/bin/python experiment/analysis/extract_pro_qi_commands.py \
    --logs experiment/logs/deepseek--deepseek-v4-flash/<batch> \
    --out experiment/results/pro_runs/<batch>/qi_commands.csv

experiment/.venv_pro/bin/python experiment/analysis/report_pro_qi_commands.py \
    --csv experiment/results/pro_runs/<batch>/qi_commands.csv

experiment/.venv_pro/bin/python experiment/analysis/analyze_pro_eval_granular.py \
    --csv experiment/results/pro_runs/<batch>/eval_results.csv
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
Step 4: analyze_pro_stats.py         ──→ pro_stats_summary.csv + report
---
Step 5: extract_pro_qi_commands.py   ──→ qi_commands.csv
Step 6: report_pro_qi_commands.py    ──→ qi usage report (stdout)
Step 7: analyze_pro_eval_granular.py ──→ granular eval comparison (stdout)
```

Steps 1-4 are sequential (each depends on the prior output). Steps 5-7 can run anytime after step 2 is complete. Steps 5-7 are **not** included in `run_pro_pipeline.sh`.

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

## Step 4: Pro Stats

Rep-level arm comparison: per-metric median/mean/IQR, Mann-Whitney U, bootstrap CI for difference in medians, resolve rate with Wilson interval, blow-up rate.

```bash
experiment/.venv_pro/bin/python experiment/analysis/analyze_pro_stats.py \
    --dir experiment/results/pro_runs/<batch>
```

Produces: `pro_stats_summary.csv` + report (stdout). Use `--no-charts` to skip matplotlib.

## Step 5: Extract qi Commands

Parses Pro trajectories and emits one row per shell command, with qi flag decomposition, output size, and error tracking.

```bash
experiment/.venv_pro/bin/python experiment/analysis/extract_pro_qi_commands.py \
    --logs experiment/logs/deepseek--deepseek-v4-flash/<batch> \
    --out experiment/results/pro_runs/<batch>/qi_commands.csv
```

Produces: `qi_commands.csv` (one row per shell command across all runs; qi-pure commands carry parsed flag columns).

## Step 6: Report qi Commands

Per-arm qi usage report: tool share, flag vocabulary, output sizes, limit adoption, antipattern rates, tool timing (onset/abandonment), streak dynamics, grep sophistication.

```bash
experiment/.venv_pro/bin/python experiment/analysis/report_pro_qi_commands.py \
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

## Output Artifacts

All outputs land under `experiment/results/pro_runs/<batch>/`:

| File | Produced By | Contents |
|------|-------------|----------|
| `runs.csv` | Step 1 | Per-run token/turn/cost/qi metrics |
| `eval_results.csv` | Step 2 | Per-rep harness outcome (resolved, pass_rate, F2P/P2P) |
| `eval_test_failures.csv` | Step 2 | Individual failing tests per rep |
| `runs_with_success.csv` | Step 3 | Merged metrics + outcomes table |
| `pro_stats_summary.csv` | Step 4 | Arm-level statistical summary |
| `qi_commands.csv` | Step 5 | Per-shell-command rows with qi flag decomposition |

## The run_pro_pipeline.sh Script

The one-command script at `experiment/scripts/run_pro_pipeline.sh` covers Steps 1-4:

```bash
bash experiment/scripts/run_pro_pipeline.sh <batch> \
    --logs experiment/logs/deepseek--deepseek-v4-flash/<batch> \
    --workers 1
```

What it does **not** cover: qi command extraction/reporting (Steps 5-6) and granular eval comparison (Step 7). Run those separately after the main pipeline completes.

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

Every Pro batch is N reps of a single instance — provider nondeterminism on one task. Per-rep statistics (MWU, bootstrap CI) describe within-instance variance, not cross-instance generalizability. Read effects as direction, not proof.

### Vendored Code Overwrites

The `GOMAXPROCS` cap and some eval patches live in `experiment/vendor/swebench_pro_os/swe_bench_pro_eval.py`. Re-pulling upstream will overwrite them.

### Containers Left Behind

If `index_instance_pro.sh` crashes mid-run, it can leak a detached `sleep 3600` container. Clean up with:

```bash
docker ps -a --filter "ancestor=sweb.eval.x86_64.*" --format '{{.ID}}' | xargs -r docker rm -f
```

## Cross-Batch Comparison

For comparing results across batches (e.g. Flash vs v4-pro, or teleport vs ansible), read each batch's `pro_stats_summary.csv` and `eval_results.csv` and assemble manually. A `pro_batch_status.py` dashboard was proposed but not yet built.

## See Also

- `experiment/RUN_BOOKKEEPING.md` — manifest, ledger, and resume system for `run_pro_reps.py`
- `experiment/SETUP.md` — Verified experiment setup (different pipeline)
- `experiment/PREREGISTRATION.md` — frozen experimental design for the Verified experiment
- `experiment/TROUBLESHOOTING.md` — common failure modes
- `experiment/CONTAINER_CLEANUP.md` — Docker container management
