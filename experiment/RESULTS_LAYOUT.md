# Results Layout

Map of the experiment's on-disk layout: where logs, results, and analysis artifacts live, what each file contains, and how data flows between pipeline steps.

## Two Experiment Tracks

The repo runs two SWE-bench experiment tracks with **different layouts and analysis scripts**:

| | Verified / Lite | Pro |
|---|---|---|
| **Runner** | `run_experiment.py` | `run_pro_reps.py` |
| **Logs** | `logs/<model>/<batch>/<arm>/<instance_id>/` | `logs/<model>/<batch>/<arm>/<instance_id>/` |
| **Trajectory file** | `<instance_id>/<rep>.traj.json` | `<instance_id>/<instance_id>.repNN.traj.json` |
| **Results** | `results/runs/<batch>/` | `results/pro_runs/<batch>/` |
| **Analysis venv** | `.venv/bin/python` | `.venv_pro/bin/python` |
| **Pipeline script** | `scripts/run_pipeline.sh` | `scripts/run_pro_pipeline.sh` |
| **Config** | `config/{control,treatment,shared}.yaml` | `config/{swebp_control,swebp_treatment}.yaml` |

## Logs: `experiment/logs/`

Organized as `logs/<model_slug>/<batch>/<arm>/`. The model slug is produced by `lib/model.py:model_dir()`, which replaces `/` with `--` (e.g. `deepseek/deepseek-v4-flash` → `deepseek--deepseek-v4-flash`).

```
logs/
├── deepseek--deepseek-v4-flash/
│   ├── n18_v4_flash/                    # Verified batch (n=18 instances)
│   │   ├── control/
│   │   │   └── <instance_id>/
│   │   │       ├── 1.traj.json          # full trajectory (states, actions, submission)
│   │   │       ├── 1.manifest.json      # metadata (arm, instance, model, run_id)
│   │   │       ├── 1.log                # human-readable agent text log
│   │   │       ├── 2.traj.json, ...
│   │   │       └── ...
│   │   └── treatment/
│   │       └── ...
│   ├── pro_pilot_teleport_v4_flash/     # Pro batch (N reps of 1 instance)
│   │   ├── swebp_control/
│   │   │   └── <instance_id>/                  # instance subdir (NOT flat)
│   │   │       ├── <instance_id>.rep01.traj.json
│   │   │       ├── <instance_id>.rep01.pred    # submitted patch
│   │   │       ├── <instance_id>.rep02.traj.json, ...
│   │   │       └── ...
│   │   ├── swebp_control_rep01.log             # logs at BATCH level: <arm>_repNN.log
│   │   ├── swebp_control_rep02.log
│   │   ├── swebp_treatment/
│   │   │   └── <instance_id>/
│   │   │       └── ...
│   │   ├── swebp_treatment_rep01.log
│   │   └── ...
│   └── prompt_study_adherence/          # Prompt-study batch (many arms)
│       ├── decision_rules/
│       ├── minimalist/
│       ├── qi_lean/
│       ├── qi_power/
│       └── treatment/
├── deepseek--deepseek-v4-pro/
│   └── ...
├── anthropic--claude-haiku-4-5-20251001/
│   └── pilot/
└── pro_pilot/                           # Pro harness eval artifacts
    └── swebp_{control,treatment}/
```

### Key file types in `logs/`

| File | Description |
|------|-------------|
| `{rep}.traj.json` | Full agent trajectory — messages, tool calls, token usage (`extra.response.usage`), patch submission. Pro names it `<instance_id>.repNN.traj.json` inside the instance subdir |
| `{rep}.manifest.json` | **Verified/Lite only** — per-run metadata: `arm`, `instance_id`, `run_id`, `model`, `exit_status`, timestamps. Pro batches do **not** write a manifest |
| `{rep}.log` | Human-readable console log (rich-formatted agent output). Verified/Lite: `<instance>/<rep>.log`. **Pro: at the batch level**, named `<arm>_repNN.log` (e.g. `swebp_control_rep01.log`) — not beside the trajectory, not named by instance |
| `{rep}.pred` | **Pro only** — the submitted patch diff, `<instance_id>.repNN.pred` in the instance subdir |

### Layout (both tracks nest the same way)

Both tracks put each instance in its own subdirectory under the arm — Pro is **no longer flat**:

