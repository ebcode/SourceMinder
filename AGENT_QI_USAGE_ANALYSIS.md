# Agent Qi Usage Analysis

Statistical comparison of `xiaomi_mimo/mimo-v2.5-pro` control vs treatment arms on the ansible instance. Control solved in 211 turns; treatment exhausted its 250-turn budget (EOFError).

## Basic comparison

| | Control | Treatment |
|---|---|---|
| Total turns | 211 | 250 |
| Solved | YES | NO |
| Format errors | 37 (17.5%) | 23 (9.2%) |
| Effective turns (non-error) | 174 | 227 |
| Qi queries | 0 | 33 |
| Grep/find queries | 58 | 57 |
| Test runs | 31 | 56 |
| FAILED lines observed | 30 | 45 |
| PASSED lines observed | 560 | 128 |

Treatment had fewer format errors and 53 more effective turns — but still failed. The format errors weren't the differentiator. Treatment spent its extra turns running more failing tests (56 vs 31) without converging on a correct fix.

## Qi behavior

All 33 qi queries returned results except two dead-end guesses (`run_lookup`, `_run_lookup` — neither exists in the codebase). qi's symbol index was complete for this repo.

### Two-turn expand tax

Every qi query followed the same pattern: `qi X -v` (locate) → `qi X -e --raw` (expand). Half the qi turns were re-querying the same symbol. If `-v` included a small code snippet, the agent could decide relevance in 1 turn instead of 2. Estimated savings: ~16 turns.

### Bug: `-e --raw` returns empty when results include CALL entries (T129)

`qi error -p Display -e --raw -x noise` found 52 results (49 CALL, 3 FUNC) but emitted **zero output**. CALL entries have no definition body to expand; qi silently fails for ALL results rather than expanding the expandable 3 FUNC entries. Adding `-i func` fixed it (T130), but only because the agent guessed correctly after a wasted turn.

### Dead-end guesses (T20, T138)

Agent searched for `run_lookup` → 0 results → tried `_run_lookup` → still 0. The right answer (`run` on `LookupBase`) was found at T21 anyway. qi's 0-result message gives no suggestion of nearby symbols.

### Path mismatch (T182)

`qi 'maybe_capture_traceback' -f .../__init__.py` → "1 match excluded by -f." The function lives at `.../_traceback.py`. qi knew where the match was but didn't tell the agent.

## What grep found that qi can't

Three discoveries grep made that qi fundamentally can't:

1. **`timedout`** — a Jinja2 test filter, not a conventional Python function. Found by grepping the literal string across the repo. Treatment never used qi for it (0 qi queries, 11 grep hits).

2. **`is_controller`** — a runtime boolean attribute set dynamically on `PluginExecContext`. Found by grepping for the string across lib and test files. Treatment never used qi for it (0 qi queries, 3 grep hits).

3. **Cross-cutting regex patterns** — `errors.*warn\|errors.*ignore`, `help_text.*error`, `deprecation_warnings_enabled`. These span function/method boundaries. qi indexes symbols, not patterns. Treatment used qi for `error` and `_run_lookup` but had to run 4 separate grep commands to find what control found in one grep.

## Prompt problems in the treatment

1. **"STAY IN QI" told the agent to retry in qi before falling back to grep.** When `run_lookup` returned 0, the prompt said "go broader in qi" — so the agent tried `_run_lookup` (also 0) instead of switching to grep. Control found equivalent information in 1 grep command.

2. **No qi boundaries taught.** The prompt mentioned only one gotcha ("single-letter symbols and common words"). The guides document ~20 verified gotchas: keywords not indexed, no punctuation, no regex, prefix patterns need explicit `*`, constants not flagged as definitions, SCOPE/MODIFIER columns empty per-language.

3. **No `-i func`-before-`-e` rule.** The agent consistently ran `qi <broad_term> -e --raw` without `-i`, hitting bug #1 on common words like "error."

4. **Grep framed as a shameful fallback.** The prompt says "use grep to get unblocked, then go back to using qi." In reality grep found things qi can never find. The agent used grep 57 times despite the prompt discouraging it — evidence it needed grep and used it anyway.

## Test outcome asymmetry

Control's grep approach found the right fix surface quickly: YAML objects, template overrides, timedout filter, deprecation warnings, CLI error handling. Tests mostly passed (560 PASSED, 30 FAILED).

Treatment's qi+grep approach found the same code areas but the model didn't converge. Tests kept failing (128 PASSED, 45 FAILED). From T162 onward, treatment added `timeout` wrappers to pytest because tests were hanging — the model was debugging without making progress.

## Root cause

Treatment failed NOT because qi is worse than grep, but because the prompt told the model to use qi for things qi can't do, and the model's edits didn't fix the underlying test failures. The extra 53 effective turns were spent running failing tests, not making productive edits.
