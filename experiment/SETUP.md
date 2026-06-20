# Experiment Setup Guide

Step-by-step from a fresh checkout to a running experiment.

## Prerequisites

| Requirement | Minimum Version | Check With |
|-------------|----------------|------------|
| Linux (x86-64) | — | `uname -m` → `x86_64` |
| Python | 3.10+ | `python3 --version` |
| Docker | 20.10+ | `docker --version` |
| SQLite static lib | — | `apt-get install libsqlite3-dev` |
| tree-sitter static lib | — | built from source in `tree-sitter/lib/` |
| ~30 GB free disk | — | `df -h .` (Docker images are large) |

You also need an API key for the model provider you plan to use (see [Model Configuration](#model-configuration) below).

## 1. Clone and Install

```bash
git clone https://github.com/anomalyco/SourceMinder.git
cd SourceMinder
git checkout 3bb273c   # pinned commit from PREREGISTRATION.md
```

Create the experiment venv and install mini-swe-agent + SWE-bench:

```bash
python3 -m venv experiment/.venv
source experiment/.venv/bin/activate
pip install --upgrade pip

# mini-swe-agent pinned version
pip install "mini-swe-agent==2.4.1"

# SWE-bench harness for evaluation
pip install swebench==4.1.0 datasets
```

Verify:

```bash
experiment/.venv/bin/mini-extra --help
```

## 2. Build SourceMinder and qi

```bash
# Build everything (produces .o files needed for static linking)
./configure --enable-all && make

# Build the static qi binary for Docker containers
bash experiment/build_qi_static.sh
# → build/qi-static (~3MB, fully static)

# Build the static Python indexer
bash experiment/build_index_python_static.sh
# → build/index-python-static (~3.5MB, fully static)
```

Verify the static binaries:

```bash
ldd build/qi-static              # → "not a dynamic executable"
ldd build/index-python-static    # → "not a dynamic executable"
qi test -i func --limit 3        # basic smoke test (needs a DB; just check it runs)
```

## 3. Choose Instances

The experiment uses **SWE-bench Verified** (multi-file instances) — qi's cross-file
navigation advantage only manifests on multi-file tasks, so Lite (all single-file
edits by design) was dropped. The pre-generated list is:

| File | Source | Count | Description |
|------|--------|-------|-------------|
| `experiment/verified_instance_ids.txt` | SWE-bench Verified | 20 | 2-4 file edits (cross-file signal) |
| `experiment/verified_docker_images.txt` | SWE-bench Verified | 70 | Larger pool + DockerHub image names |

If you want to sample your own:

```python
import random, json
from datasets import load_dataset

ds = load_dataset("princeton-nlp/SWE-bench_Verified", split="test")
multi = [ex for ex in ds if ex["FAIL_TO_PASS"] and len(ex["FAIL_TO_PASS"].split(",")) >= 2]
sampled = random.Random(42).sample(multi, min(20, len(multi)))

with open("experiment/my_instances.txt", "w") as f:
    for ex in sampled:
        f.write(f"{ex['instance_id']} {ex['repo']} {ex['base_commit']}\n")
```

## 4. Pull Docker Images and Pre-Index

This step pulls the SWE-bench Docker images (each ~3-8 GB) and builds a `code-index.db`
for each instance. Expect ~30-60 minutes for pulls and ~10 minutes for indexing with 4 workers.

```bash
# One command: pull + index all instances
python3 experiment/pre_index.py \
    --instances-file experiment/verified_instance_ids.txt \
    --pull --workers 4
```

To index a single instance (e.g. for testing):

```bash
docker pull swebench/sweb.eval.x86_64.django_1776_django-10554:latest
bash experiment/index_instance.sh django__django-10554
```

Indexed databases go to `experiment/dbs/<instance_id>.db`. The script skips
instances that already have a DB, so it's safe to re-run as new images are pulled.

## 5. Quick Test: Run Both Arms on One Instance

Before launching the full experiment, verify the pipeline works end-to-end:

```bash
export DEEPSEEK_API_KEY="sk-..."

# Control arm (no qi)
python3 experiment/run_one.py \
    --arm control \
    --instance django__django-10554

# Treatment arm (qi available)
python3 experiment/run_one.py \
    --arm treatment \
    --instance django__django-10554
```

Trajectories land in `experiment/logs/<arm>/<instance_id>/<run_id>.traj.json`.

Open a `.traj.json` and confirm `messages[*].extra.response.usage.prompt_tokens`
exists — this is the primary metric source.

## 6. Run the Full Experiment

```bash
# Dry-run first to see the plan (no API key needed)
python3 experiment/run_experiment.py \
    --instances-file experiment/verified_instance_ids.txt \
    --reps 10 --dry-run
# → 400 runs (20 instances × 2 arms × 10 reps)

# Run for real (single worker, safe for overnight)
python3 experiment/run_experiment.py \
    --instances-file experiment/verified_instance_ids.txt \
    --reps 10

# Test a single instance first:
python3 experiment/run_experiment.py \
    --instance-id matplotlib__matplotlib-14623 --runs 5

# Or with parallel workers (be mindful of API rate limits)
python3 experiment/run_experiment.py \
    --instances-file experiment/verified_instance_ids.txt \
    --reps 10 --workers 2

# Stream logs in real-time (prints tail -f path for each run):
python3 experiment/run_experiment.py \
    --instances-file experiment/verified_instance_ids.txt \
    --reps 10 --quiet

# Re-run crashed runs (e.g. after a ValueError/LimitsExceeded prompt crash):
python3 experiment/run_experiment.py \
    --instances-file experiment/verified_instance_ids.txt \
    --reps 10 --retry-failed
```

The script is restart-safe — completed runs are skipped based on their per-run
`.manifest.json` (the source of truth for resume), with `.traj.json` presence as
fallback for runs predating the manifest system. Runs that crashed mid-execution
(`exit_status` not in {`Submitted`, `LimitsExceeded`}) are also skipped by default;
pass `--retry-failed` to re-run them. Randomized order is seed-controlled (seed 42).

To run only one arm (e.g. treatment-only debugging):

```bash
python3 experiment/run_experiment.py \
    --instances-file experiment/verified_instance_ids.txt \
    --reps 5 --arms treatment
```

## Model Configuration

### Switching DeepSeek Models

Edit the `model_name` field in `experiment/config/shared.yaml` (applied to
both arms), or override on the command line:

```bash
# Override via -c (applied over the config file)
python3 experiment/run_one.py \
    --arm control --instance django__django-10554 \
    -c experiment/config/shared.yaml \
    -c experiment/config/control.yaml \
    -c <(echo 'model: {model_name: "deepseek/deepseek-v4"}')
```

Available DeepSeek models via litellm:

| Litellm ID | Model | Context | Notes |
|------------|-------|---------|-------|
| `deepseek/deepseek-v4-flash` | V4 Flash | 1M | Fast, cheapest. Default for this experiment. |
| `deepseek/deepseek-v4` | V4 | 1M | Stronger reasoning, ~2× cost of Flash. |
| `deepseek/deepseek-reasoner` | R1 | 128K | Deep reasoning, uses thinking tokens. |

### Using Other Providers

The experiment uses litellm through mini-swe-agent. You can switch providers by
changing the `model_name` prefix and the corresponding API key env var.

#### Anthropic

```
model_name: "anthropic/claude-sonnet-4-5-20250929"
```

Set the API key:

```bash
export ANTHROPIC_API_KEY="sk-ant-..."
```

Recommended models:

| Litellm ID | Model | Context |
|------------|-------|---------|
| `anthropic/claude-sonnet-4-5-20250929` | Sonnet 4.5 | 200K |
| `anthropic/claude-haiku-4-5-20250929` | Haiku 4.5 | 200K |
| `anthropic/claude-opus-4-20250514` | Opus 4 | 200K |

**Note:** Anthropic models handle tool-calling natively and may produce different
agent behavior than DeepSeek. The `model_kwargs.drop_params: true` in the config
ensures litellm strips unsupported parameters automatically.

#### OpenAI

```
model_name: "openai/gpt-4.1"
```

Set the API key:

```bash
export OPENAI_API_KEY="sk-..."
```

Recommended models:

| Litellm ID | Model | Context |
|------------|-------|---------|
| `openai/gpt-4.1` | GPT-4.1 | 1M |
| `openai/gpt-4.1-nano` | GPT-4.1 Nano | 1M |
| `openai/gpt-4o` | GPT-4o | 128K |

**Note:** `gpt-4.1` has a 1M context window (matches DeepSeek) and is the best
apples-to-apples comparison. `gpt-4.1-nano` is similar cost but lower capability.

#### Google Gemini

```
model_name: "gemini/gemini-2.5-pro"
```

Set the API key:

```bash
export GEMINI_API_KEY="..."
```

Recommended models:

| Litellm ID | Model | Context |
|------------|-------|---------|
| `gemini/gemini-2.5-pro` | Gemini 2.5 Pro | 1M |
| `gemini/gemini-2.5-flash` | Gemini 2.5 Flash | 1M |

**Note:** Gemini models behave differently with tool-calling and truncation.
Non-thinking mode is the default. Test a few dry-runs before launching batch.

### Cross-Provider Caveats

When switching providers:

1. **Token counting differs.** Each provider has its own tokenizer. The
   `usage.prompt_tokens` from the API response is the authoritative count,
   and mini-swe-agent stores it in the trajectory automatically.

2. **Thinking/reasoning tokens.** DeepSeek V4 Flash/V4 and Anthropic Sonnet
   use thinking modes that produce internal reasoning tokens. These appear
   in `usage.completion_tokens` but not in visible output. Factor this into
   token-efficiency comparisons across providers.

3. **Cost tracking.** Adjust `--cost-limit` (in dollars) in `run_one.py`
   to match the provider's pricing. The default `0.5` per run is conservative
   for DeepSeek but may be too low for Anthropic Opus or Gemini 2.5 Pro.

4. **Rate limits.** DeepSeek allows ~100 concurrent requests on pay-as-you-go.
   Anthropic and OpenAI have stricter tier-based limits. If using `--workers`,
   start with `--workers 1` and increase gradually.

## Environment Variables

| Variable | Purpose |
|----------|---------|
| `DEEPSEEK_API_KEY` | DeepSeek API key (required for DeepSeek models) |
| *(inline)* | Alternatively, paste your key into `API_KEY = ""` at the top of `run_experiment.py` — takes precedence over the env var |
| `ANTHROPIC_API_KEY` | Anthropic API key |
| `OPENAI_API_KEY` | OpenAI API key |
| `GEMINI_API_KEY` | Google Gemini API key |
| `DOCKER_HOST` | Docker daemon socket (if not at default) |

All are standard litellm environment variables — see the
[litellm docs](https://docs.litellm.ai/docs/providers) for the full list.

## Directory Layout After Setup

```
experiment/
├── .venv/                          # Python venv with mini-swe-agent, swebench
├── SETUP.md                        # This file
├── TROUBLESHOOTING.md               # Common failure modes and fixes
├── MONITORING.md                    # Watching runs in progress
├── RUN_BOOKKEEPING.md               # Manifest, ledger, and resume system
├── PREREGISTRATION.md              # Frozen experimental design
├── README.md
├── config/
│   ├── shared.yaml                # Shared overrides (step_limit, model, timeout)
│   ├── control.yaml               # Arm A: bash only
│   └── treatment.yaml             # Arm B: qi-preferred
├── dbs/                            # Pre-built code-index.db per instance
│   ├── django__django-10554.db
│   ├── sympy__sympy-13877.db
│   └── ...
├── logs/                           # Per-run trajectories + manifests + ledger
│   ├── run_ledger.jsonl            # Append-only record of every run attempt
│   ├── control/
│   │   └── <instance_id>/
│   │       └── <rep>.traj.json + <rep>.manifest.json
│   └── treatment/
│       └── ...
├── build_qi_static.sh              # Build static qi binary
├── index_instance.sh               # Index a single instance via Docker
├── pre_index.py                    # Batch index orchestrator
├── run_one.py                    # Single arm/instance/rep runner
├── run_experiment.py               # Full experiment orchestrator
├── compare.sh                      # [legacy] single-instance A/B via current.db symlink
├── analysis/
│   ├── DESIGN.md                   # Patch-evaluation design notes
│   ├── analyze_trajectories.py     # Extract per-run metrics -> runs.csv
│   ├── evaluate_patches.py         # SWE-bench harness -> eval_results.csv
│   ├── merge_results.py            # Join -> runs_with_success.csv
│   └── <timestamp>/                # Per-analysis-run output (generated)
├── verified_instance_ids.txt       # 20 multi-file Verified instances
└── verified_docker_images.txt      # 70 Verified instances + DockerHub image names
```

## Next Steps

1. Extract per-run token metrics with the analyzer (reads `messages[*].extra.response.usage`):

   ```bash
   python3 experiment/analysis/analyze_trajectories.py \
       --logs experiment/logs --dir experiment/analysis/session-01
   ```

2. Evaluate the submitted patches for `task_success` (Docker-only, no API spend):

   ```bash
   python3 experiment/analysis/evaluate_patches.py \
       --logs experiment/logs --dir experiment/analysis/session-01
   python3 experiment/analysis/merge_results.py \
       --dir experiment/analysis/session-01
   ```

3. Compute aggregate statistics from `runs_with_success.csv` (treatment-vs-control
   token deltas, success rates).

All three analysis scripts accept `--dir` (defaults to `analysis/<YYYYMMDD_HHMMSS>/`),
so repeated analysis runs produce timestamped directories that never clobber each other.
