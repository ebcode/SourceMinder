# Pro Harness

How the SWE-bench Pro experiment fits together: the scaffolding, how qi gets injected into Pro containers, how patches get evaluated, and the operational gotchas.

## Architecture Overview

The Pro experiment uses Scale's vendored mini-swe-agent fork as a library (not its CLI). Our wrapper (`run_pro_one.py`) calls the scaffold directly, capturing the full 3-tuple `(exit_status, result, patch)` that the upstream CLI drops.

```
run_pro_reps.py         (orchestrator: ThreadPoolExecutor → subprocess.run)
  └─→ run_pro_one.py    (per-rep wrapper: loads vendored scaffold, injects qi mounts)
        ├─→ vendor/swebench_pro_mini/   (Scale mini-swe-agent fork: agent + models + Docker env)
        │     └─→ Docker container (SWE-bench Pro image, /app)
        │           ├── qi-static          (/usr/local/bin/qi, mount:ro)
        │           ├── code-index.db      (/dev/shm/code-index.db, ramdisk copy of seed)
        │           ├── .smconfig          (/root/.smconfig, mount:ro, treatment only)
        │           ├── index-<lang>       (/usr/local/bin/index-<lang>, mount:ro, treatment only)
        │           ├── <lang>/config      (/sm-config/<lang>/config, mount:ro, treatment only)
        │           ├── shared/config      (/sm-config/shared/config, mount:ro, treatment only)
        │           └── indexer daemon     (background, --watch-only, treatment only)
        │
        └─→ vendor/swebench_pro_os/     (evaluator: runs test suite in Docker)
              ├── run_scripts/          (~1000 instance-specific eval scripts)
              └── swe_bench_pro_eval.py
```

### Two venvs

| Venv | Path | Purpose |
|------|------|---------|
| **Verified/Lite** | `experiment/.venv/bin/python` | mini-swe-agent v2.4.1, swebench v4.1.0 |
| **Pro** | `experiment/.venv_pro/bin/python` | Scale mini-swe-agent fork, datasets, pandas, yaml, docker SDK |

The Pro venv is required for running reps, evaluating patches, and the full analysis pipeline. The Scale fork is installed into `.venv_pro`.

## Vendored Repositories

| Repo | Pinned commit | Purpose |
|------|---------------|---------|
| `scaleapi/mini-swe-agent` | `0d6a460` | Patch generation — the agent scaffold, model wrappers, Docker environment |
| `scaleapi/SWE-bench_Pro-os` | `ca10a60` | Evaluation harness — per-instance `run_script.sh`, `parser.py`, test runner |

Both live under `experiment/vendor/`. We treat them as read-only by default — our code wraps them rather than modifying them — with two deliberate local exceptions to fix non-Anthropic model handling, mirrored in `our_vendored_backups/` so a vendor re-sync doesn't silently drop them:

- **`default.py` `parse_action` — multi-format (LIVE).** The upstream parser only matched ```` ```bash ```` fences. It now accepts ```` ```bash ````, `<command>` tags, and Qwen-style `<tool_call><function name="bash"><parameter=command>...` (first-matching convention wins, still exactly-one-action). This fixed MiMo-v2.5-pro, which emits the XML form and was losing ~14-18% of turns to `found 0 actions`.
- **`litellm_model.py` `reasoning_content` fold — REVERTED 2026-06-26.** A brief change folded `reasoning_content` into a blank `content` (for DeepSeek-v4-flash reasoning turns) and tagged `extra["content_source"]`, with an `interactive.py` header marker. It was reverted because it distorted token/turn counts. **Do not re-add it** — the `our_vendored_backups/README.md` still documents it as shipped, but the live tree does not have it. See `PRO_ANALYZE.md` → *Format tax* for how this affects metrics.

## Setup Prerequisites

