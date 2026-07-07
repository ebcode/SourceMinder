# Experiment Code-Quality & Orderliness Cleanup Plan

A ranked audit of the Python under `experiment/` (12 scripts, ~2,470 LoC). Each
item has **Why**, **What**, and an **Effort/Impact** tag. Ranked top-to-bottom by
(impact ÷ effort): do the high tiers first. Nothing here changes experiment
*semantics* — it's about correctness hygiene, where files land, and not
repeating the same code five times.

## Script inventory

| Script | LoC | Role |
|--------|-----|------|
| `run_experiment.py` | ~410 | Orchestrator: loops (arm, instance, rep), resume, ledger, batch-id |
| `run_one.py` | ~260 | Single run: builds configs, invokes mini-swe-agent |
| `pre_index.py` | 174 | Pull images + build per-instance qi DBs |
| `check_dbs.py` | 42 | `PRAGMA integrity_check` over `dbs/*.db` |
| `analysis/evaluate_patches.py` | ~370 | Run SWE-bench harness → outcomes → DB/CSV |
| `analysis/estimate_cost.py` | ~350 | Token/price → cost projection |
| `analysis/analyze_trajectories.py` | ~210 | Trajectory → per-run metrics CSV |
| `analysis/eval_db.py` | ~200 | WAL SQLite store for eval results |
| `analysis/traj_diff.py` | 114 | Side-by-side A/B trajectory diff |
| `analysis/merge_results.py` | ~100 | Join metrics + eval → `runs_with_success.csv` |
| `analysis/analyze_stats.py` | ~550 | Statistical analysis: Wilcoxon, bootstrap CIs, size interaction, charts |
| `analysis/compare_models.py` | ~430 | Cross-model comparison: effects, correlation, success parity, charts |
| `analysis/correct_submissions.py` | ~200 | Recover working-tree patches for empty-submission reps, re-score |
| `scripts/build_pool.py` | 153 | Pin sampling-pool image digests |
| `scripts/sample_pool.py` | 181 | Draw a balanced reproducible sample |
| `scripts/run_pipeline.sh` | ~30 | Single-command pipeline runner: chains 5 analysis steps |
| `lib/paths.py` | 90 | Canonical filesystem roots, `batch_run_dir()`, `cwd()` |
| `lib/trajmeta.py` | ~80 | Path parsing (4-tuple), `batch_of`, `n_files_of`, `patch_files_of` |
| `lib/instances.py` | ~20 | `parse_instance_ids` |
| `lib/model.py` | ~20 | `MODEL`, `model_dir()`, `normalize_model()` |
| `lib/cmds.py` | ~20 | `QI_RE`, `GREP_RE`, `READ_RE`, `count_tools()` |
| `lib/naming.py` | ~20 | `image_of()`, `instance_id_of()` |
| `lib/dbcheck.py` | ~20 | `integrity_ok()` |

---

## Tier 0 — Correctness & Safety (do first)

> **Status: COMPLETE** — implemented across sessions 20260618_190017 and 20260619_012328.

### 0.1 Remove the hardcoded API key ✅
- **What was done:** `run_experiment.py:60` → `API_KEY = ""`. The env-var path
  (`{PROVIDER}_API_KEY`) is the sole key source. A gitignored `experiment/.env`
  sources the key at launch. Provider-side rotation is the user's external step.
- **Effort/Impact:** trivial / critical.

### 0.2 Stop the harness from littering the experiment root ✅
- **What was done:** `lib/paths.py` exports `cwd()` context manager and
  `REPORTS_DIR`. `evaluate_patches.py:346` wraps the harness call in
  `with paths.cwd(paths.REPORTS_DIR)`. `correct_submissions.py` also uses it.
  Stray report files were deleted.
- **Effort/Impact:** low / high.

### 0.3 Replace silent `except Exception: pass` ✅
- **What was done:** `estimate_cost.py:115` now catches `(OSError, csv.Error)`
  and prints a warning to stderr before falling back. Other exception handlers
  catch specific types (`ValueError`, `TypeError`, `AttributeError`) rather than
  bare `Exception`.
- **Effort/Impact:** trivial / medium.

---

## Tier 1 — Orderliness: where files land (the core ask)

> **Status: COMPLETE** (1.1, 1.2 done; 1.3 partial — see remaining work below).

The root problem: **generated artifacts are interleaved with source code**, and
**output roots are re-derived ad hoc** in each script. All three issues are now
resolved (1.2 was the enabling change; 1.1 and 1.3 followed).