- **Verified/Lite**: `logs/<model>/<batch>/<arm>/<instance_id>/<rep>.traj.json` (rep = `1`, `2`, …)
- **Pro**: `logs/<model>/<batch>/<arm>/<instance_id>/<instance_id>.repNN.traj.json` — same nesting, but the run-id filename repeats the `instance_id` and uses `repNN`. Pro's per-rep `.log` files live one level up at the **batch** level as `<arm>_repNN.log`, and Pro writes **no `.manifest.json`**.

`lib/trajmeta.py:infer_path_meta()` detects the arm **structurally** — it's the directory that directly contains the instance subdirectory — so it handles both tracks (and arbitrary arm names like `treatment_short`) without relying on fixed depths.

## Results: `experiment/results/`

```
results/
├── eval_results.db                     # Central SQLite DB: all harness pass/fail results
├── pro_eval/                           # Pro harness raw outputs
│   └── swebp_{control,treatment}/
│       ├── patches.json                # submitted patches for eval
│       ├── eval_results.json           # harness result JSON
│       └── <instance_id>/
│           ├── swebp_*_output.json     # per-test pass/fail
│           └── swebp_*_std{out,err}.log
├── pro_runs/                           # Pro batch results (one directory per batch)
│   └── <batch>/
│       ├── runs.csv
│       ├── eval_results.csv
│       ├── eval_test_failures.csv      # individual failing tests per rep
│       ├── runs_with_success.csv
│       ├── qi_commands.csv
│       └── pro_stats_summary.csv
├── runs/                               # Verified/Lite batch results
│   └── <batch>/
│       ├── runs.csv
│       ├── eval_results.csv
│       ├── eval_test_failures.csv
│       ├── runs_with_success.csv
│       ├── qi_commands.csv
│       ├── stats.json                  # JSON statistical summary
│       ├── stats_summary.txt           # human-readable stats with bootstrap CIs
│       └── charts/                     # matplotlib charts (if generated)
├── reports/                            # SWE-bench harness report JSONs
│   └── sourceminder-{arm}.qiexp_{batch}_{model}_{arm}_rep{N}.json
└── samples/                            # Instance-sample manifests (timestamped, seeded)
    ├── sample_20260618_*.json
    └── pro_sample_20260621_*.json
```

### Key result files (per batch)

| File | Produced by | Columns / contents |
|------|-------------|-------------------|
| `runs.csv` | `analyze_{pro_}trajectories.py` | `model`, `arm`, `instance_id`, `run_id`, `turn_count`, `total_input_tokens`, `total_tokens`, `peak_prompt_tokens`, `cost`, `qi_invocations`, `grep_invocations`, `file_read_invocations`, `qi_parent_calls`, `qi_verbose_calls`, `patch_chars`, `patch_lines`, `files_touched`, `submitted`, plus format-tax columns `empty_content_turns`, `reasoning_recovered_turns`, `reasoning_recovered_rate` (see `PRO_ANALYZE.md` → *Format tax*) |
| `eval_results.csv` | `evaluate_{pro_}patches.py` | `model`, `arm`, `instance_id`, `rep`, `outcome`, `resolved`, `failure_mode`, `pass_rate`, `required_passed`, `required_total`, `f2p_total`, `f2p_passed`, `p2p_total`, `p2p_passed` |
| `eval_test_failures.csv` | `evaluate_{pro_}patches.py` | Individual failing tests per rep (for breadth analysis) |
| `runs_with_success.csv` | `merge_results.py` | Left-join of `runs.csv` + `eval_results.csv` (at least columns from both; runs missing eval get `outcome=not_evaluated`) |
| `qi_commands.csv` | `extract_{pro_}qi_commands.py` | One row per shell command across all trajectories: `model`, `arm`, `instance_id`, `run_id`, `turn`, `command`, `tool`, `qi_pure`, `qi_results`, plus qi flag columns (`qi_limit`, `qi_expand`, `qi_verbose`, etc.) |
| `pro_stats_summary.csv` | `analyze_pro_stats.py` | Per-arm medians/means/IQR, MWU p-values, bootstrap CIs, resolve rates with Wilson intervals, blow-up rates |
| `stats.json` | `analyze_stats.py` | Structured JSON: per-model+arm token/turn medians, IQR, mean, SD |
| `stats_summary.txt` | `analyze_stats.py` | Human-readable formatted stats with bootstrap confidence intervals |