```bash
# 1. Build qi-static (one-time, from repo root)
./configure --enable-all && make
bash experiment/build_qi_static.sh          # → build/qi-static (~3 MB, fully static)

# 2. Build language-specific static indexers
bash experiment/build_index_python_static.sh  # → build/index-python-static
bash experiment/build_index_go_static.sh      # → build/index-go-static
bash experiment/build_index_ts_static.sh      # → build/index-ts-static

# 3. Build the Pro dataset (pulls from HuggingFace, writes data/pool_pro.csv)
experiment/.venv_pro/bin/python experiment/prep_pro_dataset.py

# 4. Pull the instance Docker image + pre-index
docker pull <image-from-pool_pro.csv>
bash experiment/index_instance_pro.sh <instance_id>
# → experiment/dbs/<instance_id>.db (~15–290 MB)
```

Verify static binaries:
```bash
ldd build/qi-static              # → "not a dynamic executable"
experiment/.venv_pro/bin/python experiment/check_dbs.py  # DB integrity
```

## Config Anatomy

Two config pairs control each arm, loaded by `run_pro_one.py`:

```
experiment/config/
├── swebp_control.yaml        # Control arm: standard prompt, no qi instruction
├── swebp_treatment.yaml      # Treatment arm: qi workflow instruction + mounts
├── swebp_treatment.smconfig  # Treatment-only: qi CLI flags + DB path
└── pro_shared.yaml           # Shared overrides (step_limit, model, /app cwd)
```

### Template variables

The `instance_template` in each YAML uses several template variables auto-populated by the scaffold:

| Variable | Source | Example |
|----------|--------|---------|
| `{{working_dir}}` | `pwd` inside container | `/app` |
| `{{task}}` | `instance["problem_statement"]` | PR description from the Pro dataset |

The Pro dataset's `problem_statement` field contains three sections composed by Scale: `<problem>`, `<requirements>`, and `<stub>`. The `interface` section often names specific files/symbols, making most instances navigation-trivial for the control arm. This is why instance selection matters: to test qi's navigation edge, pick instances where the fix site is **not** named in the task text.

### Key differences from Verified

