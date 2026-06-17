# Pre-Registration: qi Context Preservation Experiment

**Status:** FINALIZED — ready to freeze (dry-run + eval plumbing verified §16)
**Date of Freeze:** (set to the date the first confirmatory run begins)
**Branch:** `experiment`

> This document was revised on 2026-06-16 to reflect the pivot from SWE-bench
> Lite to SWE-bench Verified and the switch from `env_startup_command` to direct
> DB volume mounting. See §18 for the deviation log. The §16 end-to-end dry-run
> and SWE-bench eval plumbing have now passed; the only remaining pre-freeze item
> (exact tool-output tokenizer) is deferred to the confirmatory study. Do not
> change prompts, task definitions, or metrics after the freeze date.

---

## 1. Research Question

Does directing an LLM coding agent to use `qi` for code exploration reduce
prompt-token consumption and preserve more usable context-window headroom than a
standard bash-only workflow, without degrading task success on SWE-bench
Verified?

## 2. Claims Under Test

- **Primary:** The `qi` treatment reduces total input tokens, peak prompt tokens,
  and tool-output tokens compared to control.
- **Non-inferiority:** The `qi` treatment does not reduce task success rate
  (within a 5 percentage-point margin).

## 3. Experimental Platform

### 3.1 Benchmark

| Property | Value |
|----------|-------|
| Benchmark | SWE-bench Verified (`princeton-nlp/SWE-bench_Verified`) |
| Instances | 500 total across Python repositories |
| Multi-file pool | 70 instances with 2–6 edited files (14.0%) — the population we sample from |
| Split | official Verified test split |
| Evaluation harness | `swebench.harness.run_evaluation` (Docker-based) |

**Why Verified, not Lite:** All 300 SWE-bench Lite instances are single-file
edits by curation. qi's advantage is cross-file navigation, which cannot manifest
on single-file tasks. Verified contains 70 multi-file instances
(`verified_docker_images.txt`); we sample our study set from those.

### 3.2 Agent Framework

| Property | Value |
|----------|-------|
| Framework | mini-swe-agent |
| Version | `v2.4.1` |
| Commit | `531dbaf336b9d42486a89ab97814c6b3f1d1c0ee` |
| Base config | `swebench.yaml`, overridden by `config/control.yaml` / `config/treatment.yaml` |
| Environment class | `docker` (uses `sweb.eval.x86_64.<instance_id>:latest` images) |
| Tool interface | Bash only (no custom tools) |
| History | Linear append-only message history |

### 3.3 SWE-bench

| Property | Value |
|----------|-------|
| Version | `v4.1.0` |
| Commit | `726c5461e2ef52d83cf1ea2107870a8bb3328d57` |
| Benchmark subset | SWE-bench Verified (`princeton-nlp/SWE-bench_Verified`) |
| Split | `test` (500 instances) |
| Evaluation | `swebench.harness.run_evaluation` (Docker-based) |
| Image naming | `sweb.eval.x86_64.<id>` where `__` → `_1776_` (e.g. `django_1776_django-10554`) |

### 3.4 Model

| Property | Value |
|----------|-------|
| Model | `deepseek-v4-flash` |
| Litellm identifier | `deepseek/deepseek-v4-flash` |
| Mode | Thinking mode (default) |
| Provider | DeepSeek API (`api.deepseek.com`) |
| Context window | 1M tokens |
| Max output tokens | 384K |
| Pricing (input, cache miss) | $0.14/M tokens |
| Pricing (output) | $0.28/M tokens |
| Model version | Frozen at time of first run |

### 3.5 SourceMinder / qi

| Property | Value |
|----------|-------|
| SourceMinder commit | `3bb273c22e311273779f2bb7b7452a10793875aa` (+ docstring-guard fix to `python/python_language.c`) |
| Build | `./configure --enable-all && make` |
| qi binary | `build/qi-static` (statically linked, via `experiment/build_qi_static.sh`) |
| Indexer binary | `build/index-python-static` (statically linked) |
| Index database | Pre-built `code-index.db` per instance at its base commit |
| Languages indexed | Python only (SWE-bench repos are all Python) |
| qi delivery (treatment) | `qi-static` + per-instance `code-index.db` **volume-mounted** into the container (see §5.5) |

### 3.6 Indexer note