### 1.1 Separate results from code with one results root ✅
- **What was done:** `lib/paths.py` defines the canonical tree:
  `RESULTS_DIR`, `RUNS_DIR`, `REPORTS_DIR`, `SAMPLES_DIR`, `EVAL_DB`. All
  generated output (runs/, reports/, samples/, eval_results.db) lives under
  `results/`. The old `analysis/20260617_*` timestamped dirs are gone.
  `analysis/` is code-only (plus design docs).
- **Effort/Impact:** medium / high.

### 1.2 Centralize all path roots in one module ✅
- **What was done:** `experiment/lib/paths.py` (90 lines) exports
  `REPO_ROOT`, `EXPERIMENT_DIR`, `LOGS_DIR`, `DBS_DIR`, `DATA_DIR`, `RESULTS_DIR`,
  `CONFIG_DIR`, `ANALYSIS_DIR`, `SCRIPTS_DIR`, `RUNS_DIR`, `REPORTS_DIR`,
  `SAMPLES_DIR`, `EVAL_DB` — all derived from `Path(__file__)`. Also exports
  `batch_run_dir()`, `new_run_dir()`, and the `cwd()` context manager. All
  analysis scripts and orchestrators import from it; argparse defaults reference
  these constants so they resolve regardless of cwd.
- **Effort/Impact:** medium / high.

### 1.3 Make `analysis/` imports package-safe ⚠️ PARTIAL
- **What is done:** `experiment/lib/` is a real package (has `__init__.py`).
  Most analysis scripts use `from lib import paths` / `from lib.trajmeta import
  ...`. The shared code (`trajmeta`, `cmds`, `naming`, `model`, `instances`,
  `dbcheck`) all lives in `lib/`.
- **What remains:** `experiment/analysis/` has no `__init__.py`. Three scripts
  (`evaluate_patches.py:64`, `correct_submissions.py:36`, and implicitly) still
  do bare `import eval_db` — which only works when `sys.path[0]` happens to
  include `analysis/`. Adding `__init__.py` and changing these to
  `from . import eval_db` (or `from analysis import eval_db`) would make them
  cwd-independent.
- **Effort/Impact:** low / medium.

---

## Tier 2 — DRY: stop repeating the same logic

> **Status: COMPLETE** — all five duplication clusters extracted into `lib/`
> across sessions 20260618_205250 (NAMED_BATCH), 20260618_213250 (LOGS_BATCH_FOLDER),
> and 20260618_190017 (paths.cwd).

### 2.1 `infer_path_meta` + `ARMS` (trajectory path → model/arm/instance) ✅
- **What was done:** Moved to `lib/trajmeta.py`. Exports `ARMS`,
  `infer_path_meta` (4-tuple: model, batch_id, arm, instance), `rep_of`,
  `batch_of`, `n_files_of`, `patch_files_of`. Both `analyze_trajectories.py` and
  `evaluate_patches.py` import from it. Single source for the
  `logs/<model>/<batch>/<arm>/<instance>` contract.
- **Effort/Impact:** low / high.

### 2.2 Instance-list parsing ✅
- **What was done:** `lib/instances.py` exports `parse_instance_ids(path) -> list[str]`
  (strip blanks, skip `#`, take first token). Used by `run_experiment.py`,
  `pre_index.py`, and `evaluate_patches.py`.
- **Effort/Impact:** low / medium.

### 2.3 instance_id ↔ Docker image munging ✅
- **What was done:** `lib/naming.py` exports `image_of(instance_id)` and
  `instance_id_of(image)` with the shared `sweb.eval.x86_64.` prefix constant.
  Used by `pre_index.py` and `build_pool.py`.
- **Effort/Impact:** low / medium.

### 2.4 MODEL / MODEL_DIR ✅
- **What was done:** `lib/model.py` exports `MODEL`, `model_dir(model)`, and
  `normalize_model(model)`. Both `run_experiment.py` and `run_one.py` import
  from it. The orchestrator has a `--model` CLI flag; the global is set once in
  `main()`.
- **Effort/Impact:** low / medium.

### 2.5 qi/grep command detection regex ✅
- **What was done:** `lib/cmds.py` exports the canonical `QI_RE`, `GREP_RE`,
  `READ_RE` and `count_tools(cmd)`. `analyze_trajectories.py` and `traj_diff.py`
  import from it. Single metric definition across all analyzers.
- **Effort/Impact:** low / medium.

---

## Tier 3 — Consistency & polish

> **Status: PARTIAL** — only 3.4 done. Items below are still unaddressed.

### 3.1 One CLI/exit convention
- **Why:** Mixed styles: `evaluate_patches`/`merge_results`/pool scripts do
  `raise SystemExit(main())` returning ints; `run_experiment`/`run_one` call
  `main()` and `sys.exit()` internally; `check_dbs.py` is bare module-level code
  with no `main()`. Output dir flags are variously `--dir`, `--out`, `--logs`.
