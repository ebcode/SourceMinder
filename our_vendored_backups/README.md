# Our Vendored Backups

Backup copies of files we modified or created inside `experiment/vendor/`, so our
changes survive a vendor re-sync/upgrade (which would otherwise overwrite them).

Paths here mirror the layout under `experiment/vendor/`.

> **IMPORTANT — not everything here is live.** The DeepSeek `reasoning_content`
> fold (and its marker + tests) was **reverted in the vendor tree on 2026-06-26**
> because it distorted token/turn counts. The backup copies of those files still
> contain the fold for historical reference, but **do NOT re-apply them** — doing so
> would resurrect a reverted change. Only the `parse_action` change is live. See the
> *Status* column below and `experiment/PRO_HARNESS.md` → *Vendored Repositories*.

| Backup file | Lives at | Status |
|---|---|---|
| `swebench_pro_mini/src/minisweagent/agents/default.py` | `experiment/vendor/swebench_pro_mini/src/minisweagent/agents/default.py` | **LIVE — modified** |
| `swebench_pro_mini/tests/agents/test_parse_action_formats.py` | `experiment/vendor/swebench_pro_mini/tests/agents/test_parse_action_formats.py` | **LIVE — created (ours)** |
| `swebench_pro_mini/src/minisweagent/models/litellm_model.py` | `experiment/vendor/swebench_pro_mini/src/minisweagent/models/litellm_model.py` | **REVERTED — backup is historical, do not re-apply** |
| `swebench_pro_mini/tests/models/test_litellm_model.py` | `experiment/vendor/swebench_pro_mini/tests/models/test_litellm_model.py` | **REVERTED — fold tests removed from live tree** |
| `swebench_pro_mini/src/minisweagent/agents/interactive.py` | `experiment/vendor/swebench_pro_mini/src/minisweagent/agents/interactive.py` | **REVERTED — marker removed from live tree** |
| `swebench_pro_mini/tests/agents/test_reasoning_content_marker.py` | `experiment/vendor/swebench_pro_mini/tests/agents/test_reasoning_content_marker.py` | **REVERTED — deleted from live tree** |

## LIVE: what changed in `default.py`

Made `parse_action()` convention-agnostic. It now accepts three action formats
(first matching convention wins, still exactly-one-action):

1. ```` ```bash ```` fences (original default)
2. `<command>...</command>` tags
3. Qwen/Hermes `<tool_call><function name="bash"><parameter=command>...</parameter>`

Motivation: MiMo (Xiaomi) defaults to XML tool-calling and was getting a
`FormatError` every turn (~18% of MiMo's steps wasted). See
`MINISWEAGENT_ALT_COMMAND_FORMAT_PLAN.md` for background. This is the only change
in this directory that is still applied to the vendor tree.

## REVERTED: the `reasoning_content` fold (`litellm_model.py` + `interactive.py` + tests)

A brief change (mid-2026-06) folded `message.reasoning_content` into a blank
`message.content` for reasoning models (e.g. DeepSeek-v4-flash, which routed ~13%
of turns' fenced ```bash command into `reasoning_content` and returned empty
`content`). It tagged the returned dict with `extra["content_source"]`, marked
folded turns in the `interactive.py` step header with
`(recovered-from-reasoning_content)`, and added fold tests in
`test_litellm_model.py` plus a dedicated `test_reasoning_content_marker.py`.

**This was reverted on 2026-06-26.** Rationale: a harness that silently "recovers"
model failures distorts the token/turn metric (turns look more efficient than they
really were) and hides what the model actually did. Future DeepSeek runs again lose
blank-content turns — that is now the known, documented behavior; the right fix, if
any, is at the prompt level (instruct the model to put its command in `content`).
`experiment/analysis/analyze_pro_trajectories.py` still surfaces the tax via
`empty_content_turns` / `reasoning_recovered_turns` / `reasoning_recovered_rate`,
now inferred from blank `content` + fenced `reasoning_content` (no `content_source`
tag is ever set anymore). See `experiment/PRO_ANALYZE.md` → *Format tax*.

The four files above are kept here only as a record of what the fold looked like.
**Do not copy them back over the vendor tree.**

## After a vendor re-sync

Re-apply **only the live `parse_action` change**, then run its tests:

    cp our_vendored_backups/swebench_pro_mini/src/minisweagent/agents/default.py \
       experiment/vendor/swebench_pro_mini/src/minisweagent/agents/default.py
    cp our_vendored_backups/swebench_pro_mini/tests/agents/test_parse_action_formats.py \
       experiment/vendor/swebench_pro_mini/tests/agents/test_parse_action_formats.py

    experiment/.venv_pro/bin/python -m pytest \
        experiment/vendor/swebench_pro_mini/tests/agents/test_parse_action_formats.py \
        experiment/vendor/swebench_pro_mini/tests/agents/test_default.py

Do **not** re-apply `litellm_model.py`, `interactive.py`,
`test_litellm_model.py`'s fold tests, or `test_reasoning_content_marker.py` — those
belong to the reverted fold.

Note: `pytest` is installed in `experiment/.venv_pro` (added for these tests).
Skip `tests/agents/test_interactive_textual.py` -- it has 22 pre-existing
failures from a `textual` library version mismatch, unrelated to our changes.
