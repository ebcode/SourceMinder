# Instance Selection Rules — dated, append-only ledger

Every rule that has governed which SWE-bench Pro instances we run and which
completed batches enter the cross-instance manifest, with the evidence that
created or demoted it. **Append entries; never rewrite history.** The
defensibility of the pooled results depends on this file.

Signals are computed by `analysis/pro_select.py` (screening CLI) and
`tmp/qi_fit_signals.py` (gold-patch analysis + `--validate`). The manifest is
`analysis/cross_instance_manifest.txt`.

---

## 2026-06 (retroactive entry): discovery gates

Original hard gates in `qi_fit_signals.py`: `n_src >= 2`, `src_frac >= 0.5`,
`n_graded >= 2`. Theory: qi's benefit is *file discovery*, so instances whose
gold patch is broad, source-heavy, and test-graded should benefit most.

## 2026-07-05: discovery gates demoted to descriptive

`--validate` (n=8) put the discovery signals at noise or worse: `src_frac`
rho=+0.10, `n_graded` rho=−0.15, `disc` rho=−0.56 (**anti**-predictive — the
qutebrowser trajectories showed qi's win mechanism is structural reading
(`--toc`) + impact tracing (`--usage`), not discovery). Defaults now gate on
`n_src >= 1` only; old thresholds remain reachable via explicit flags.

## 2026-07-05: screen-based forward selection (`pro_select.py --screen`)

Selection for **new** runs moved from score-and-rank to four outcome-blind
failure-mode screens, each with recorded evidence: `already_run`,
`unresolvable` (frontier `resolve_rate == 0` or unknown; 270/731 of the pool),
`no_p2p` (`n_p2p_files == 0` — no impact surface for `--usage`; rho=+0.79 on
observed token effects), `small_surface` (`repo_defs < 10000`, PROVISIONAL).
First instance selected under this rule: teleport `3ff19cf7c4` (documented in
`work_summary_20260705_192709.md`; `d6ffe82aaf` rejected solely on
resolvability).

## 2026-07-05: manifest composition — flipt out (generated-patch rule)

flipt `967855` excluded from the cross-instance manifest: gold patch is
majority machine-generated content (1/5 source; protobuf/swagger/CHANGELOG).
qi indexes source code, so the task is outside its scope. Verified from
gold-patch composition (`NEW_QI_VS_GREP_CAT_STORY_IN_CHARTS.md` §1b). NOTE: a
broader "majority non-source" rule was briefly applied the same day and
retracted — it would also exclude openlibrary (2/8 source, hand-written
templates/CSS), which *won* under qi (−22% tokens), falsifying that scope.
openlibrary stays in.

## 2026-07-05: manifest composition — webclients out (minimal navigation surface)

webclients `cfd7571` removed from the manifest on three signals: 47-line gold
patch (smallest real task in the set), 2 gold files — one of which does not
exist at base (the patch creates `AddressesAutocomplete.helper.ts` and
`splitBySeparator`, so it was unnavigable-by-definition; the other is
tsx-degraded) — and `n_p2p_files=0`. The instance predates all fit tooling and
passes the (since-falsified) discovery gates cleanly; it is the instance whose
wrong-direction result (+6% tokens) helped falsify them. Trajectory evidence:
treatment reps probed `splitBySeparator`, got ~1 row (symbol not yet in
existence), and abandoned qi for grep by turn ~8.

Caveat, recorded deliberately: vuls2 and ansible2 fail the same `no_p2p` and
`unresolvable` screens and **remain in the manifest** — completed batches are
too expensive to discard wholesale, and retroactively applying forward screens
to the pool would trim exactly the predicted-weak half, inflating the pooled
effect by construction. webclients' removal rests on the *navigation-surface*
rationale above (patch size + created-file + p2p), not on the screens alone.

## 2026-07-05: two-pools policy (forward-looking)

Going forward: **all** completed batches (minus flipt's generated-patch
exclusion) form the full pool; the `n_p2p_files > 0` subset is the
pre-specified "qi good-fit" mechanism scope, reported alongside it. New
instances are chosen via `pro_select.py --screen` (which requires p2p > 0), so
future additions enter both pools by construction. No further retroactive
exclusions. Standing check: re-run `qi_fit_signals.py --validate` as each
batch lands. As of n=10, `n_p2p_files > 0` vs `= 0` perfectly rank-separates
token effects (p2p>0: −46/−26/−22/−16/−12%; p2p=0: −11/−8/−7/+6/+27%).