- **What:** Pick `def main() -> int` + `raise SystemExit(main())` everywhere;
  give `check_dbs.py` a `main()`. Standardize on `--out`/`--dir` naming (document
  which means "file" vs "directory").
- **Effort/Impact:** medium / medium.

### 3.2 Drop mutable-global pattern in `run_one.py`
- **Why:** `main()` does `global MODEL, MODEL_DIR` and mutates module state from
  the parsed args — surprising and untestable.
- **What:** Thread `model` through `run_instance(...)` as a parameter (it already
  takes most config explicitly).
- **Effort/Impact:** low / low.

### 3.3 Tidy `traj_diff.py`
- **Why:** `json.load(open(path))` leaks the handle; single-letter names (`a`,
  `b`, `va`, `vb`, `ca`, `cb`) hurt readability; it had its own private qi-regex.
- **What was done:** Now imports `from lib import cmds` (shared QI_RE).
- **What remains:** Replace `json.load(open(path))` with `Path.read_text()` +
  `json.loads`; use descriptive variable names.
- **Effort/Impact:** low / low.

### 3.4 De-duplicate the sqlite integrity check ✅
- **What was done:** `lib/dbcheck.py` exports `integrity_ok(db_path) -> tuple[bool, str]`.
  Both `check_dbs.py` and `run_one.check_prerequisites` call it.
- **Effort/Impact:** low / low.

### 3.5 Pricing table maintainability (`estimate_cost.py`)
- **Why:** `PRICING` is a hand-maintained dict that will silently go stale; the
  prefix-match fallback can mis-price a typo'd model as Haiku.
- **What:** Lower priority — at minimum make the "no exact match, used prefix X"
  path print which entry it matched, not just the silent default. Consider moving
  the table to a small `pricing.json` data file.
- **Effort/Impact:** low / low.

---

## Proposed end-state layout

```
experiment/
  lib/                      # ✅ EXISTS — shared package (Tier 1.2, 2.x)
    __init__.py
    paths.py                # REPO_ROOT, *_DIR constants (Path(__file__)-based)
    instances.py            # parse_instance_ids
    trajmeta.py             # ARMS, infer_path_meta (4-tuple), batch_of, n_files_of, patch_files_of
    naming.py               # image_of / instance_id_of
    cmds.py                 # QI_RE/GREP_RE/READ_RE, count_tools
    model.py                # MODEL, model_dir(), normalize_model
    dbcheck.py              # integrity_ok()
  analysis/                 # CODE ONLY (+ design docs)
    ⚠️ __init__.py          # MISSING — needed for package-safe imports (Tier 1.3)
    analyze_trajectories.py  evaluate_patches.py  eval_db.py
    merge_results.py  estimate_cost.py  traj_diff.py  correct_submissions.py
    analyze_stats.py  compare_models.py
  scripts/                  # build_pool.py  sample_pool.py  run_pipeline.sh
  run_experiment.py  run_one.py  pre_index.py  check_dbs.py
  config/   data/   dbs/   logs/
  results/                  # ✅ EXISTS — all generated artifacts (Tier 1.1)
    runs/<ts>|<batch>/   reports/   samples/   eval_results.db
  *.md                      # docs (unchanged)
```


---

## Suggested execution order

1. ~~**Tier 0** (key, harness cwd, silent except)~~ — **DONE.**
2. ~~**Tier 1.2** (`lib/paths.py`)~~ — **DONE.**
3. ~~**Tier 1.1 + 1.3** (results root + package imports)~~ — **MOSTLY DONE.** Finish 1.3: add `analysis/__init__.py` and fix the 3 bare `import eval_db` to relative/package imports.
4. ~~**Tier 2** (DRY into `lib/`)~~ — **DONE.** All five duplication clusters extracted.
5. **Tier 3** (polish) — remaining: 3.1 (CLI convention), 3.2 (global mutation), 3.3 (traj_diff tidy), 3.5 (pricing table). Opportunistic, none blocking.

The only remaining structural item is **Tier 1.3** (the bare `import eval_db` in 3 scripts). Everything else in Tiers 0–2 is implemented.

---

## Open questions

1. **Results root name/placement** — resolved: `experiment/results/`. Implemented.
2. **Backward compatibility** — resolved: legacy `analysis/20260617_*` dirs have been removed; output now goes to `results/runs/`.
3. **`lib/` vs flat helpers** — resolved: `experiment/lib/` is a real package with `__init__.py`. `analysis/` is NOT yet a package (see Tier 1.3 remaining).
4. **Scope of this pass** — resolved: Tiers 0–2 are done. Only Tier 1.3 (bare `import eval_db`) and Tier 3 polish remain.
```
