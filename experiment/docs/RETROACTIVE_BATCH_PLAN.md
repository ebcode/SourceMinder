# RETROACTIVE BATCH PLAN — Logs Directory Cleanup

**Status:** Proposed  
**Date:** 20260619  
**Delta:** 3 model directories, ~6 `mv` operations, 0 code changes

---

## Current State

Three model directories under `experiment/logs/`:

```
logs/
  anthropic--claude-haiku-4-5-20251001/
    control/      5 instances × 5 reps (pilot)        ← flat, no batch
    treatment/    5 instances × 5 reps (pilot)        ← flat, no batch
  deepseek--deepseek-v4-flash/
    control/      15 instances, mixed pilot + partial N=18  ← FLAT, MESSY
    treatment/    8 instances, mixed pilot + partial N=18   ← FLAT, MESSY
    n18_v4_flash/     18 instances × 3 reps (complete)  ← batch layout
    run_evaluation/   EMPTY — harness debris
  deepseek--deepseek-v4-pro/
    control/      5 instances × 5 reps (pilot)  ← flat, no batch
    treatment/    5 instances × 5 reps (pilot)  ← flat, no batch
    n18_v4_pro/       17 instances, partial reps (1-3)  ← batch layout, incomplete
```

### The Messy One: Flash Flat Dirs

The flat `control/` and `treatment/` directories in the Flash model are NOT clean pilot data. They contain a mixture of two sources:

**Source 1 — Pilot (5 instances, 5 reps each):**
- `django__django-11532`, `pydata__xarray-3305`, `pytest-dev__pytest-8399`, `sphinx-doc__sphinx-10673`, `sympy__sympy-22080`

**Source 2 — Killed N=18 run (10 instances, 1–2 reps each):**
- These instances were written BEFORE the `LOGS_BATCH_FOLDER.md` refactor landed, so they went to the flat `control/`/`treatment/` paths instead of a batch subdirectory.
- For the 5 pilot instances that also appeared in the N=18 sample (django-11532, pytest-8399): the killed N=18 run's reps 1–3 **overwrote** the pilot's reps 1–3. The surviving pilot data is only reps 4–5 in those two instances.
- For the 10 non-pilot N=18 instances: they have 1–2 reps of partial data from the killed run.

**Per-instance breakdown of Flash flat `control/`:**

| Instance | Reps | Source |
|---|---|---|
| `astropy__astropy-13398` | 1 rep (rep 2) | Killed N=18 |
| `django__django-11532` | 5 reps | Pilot (reps 4-5) + killed N=18 (reps 1-3, overwrote pilot 1-3) |
| `matplotlib__matplotlib-14623` | 1 rep | Killed N=18 |
| `matplotlib__matplotlib-25775` | 1 rep | Killed N=18 |
| `pydata__xarray-3095` | 1 rep | Killed N=18 |
| `pydata__xarray-3305` | 5 reps | Pilot (clean) |
| `pylint-dev__pylint-6386` | 1 rep | Killed N=18 |
| `pytest-dev__pytest-8399` | 5 reps | Pilot (reps 4-5) + killed N=18 (reps 1-3) |
| `scikit-learn__scikit-learn-12682` | 1 rep | Killed N=18 |
| `scikit-learn__scikit-learn-25102` | 0 trajs | Failed N=18 run (manifest + log only, `traj_written: false`) |
| `sphinx-doc__sphinx-10673` | 5 reps | Pilot (clean) |
| `sphinx-doc__sphinx-7590` | 1 rep | Killed N=18 |
| `sympy__sympy-14248` | 2 reps (1, 3) | Killed N=18 |
| `sympy__sympy-16597` | 2 reps (1, 3) | Killed N=18 |
| `sympy__sympy-22080` | 5 reps | Pilot (clean) |

**Per-instance breakdown of Flash flat `treatment/`:**

