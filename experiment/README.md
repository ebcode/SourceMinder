# qi Context Preservation Experiment

Measures whether instructing an LLM coding agent to use `qi` for code
exploration reduces token/turn/cost consumption relative to a bash-only
control, without hurting task success.

Substrate: **SWE-bench Pro** (multi-language, repo-per-instance), run through
the vendored Scale mini-swe-agent fork against the official `sweap-images`
Docker images. The original SWE-bench **Verified** phase is retired; its
tooling was removed and is recoverable from git history (see the last section).

Each batch is one instance × N reps per arm (control vs treatment). Claims
across instances come from a log-ratio meta-analysis over a curated manifest —
see `docs/CROSS_INSTANCE.md` and `docs/STATISTICAL_METHODS.md`.

> Last verified against checkpoint `5838094` (2026-07-06). Every command below
> was checked against the scripts at that commit; when in doubt, the script
> docstrings are authoritative.

## Directory Structure

```
experiment/
├── README.md                    # This file
├── docs/                        # All documentation (see pointer table below)
├── config/
│   ├── pro_shared.yaml          # Pro overrides shared by both arms (/app cwd, step_limit)
│   ├── swebp_control.yaml       # Pro control arm (bash only)
│   ├── swebp_treatment.yaml     # Pro treatment arm (qi instruction + decision table)
│   └── swebp_treatment.smconfig # Per-arm qi config forced into the container
├── lib/                         # Shared modules (model_dir, pro_test_parser, repo_size, ...)
├── scripts/
│   ├── run_pro_pipeline.sh      # Pro per-batch analysis pipeline (one command)
│   └── sample_pool_pro.py       # Pool sampler
├── data/
│   ├── pool_pro.csv             # Pro instance pool with selection axes
│   ├── pro_resolve_rates.csv    # Frontier-model resolve rates (Scale scoring)
│   └── swebench_pro/            # Local dataset subset consumed by run_pro_one.py
├── dbs/                         # Pre-built code-index.db per instance (seed DBs)
├── logs/                        # Gitignored; per-run trajectories + ledger
│   ├── run_pro_ledger.jsonl     # Append-only record of every Pro run attempt
│   └── <model_dir>/<batch_id>/  # e.g. anthropic--claude-haiku-4-5-20251001/pro_pilot_x_haiku/
├── analysis/                    # Analysis scripts (code only, no .md)
│   ├── cross_instance_manifest.txt  # Batches in the cross-instance publication set
│   └── ...                      # per-batch + cross-batch + selection tools
├── results/pro_runs/            # Per-batch CSVs + charts; _cross/ = meta-analysis
├── vendor/
│   ├── swebench_pro_mini/       # Scale mini-swe-agent fork (patch generation)
│   └── swebench_pro_os/         # Scale SWE-bench_Pro-os (evaluation harness)
├── run_pro_one.py               # One instance/arm/rep through the Scale scaffold
├── run_pro_reps.py              # N reps × both arms in parallel (the orchestrator)
├── prep_pro_dataset.py          # Build data/swebench_pro from the upstream dataset
├── index_instance_pro.sh        # Index one Pro instance's repo into dbs/
└── .venv_pro/                   # Pro venv — REQUIRED interpreter for all Pro scripts
```

## qi Delivery Mechanism (Pro)

The Pro images don't include qi or its index; `run_pro_one.py` injects them:

1. **qi binary** — statically linked `build/qi-static` (built by
   `build_qi_static.sh`), mounted at `/usr/local/bin/qi`.
2. **code-index.db** — the per-instance seed DB under `experiment/dbs/` is
   bind-mounted read-only, then copied to a `/dev/shm` ramdisk inside the
   container so qi serves from RAM and has a writable home for WAL sidecars.
3. **indexer daemon** — a static language indexer watches the repo from
   `/sm-config` so agent edits are re-indexed live.
4. **arm config** — the matching `config/*.smconfig` is forced into the
   container so qi behaves identically across reps of an arm.

Startup ordering (seed → reconcile → git-ref strip → watch daemon) matters and
is documented in `docs/PRO_HARNESS.md`. The ref strip exists because the
sweap-images leak the gold fix via git history.

