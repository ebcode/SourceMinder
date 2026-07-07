# PATCH QUALITY REVIEW PLAN — Canonical-5 Control vs. Treatment

**Status:** Proposed
**Date:** 20260701
**Delta:** 0 code changes required to execute; optionally one small analysis script (see Step 2)
**Motivation:** The `pro_pilot_tutanota2_sonnet5` investigation (2026-07-01) showed that `resolved`/`unresolved` is a lossy signal — both arms can pass the required tests while differing meaningfully in code quality (redundant fetches, unnecessary async coupling, blast radius). The canonical-5 manifest backs every pooled cross-instance stat in `CROSS_INSTANCE.md`; this plan reviews the actual patches behind those numbers, not just their pass/fail outcome.

---

## Scope

`experiment/analysis/cross_instance_manifest.txt` lists 5 batches. Each batch is **1 instance × 2 arms × 5 reps** (confirmed against `runs.csv` and the `.pred` file counts on disk — no batch here has more than one instance or a different rep count):

| Batch | Instance ID |
|---|---|
| `pro_pilot_webclients_haiku` | `instance_protonmail__webclients-cfd7571485186049c10c822f214d474f1edde8d1` |
| `pro_pilot_nodebb_haiku` | `instance_NodeBB__NodeBB-04998908ba6721d64eba79ae3b65a351dcfbc5b5-vnan` |
| `pro_pilot_openlibrary_haiku_v2` | `instance_internetarchive__openlibrary-5fb312632097be7e9ac6ab657964af115224d15d-v0f5aece3601a5b4419f7ccec1dbda2071be28ee4` |
| `pro_pilot_ansible2_haiku` | `instance_ansible__ansible-f86c58e2d235d8b96029d102c71ee2dfafd57997-v0f01c69f1e2528b935359cfe578530722bca2c59` |
| `pro_pilot_qutebrowser_haiku_v1` | `instance_qutebrowser__qutebrowser-f91ace96223cac8161c16dd061907e138fe85111-v059c6fdc75567943479b23ebca7c07b5e9a7f34c` |

