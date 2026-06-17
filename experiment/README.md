# qi Context Preservation Experiment

Measures whether instructing an LLM coding agent to use `qi` for code
exploration reduces prompt-token consumption (and preserves context-window
headroom) relative to a bash-only control, without hurting task success.

Substrate: **SWE-bench Verified** (multi-file instances), run with
mini-swe-agent inside the official SWE-bench Docker images.

> Read `PREREGISTRATION.md` for the frozen experimental design.
> `EXPERIMENT_PLAN.md` (in this directory) is the higher-level methodology rationale.

## Directory Structure

```
experiment/
├── README.md                  # This file
├── PREREGISTRATION.md         # Frozen experimental design (READ FIRST)
├── EXPERIMENT_PLAN.md         # Higher-level methodology rationale
├── SETUP.md                   # Zero-to-first-run guide
├── TROUBLESHOOTING.md          # Common failure modes and fixes
├── MONITORING.md               # Watching runs in progress
├── RUN_BOOKKEEPING.md           # Manifest, ledger, and resume system
├── build_qi_static.sh         # Build static qi binary for Docker containers
├── verified_instance_ids.txt  # 20 sampled SWE-bench Verified instances (seed 42)
├── verified_docker_images.txt # 70 multi-file Verified instances + image names
├── index_instance.sh          # Index one instance's /testbed via its Docker image
├── pre_index.py               # Batch: pull images + index a whole instance list
├── run_pilot.py               # Run a single arm/instance/rep
├── run_experiment.py          # Orchestrate both arms × N reps, randomized
├── compare.sh                 # [legacy] single-instance A/B via current.db symlink
├── config/
│   ├── shared.yaml            # Shared settings (step_limit, model, timeout)
│   ├── control.yaml           # Control arm (bash only)
│   └── treatment.yaml         # Treatment arm (adds qi instruction + mounts)
├── dbs/                       # Pre-built code-index.db per instance
│   └── <instance_id>.db
├── logs/                      # Per-run trajectories + manifests + run_ledger.jsonl
│   ├── run_ledger.jsonl       # Append-only record of every run attempt
│   ├── control/               # <instance>/<rep>.traj.json + <rep>.manifest.json
│   └── treatment/             # <instance>/<rep>.traj.json + <rep>.manifest.json
└── analysis/
    ├── DESIGN.md                # Patch-evaluation design notes
    ├── analyze_trajectories.py  # Extract per-run token metrics -> runs.csv
    ├── evaluate_patches.py      # SWE-bench harness -> task_success (eval_results.csv)
    ├── merge_results.py         # Join runs.csv + eval_results.csv -> runs_with_success.csv
    └── <timestamp>/             # Per-analysis-run output (runs.csv, eval_results.csv, runs_with_success.csv)
```

## Pinned Versions

| Component | Version | Commit |
|-----------|---------|--------|
| SourceMinder | experiment branch | `3bb273c` |
| mini-swe-agent | v2.4.1 | `531dbaf` |
| SWE-bench | v4.1.0 | `726c546` |
| Model | `deepseek/deepseek-v4-flash` | — |

## qi Delivery Mechanism

The SWE-bench Docker images don't include qi or its index. We inject both via
Docker volume mounts in `treatment.yaml`'s `environment.run_args`:

1. **qi binary** — the statically linked `build/qi-static` is mounted at
   `/usr/local/bin/qi` (no shared-library dependencies).
2. **code-index.db** — pre-built per instance under `experiment/dbs/`, mounted
   at `/testbed/code-index.db`. `run_pilot.py` generates a per-instance temp
   YAML that points the mount at the correct DB.

> Note: `env_startup_command` (copy-at-startup) is **broken** in mini-swe-agent
> v2.4.1 with DockerEnvironment, so we mount the DB directly instead. The mount
> is read-write (not `:ro`) because SQLite needs a writable WAL/lock file; the
> container is ephemeral (`--rm`) so this is safe.

## Workflow

```bash
# 1. Build SourceMinder + the static binaries for Docker
./configure --enable-all && make
bash experiment/build_qi_static.sh        # -> build/qi-static
#   (build/index-python-static is built the same way; see PREREGISTRATION §3.5)

# 2. Pull images + pre-index the Verified instances
python3 experiment/pre_index.py \
    --instances-file experiment/verified_instance_ids.txt --pull --workers 4

# 3. Run the experiment (both arms, randomized order, N reps)
export DEEPSEEK_API_KEY=...
python3 experiment/run_experiment.py \
    --instances-file experiment/verified_instance_ids.txt --reps 10

# Or test a single instance first:
python3 experiment/run_experiment.py \
    --instance-id matplotlib__matplotlib-14623 --runs 5

# 4. Extract per-run token metrics -> <timestamp>/runs.csv
python3 experiment/analysis/analyze_trajectories.py \
    --logs experiment/logs --dir experiment/analysis/session-01

# 5. Evaluate submitted patches (Docker-only, no API spend) -> task_success
python3 experiment/analysis/evaluate_patches.py \
    --logs experiment/logs --dir experiment/analysis/session-01

# 6. Join metrics + outcomes into one table -> runs_with_success.csv
python3 experiment/analysis/merge_results.py \
    --dir experiment/analysis/session-01
```

Use `--dry-run` on `pre_index.py` / `run_experiment.py` to preview without
pulling images or spending tokens. `run_experiment.py` also supports an inline
`API_KEY` constant as an alternative to the `DEEPSEEK_API_KEY` environment
variable, and `--quiet` for real-time log streaming.