## Workflow (Pro)

All Pro scripts must run under the Pro venv interpreter.

```bash
# 0. One-time: build qi + the static indexers, prep the dataset, index the instance
./configure --enable-all && make
bash experiment/build_qi_static.sh          # -> build/qi-static
bash experiment/index_instance_pro.sh <instance_id>   # -> experiment/dbs/<...>.db

# 1. Run a batch: N reps x both arms, parallel, resume-safe
experiment/.venv_pro/bin/python experiment/run_pro_reps.py \
    --instance <instance_id> --model anthropic/claude-haiku-4-5-20251001 \
    --batch-id pro_pilot_myrepo_haiku --reps 5 --workers 5
# Logs land in logs/<model_dir>/<batch_id>/; every attempt is recorded in
# logs/run_pro_ledger.jsonl.

# 2. Evaluate patches (Docker, no API spend) — often run separately first
experiment/.venv_pro/bin/python experiment/analysis/evaluate_pro_patches.py \
    --logs experiment/logs/<model_dir>/<batch_id> \
    --dir experiment/results/pro_runs/<batch> --workers 5

# 3. Per-batch analysis pipeline (8 steps; skips eval if already done)
bash experiment/scripts/run_pro_pipeline.sh <batch> \
    --batch-id <batch_id> --model <model> --skip-patch-evals
# -> results/pro_runs/<batch>/: runs.csv, runs_with_success.csv, wall_time.csv,
#    qi_commands.csv, eval_results.csv, pro_stats_summary.csv, charts/

# 4. Cross-instance meta-analysis over the manifest
experiment/.venv_pro/bin/python experiment/analysis/cross_batch_compare.py \
    --manifest experiment/analysis/cross_instance_manifest.txt \
    --out experiment/results/pro_runs/_cross/
```

`pro_batch_status.py` audits which batches are complete. `run_pro_reps.py` is
resume-safe (finished reps are skipped; `--force` redoes them).

## Instance Selection

`analysis/pro_select.py` is the canonical selection CLI: it ranks the pool on
mechanism-grounded axes (notably `n_p2p_files`, regression-test breadth) and
`--screen` applies the known failure-mode screens (already run,
frontier-unresolvable, no p2p breadth, tiny surface). Every selection rule,
demotion, and manifest exclusion is recorded — dated, append-only — in
`docs/SELECTION_RULES.md`. **No manifest edit without a ledger entry.**

## Documentation Map

| Doc | Covers |
|-----|--------|
| `docs/PRO_HARNESS.md` | How run_pro_one.py drives the Scale scaffold; container startup |
| `docs/PRO_ANALYZE.md` | The 8-step per-batch pipeline in detail |
| `docs/CROSS_INSTANCE.md` | Cross-instance meta-analysis + chart set |
| `docs/STATISTICAL_METHODS.md` | Log-ratio meta, sign tests, CIs |
| `docs/SELECTION_RULES.md` | Instance-selection ledger (append-only) |
| `docs/PRO_SELECT.md` | pro_select.py axes and screens |
| `docs/NAMED_BATCH.md` | batch_id system: logs, ledger, results naming |
| `docs/MONITORING.md`, `docs/TROUBLESHOOTING.md` | Watching runs; failure modes |
| `docs/PREREGISTRATION.md`, `docs/EXPERIMENT_PLAN.md` | Original (Verified-era) frozen design + rationale |

## The Verified Phase (retired)

The experiment began on SWE-bench Verified with mini-swe-agent's
DockerEnvironment. That phase is complete and will not be re-run; its runners,
configs, instance lists, pipeline, and raw run data were removed in the
2026-07-06 cleanup (tracked files are recoverable from git history at
checkpoint `5838094` and earlier). Its methodology and findings remain
documented in `docs/PREREGISTRATION.md`, `docs/EXPERIMENT_PLAN.md`,
`docs/PILOT_FINDINGS.md`, and the prompt-study docs. `analysis/merge_results.py`
survives because the Pro pipeline reuses it.