Total surface: **5 batches × 5 reps = 25 control/treatment pairs** (rep-matched: `swebp_control repNN` vs. `swebp_treatment repNN` for the same `NN`, same instance — cross-rep comparison isn't meaningful since each rep is an independent model rollout). That's 50 individual patches read, but 25 pairs to actually judge, which is a manageable scope for one focused pass — no sampling needed.

---

## Data Source

Use the `.pred` files under `experiment/logs/anthropic--claude-haiku-4-5-20251001/<batch>/<arm>/<instance>/<instance>.repNN.pred` (`model_patch` field) as the patch source — **not** the `eval/` directory's `*_patch.diff` snapshots.

**Why `.pred` over `eval/`:** `eval/<arm>/<rep>/<instance>/<prefix>_patch.diff` only exists for reps that have actually been evaluated at least once, and (per `INCONCLUSIVE_EVAL_RUNS.md`) may have been silently overwritten by a later re-run with `--redo`. The `.pred` file is the harness's own record of what the agent submitted, is written exactly once per rep, and is guaranteed to exist for every rep regardless of eval state.

---

## Step-by-Step Plan

### Step 1: Pull the gold patch for grounding

For each of the 5 instances, pull `patch` (gold) and `problem_statement` from the dataset via `load_pro_dataset()` — needed to judge whether either arm's approach deviates from or improves on the reference fix, not just whether it passes tests.

```bash
./experiment/.venv_pro/bin/python3 -c "
import sys; sys.path.insert(0, 'experiment')
from lib.pro_dataset import load_pro_dataset
ds = load_pro_dataset('experiment/data/swebench_pro', split='test')
ids = {
    'webclients_haiku': 'instance_protonmail__webclients-cfd7571485186049c10c822f214d474f1edde8d1',
    'nodebb_haiku': 'instance_NodeBB__NodeBB-04998908ba6721d64eba79ae3b65a351dcfbc5b5-vnan',
    'openlibrary_haiku_v2': 'instance_internetarchive__openlibrary-5fb312632097be7e9ac6ab657964af115224d15d-v0f5aece3601a5b4419f7ccec1dbda2071be28ee4',
    'ansible2_haiku': 'instance_ansible__ansible-f86c58e2d235d8b96029d102c71ee2dfafd57997-v0f01c69f1e2528b935359cfe578530722bca2c59',
    'qutebrowser_haiku_v1': 'instance_qutebrowser__qutebrowser-f91ace96223cac8161c16dd061907e138fe85111-v059c6fdc75567943479b23ebca7c07b5e9a7f34c',
}
by_id = {r['instance_id']: r for r in ds}
for name, iid in ids.items():
    row = by_id[iid]
    print(f'--- {name} ---')
    print(row['problem_statement'][:500])
    print()
"
```

- [ ] Run the above; save each instance's gold patch + problem statement to `tmp/` for reference during review (throwaway, per project convention — not checked in).

### Step 2: Extract and pre-diff all 25 pairs

Before reading anything, mechanically reduce the reading burden: for each (batch, rep) pair, diff `swebp_control repNN`'s patch against `swebp_treatment repNN`'s patch **directly against each other** (not against gold). This immediately separates "identical files, skip" from "the actual delta to read" — exactly the technique that made the tutanota comparison tractable (most files were byte-identical; only 2 of 8 touched files actually differed).

Proposed helper (~30 lines, not yet built — see Tooling below): `experiment/analysis/diff_patch_pair.py --batch <name> --rep <NN>` that:
1. Loads both `.pred` files' `model_patch`.
2. Splits each into per-file hunks (on `diff --git` boundaries).
3. Prints: files identical between arms (count only), then a unified diff of just the files that differ.

- [ ] Build `diff_patch_pair.py` (or do this ad hoc per pair with the same `diff <(...) <(...)` approach used for tutanota — either is fine for a one-time 25-pair review; only worth building if this becomes a recurring check).
- [ ] Run it (or the ad hoc equivalent) across all 25 pairs; note which pairs are 100% identical (likely common — Haiku's treatment/control prompts share the same underlying task, and a converged fix often looks the same regardless of qi-navigation path).

### Step 3: Apply a consistent quality rubric to each pair with a real delta

Skip pairs that come back byte-identical (Step 2) — nothing to compare. For every pair with a genuine diff, judge both arms against the same rubric used ad hoc for the tutanota comparison, made explicit here so 25 pairs get consistent treatment instead of 25 independent judgment calls:

1. **Correctness vs. gold intent** — does the patch address the actual bug described in `problem_statement`, or does it patch around symptoms? (Read gold patch from Step 1 for reference, not as a required match — divergent-but-correct approaches are fine.)
2. **Side-effect discipline** — redundant network/IO calls, uncached repeated fetches, extra re-renders/re-computation introduced by the fix (the `ReferralLinkNews.ts` finding).
3. **Blast radius / coupling** — does the fix touch only what's needed, or does it introduce unrelated behavioral changes to code paths that didn't need to change (the `SettingsView.ts` finding)?
4. **Scope creep** — unrelated refactors, renames, or "while I'm here" changes bundled into the fix.
5. **Test-file changes** — did the arm edit test files beyond what `test_patch` already stages? (Cross-check against `suspect_test_only_patch` bookkeeping if already computed for that batch.)
6. **Style/idiom consistency** — does the patch match the surrounding codebase's conventions (naming, async patterns, error handling), or does it introduce a locally inconsistent style?

- [ ] For each non-identical pair, produce a short verdict: which arm is better, on which axis, one sentence each — not a full essay per pair. Flag any pair where one arm is **substantively** better (not just a stylistic nit) for a highlighted callout in the final report.

### Step 4: Write up findings

- [ ] Write `experiment/docs/PATCH_QUALITY_FINDINGS.md` (or a dated variant if this becomes a recurring exercise) with:
  - A summary table: batch × rep → {identical / control-better / treatment-better / a-wash}, one row per pair (25 rows).
  - A short section per **substantively differing** pair (likely a handful, based on the tutanota precedent), each with the specific quality issue quoted, mirroring how the tutanota `ReferralLinkNews.ts`/`SettingsView.ts` comparison was written up in conversation.
  - An aggregate takeaway: does either arm (control/grep vs. treatment/qi) show a systematic quality pattern across instances, or is it pair-by-pair noise? This is the actual research question this review serves — resolved-rate already says qi is efficient; this asks whether it's also *as good or better* on the code it produces.

---

## What This Does NOT Address

1. **Non-canonical batches** (Tutanota, flipt, teleport pilots) — out of scope; this plan is specifically about the 5 batches backing the published pooled stats. A similar review could be run on those separately if useful, but they have different instance/rep shapes and aren't part of this scope.
2. **Automated quality scoring** — this plan is a manual (or agent-assisted) read-and-judge exercise, not a linter or static-analysis pass. The rubric in Step 3 is meant to keep 25 independent reads consistent, not to be mechanically computed.
3. **Statistical significance of quality differences** — with only 5 reps per arm per instance (and likely many identical pairs), this won't produce a quantitative "treatment is X% better" claim. It's a qualitative audit, same spirit as the tutanota conversation, scaled to the full canonical set.
4. **Re-running or re-evaluating any patches** — this is a read-only review of already-submitted patches. No Docker, no `--redo`, no changes to `eval_results.csv`.

---

## Tooling Ideas (for Step 2)

1. **`diff_patch_pair.py`** — as scoped in Step 2. Only worth building if this kind of side-by-side patch diff becomes a recurring need (e.g. if this review is repeated per-batch as new instances get added to the canonical set); for a single one-time pass across 25 pairs, ad hoc `diff <(...) <(...)` per pair (as used for the tutanota instance) is equally fast and doesn't add a new script to maintain.
2. **A `--pairs` flag on `patch_peek.py`** (if that script's structure supports it) to print both arms' patches for a given batch/rep side by side, rather than requiring two separate invocations — a smaller, lower-effort version of idea 1.

---

## Estimated Effort

- Step 1 (gold patches): ~5 minutes, one script run.
- Step 2 (pre-diff all 25 pairs): ~15-30 minutes if built as a script; ~30-45 minutes if done ad hoc per pair.
- Step 3 (rubric read of non-identical pairs): the real time sink — depends entirely on how many of the 25 pairs have genuine deltas. If the tutanota precedent generalizes (most files identical, 1-2 files genuinely differing per non-trivial pair), expect well under half the pairs to need a real read. Budget ~10-15 minutes per pair needing full judgment.
- Step 4 (write-up): ~20-30 minutes.

Total: likely a single focused session (2-4 hours) depending on how many pairs turn out identical vs. genuinely divergent — won't be known precisely until Step 2 completes.
