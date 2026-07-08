# Indexer Watch-Only Plan

Replace the static seed-DB snapshot with a live daemon: pre-built DB is still
copied to ramdisk at container start, then the language indexer runs in a new
`--watch-only` mode — skipping the initial full pass and entering the watch loop
to re-index files as the agent edits them.

## Motivation

Currently qi queries see a **static snapshot** of the repo at agent startup.
After the agent edits a file, the index is stale — subsequent qi queries
(`--usage`, `--def`, wildcard searches) return pre-edit results. The daemon
closes this gap: inotify detects file changes, the indexer re-indexes the
changed file(s), and qi sees fresh symbols.

The pre-built DB is still needed: the initial full pass takes minutes on large
repos (teleport = 291 MB), and `/dev/shm` sizing depends on the pre-built size.

## Tasks

### Phase 1: Add `--watch-only` flag to the indexer

- [ ] **Add `FLAG_WATCH_ONLY` to flag bits** (`shared/indexer_main.c`)
  - Add `#define FLAG_WATCH_ONLY (1 << 7)` alongside the other flags (~line 101)

- [ ] **Parse `--watch-only` in `scan_cli_flags()`** (~line 107)
  - Add: `else if (strcmp(argv[i], "--watch-only") == 0) flags |= FLAG_WATCH_ONLY;`

- [ ] **Parse `--watch-only` in `should_skip_config_line()`** (~line 125)
  - Add: `if ((cli_flags & FLAG_WATCH_ONLY) && strstr(line, "--watch-only") == line) return 1;`

- [ ] **Parse `--watch-only` in `main()` arg loop** (~line 314)
  - Add: `else if (strcmp(argv[i], "--watch-only") == 0) { watch_only = 1; }`
  - Declare `int watch_only = 0;` alongside `daemon_mode` (~line 305)

- [ ] **Validate flag combinations**
  - `--watch-only` + `--once` is contradictory → error and exit
  - `--watch-only` with `MODE_FILES` → error and exit (only meaningful for directory mode)

- [ ] **Guard the initial indexing pass** (~line ~580, the big `for (target in targets)` loop)
  - Wrap in `if (!watch_only) { ... }`
  - When `watch_only`, skip directly to daemon loop setup

- [ ] **Guard the completion message** (~line 628)
  - Wrap `Indexing complete` printf in `if (!watch_only)`

- [ ] **Add startup message for watch-only mode**
  - Print: "Skipping initial index (--watch-only). Watching for file changes..."
  - Must respect `--silent` / `--quiet`

- [ ] **Add `--watch-only` to help output** (~line 240)
  - Add: `  --watch-only                skip initial indexing; watch and re-index on changes`

- [ ] **Verify `reindex_single_file()` semantics**
  - Confirmed at `indexer_main.c:70-93`: deletes old entries by filepath, then inserts fresh parse results. Correct behavior for incremental re-index on edit.

- [ ] **Compile + smoke test**
  - Run `make` in repo root
  - Verify `build/index-python-static --help` shows `--watch-only`
  - Create a throwaway DB with known symbols, edit a file, run `index-python /tmp/dir --db-file /tmp/test.db --watch-only --silent &`, verify re-index picks up the change via `qi` query

### Phase 2: Integrate daemon launch into `run_pro_one.py`

- [ ] **Mount the language indexer binary** (`run_pro_one.py:prepare_treatment_mounts()`)
  - Determine which indexer to use based on the instance's language (same logic as `index_instance_pro.sh:lang_to_indexer()`): python → `build/index-python-static`, go → `build/index-go-static`, ts/js → `build/index-ts-static`
  - Mount at `/usr/local/bin/index-<lang>` (read-only, same pattern as qi-static)
  - Need the `repo_language` info — either read from `pool_pro.csv` or pass as a CLI arg