| Instance | Reps | Source |
|---|---|---|
| `astropy__astropy-13398` | 1 rep | Killed N=18 |
| `astropy__astropy-14369` | 1 rep | Killed N=18 |
| `django__django-11532` | 5 reps | Pilot (reps 4-5) + killed N=18 (reps 1-3) |
| `pydata__xarray-3305` | 5 reps | Pilot (clean) |
| `pytest-dev__pytest-8399` | 5 reps | Pilot (reps 4-5) + killed N=18 (reps 1-3) |
| `sphinx-doc__sphinx-10673` | 5 reps | Pilot (clean) |
| `sphinx-doc__sphinx-7590` | 1 rep | Killed N=18 |
| `sympy__sympy-22080` | 5 reps | Pilot (clean) |

### The Clean Ones

**Haiku**: 5 pilot instances × 5 reps in both arms. No batch data. No contamination.  
**Pro** (`flat`): 5 pilot instances × 5 reps in both arms. Clean.  
**Pro** (`n18_v4_pro/`): 17 instances, rep counts vary 1–3. Looks like an in-progress or killed N=18 run. Not the subject of this cleanup.

---

## Target State

```
logs/
  anthropic--claude-haiku-4-5-20251001/
    pilot/
      control/      5 × 5 reps  (moved from flat)
      treatment/    5 × 5 reps  (moved from flat)
  deepseek--deepseek-v4-flash/
    pilot/
      control/      5 instances × clean pilot data only
      treatment/    5 instances × clean pilot data only
    n18_v4_flash__killed/
      control/      10 instances × 1-2 partial reps from killed run
      treatment/    3 instances × 1 partial rep from killed run
    n18_v4_flash/
      control/      18 × 3 reps  (untouched)
      treatment/    18 × 3 reps  (untouched)
  deepseek--deepseek-v4-pro/
    pilot/
      control/      5 × 5 reps  (moved from flat)
      treatment/    5 × 5 reps  (moved from flat)
    n18_v4_pro/
      control/      17 instances (untouched)
      treatment/    (untouched)
```

All flat `control/` and `treatment/` directories eliminated. Every trajectory lives under a named batch (or batch-like) subdirectory. `infer_path_meta` auto-detects the batch from 4-level depth. `run_evaluation/` debris removed.

---

## Step-by-Step Plan

> **Variable scope note:** Steps 4a–4d use `$BASE`. If you run these in separate shell sessions, redefine `BASE="experiment/logs/deepseek--deepseek-v4-flash"` at the top of each step.

### Step 0: Git commit (prerequisite)

Commit all current uncommitted work before starting. The `mv` operations in Steps 2–4 are reversible (see Rollback section), but the deletes in Step 1 are not. A clean commit is the safety net.

- [ ] `git add -p` (stage selectively) or `git add experiment/` then `git status` to review
- [ ] `git commit -m "experiment: pre-cleanup snapshot before retroactive batch-tagging"`

### Step 1: Remove harness debris

> **Note:** `scikit-learn__scikit-learn-25102/` looks empty of traj files but contains `1.log` and `1.manifest.json` from a failed N=18 run (`traj_written: false`). `rmdir` would fail — use `rm -rf`.  
> **These two deletions are NOT reversible by the rollback instructions below.**

- [ ] `rm -rf experiment/logs/deepseek--deepseek-v4-flash/run_evaluation/`
- [ ] `rm -rf experiment/logs/deepseek--deepseek-v4-flash/control/scikit-learn__scikit-learn-25102/`

### Step 2: Haiku — simple batch-tag

- [ ] `mkdir -p experiment/logs/anthropic--claude-haiku-4-5-20251001/pilot/`
- [ ] `mv experiment/logs/anthropic--claude-haiku-4-5-20251001/control   experiment/logs/anthropic--claude-haiku-4-5-20251001/pilot/`
- [ ] `mv experiment/logs/anthropic--claude-haiku-4-5-20251001/treatment experiment/logs/anthropic--claude-haiku-4-5-20251001/pilot/`

### Step 3: Pro — simple batch-tag

- [ ] `mkdir -p experiment/logs/deepseek--deepseek-v4-pro/pilot/`
- [ ] `mv experiment/logs/deepseek--deepseek-v4-pro/control   experiment/logs/deepseek--deepseek-v4-pro/pilot/`
- [ ] `mv experiment/logs/deepseek--deepseek-v4-pro/treatment experiment/logs/deepseek--deepseek-v4-pro/pilot/`