### `eval_results.db` — central truth

The canonical source for all harness pass/fail results. Defined in `lib/paths.py` as `results/eval_results.db`. Keyed by `(run_tag, arm, instance_id, rep)`. The `eval_results.csv` in each batch directory is a re-export from this DB, not the primary store.

## Data Flow

### Verified / Lite pipeline

```
run_experiment.py
  └→ logs/<model>/<batch>/<arm>/<instance>/<rep>.traj.json
                    │
     ┌──────────────┼──────────────┐
     ▼              ▼              ▼
analyze_        evaluate_      extract_
trajectories    patches        qi_commands
  │               │               │
  ▼               ▼               ▼
runs.csv      [eval_results.db] qi_commands.csv
  │               │               │
  │               ▼               ▼
  │          eval_results.csv  report_qi_commands  (stdout)
  │               │
  └───────┬───────┘
          ▼
    merge_results.py
          │
          ▼
  runs_with_success.csv
          │
          ├──→ analyze_stats.py → stats.json + stats_summary.txt + charts/
          └──→ compare_models.py → model_comparison.json + cross-model charts/
```

### Pro pipeline

```
run_pro_reps.py
  └→ logs/<model>/<batch>/<arm>/<instance_id>/<instance_id>.repNN.traj.json + .pred
     (per-rep .log at the batch level: logs/<model>/<batch>/<arm>_repNN.log)
                    │
     ┌──────────────┼──────────────┐
     ▼              ▼              ▼
analyze_pro_    evaluate_pro_   extract_pro_
trajectories    patches         qi_commands
  │               │               │
  ▼               ▼               ▼
runs.csv      eval_results.csv  qi_commands.csv
  │            + eval_test_        │
  │            failures.csv        ▼
  │               │           report_pro_qi_commands  (stdout)
  │               │
  │               ▼
  │          analyze_pro_eval_granular  (stdout)
  │               │
  └───────┬───────┘
          ▼
    merge_results.py  (shared between both pipelines)
          │
          ▼
  runs_with_success.csv
          │
          ▼
    analyze_pro_stats.py → pro_stats_summary.csv + report (stdout)
```

### `merge_results.py` — shared

The merge script is shared between both pipelines. It left-joins `eval_results.csv` onto `runs.csv` by `(arm, instance_id, run_id/rep)`. Runs in `runs.csv` but not in `eval_results.csv` get `outcome=not_evaluated` — never silently dropped.

## Other Directories

### `experiment/dbs/` — per-instance index databases

SQLite `code-index.db` files, one per SWE-bench Pro instance. Built by `index_instance_pro.sh` (or `pre_index.py` for Verified). Filenames: `<instance_id>.db`. For **Pro**, the host DB is mounted read-only at `/code-index.db.seed` and copied to the ramdisk `/dev/shm/code-index.db` at runtime (qi reads RAM; see `PRO_HARNESS.md` → *Qi Delivery Mechanism*). The Verified track mounts at `/testbed/code-index.db`.

### `experiment/data/`

| File | Purpose |
|------|---------|
| `pool.csv` | Verified/Lite sampling pool (instance metadata with Docker image digests) |
| `pool_pro.csv` | Pro sampling pool |
| `swebench_pro/test.parquet` | SWE-bench Pro test dataset (

### `experiment/vendor/`

Third-party code for the Pro experiment:

- `swebench_pro_mini/` — ScaleAI mini-swe-agent fork (patch generation runner)
- `swebench_pro_os/` — Official SWE-bench Pro harness (evaluation, baseline trajectories)

## Locating a Batch by Name

Batch names correspond to a directory under `results/runs/` or `results/pro_runs/`. The same name (or a matching `--batch-id`) is used to locate logs under `logs/<model>/`. The mapping is not perfectly uniform — some log directories use different naming than results directories, and `prompt_study*` batches may have ad hoc log paths. When in doubt, read the `run_pro_pipeline.sh` logs-resolver logic (or pass `--logs` explicitly to each script).

## See Also

- `PRO_ANALYZE.md` — Pro analysis pipeline reference (exact commands, gotchas)
- `RUN_BOOKKEEPING.md` — manifest, ledger, and resume system
- `LOGS_BATCH_FOLDER.md` — log directory contract details
- `NAMED_BATCH.md` — batch ID conventions