`index-python` previously called `exit(1)` on string nodes exceeding the
16384-byte `CLEANED_WORD_BUFFER` (e.g. sympy's 17960-byte module docstrings),
aborting the DB transaction. A length guard in `handle_string()` now skips
oversized strings with a stderr warning. This fix is part of the frozen build.

## 4. Experimental Arms

### Arm A: Naturalistic Control

- Standard mini-swe-agent with bash as the only tool (`config/control.yaml`).
- Instance template: the feature request + submission instructions, **no mention
  of qi**.
- `qi` is **not** mounted into the container and is not on PATH.
- Agent may use grep, rg, cat, sed, find, ls, python, pytest, git, and any other
  standard Unix tools.

### Arm B: qi-Preferred Treatment

- Identical to Arm A (`config/treatment.yaml`), with exactly two differences:
  1. `qi` + a pre-built `code-index.db` are volume-mounted into the container.
  2. A qi instruction block is appended to the otherwise-identical instance
     template:

```
I have a sqlite code index (code-index.db), and querying tool (qi) that I want
you to use instead of your usual Search/Grep/Read commands for this project.
If you run into issues using qi, fall back to grep, otherwise, be as persistent
as you can in learning how to use this new tool.
I'll ask you for your feedback on using it at the end of this session.
To start, run: qi --help
```

> **Template parity:** Both arms override `instance_template` with the *same*
> short template; the qi block is the only textual difference. This eliminates
> the earlier confound where the treatment inherited a different-length default
> template than the control.

## 5. Pilot Design

### 5.1 Instance Selection

- **Population:** the 70 SWE-bench Verified instances with 2–6 edited files
  (`verified_docker_images.txt`).
- **Count:** 20 instances sampled from that population.
- **Seed:** `42` (recorded; sample frozen in `verified_instance_ids.txt`).
- The same 20 instances are used for both arms.

### 5.2 Repetition

- **Runs per instance per arm:** 10
- **Total runs:** 20 instances × 2 arms × 10 repetitions = 400 runs
- Run order: fully randomized across instance-arm-rep combinations (seed-controlled).
- Each run starts from a fresh session with no prior conversation state.

### 5.3 Termination Conditions

A run ends when any of these occur:
- Agent explicitly declares completion (`echo COMPLETE_TASK_AND_SUBMIT_FINAL_OUTPUT && cat patch.txt`).
- Turn budget exhausted: **45 turns** (overrides mini-swe-agent's default 250).
- Agent becomes blocked and stops.

(There is no per-instance wall-clock budget; termination is bounded by the
45-turn limit. `environment.timeout: 60` is a per-command exec timeout, not a
per-instance budget.)

### 5.4 Agent Configuration Overrides

Config is applied in order: `swebench.yaml` (mini-swe-agent default) →
`shared.yaml` (common overrides) → arm-specific config (`control.yaml` /
`treatment.yaml`). Shared settings reduce duplication; arm configs contain only
their arm-specific differences.

**`config/shared.yaml` (applied to both arms):**

| Parameter | Default | Experiment value | Reason |
|-----------|---------|-----------------|--------|
| `agent.step_limit` | 250 | 100 | Turn budget |
| `model.model_name` | `anthropic/claude-sonnet-4-5-...` | `deepseek/deepseek-v4-flash` | Chosen model |
| `model.cost_limit` | 3 (dollars) | 0 (disabled) | We track costs ourselves |
| `environment.timeout` | 60 (seconds) | 60 | Per-command exec timeout (not a per-instance budget); adequate for qi queries |

**Arm-specific configs (`control.yaml` / `treatment.yaml`):**

| Parameter | Control | Treatment | Reason |
|-----------|---------|-----------|--------|
| `agent.instance_template` | short template | same + qi instruction block | Template parity between arms |
| `environment.run_args` | *(none)* | `--rm` + qi/DB mounts | Inject qi + index (§5.5) |

> Change `step_limit` or `model_name` in `shared.yaml` to affect both arms at once.

### 5.5 qi Delivery (treatment arm)

`run_args` is replaced wholesale by `recursive_merge` (lists are not appended),
so `--rm` is included explicitly alongside the mounts:

```yaml
run_args:
  - "--rm"
  - "-v"
  - "<repo>/build/qi-static:/usr/local/bin/qi"
  - "-v"
  - "<repo>/experiment/dbs/<instance_id>.db:/testbed/code-index.db"
```

`run_pilot.py` generates a per-instance temporary YAML that substitutes the
correct DB path, because a static config cannot express a per-instance path in a
list value. The DB mount is **read-write** (not `:ro`): SQLite needs a writable
WAL/lock file even for read-only queries, and the container is ephemeral.

## 6. Reset Procedure (Before Each Run)

1. Start a fresh container from the instance's `sweb.eval.x86_64.<id>` image
   (already at the base commit).
2. Mount the pre-built `code-index.db` (treatment only).
3. Start a fresh mini-swe-agent session with no prior state.
4. Verify `qi` is (treatment) / is not (control) available per arm assignment.
5. Begin logging before the first model turn.

## 7. Metrics

### 7.1 Primary Outcome Metrics

| Metric | Definition | Relevance |
|--------|-----------|-----------|
| Total input tokens | Sum of all prompt tokens across all turns | Overall context consumption |
| Peak prompt tokens | Maximum prompt tokens in any single turn | Context-window pressure; headroom = 1M − peak |
| Total tool-output tokens | Sum of tokenized tool outputs shown to the model | Exploration overhead |
| Task success | Binary: SWE-bench `resolved` (all tests pass). Runs with no `.traj.json` or `submitted=False` are `task_success = False` without harness execution | Non-inferiority check |

### 7.2 Secondary Outcome Metrics

| Metric | Definition |
|--------|-----------|
| Total completion tokens | Sum of all model output tokens |
| Total reasoning tokens | Sum of thinking-mode reasoning tokens |
| Turn count | Number of agent turns before termination |
| Wall-clock time | Elapsed time from start to termination |
| Full-file reads | `cat`/`sed`/`head`/`tail` invocations |
| qi invocations | Number of `qi` commands executed |
| grep/rg invocations | Number of grep/rg commands executed |
| Patch size | Lines changed in final diff |

### 7.3 Token Measurement

- **Model turns:** DeepSeek API's `usage.prompt_tokens` /
  `usage.completion_tokens`, read from each assistant message's
  `extra.response.usage` in the trajectory file.
- **Tool outputs:** Tokenize the exact text injected into context using the
  DeepSeek tokenizer (see §18 Q4). The pilot uses a ~4 chars/token approximation
  until the exact tokenizer is wired in; the confirmatory study uses the exact
  tokenizer.
- **Do not** report primary results from character/word/line counts alone.

## 8. Instrumentation

Metrics are extracted post-hoc — no agent-side instrumentation is required:
`LitellmModel.query()` already serializes the full API response (including
`usage`) into each assistant message's `extra.response`, and the submitted patch
is stored at `info.submission`. Three scripts produce the analysis tables:

1. `analysis/analyze_trajectories.py` — token/usage metrics from the `.traj.json`
   files → `runs.csv`.
2. `analysis/evaluate_patches.py` — runs `swebench.harness.run_evaluation` over
   the `info.submission` patches (batched per `(arm, rep)` because the harness
   keys predictions by `instance_id`) → `eval_results.csv` with the per-run
   `resolved`/`outcome` (the `task_success` source).
3. `analysis/merge_results.py` — left-joins the two on `(arm, instance_id, run)`
   → `runs_with_success.csv`, the combined table used for statistics.

### 8.1 Per-Run Output (CSV)

`runs.csv` (from `analyze_trajectories.py`), per run: `run_id`, `instance_id`,
`arm`, `exit_status`, `turn_count`, `total_input_tokens`, `peak_prompt_tokens`,
`total_completion_tokens`, `total_reasoning_tokens`, `total_cached_tokens`,
`tool_output_tokens_approx`, `qi_invocations`, `grep_invocations`,
`file_read_invocations`, `submitted`. (Note: `task_success` is **not** in this
file — it is added by the join below.)

`eval_results.csv` (from `evaluate_patches.py`), per run: `arm`, `instance_id`,
`rep`, `exit_status`, `has_patch`, `outcome`, `resolved`.

`runs_with_success.csv` (from `merge_results.py`): every `runs.csv` column plus
`outcome` and `task_success`. A run with no eval row is `task_success = 0`,
`outcome = not_evaluated`.

**Run-attempt accounting (do not rely on trajectory presence alone).** A run is
recorded in two places independent of whether it produced a `.traj.json`:

- **Per-run manifest** `logs/<arm>/<instance>/<rep>.manifest.json` — written by
  `run_pilot.py`: `status` `started` → `completed`/`failed`, with `exit_code`,
  `traj_written`, `started_at`/`finished_at`. This is the resume source of truth
  (`status: started` with no finish = a crashed/orphaned run, re-run on resume).
- **Append-only ledger** `logs/run_ledger.jsonl` — written by `run_experiment.py`
  after *every* attempt returns (the parent survives a child `SIGKILL`), one row
  per attempt: `arm`, `instance_id`, `rep`, `started_at`/`finished_at`,
  `returncode`, `traj_written`, `exit_status`, `ok`. This is the durable record
  of crashed/no-result runs and the basis for completion-rate (§9.3) and
  failure-by-cause (§11) reporting (`run_experiment.py` prints an exit-status
  breakdown at the end of each session).

A run that terminates *with* a trajectory but no patch — `exit_status`
`LimitsExceeded` (turn budget) or an error — still yields a `runs.csv` row and is
scored `task_success = 0` (`empty_patch`). A run that produces *no* trajectory
has no `runs.csv` row and no patch; it is captured by the manifest + ledger and
mapped to `task_success = 0`. Both are normal failure modes, not errors to
suppress; do not silently exclude them.

### 8.2 Trajectory Layout

`logs/<arm>/<instance>/<run_id>.traj.json` (one file per run), aggregated into a
single CSV for analysis.

## 9. Statistical Analysis Plan (Pilot)

### 9.1 Pilot Goals

1. Estimate mean and variance of primary metrics in each arm.
2. Compute observed effect sizes.
3. Power-analyze using pilot estimates for the confirmatory study (target: 80%
   power to detect ≥20% reduction in peak prompt tokens).

### 9.2 Pilot Analyses

- **Descriptive:** Median, IQR, full distributions for all metrics, by arm.
- **Visual:** Boxplots / violin plots for the three token metrics.
- **Inferential (exploratory):** Mann-Whitney U for token metrics, two-proportion
  z-test for success rate. NOT confirmatory — the pilot is underpowered by design.

### 9.3 Predefined Pilot Evaluation

The pilot is informative if:
- ≥90% of runs complete (produce a patch before budget exhaustion).
- ≥30% of control runs succeed (non-trivial baseline).
- Variance estimates are stable (bootstrap SE of variance < 30% of the point estimate).

## 10. Success Thresholds (Confirmatory Study)

Defined here for pre-registration; NOT expected to be met by the pilot. The
treatment is successful if ALL hold:
1. Median total input tokens reduced by ≥20%.
2. Median peak prompt tokens reduced by ≥20%.
3. 95% bootstrap CI for the median difference in peak prompt tokens excludes zero
   in the beneficial direction.
4. Success rate non-inferior within a 5 percentage-point margin.

## 11. Handling Failures

- All runs remain in the dataset regardless of outcome.
- Failed runs (no patch produced, including those with no `.traj.json` file)
  are reported separately: count, cause (budget, timeout, error), and
  whether failure patterns differ by arm.
- Runs without a `.traj.json` file are `task_success = False` with no SWE-bench
  evaluation attempted. They contribute to completion-rate statistics but not to
  token metrics (no data).
- Do not silently exclude difficult runs.

## 12. Contamination Rules

- Control arm: qi is not mounted; any `qi` invocation fails with "command not
  found" (logged; the agent can retry with bash tools).
- Treatment arm: qi is available; we record actual qi invocation count per run.
- No per-protocol analysis is needed for the pilot since contamination is
  structurally prevented.

## 13. Randomization

- Run order: fully randomized across all instance-arm-rep combinations.
- Random seed: fixed and recorded.
- Blocking: none (simple randomization is sufficient for the pilot).

## 14. Threats to Validity

| Threat | Mitigation |
|--------|-----------|
| Model nondeterminism | 10 repeated runs per instance per arm |
| Instance selection bias | 20-instance random sample (seed 42) from the multi-file pool |
| qi index staleness | Pre-built at base commit; identical DB across reps |
| SWE-bench evaluation variance | Standard Docker-based harness |
| Tokenizer mismatch | DeepSeek's own API-reported token counts for model turns |
| Learning effects | Randomized run ordering; no carryover session memory |
| Template-length confound | Identical short template in both arms; qi block is the only delta |
| Environment differences between arms | Identical Docker image; qi mount is the only difference |

## 15. Artifacts to Publish

- This `PREREGISTRATION.md` (time-stamped and frozen before data collection)
- `verified_instance_ids.txt` (the 20 sampled instances)
- System/instance prompts for each arm (exact text — `config/*.yaml`)
- `logs/` (all per-run trajectories + `<rep>.manifest.json` attempt records)
- `logs/run_ledger.jsonl` (append-only record of every run attempt, incl. crashes)
- `analysis/analyze_trajectories.py` (token-metric extraction → `runs.csv`)
- `analysis/evaluate_patches.py` (SWE-bench harness → `task_success`, `eval_results.csv`)
- `analysis/merge_results.py` (join → `runs_with_success.csv`)
- Statistical analysis script / notebook
- `PILOT_RESULTS.md` (summary report)

## 16. Implementation Tasks (Pre-Data-Collection)

- [x] Compile SourceMinder at `3bb273c` (+ docstring fix) with `--enable-all`
- [x] Build static `qi-static` and `index-python-static`
- [x] Pre-index pipeline (`index_instance.sh`, `pre_index.py`) via Docker images
- [x] Verify qi queries work on indexed DBs (Django, sympy)
- [x] Resolve qi delivery into containers (direct volume mount)
- [x] Arm config files with matched templates (`config/control.yaml`, `treatment.yaml`)
- [x] Sample 20 Verified instances (seed 42) → `verified_instance_ids.txt`
- [x] Run orchestrator with randomization + resume (`run_experiment.py`)
- [x] Metric-extraction script (`analysis/analyze_trajectories.py`)
- [x] Pull + index all 20 Verified instances
- [x] End-to-end dry-run: 1 control + 1 treatment on one instance — trajectory
      written, token counts present, qi available (treatment), patch produced
      (matplotlib-14623)
- [x] Run SWE-bench evaluation on dry-run patches to confirm pass/fail plumbing
      (treatment `resolved`, control `empty_patch`; `evaluate_patches.py` +
      `merge_results.py`)
- [ ] Wire the exact DeepSeek tokenizer for tool-output tokens (§18 Q4) —
      deferred to the confirmatory study; not required for the pilot
- [ ] Freeze this document with a date and begin data collection

## 17. Budget Estimate

| Item | Estimate |
|------|----------|
| Runs | 400 (20 instances × 2 arms × 10 repeats) |
| Estimated input tokens per run | ~300K–450K (Verified multi-file) |
| Estimated output tokens per run | ~20K |
| Cost per run | ~$0.07 |
| Dry-run overhead | ~20 runs (~$1.5) |
| **Total estimated cost** | **~$30** |
| Budget ceiling | $50 |

Budget includes only API token costs. Compute costs are local.

## 18. Open Questions / Deviation Log

1. ~~**mini-swe-agent version**~~ → `v2.4.1`, commit `531dbaf`.
2. ~~**SWE-bench commit**~~ → `v4.1.0`, commit `726c546`.
3. **Thinking mode:** Use thinking mode (default) for realism, but report
   `prompt_tokens` and `completion_tokens` separately so reasoning tokens don't
   contaminate the input-token metric. **Decision: keep thinking mode.**
4. **Tool-output tokenizer:** Identify the tokenizer that matches DeepSeek V4
   Flash. Pilot uses a ~4 chars/token approximation
   (`analyze_trajectories.py:CHARS_PER_TOKEN`); switch to the exact tokenizer for
   the confirmatory study.
5. ~~**Timeout per instance:**~~ → **Resolved: 45-turn budget only, no wall-clock.**
   The dry-run (matplotlib-14623) hit 45 turns on both arms, so 45 is the binding
   constraint for Verified instances; revisit if the pilot shows runs routinely
   maxing out turns without submitting.
6. ~~**Indexer coverage of Python constructs**~~ → Verified on Django and sympy;
   docstring-overflow crash fixed (§3.6). Remaining repos (astropy, matplotlib,
   xarray, pylint, sphinx) covered as `pre_index.py` completes.
7. ~~**qi delivery into Docker**~~ → **Resolved: direct volume mount.**
   `env_startup_command` is broken in mini-swe-agent v2.4.1 with
   DockerEnvironment, so we mount `qi-static` and `<instance_id>.db` directly via
   `run_args`. Mount is read-write (SQLite WAL); container is ephemeral.
8. ~~**Benchmark choice**~~ → **Verified, not Lite.** Lite is 100% single-file
   edits; we sample 20 from the 70 multi-file Verified instances (§3.1, §5.1).
9. **Static binary portability:** `qi-static` / `index-python-static` are built on
   the host. Verified on Django images; confirm on non-Django images during the
   dry-run and pre-indexing.