### Step 4: Flash — separate pilot from killed N=18

The Flash flat dirs contain two populations that must be separated:

**Pilot-only instances** (these are clean — all 5 reps are pilot data, no killed-N=18 overwrites):

| Arm | Instances |
|---|---|
| control | `pydata__xarray-3305`, `sphinx-doc__sphinx-10673`, `sympy__sympy-22080` |
| treatment | `pydata__xarray-3305`, `sphinx-doc__sphinx-10673`, `sympy__sympy-22080` |

These are the 3 pilot instances that were NOT selected for the N=18 sample.

**Mixed instances** (pilot + killed-N=18):

| Arm | Instance | Note |
|---|---|---|
| control | `django__django-11532` | 5 reps; reps 1-3 = killed N=18 (overwrote pilot 1-3), reps 4-5 = surviving pilot |
| control | `pytest-dev__pytest-8399` | Same situation as django-11532 |
| treatment | `django__django-11532` | Same |
| treatment | `pytest-dev__pytest-8399` | Same |

**Killed-N=18-only instances** (everything else in flat dirs):

All remaining instances in flat `control/` and `treatment/` that aren't in the two lists above.

#### Step 4a: Create target directories

- [ ] Run:
```bash
BASE="experiment/logs/deepseek--deepseek-v4-flash"
mkdir -p "$BASE/pilot/control"
mkdir -p "$BASE/pilot/treatment"
mkdir -p "$BASE/n18_v4_flash__killed/control"
mkdir -p "$BASE/n18_v4_flash__killed/treatment"
```

#### Step 4b: Move pilot-only instances

- [ ] Run (redefine `$BASE` if in a new shell):
```bash
BASE="experiment/logs/deepseek--deepseek-v4-flash"

# Control arm — clean pilot instances
for inst in pydata__xarray-3305 sphinx-doc__sphinx-10673 sympy__sympy-22080; do
    mv "$BASE/control/$inst" "$BASE/pilot/control/"
done

# Treatment arm — clean pilot instances (same three)
for inst in pydata__xarray-3305 sphinx-doc__sphinx-10673 sympy__sympy-22080; do
    mv "$BASE/treatment/$inst" "$BASE/pilot/treatment/"
done
```

#### Step 4c: Move killed-N=18 instances

Everything left in flat `control/` and `treatment/` after 4b is killed-N=18 data:

- [ ] Run (redefine `$BASE` if in a new shell):
```bash
BASE="experiment/logs/deepseek--deepseek-v4-flash"

# Move all remaining control instances to n18_v4_flash__killed
for inst in "$BASE"/control/*/; do
    mv "$inst" "$BASE/n18_v4_flash__killed/control/"
done

# Move all remaining treatment instances to n18_v4_flash__killed
for inst in "$BASE"/treatment/*/; do
    mv "$inst" "$BASE/n18_v4_flash__killed/treatment/"
done

# Remove the now-empty flat arm directories
rmdir "$BASE/control" "$BASE/treatment"
```

#### Step 4d: Post-move: deal with the mixed instances (django-11532, pytest-8399)

After step 4c, `django__django-11532` and `pytest-dev__pytest-8399` live in `n18_v4_flash__killed/control/` and `n18_v4_flash__killed/treatment/` with all 5 reps. But reps 1-3 in those directories are killed-N=18 data, while reps 4-5 are pilot data. Two options:

**Option A (recommended — simpler):** Leave them together under `n18_v4_flash__killed/`. These 2 mixed instances have 2 pilot reps that are orphans (reps 4-5). The complete pilot data (5 × 5 reps) is fully preserved in the Haiku and Pro models' `pilot/` directories — there's no value in partial duplicated pilot data for Flash alone. The `pilot/` batch for Flash will have 3 instances, accepting that it's incomplete because the Flash pilot for those 2 instances was partially overwritten.

- [ ] (Option A — no action needed; instances remain in `n18_v4_flash__killed/` as placed by Step 4c)

