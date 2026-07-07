# Inconclusive Pro eval runs (harness-crash false negatives)

## What happened

The vendored Pro Docker evaluator's `parser.py` emits a synthetic
`"Build/Runtime Error: <line>"` test entry (status `ERROR`) when the test suite
aborts before finishing -- e.g. an unhandled exception thrown by an unrelated
test fixture crashes the whole run. When this happens, none of the actual
FAIL_TO_PASS / PASS_TO_PASS tests get a real PASSED/FAILED status; they're just
absent from the output. Before this fix, `evaluate_pro_patches.py` had no way to
tell "the harness crashed before running the required tests" apart from "the
patch genuinely didn't fix the bug" -- both looked like `resolved=0`, and the
crash case got mislabeled `unresolved` / `failure_mode=bug_not_fixed`.

Caught live on 2026-07-01 investigating a Sonnet-5 Tutanota pilot
(`pro_pilot_tutanota2_sonnet5`) where re-running the *identical* patch flipped
`resolved` -> `unresolved` with no code change: the second run's `output.json`
was just `{"tests": [{"name": "Build/Runtime Error: Error while initializing
offline cache storage Error: oh no!!!", "status": "ERROR"}]}` -- a fixture in
Tutanota's own test suite that flakily throws a simulated error and takes the
whole suite down with it.

## The fix

`evaluate_pro_patches.py::is_inconclusive(status_by, required)` now flags a
run as `outcome="inconclusive"` / `failure_mode="inconclusive"` (instead of
`unresolved`) when either:

- a `"Build/Runtime Error: ..."` entry is present, or
- every required test is missing a reported status entirely (no PASSED/FAILED
  for any of them -- a pure collection failure, not partial credit).

A run where *some* required tests ran and genuinely failed is left as a real
`unresolved`/`bug_not_fixed` verdict even if others are missing.

The script also gained `--arm`/`--rep` flags to target and re-evaluate one
specific run (see its `--help`).

## Retroactive audit

Ran `is_inconclusive()` against every already-saved `output.json` across all
batches (397 checked, one script, no Docker re-run needed -- classification is
a pure function of the cached test-status JSON plus the instance's
FAIL_TO_PASS/PASS_TO_PASS set). **13 of 397 (~3%) previously-labeled
`unresolved` rows are actually `inconclusive`:**

| Batch | Arm | Rep | Instance |
|---|---|---|---|
| `pro_pilot_tutanota2_sonnet5` | swebp_treatment | rep01 | `tutao__tutanota-fbdb72a2bd39b05131ff905780d9d4a2a074de26-vbc0d9ba8f0071fbe982809910959a6ff8884dbbf` |
| `pro_pilot_tutanota2_sonnet5` | swebp_treatment | rep02 | `tutao__tutanota-fbdb72a2bd39b05131ff905780d9d4a2a074de26-vbc0d9ba8f0071fbe982809910959a6ff8884dbbf` |
| `pro_pilot_tutanota2_haiku` | swebp_treatment | rep01 | `tutao__tutanota-fbdb72a2bd39b05131ff905780d9d4a2a074de26-vbc0d9ba8f0071fbe982809910959a6ff8884dbbf` |
| `pro_pilot_tutanota_ds_v4_flash` | swebp_control | rep01 | `tutao__tutanota-1ff82aa365763cee2d609c9d19360ad87fdf2ec7-vc4e41fd0029957297843cb9dec4a25c7c756f029` |
| `pro_pilot_tutanota_ds_v4_flash` | swebp_control | rep03 | `tutao__tutanota-1ff82aa365763cee2d609c9d19360ad87fdf2ec7-vc4e41fd0029957297843cb9dec4a25c7c756f029` |
| `pro_pilot_tutanota_ds_v4_flash` | swebp_treatment | rep01 | `tutao__tutanota-1ff82aa365763cee2d609c9d19360ad87fdf2ec7-vc4e41fd0029957297843cb9dec4a25c7c756f029` |
| `pro_pilot_tutanota_ds_v4_flash` | swebp_treatment | rep03 | `tutao__tutanota-1ff82aa365763cee2d609c9d19360ad87fdf2ec7-vc4e41fd0029957297843cb9dec4a25c7c756f029` |
| `pro_pilot_tutanota2_ds_v4_flash` | swebp_control | rep01 | `tutao__tutanota-fbdb72a2bd39b05131ff905780d9d4a2a074de26-vbc0d9ba8f0071fbe982809910959a6ff8884dbbf` |
| `pro_pilot_tutanota_haiku` | swebp_treatment | rep01 | `tutao__tutanota-1ff82aa365763cee2d609c9d19360ad87fdf2ec7-vc4e41fd0029957297843cb9dec4a25c7c756f029` |
| `pro_pilot_flipt_haiku` | swebp_control | rep05 | `flipt-io__flipt-967855b429f749c28c112b8cb1b15bc79157f973` |
| `pro_pilot_flipt_haiku` | swebp_treatment | rep03 | `flipt-io__flipt-967855b429f749c28c112b8cb1b15bc79157f973` |
| `pro_pilot_flipt_haiku` | swebp_treatment | rep05 | `flipt-io__flipt-967855b429f749c28c112b8cb1b15bc79157f973` |
| `pro_pilot_teleport_v4_flash` | swebp_treatment | rep02 | `gravitational__teleport-e6d86299a855687b21970504fbf06f52a8f80c74-vce94f93ad1030e3136852817f2423c1b3ac37bc4` |

**None of these touch the canonical-5 manifest**
(`analysis/cross_instance_manifest.txt`: `webclients_haiku`, `nodebb_haiku`,
`openlibrary_haiku_v2`, `ansible2_haiku`, `qutebrowser_haiku_v1`) -- the pooled
cross-instance stats in `CROSS_INSTANCE.md` are unaffected. This is confined to
pilot-only batches, mostly Tutanota (which appears to have a genuinely flaky
offline-cache-storage test fixture) plus isolated flipt/teleport reps.

## Follow-up

Relabel each affected batch's `eval_results.csv` in place -- no Docker re-run
needed, since `evaluate_pro_patches.py` reuses the cached `output.json` when
`--redo` is omitted:

```bash
./experiment/.venv_pro/bin/python experiment/analysis/evaluate_pro_patches.py \
    --logs experiment/logs/<model-dir>/<batch>/ --dir experiment/results/pro_runs/<batch> --workers 1
```

Run once per batch listed above. Any batch whose pooled stats matter for a
decision should be re-evaluated before drawing conclusions from its
`resolved`/`unresolved` counts.
