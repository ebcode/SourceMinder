# Prompt-Engineering Experiments — Runbook

How to define a prompt-variant arm, run it, and analyze how the prompt changed
the agent's behavior. For the full success/token-cost analysis (the main RCT
pipeline) see the outcome pipeline at the bottom; this doc centers on the
*behavioral* question a prompt study actually asks: **did the new prompt change
how the agent uses qi?**

---

## 1. Define a prompt-variant arm

Arms are resolved by convention (see `run_one._arm_config`):

- `config/<arm>.yaml` — the arm's prompt (`agent.instance_template`). **Required.**
- `config/<arm>.smconfig` — *optional.* If present, it is mounted into the
  container's `~/.smconfig` (via `run_one.make_treatment_config`, with
  `-e HOME=/root` pinned) so it can force qi defaults for **every** call the
  agent makes, without relying on the prompt to request them. Format is INI with
  a `[qi]` section whose lines are prepended to argv (CLI flags still override).

Any arm name other than `control` automatically gets the qi binary
(`build/qi-static`) and the per-instance DB mounted.

Example (the composability arm): `config/treatment_compose.yaml` is
`treatment.yaml` + a "qi is a composable unix filter" section, and
`config/treatment_compose.smconfig` forces `-q` (quiet output: no banner /
separator rule / footer).

> Before running any non-control arm, build the static binary:
> `bash experiment/build_qi_static.sh` (run after `make`; produces `build/qi-static`).

---

## 2. Run the arm

```bash
python3 run_experiment.py --arms <arm> --batch-id <batch> --runs 3   # + your instance set
```

Use a space-separated list to run several arms in one batch
(`--arms treatment treatment_compose`). **Note:** `--arms` takes space-separated
values, not commas. Outputs land in `logs/<model>/<batch>/<arm>/<instance>/` and
all analysis is keyed by `--batch <batch>`.

---

## 3. Behavioral analysis (the prompt-study core)

This is the part that answers "did the prompt change behavior?" Both scripts are
**descriptive only** — a prompt study has too few runs per arm for inference;
read medians as direction, not proof.

### 3a. Extract per-command table

```bash
python3 analysis/extract_qi_commands.py --batch <batch>
# writes results/runs/<batch>/qi_commands.csv
```

One row per shell command, paired to its tool output via `tool_call_id`:
`tool` (qi/grep/read/other), the full `command`, `output_chars` /
`output_tokens_approx`, and which qi flags were used. This is the per-COMMAND
companion to `analyze_trajectories.py` (which is per-run).

### 3b. Per-arm report

```bash
python3 analysis/report_qi_commands.py --csv results/runs/<batch>/qi_commands.csv
```

By-arm comparison aimed at the prompt study: qi-vs-grep mix, flag vocabulary
(did the prompt's `--limit`/`-x noise` guidance get used?), output sizes, and
error rate. `--model <m>` to scope; `--cross-model` to compare models.

### 3c. Per-run trajectory metrics

```bash
python3 analysis/analyze_trajectories.py --batch <batch>
# writes results/runs/<batch>/runs.csv
```

One row per run with per-turn token usage (`total_input_tokens`,
`peak_prompt_tokens`, `total_tool_output`), **`turn_count`**, and qi/grep/file
invocation counts, plus a printed by-arm summary. `turn_count` is the key
behavioral lever for prompt studies (more turns ⇒ more re-sent context ⇒ higher
`total_input`).

---

## 4. Full outcome pipeline (success + token cost)

When you also want resolved-rate and the inferential token analysis (same
pipeline as the main experiment):

| Step | Command | Output |
|---|---|---|
| 1 | `analyze_trajectories.py --batch <batch>` | `runs.csv` |
| 2 | `evaluate_patches.py --batch <batch>` *(run by hand — drives Docker)* | `eval_results.csv` |
| 3 | `merge_results.py --batch <batch>` | `runs_with_success.csv` |
| 4 | `analyze_stats.py --batch <batch>` | `stats.json`, `stats_summary.txt`, charts |

---

## 5. Deep-dive utilities

- `compare_runs.py a.traj.json b.traj.json` — side-by-side of two trajectories.
- `traj_diff.py` — trajectory diff (for outlier instances).
- `compare_models.py`, `estimate_cost.py` — cross-model / cost.

---

## Notes / known gaps

- **Composability metrics not yet first-class.** For arms that target
  *fewer round-trips* (e.g. `treatment_compose`), the metrics of interest are
  **commands-per-action (batching)** and **pipe rate (`| head` / `| wc` /
  `| grep`)**. `turn_count` is captured (step 3c), but batching and pipe-rate
  are not in the standard reports — they currently require ad-hoc slicing of
  `qi_commands.csv`. Adding them to `report_qi_commands.py` is cheap (it already
  parses per-command).
- **Why chrome (`-q`) is a small lever and turns are the big one:** chrome
  removal (`-q`) is ~1% of `total_input`; turn count drives the rest, because
  each turn re-sends the growing session (measured ~61× amplification on
  observations). Prompt variants that cut turns move the cost far more than
  output-trimming flags.