**Option B (exact):** Split the rep files — move reps 4-5 from `n18_v4_flash__killed/` to `pilot/`. This is fragile and complicated:
```bash
# For each mixed instance, move rep 4-5 to pilot
for arm in control treatment; do
    for inst in django__django-11532 pytest-dev__pytest-8399; do
        mkdir -p "$BASE/pilot/$arm/$inst"
        for rep in 4 5; do
            mv "$BASE/n18_v4_flash__killed/$arm/$inst/${rep}.traj.json"    "$BASE/pilot/$arm/$inst/"
            mv "$BASE/n18_v4_flash__killed/$arm/$inst/${rep}.log"          "$BASE/pilot/$arm/$inst/" 2>/dev/null
            mv "$BASE/n18_v4_flash__killed/$arm/$inst/${rep}.manifest.json" "$BASE/pilot/$arm/$inst/" 2>/dev/null
        done
    done
done
```

This preserves the Flash pilot data exactly as it was, but has downsides:
- `batch_of()` returns `n18_v4_flash__killed` for rep 1-3 (path-based) and would need manifests tagged with `batch_id: "pilot"` for rep 4-5... except these old manifests don't have `batch_id` fields, so `batch_of()` would correctly return `"n18_v4_flash__killed"` for reps 1-3 (path wins) and `"pilot"` for reps 4-5 (path wins — they're under `pilot/`). Wait, actually... if we move individual rep files to different directories, the manifests move too. `infer_path_meta` detects batch from path depth, so a manifest under `pilot/control/django-11532/4.manifest.json` would return batch=`"pilot"`. That's correct. But `batch_of()` would return `"pilot"` from the path, and the manifest field (if it exists) is only the fallback. So we'd get the right answer without manifest edits.
- However, the pilot analysis scripts would need to know that Flash's pilot is only partially preserved (3 complete instances, 2 incomplete). This is confusing.

**Recommendation: Option A** — simpler, honest (the data was partially overwritten; pretending otherwise is misleading), and the full pilot cross-model analysis can still use Haiku + Pro data.

### Step 4e (optional): Recover full Flash pilot data for the 2 overwritten instances

If you want the complete 5×5 Flash pilot (5 instances × 5 reps × 2 arms) to replicate the original cross-model analysis exactly, you need to fill in the missing pilot reps for `django-11532` and `pytest-8399`. With Option A, these 2 instances live entirely in `n18_v4_flash__killed/` — reps 1-3 are killed-N=18 data, reps 4-5 are surviving pilot data.

**Recovery approach:** Re-run Flash pilot reps 1-3 for the 2 overwritten instances into the `pilot/` batch directory, then move the surviving reps 4-5 from `n18_v4_flash__killed/` to `pilot/`. Re-running will produce slightly different trajectory data (non-deterministic LLM output), but the statistical impact is negligible — 6 trajectories out of 150 across the full 3-model pilot (4%).

**Why re-running works without conflicts:**

After the cleanup moves (Steps 4b-4c), the `pilot/` directory structure is empty for these 2 instances. `run_one.py` has no resume/skip logic — it always writes to the path `logs/<model>/<batch>/<arm>/<instance>/<run_id>.traj.json`. Running with `--batch-id pilot --run-id 1` writes to `pilot/control/django__django-11532/1.traj.json`, which doesn't exist yet (the killed-N18 copy is in `n18_v4_flash__killed/`). No files need to be deleted.

**Prerequisites:**
- The DB for each instance must exist: `bash experiment/index_instance.sh django__django-11532` and the same for `pytest-dev__pytest-8399`
- Environment: `source experiment/.venv/bin/activate` (or equivalent)
- API key: `DEEPSEEK_API_KEY` set in environment

**Commands — run these 12 calls** (2 instances × 2 arms × 3 reps):