- [ ] **Add daemon launch in `seed_ramdisk_db()`**
  - After the existing `cp {DB_SEED_PATH} → {RAMDISK_DB_PATH}`
  - Launch: `env.execute("index-python /app --db-file /dev/shm/code-index.db --watch-only --silent &", background=True)`
  - Problem: `env.execute()` is synchronous — need a way to background the process. Options:
    - A) Write a small shell wrapper to the container that backgrounds the daemon
    - B) Use the scaffold's environment to run a background command
    - C) `nohup index-python ... &` in a shell invocation
  - Store daemon PID for cleanup

- [ ] **Add daemon readiness check**
  - After launch, wait for the daemon to set up watches (inotify isn't instant)
  - Simple approach: poll `/dev/shm/code-index.db` mtime or `pidof index-python`
  - Or: just `sleep 0.5` — the watcher setup is fast and the agent's first turn has latency anyway

- [ ] **Add daemon cleanup**
  - In container teardown (the `finally` block in `main()`, or a cleanup callback from `prepare_treatment_mounts()`)
  - `env.execute("kill <pid>")` or `env.execute("pkill index-python")`
  - Docker `--rm` handles it if we miss, but clean shutdown avoids DB corruption

- [ ] **Pass language info to `prepare_treatment_mounts()`**
  - Currently it receives `(config, arm, instance_id, experiment_dir)`
  - Need to know the repo language to select the right indexer binary
  - Options: read from `pool_pro.csv` (like `index_instance_pro.sh` does), or add a `--lang` flag to `run_pro_reps.py` → `run_pro_one.py`

### Phase 3: Update documentation

- [ ] **Update `PRO_HARNESS.md`**
  - Qi delivery mechanism section: document the daemon alongside qi-static and DB
  - Add note that the indexer daemon watches for file changes and keeps the index current
  - Update the architecture diagram to show the indexer daemon process

- [ ] **Update `index_instance_pro.sh`** (optional)
  - No code changes needed (still uses `--once`)
  - Optionally add a comment noting that `--watch-only` is used at runtime inside containers

### Phase 4: Smoke test end-to-end

- [ ] **Single-rep smoke test** on a small instance
  - Run `run_pro_one.py` with treatment arm on a Python instance
  - Verify the daemon starts and shows in `ps aux` inside the container
  - Make an edit, verify qi sees the updated symbol
  - Verify daemon is killed on agent exit

- [ ] **Multi-rep test** (N=2–3) on teleport (Go)
  - Verify the Go indexer daemon works correctly
  - Check that no `empty_patch` artifacts appear from daemon overhead
  - Verify the daemon doesn't cause container resource issues

- [ ] **Check WAL lock behavior**
  - While agent is running qi queries, manually trigger a file edit
  - Verify no "database is locked" errors from either qi or the indexer

## Files Touched

| File | Change |
|------|--------|
| `shared/indexer_main.c` | Add `--watch-only` flag: parsing, validation, guard initial pass |
| `run_pro_one.py` | Mount indexer binary, launch daemon, cleanup |
| `experiment/PRO_HARNESS.md` | Document daemon in qi delivery section |

## Not Changing

| File | Why |
|------|-----|
| `config/swebp_treatment.smconfig` | Already points qi at `/dev/shm/code-index.db` |
| `config/swebp_treatment.yaml` | No prompt changes needed |
| `experiment/index_instance_pro.sh` | Still uses `--once` for pre-indexing |
| `shared/file_watcher.c` | inotify implementation is mature, no changes needed |

## Risks

| Risk | Severity | Mitigation |
|------|----------|------------|
| Daemon not cleaned up on agent crash | LOW | Docker `--rm` kills all processes |
| WAL lock contention (qi reads + indexer writes) | LOW | SQLite WAL handles concurrent readers + single writer natively |
| Daemon consumes CPU during agent session | LOW | Inotify wait is blocking (near-zero CPU when idle); single-file re-index is sub-second |
| Indexer binary missing for language | MEDIUM | Check binary exists at mount time, fail fast with clear error |
| Daemon confound vs control arm | LOW | Background process with near-zero idle CPU; document, don't avoid |