| | Verified | Pro |
|---|---|---|
| Working dir | `/testbed` | `/app` |
| Shell | `bash` (non-login) | `bash -lc` (login shell; image's own PATH) |
| Entrypoint | image default | `--entrypoint ""` (Pro images set `ENTRYPOINT ["/bin/bash"]`, which breaks `sleep`) |
| Config | `control.yaml` / `treatment.yaml` | `swebp_control.yaml` / `swebp_treatment.yaml` |

## Running Reps

```bash
experiment/.venv_pro/bin/python experiment/run_pro_reps.py \
    --instance <instance_id> \
    --reps 10 \
    --workers 2 \
    --batch-id pro_pilot_teleport_v4_flash
```

### All CLI flags

| Flag | Default | Effect |
|------|---------|--------|
| `--reps N` | 5 | Reps per arm |
| `--workers N` | 5 | Parallel workers (ThreadPoolExecutor) |
| `--arms ...` | `swebp_control swebp_treatment` | Which arms to run |
| `--instance ID` | qutebrowser (hardcoded) | Instance id |
| `--model ID` | `deepseek/deepseek-v4-flash` | litellm model id |
| `--batch-id BATCH` | (empty) | Routes output to `logs/<model_slug>/<batch>/` instead of `logs/pro_pilot/` |
| `--run-id-prefix P` | (empty) | Prepended to run-ids (e.g. `oldprompt_` → `oldprompt_rep01`) |
| `--force` | false | Redo even if already done |
| `--subset PATH` | local `data/swebench_pro` | Dataset location |
| `--output DIR` | auto (see `default_output()`) | Override output root |

### Resume / idempotency

A rep is considered "done" if its `.traj.json` exists with a clean `exit_status` in `{"Submitted", "LimitsExceeded", "Completed"}`. Crashed or empty-status runs are automatically retried. Pass `--force` to redo clean runs.

### Parallelism model

`run_pro_reps.py` uses a `ThreadPoolExecutor` where each task is a `subprocess.run()` call to `run_pro_one.py`. Threads are appropriate because each task spends its entire life blocked on the subprocess. Each rep gets its own Docker container, isolated from all others.

### Ledger

An append-only ledger at `logs/run_pro_ledger.jsonl` records every completed attempt — including crashes that leave no trajectory. Written by the orchestrator after each subprocess returns.

## Qi Delivery Mechanism

The treatment arm injects four things into the Pro Docker container:

### 1. qi binary (volume mount)

```
-v build/qi-static:/usr/local/bin/qi:ro
```

The static binary has zero shared-library dependencies. Mounted read-only.

### 2. Code-index database (seed → ramdisk)

Two-stage design:

1. **Seed mount**: The host-side `dbs/<instance_id>.db` is mounted read-only at `/code-index.db.seed`. This mount lives outside the repo directory so `git diff` never sees it.
2. **Ramdisk copy**: `run_pro_one.py:seed_ramdisk_db()` copies the seed to `/dev/shm/code-index.db` after the container's `env_startup_command` runs and before the agent starts. `/dev/shm` is a ramdisk, so all qi queries hit RAM.

Why the copy? SQLite WAL mode needs write access for its `-wal`/`-shm` sidecars. A read-only mount would fail to open. The ramdisk copy also isolates concurrent reps — each container has its own DB.

**`--shm-size` sizing** (`prepare_treatment_mounts`): `64 MB base + DB size + WAL allowance`, where the WAL allowance is `max(128 MB, DB size)`. We write the DB at runtime now (reconcile + the watch daemon's per-edit re-indexing), so the `-wal` sidecar lives in the ramdisk too. The indexer runs `journal_mode=WAL` with the default 1000-page autocheckpoint and **no `journal_size_limit`**, so the WAL is not hard-bounded: short qi reads colliding with checkpoints over a long session can let it grow toward the DB's own size before it truncates. Reserving a full-DB WAL allowance covers the worst case (a full rewrite before checkpoint). `--shm-size` is a *ceiling on a lazy tmpfs*, not a reservation, so the headroom only costs RAM if the WAL actually grows — and ramdisk overflow is a hard failure (`database disk image is malformed` / `disk I/O error`), so we err generous. (150 MB DB → 364 MB `/dev/shm`.)

### 3. smconfig (volume mount, treatment only)

```
-v config/swebp_treatment.smconfig:/root/.smconfig:ro
-e HOME=/root
```

The `.smconfig` file forces `-q` (quiet) and `--db-file /dev/shm/code-index.db` on every qi invocation. Config lines are whitespace-tokenized and **appended** after the user's command-line args (after patterns, before any user-specified `--db-file`). Since qi requires patterns first and flags second, and the smconfig flags come last, they never interfere with positional-argument parsing.

Content of `swebp_treatment.smconfig`:
```ini
[qi]
-q
--db-file /dev/shm/code-index.db
```

### 4. Indexer daemon (treatment only)

The seed DB is a static snapshot of the repo at container start. After the agent edits a file, qi queries would see stale symbols from the pre-edit index. The indexer daemon closes this gap: it watches `/app` with inotify and re-indexes any file the agent modifies.

**How it starts** (`run_pro_one.py:start_indexer_daemon`):

```bash
( cd /sm-config && exec index-<lang> /app --watch-only --silent \
      --db-file /dev/shm/code-index.db ) > /dev/shm/sm-indexer.log 2>&1 &
echo $! > /dev/shm/sm-indexer.pid
```

The daemon's PID is written to `/dev/shm/sm-indexer.pid`. `daemon_stop()` sends SIGTERM to that PID; `run_pro_one.py` calls it in its `finally` block.

**`--watch-only` flag**: skips the initial full-repo indexing pass (which takes minutes on large repos) and enters the inotify watch loop directly against the pre-built seed DB.

**`db_open_watch_only`**: the daemon uses a lighter DB open path that skips schema migrations (the `CREATE/DROP INDEX` block in `db_init`). This matters because the seed DB may contain old index names (e.g., `idx_parent_symbol`) that `db_init` would drop and recreate — a write that creates a large WAL and takes 2–3 seconds on a 150 MB DB, during which no watches are set up. `db_open_watch_only` opens the DB and prepares the INSERT statement without touching the schema.

**Path consistency**: the daemon runs with `cwd = /sm-config` (outside `/app`). `get_relative_path` stores files whose path does not start with cwd verbatim, so all `/app/...` paths are stored as absolute — matching the seed DB, which was indexed from outside `/app` by `index_instance_pro.sh`.

**Config mounts**: the daemon resolves `<lang>/config/file_extensions.txt` relative to its cwd. Two read-only mounts provide this:

```
-v python/config:/sm-config/python/config:ro
-v shared/config:/sm-config/shared/config:ro
```

(The language and config subdir are resolved from `pool_pro.csv` via `get_repo_language` in `run_pro_one.py`.)

**Signal handling**: uses `sigaction()` with `SA_RESTART` cleared so that `select()` returns `EINTR` on SIGTERM, allowing the daemon loop to check `keep_running` and exit. (`signal()` on Linux sets `SA_RESTART` by default, which would cause `select()` to restart silently after the handler runs, trapping the daemon indefinitely.)

**Zombie after stop**: when `daemon_stop()` sends SIGTERM and the daemon exits, it becomes a zombie because the container's PID 1 (`sleep 2h`) never calls `wait()`. This is expected and harmless — `docker --rm` reaps all processes when the container is removed.

## Container Startup Sequence

Before the agent's first action, each container goes through an ordered startup that (a) applies the failing tests, (b) closes a git-history leak, and (c) makes the on-disk repo state match the qi index. Order matters — several steps would corrupt each other if reordered.

```
get_sb_environment(config, instance)                    [both arms]
  └─ env_startup_command  (config run.env_startup_command, rendered with instance fields)
       1. before_repo_set_cmd   → reset to base, clean, `git checkout <fix> -- <test files>`
       2. ref-only history strip → delete all refs + reflog expire + gc --prune
seed_ramdisk_db(env)                                    [treatment]  cp seed → /dev/shm
reconcile_startup_changes(env, indexer)                 [treatment]  index the checked-out test files
start_indexer_daemon(env, indexer)                      [treatment]  --watch-only daemon
agent.run(problem_statement)
```

### Step 1 — `env_startup_command`: apply tests, then strip history

`get_sb_environment` (vendored `swebench.py`) runs `config["run"]["env_startup_command"]` once, Jinja-rendered with the instance row, and aborts the run on a non-zero exit (`set -e`). It does two things, in this order:

**1a. `before_repo_set_cmd`** (from the Pro dataset) resets the working tree to `{{base_commit}}`, cleans, then `git checkout <fix> -- <test files>` to lay down the **`test_patch`** — the failing tests the agent must satisfy. It checks out *only* the test files; the gold solution (`patch`) is never written, so every solution file stays at base content on disk (the agent has to write the fix). This must run **first**, while the fix commit still exists, because the checkout reads from it.

**1b. Ref-only contamination guard.** The `jefzda/sweap-images` ship the **full upstream history** with the gold fix commit still reachable from `main`/`master`. Without intervention an agent can `git log --all` / `git show <fix>` to copy the real solution — verified in the openlibrary pilot, where the only control reps that "solved" did exactly that (`git show 1a3928d7`), and the contamination was systemic across the image set. The guard removes the leak:

```sh
git remote remove origin 2>/dev/null || true
git for-each-ref --format='delete %(refname)' refs/heads refs/remotes refs/tags | git update-ref --stdin
git reflog expire --expire=now --all
git gc --prune=now --quiet
```

`before_repo_set_cmd`'s `git checkout <base>` already detached HEAD at base, so deleting every branch/remote/tag ref leaves only base + its ancestors reachable; `gc --prune=now` then deletes the now-unreachable fix commit (`git show <fix>` → "unknown revision"). Crucially this is **ref-only** — no `reset --hard`/`clean`, which would wipe the test files just checked out in 1a. The freshly checked-out test-file *blobs* survive because the index still references them; the full ancestor history survives too, so `git diff` / `git log` / `git blame` still work and the agent can tell a failing test is pre-existing, not its own change.

### Step 2 — Seed the ramdisk

`seed_ramdisk_db()` copies the read-only seed to `/dev/shm/code-index.db` (see *Qi Delivery → Code-index database*). The seed was built at the **base commit**, so it does not yet know about the test files step 1a checked out.

### Step 3 — Reconcile startup changes (treatment)

`reconcile_startup_changes()` indexes the startup-added files into the ramdisk DB **before** the watch daemon starts. This is necessary because:

- `--watch-only` **skips initial indexing** — it trusts the seed for existing state and only catches post-startup events.
- **inotify does not catch git's writes.** `git checkout` writes via atomic temp+rename / index plumbing that does not fire the `IN_MODIFY`/`IN_CREATE` events the daemon re-indexes on. Confirmed empirically on the ansible instance: with the daemon watching, the startup test symbols still returned **0 matches** — and stayed missing regardless of whether the daemon started before or after the checkout. (A *normal* edit is caught fine, and its full-file re-index back-fills the git-checkout content — but that only happens if the agent later edits that exact file.)

The reconcile closes the gap deterministically:

```sh
cd /app && CHANGED=$(git status --porcelain | awk '{print $2}' | sort -u | sed 's#^#/app/#')
[ -z "$CHANGED" ] || ( cd /sm-config && index-<lang> $CHANGED --db-file /dev/shm/code-index.db --silent )
```

**Scope = `git status --porcelain`** — exactly the files startup touched. By construction that is the `test_patch` set, which is disjoint from the gold `patch`; and since the solution is never written to the working tree, this can *never* index solution code (even a full `--once` reconcile would only index the base content of solution files). Indexing the test files is intended: the agent is explicitly told the tests are provided and is meant to read them. Paths are absolutized to `/app/...` so DB rows match the seed's path format. Best-effort: a failure degrades qi coverage of the startup files but does not abort the run.

After this step, `qi` finds the startup-added test symbols, the watch daemon (step 4) catches every subsequent agent edit, and no solution code is reachable via either git or the index.

### Why control gets steps 1a + 1b but not 2–4

`before_repo_set_cmd` and the history strip run for **both** arms (identical repo state is required for a fair comparison; only qi differs). The seed/reconcile/daemon are treatment-only — control has no qi index to maintain.

## Evaluating Patches

Two-step process:

### Step 1: Prepare eval inputs

`prep_pro_eval.py` converts agent `.pred` files into the format the vendored evaluator expects:

```bash
experiment/.venv_pro/bin/python experiment/prep_pro_eval.py \
    --preds experiment/logs/deepseek--deepseek-v4-flash/<batch>/swebp_control \
    --prefix swebp_control \
    --out-dir experiment/results/pro_eval/swebp_control
```

Produces `raw_sample.jsonl` + `patches.json` in `--out-dir`.

### Step 2: Run the evaluator

The vendored evaluator (`vendor/swebench_pro_os/swe_bench_pro_eval.py`) must be run **from the vendor directory** (it resolves `run_scripts/` relative to its own location):

```bash
cd experiment/vendor/swebench_pro_os
../../.venv_pro/bin/python swe_bench_pro_eval.py \
    --dataset_name ../../data/swebench_pro \
    --split test \
    --patch_path ../../results/pro_eval/swebp_control/patches.json \
    --raw_patch_file ../../results/pro_eval/swebp_control/raw_sample.jsonl
```

The evaluation harness:
1. Builds a Docker image from the instance's Dockerfile
2. Applies the agent's patch
3. Runs the instance's `run_script.sh`
4. Runs `parser.py` to convert test output into `output.json`
5. Checks: `(f2p | p2p) ⊆ passed_tests` → resolved

### `evaluate_pro_patches.py` (high-level wrapper)

For convenience, `evaluate_pro_patches.py` automates the prep+eval loop and runs evaluations in parallel:

```bash
experiment/.venv_pro/bin/python experiment/analysis/evaluate_pro_patches.py \
    --logs experiment/logs/deepseek--deepseek-v4-flash/<batch> \
    --dir experiment/results/pro_runs/<batch> \
    --workers 1
```

Produces `eval_results.csv` and `eval_test_failures.csv`. **Always pass both arms in one invocation** — the script opens `eval_results.csv` with `"w"` mode and only writes the arms it processes, so separate single-arm invocations silently clobber each other.

## Gotchas

### Pro images set ENTRYPOINT to `/bin/bash`

Pro Docker images have `ENTRYPOINT ["/bin/bash"]`. The mini-swe-agent scaffold runs `<image> sleep <timeout>` to keep the container alive, which becomes `/bin/bash sleep 3600` and the container dies at startup (exit 126). Both `swebp_control.yaml` and `swebp_treatment.yaml` clear the entrypoint:

```yaml
run_args: ["--rm", "--entrypoint", ""]
```

### `/dev/null` can get unlinked

Go's `go tool compile -o /dev/null` (during failed compiles) unlinks the `/dev/null` device node. The agent's patch-collection step (`git add -A && git diff --cached`) then fails with exit 128, returning an empty patch even though edits are on disk. `run_pro_one.py:recover_empty_patch()` recreates `/dev/null` and retries collection. If a `.traj.json` shows `patch_recovered: true` in `extra_info`, this is what happened.

### Resource oversubscription → empty patches

Running too many parallel workers for the host's CPU count causes command timeouts inside containers → agent bash commands fail silently → empty diffs. Match `--workers` to available cores. For a 6-core box, use `--workers 2`.

### `GOMAXPROCS` in vendored code

The evaluator in `vendor/swebench_pro_os/swe_bench_pro_eval.py` has a hardcoded `GOMAXPROCS=3` edit to cap per-container build parallelism for Go instances. This edit is in vendored code — it will be overwritten if the vendor repo is re-pulled.

### Indexer daemon silently disabled

If the language-specific static indexer binary (`build/index-<lang>-static`) is missing or the repo language is unrecognized, `run_pro_one.py` prints a WARNING and runs the rep without the daemon. The agent still has access to qi (with the static seed DB), but edits it makes won't be reflected in subsequent qi queries. Check the treatment mount log line: it should end with `+ indexer daemon (index-<lang>)`, not `(no indexer daemon)`.

### inotify does not catch `git checkout` (qi misses startup-added files)

The `--watch-only` daemon re-indexes on inotify events, but `git checkout`/`git reset` write files via atomic temp+rename and index plumbing that does **not** fire `IN_MODIFY`/`IN_CREATE`. So files laid down by `before_repo_set_cmd` (the `test_patch`) are invisible to the daemon — and to qi — no matter when the daemon starts relative to the checkout. `reconcile_startup_changes()` (see *Container Startup Sequence → Step 3*) is what makes those files visible; if you remove or reorder it, qi will silently return 0 matches for every startup-added test symbol. The daemon still works for *agent* edits (normal writes), which is all it is responsible for after startup.

### Contamination guard must stay ref-only and run after the test checkout

The history strip in `env_startup_command` deliberately uses only ref deletion + `gc`. Do **not** add `git reset --hard`/`git clean` to it: those would wipe the test files `before_repo_set_cmd` just checked out. It also must run **after** `before_repo_set_cmd`, because the `git checkout <fix> -- <tests>` needs the fix commit to still exist (the strip prunes it). If you see `git checkout <fix>` fail with "unknown revision" at startup, the two steps have been reordered.

### Containers left behind on indexer crash

If `index_instance_pro.sh` crashes mid-index, it can leak a `sm-index-pro-*` container. The script uses an EXIT trap to clean up, but verify with:

```bash
docker ps -a --filter "name=sm-index-pro" --format '{{.ID}}' | xargs -r docker rm -f
```

## See Also

- `SETUP.md` — Verified experiment setup (qi-static build, image pull, pre-index)
- `PRO_ANALYZE.md` — Pro analysis pipeline reference
- `RESULTS_LAYOUT.md` — on-disk layout map
- `PREREGISTRATION.md` — frozen experimental design
- `RUN_BOOKKEEPING.md` — manifest, ledger, and resume system