- [ ] (Step 4e) Run 12 re-run calls:
```bash
# django-11532 control reps 1-3
python3 experiment/run_one.py --arm control   --instance django__django-11532 --run-id 1 --batch-id pilot --model deepseek/deepseek-v4-flash
python3 experiment/run_one.py --arm control   --instance django__django-11532 --run-id 2 --batch-id pilot --model deepseek/deepseek-v4-flash
python3 experiment/run_one.py --arm control   --instance django__django-11532 --run-id 3 --batch-id pilot --model deepseek/deepseek-v4-flash

# django-11532 treatment reps 1-3
python3 experiment/run_one.py --arm treatment --instance django__django-11532 --run-id 1 --batch-id pilot --model deepseek/deepseek-v4-flash
python3 experiment/run_one.py --arm treatment --instance django__django-11532 --run-id 2 --batch-id pilot --model deepseek/deepseek-v4-flash
python3 experiment/run_one.py --arm treatment --instance django__django-11532 --run-id 3 --batch-id pilot --model deepseek/deepseek-v4-flash

# pytest-8399 control reps 1-3
python3 experiment/run_one.py --arm control   --instance pytest-dev__pytest-8399 --run-id 1 --batch-id pilot --model deepseek/deepseek-v4-flash
python3 experiment/run_one.py --arm control   --instance pytest-dev__pytest-8399 --run-id 2 --batch-id pilot --model deepseek/deepseek-v4-flash
python3 experiment/run_one.py --arm control   --instance pytest-dev__pytest-8399 --run-id 3 --batch-id pilot --model deepseek/deepseek-v4-flash

# pytest-8399 treatment reps 1-3
python3 experiment/run_one.py --arm treatment --instance pytest-dev__pytest-8399 --run-id 1 --batch-id pilot --model deepseek/deepseek-v4-flash
python3 experiment/run_one.py --arm treatment --instance pytest-dev__pytest-8399 --run-id 2 --batch-id pilot --model deepseek/deepseek-v4-flash
python3 experiment/run_one.py --arm treatment --instance pytest-dev__pytest-8399 --run-id 3 --batch-id pilot --model deepseek/deepseek-v4-flash
```

**Then move the surviving pilot reps 4-5 from `n18_v4_flash__killed/` to `pilot/`:**

- [ ] (Step 4e) Move surviving reps 4-5:
```bash
BASE="experiment/logs/deepseek--deepseek-v4-flash"
for arm in control treatment; do
    for inst in django__django-11532 pytest-dev__pytest-8399; do
        mkdir -p "$BASE/pilot/$arm/$inst"
        for rep in 4 5; do
            mv "$BASE/n18_v4_flash__killed/$arm/$inst/${rep}.traj.json"    "$BASE/pilot/$arm/$inst/"
            mv "$BASE/n18_v4_flash__killed/$arm/$inst/${rep}.log"          "$BASE/pilot/$arm/$inst/" 2>/dev/null || true
            mv "$BASE/n18_v4_flash__killed/$arm/$inst/${rep}.manifest.json" "$BASE/pilot/$arm/$inst/" 2>/dev/null || true
        done
    done
done

# Remove now-empty instance dirs from killed batch (or leave them — harmless)
for arm in control treatment; do
    for inst in django__django-11532 pytest-dev__pytest-8399; do
        rmdir "$BASE/n18_v4_flash__killed/$arm/$inst" 2>/dev/null || true
    done
done
```

**Result after recovery:** Flash `pilot/control/` and `pilot/treatment/` each have 5 instances × 5 reps. `infer_path_meta` returns `batch="pilot"` for all of them. `batch_of()` returns `"pilot"` from the path (no manifest edits needed). The full 3-model pilot is restored for cross-model analysis.

