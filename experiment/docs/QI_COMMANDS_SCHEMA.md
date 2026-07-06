# qi_commands.csv Schema

Field reference for `qi_commands.csv`, produced by
`experiment/analysis/extract_qi_commands.py` (Step 5 of the Pro pipeline —
see [`PRO_ANALYZE.md`](PRO_ANALYZE.md)) and consumed by
`report_qi_commands.py`, `analyze_pro_stats.py`, and `cross_batch_compare.py`.

## What One Row Is

**One row = one whole shell action, not one sub-command.** An "action" is the
entire content of one assistant turn's ` ```bash ` block, however many
programs it chains with `;`/`&&`/`||`/`|`. `qi foo; echo ===; qi bar` is a
single row, not two. `cmd_idx` exists in the schema for a future per-turn
multi-action split but is currently always `0` — every extracted action is
turn-scoped, one per assistant message.

`turn_idx` is the trajectory's `messages[]` index of that assistant turn, so
it can be used to correlate a `qi_commands.csv` row back to the raw
`.traj.json` (see `docs/TRAJECTORY_FIELDS.md` if present, or the trajectory's
`messages[turn_idx]`).

## Columns

| Column | Type | Meaning |
|--------|------|---------|
| `arm` | str | `swebp_control` \| `swebp_treatment` |
| `instance` | str | SWE-bench Pro instance id |
| `run_id` | str | Rep identifier (e.g. `rep01`) |
| `model` | str | Normalized model id (`norm_model()`) |
| `batch_id` | str | Batch name parsed from the log path |
| `turn_idx` | int | Index into the trajectory's `messages[]` — the assistant turn this action came from |
| `cmd_idx` | int | Always `0` today (see above) |
| `tool` | str | Primary tool: `qi` \| `grep` \| `read` \| `other` — **precedence-classified, see Gotchas** |
| `command` | str | The full raw shell command string (every chained sub-command) |
| `output_chars` | int | Character length of the paired tool output |
| `output_tokens_approx` | int | `output_chars / 4` — approximate, not API-counted (kept consistent with `analyze_trajectories.py`'s convention) |
| `returncode` | int or `""` | Parsed from `<returncode>N</returncode>`; `124` is force-set for harness timeouts (no tag emitted); `""` if neither is present |
| `is_error` | 0/1 or `""` | `1` if `returncode != 0`, else `0`; `""` when `returncode` is unknown (not a judgment either way — exclude these rows from error-rate math, don't count them as clean) |
| `qi_pure` | 0/1 | `1` if the action's only content sources are qi and `echo` (see Gotchas — recomputed downstream, don't trust this column alone) |
| `qi_results` | int or `""` | qi rows only: match count parsed from output (`Found N matches` / result-table row count / `0` for a genuine miss); `""` for non-search qi output (`--toc`, `--expand`, errors) and all non-qi rows |
| `qi_miss_kind` | str or `""` | qi rows with `qi_results == 0` only: `filtered` (agent's own `-f`/`-i`/`-x` excluded every match) \| `absent` (no such symbol) \| `not_indexed` (qi declined it) \| `other`; `""` otherwise |
| `qi_limit`, `qi_limit_per_file`, `qi_toc`, `qi_expand`, `qi_include`, `qi_exclude`, `qi_within`, `qi_and`, `qi_def`, `qi_usage`, `qi_raw`, `qi_parent`, `qi_file`, `qi_type`, `qi_modifier`, `qi_scope`, `qi_verbose` | 0/1 or `""` | Flag-presence markers, **qi rows only** — `""` (not `0`) on non-qi rows, so "flag absent" and "not applicable" stay distinguishable |
| `qi_dotted_name`, `qi_quoted_phrase`, `qi_abs_path` | 0/1 or `""` | Misuse markers, qi rows only, same `""`-on-non-qi convention: a qualified `parent.symbol` passed as the whole pattern, a multi-word quoted phrase, and an absolute `-f` path, respectively |

## Gotchas

### `tool` is precedence-classified, not usage-detected — don't trust it alone for multi-tool actions

`tool` comes from `extract_qi_commands.py::_primary_tool()`, which uses the
**older** `cmds.count_tools()` precedence rule: **qi wins if present, else
grep, else read, else other** — a forced single-label partition of the whole
action, not "which tools did this action actually use." So `cat a; qi b`
is labeled `tool=qi` even though it also read a file with `cat`; `grep a; cat
b` is labeled `tool=grep`. This is a different, coarser model than
`cmds.action_tool()` (used by `load_explore()` in `analyze_pro_stats.py` and
by `report_qi_commands.py`'s `_homog()`), which detects **genuine usage** of
each of qi/grep/cat/sed_read independently and returns `mixed` when ≥2 are
used, or `None` when none are recognized (a bare `pytest | head` counts as
neither `read` nor anything else under `action_tool()` — it's excluded, not
miscategorized).

**Practical rule:** for tool-share reporting where "primary tool of the
action" is genuinely what you want (e.g. the ESSENTIALS tool-mix summary in
`report_qi_commands.py`), the `tool` column is fine. For anything measuring
"was tool X used at all" or "is this action's output cleanly attributable to
one tool," recompute from `command` with `cmds.action_tool()` /
`cmds.only_tool_and_echo()` instead of trusting `tool`/`qi_pure`. This is
exactly what `report_qi_commands.py::_homog()` does, with the comment
"computed live from the command so a stale `qi_pure` column can't skew it."
This distinction is also *why* the `other_read` bucketing bug happened in an
earlier ancestor of this pipeline (test-execution output landing in a "read"
bucket) — see `PRO_ANALYZE.md`'s Format Tax / usage-detection history.

### `tool=read` is broader than `cat`/`sed_read` elsewhere in the pipeline

`_primary_tool()`'s `read` label comes from `cmds.count_tools()`'s `READ_RE`,
which matches `cat`, `sed`, `head`, `tail`, `less`, **and** `more` — not just
the two "content" tools (`cat`, `sed -n`) that the explore charts track
separately as `cat` / `sed_read`. A command using only `head`/`tail`/`less`
gets `tool=read` in this CSV but contributes to **no** segment in
`cmds.action_tool()`'s output (those three aren't in `_recognized_kinds()` at
all). Don't assume `tool=read` rows all correspond to a `cat` or `sed_read`
segment in the explore charts.

### `sed -i` (edit) vs `sed -n` (read) both count as `read`/`sed`

`_primary_tool()` doesn't distinguish `sed -i` (in-place edit) from `sed -n`
(stdout read) — both are `tool=read`. The finer distinction
(`cmds.count_sed_read()` vs `cmds.count_sed_edit()`) exists in `cmds.py` but
isn't surfaced as a CSV column; only `cmds.action_tool()`'s `sed_read` kind
(used by the explore charts) excludes edits.

### `is_error` empty (`""`) is not the same as `is_error == 0`

`""` means the harness produced no `<returncode>` tag and no recognized
timeout message — an unknown outcome, not a confirmed success. Error-rate
calculations should filter to rows where `is_error is not None` (see
`report_qi_commands.py`'s `scored()` helper) rather than treating blank as
clean.

### Flag columns use `""` for "not applicable," `0` for "applicable but absent"

Only qi rows get real `0`/`1` values for `qi_limit`, `qi_parent`, etc. — a
`grep`/`read`/`other` row gets `""` for every qi-specific column. When
computing adoption rates (e.g. "% of qi calls using `-l`"), filter to
`tool == "qi"` first; summing `""` as falsy will silently work in Python but
relies on that coincidence, not a documented int(`0`).

### `qi_results` conflates "no search performed" and "not applicable"

`""` covers both non-qi rows and qi rows that ran a non-search subcommand
(`--toc`, `--expand`, `--def`, help text, an error). If you need "did this qi
call search for something," check `qi_results is not None` — `0` is a real
answer (a search that found nothing), not absence of data.

## See Also

- [`PRO_ANALYZE.md`](PRO_ANALYZE.md) — Step 5/6 of the pipeline (`extract_qi_commands.py` → `report_qi_commands.py`)
- [`CHART_INVENTORY.md`](CHART_INVENTORY.md) — which charts consume this CSV (`qi_commands.csv` feeds the explore stacks, the efficiency radar, and the retired qi/grep charts)
- `experiment/lib/cmds.py` — the shared tool-detection helpers (`action_tool`, `only_tool_and_echo`, `count_tools`, etc.) that both this extractor and the newer usage-detection consumers are built on
- `experiment/analysis/extract_qi_commands.py` — the extractor source (docstring has the original column list)
- `experiment/analysis/report_qi_commands.py` — the primary consumer; `_homog()`/`_percall()` show the "recompute from `command`" pattern in practice