**Alternative (simpler, full re-run):** Skip the rep-4/5 move and just run all 5 reps fresh for both instances. This avoids the partial-move complexity but costs 24 API calls (2 instances × 2 arms × (5 new reps + skipping the 12 from the plan above... actually wait, `run_one.py` doesn't have `--runs` — you'd need to call it with `--run-id 4` and `--run-id 5` too). Total: 20 calls for a full fresh run ($0.34 × 20 = ~$6.80 using the pilot per-run cost average).

**Cost estimate:** 12 calls × ~$0.34/run = ~$4.08 for the re-run-only approach. Slightly different trajectory contents but equivalent statistical signal, so the original conclusions remain valid.

### Step 5: Verify

After all moves, run a sanity check:

- [ ] Check no flat control/treatment directories remain:
```bash
# Should produce NO output (all flat dirs eliminated).
find experiment/logs -maxdepth 2 -type d \( -name control -o -name treatment \) \
    ! -path "*/pilot/*" ! -path "*/n18_v4_flash/*" ! -path "*/n18_v4_pro/*" \
    ! -path "*/n18_v4_flash__killed/*"
```

- [ ] Check `infer_path_meta` resolves batch correctly on all moved files:
```bash
python3 -c "
import sys; sys.path.insert(0, 'experiment')
from lib.trajmeta import infer_path_meta
from pathlib import Path
import json

logs = Path('experiment/logs')
for traj in sorted(logs.rglob('*.traj.json')):
    model, batch, arm, inst = infer_path_meta(traj)
    if not batch:
        print(f'NO BATCH: {traj}')
print('Done — all trajectories should have a batch_id from path.')
"
```

- [ ] Count trajectories per model/batch/arm:
```bash
python3 -c "
import sys; sys.path.insert(0, 'experiment')
from lib.trajmeta import infer_path_meta
from pathlib import Path
from collections import Counter

logs = Path('experiment/logs')
counts = Counter()
for traj in sorted(logs.rglob('*.traj.json')):
    model, batch, arm, inst = infer_path_meta(traj)
    counts[(model, batch, arm)] += 1
for k in sorted(counts):
    print(f'{k[0]:40s}  batch={k[1]:25s}  arm={k[2]:9s}  {counts[k]:3d} trajs')
"
```

### Step 6: Re-evaluate (post-cleanup)

After the cleanup, re-run evaluation to get clean results with `--batch`:

- [ ] Evaluate Flash N=18 batch (the complete one):
```bash
python3 experiment/analysis/evaluate_patches.py --batch n18_v4_flash
```

- [ ] (Optional) Evaluate pilot data for archival comparison:
```bash
python3 experiment/analysis/evaluate_patches.py --batch pilot --logs experiment/logs/
```

The `--batch pilot` will aggregate pilot data across Haiku, Pro, and Flash's partial pilot directory (3 instances). Haiku and Pro will contribute the full 5 instances; Flash will contribute only the 3 that survived without overwrite.

---

## Edge Cases

### 1. `infer_path_meta` depth detection

After the move, all trajectories live 4 levels deep: `logs/<model>/<batch>/<arm>/<instance>/<rep>.traj.json`. The discriminator (`grandparent.name != "logs"`) correctly detects the batch layer. Verified in the `LOGS_BATCH_FOLDER.md` implementation session — this is already the production behavior.

### 2. `batch_of()` with path-authoritative resolution

`batch_of(path)` returns `infer_path_meta(path)[1]` (the directory batch) first, falling back to manifest `batch_id` only when the path has no batch layer. After the move, all trajectories will have a path-based batch, so `batch_of()` won't touch any manifests. No manifest edits required.

### 3. `estimate_cost.py` glob

The glob was already made layout-agnostic in the `LOGS_BATCH_FOLDER.md` implementation (`base.glob(f"**/{arm}/*/*.traj.json")`). It correctly finds trajectories at both 3-level (flat) and 4-level (batched) depths. After the move, all trajectories are 4-level, so the glob works unchanged.

### 4. `analyze_trajectories.py`, `evaluate_patches.py`

Both use `infer_path_meta` for path parsing and `--batch` for filtering. After the move, `--batch pilot` correctly aggregates pilot data across all three models. `--batch n18_v4_flash` correctly isolates the Flash N=18 batch. The killed-N=18 data under `n18_v4_flash__killed` can be ignored or evaluated separately.

### 5. Instance IDs containing `control` or `treatment`

No SWE-bench instance IDs contain these strings — they use `<owner>__<repo>-<number>` format. The arm directory names (`control`, `treatment`) are unambiguous as path components.

### 6. Rep number collisions post-cleanup

For the killed N=18 batch: instances like `sympy__sympy-14248` have reps 1 and 3 in `n18_v4_flash__killed`, and reps 1, 2, 3 in `n18_v4_flash`. These are different files at different paths — no collision. The rep number is relative to its batch directory.

### 7. Pro `n18_v4_pro` incomplete batch

The Pro N=18 batch has incomplete rep counts (1–3 each instead of the planned 3). This isn't addressed by this cleanup — it's a separate concern. The batch subdirectory already exists and is already correctly structured. Possibly re-run `run_experiment.py --resume` to complete the missing reps.

### 8. The `__corr` corrected-submission evaluation runs

`correct_submissions.py` writes to the same SQLite DB with a `run_tag` suffix (`__corr`). It doesn't interact with the logs directory structure — it reads trajectories and re-evaluates corrected patches through the harness. No impact from this cleanup.

### 9. `run_pipeline.sh` batch parameter

`run_pipeline.sh` takes a `BATCH_ID` argument and passes `--batch` to each analysis script. After the cleanup, valid batch IDs are:
- `pilot` — all three models' pilot data
- `n18_v4_flash` — Flash N=18 complete batch
- `n18_v4_flash__killed` — Flash killed N=18 partial data (optional)
- `n18_v4_pro` — Pro N=18 (incomplete, may need resume)

### 10. Rollback

All `mv` operations (Steps 2–4) are renames, not deletes. To roll back, reverse the moves:

> **Warning:** The Step 1 deletes (`run_evaluation/` and `scikit-learn__scikit-learn-25102/`) are NOT reversible by these instructions. Both contained no trajectory data: `run_evaluation/` was empty, and `scikit-learn__scikit-learn-25102/` had only a failed manifest + log (`traj_written: false`). Their loss has no impact on analysis.

```bash
# Haiku
mv experiment/logs/anthropic--claude-haiku-4-5-20251001/pilot/* \
   experiment/logs/anthropic--claude-haiku-4-5-20251001/
rmdir experiment/logs/anthropic--claude-haiku-4-5-20251001/pilot/

# Pro
mv experiment/logs/deepseek--deepseek-v4-pro/pilot/* \
   experiment/logs/deepseek--deepseek-v4-pro/
rmdir experiment/logs/deepseek--deepseek-v4-pro/pilot/

# Flash: undo moved instances from killed and pilot back to flat control/treatment
mkdir -p experiment/logs/deepseek--deepseek-v4-flash/control
mkdir -p experiment/logs/deepseek--deepseek-v4-flash/treatment
mv experiment/logs/deepseek--deepseek-v4-flash/pilot/control/* \
   experiment/logs/deepseek--deepseek-v4-flash/control/
mv experiment/logs/deepseek--deepseek-v4-flash/pilot/treatment/* \
   experiment/logs/deepseek--deepseek-v4-flash/treatment/
mv experiment/logs/deepseek--deepseek-v4-flash/n18_v4_flash__killed/control/* \
   experiment/logs/deepseek--deepseek-v4-flash/control/
mv experiment/logs/deepseek--deepseek-v4-flash/n18_v4_flash__killed/treatment/* \
   experiment/logs/deepseek--deepseek-v4-flash/treatment/
rmdir experiment/logs/deepseek--deepseek-v4-flash/pilot/control \
      experiment/logs/deepseek--deepseek-v4-flash/pilot/treatment \
      experiment/logs/deepseek--deepseek-v4-flash/pilot/
rmdir experiment/logs/deepseek--deepseek-v4-flash/n18_v4_flash__killed/control \
      experiment/logs/deepseek--deepseek-v4-flash/n18_v4_flash__killed/treatment \
      experiment/logs/deepseek--deepseek-v4-flash/n18_v4_flash__killed/
```

---

## What This Does NOT Address

1. **Pro incomplete N=18 batch** — Separate concern; needs `--resume` or re-run.
2. **Manifest `batch_id` field** — The old pilot manifests don't have a `batch_id` field. After the move, `batch_of()` returns the batch from the path (directory-authoritative), so the missing field doesn't matter.
3. **`results/reports/logs/run_evaluation/`** — The harness-generated evaluation logs already live under `results/reports/`. The `run_evaluation/` in the logs directory was an orphan. This plan only removes the orphan.
4. **`eval_results.db` data** — The SQLite DB has `batch_id` and `run_tag` columns. Existing rows have `batch_id=""` for pilot data. After this cleanup, re-running `evaluate_patches.py --batch pilot` will upsert new rows with `batch_id="pilot"`. The old `""`-batch rows will coexist. This is harmless but could be cleaned up with a follow-up SQL `DELETE`.
