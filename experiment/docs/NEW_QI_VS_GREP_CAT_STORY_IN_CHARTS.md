# Plan: Tell the "qi displaces grep + cat + sed-read" story in charts

**Status:** APPROVED (spec agreed; ready to build)
**Supersedes:** the earlier "qi vs grep + cat" draft that used a single
mutually-exclusive `explore_bucket` per action. That model was **abandoned** — it
forced every action into one bucket, which swept test-execution commands
(`pytest … | head`) into a bogus `other_read` bucket and manufactured a false
"−31% exploration drop" on qutebrowser. See §11 for the post-mortem.

---

## 1. Motivation — two distinct axes, do not conflate them

There are **two separate questions**, and the mistake to avoid is letting one
stand in for the other:

- **Behavior axis** — *which tools does the agent reach for?* Does giving it qi
  change the exploration tool mix (grep/cat/sed-read vs qi)?
- **Outcome axis** — *what does the run cost?* Total tokens, dollar cost, and
  transcript (log) size — the context economy of the whole trajectory.

**These two axes do not track each other, and can even anti-correlate.** The
charts must present them as separate things, not merge them into a single
"responder / non-responder" verdict (the earlier draft's error).

### 1a. Behavior: the exploration tool mix (the call-count chart, §4.1)
When qi is available, on *some* instances the **grep + cat + sed-read** call block
shrinks and a **qi** block appears; on others qi simply **stacks** on top without
displacing anything. Recognized-tool actions per run (successful only):

| instance | grep+cat+sed-read calls/run (ctl → trt) | qi added | tool mix |
|---|---|---|---|
| tutanota | 70.2 → **56.2** | +12.2 | shifts (manual block shrinks) |
| nodebb | 85.0 → **55.6** | +13.6 | shifts (manual block shrinks) |
| qutebrowser | 22.8 → **25.6** | +8.2 | **stacks** (manual block grows) |

This is a real, honest contrast about *behavior* — but it is **not** a verdict on
whether qi helped. It says nothing, by itself, about cost.

### 1b. Outcome: context economy (the outcome chart, §4.4)
The efficiency payoff is **broader than the behavior shift and does not require
it**. Per-arm change (treatment vs control), current data:

| instance | cost | total_tokens | log size | tool mix (from 1a) |
|---|---|---|---|---|
| qutebrowser | **−68%** | **−59%** | **−37%** | *stacks* (more calls!) |
| tutanota | −16% | −12% | −9% | shifts |
| nodebb | −21% | −22% | −10% | shifts |
| ansible | −16% | −3% | −6% | *stacks* |

**qutebrowser is the sharpest case:** the instance whose tool mix *stacks* (it
issues **more** tool calls under treatment) is also the **biggest** cost/token/log
winner. So "qi displaces grep+cat" is *not* the mechanism behind the savings.

The mechanism is **context economy / guardrail-against-blowup**: qi keeps the
working context lean, so the trajectory doesn't balloon — even when the agent
issues more (small, targeted) qi calls. The signature is **variance collapse**,
not a mean shift: qutebrowser control ranges 689k–3.75M tokens (rep03/04 blow up
on repeated big cat/sed dumps + `pytest | head` thrash); treatment is a tight
690k–1.17M, its *worst* run below control's median. A mean-only bar hides this;
the outcome chart (§4.4) must show the spread.

### 1c. Secondary: per-tool output volume (the token chart, §4.2)
Where a call is *homogeneous* (only qi, or only cat, or only grep) we can cleanly
attribute its output tokens. A useful "how heavy is each tool's output" measure —
but it makes **no** claim to be a total of all context gathering, and it is a
behavior-axis view, not the outcome axis.

---

## 2. Design principles (what changed, and why)

1. **Detect tool *usage*, do not classify actions into a partition.** For each
   action we ask independently: was qi used? grep? cat? sed-read? We do **not**
   assign a single "primary tool" and we do **not** have `other`/`other_read`/
   `test_run` catch-alls. An action that uses none of the four recognized tools
   (a `pytest` run, an edit, a `git` command) is simply **not counted**. This is
   what dissolves the test-execution contamination: `pytest … | head` matches no
   recognized exploration tool, so it never enters any chart.
2. **Two signals, two admission rules.**
   - *Call counts* admit **every** successful action; an action using ≥2
     recognized tools is its own `mixed` segment.
   - *Token output* admits only **homogeneous** actions (only-qi / only-cat /
     only-grep, modulo echo/separators), because only there does the whole
     action's output belong unambiguously to one tool. Heterogeneous actions
     contribute no tokens to any tool — that gap is already visible as the `mixed`
     call-count segment.
3. **No fake totals.** Segments are not asserted to sum to "all exploration."
   The call-count chart's readable sub-total is specifically the **manual-search
   block = grep + cat + sed-read**, which is the quantity the story is about.
4. **Errored actions excluded** from both signals (`is_error == 0`), matching the
   existing ESSENTIALS totals.

---

## 3. Data model & detectors

### 3.1 Source
All charts read `qi_commands.csv` (written by `extract_qi_commands.py`, Step 5).
**One row = one whole shell action** (a single ```` ```bash ```` block).
`output_tokens_approx` is the combined output of the entire action.

Relevant columns: `arm`, `run_id`, `tool` (qi detection), `command`,
`output_tokens_approx`, `is_error`.

### 3.2 Recognized-tool detection (replaces the old bucket table)
Per action, independently:

- **qi** — `tool == "qi"` (extractor already identifies qi as the driving tool).
- **grep** — `grep`/`rg` invoked as a program (start-of-segment or after a
  `; && || |` separator), not merely a substring.
- **cat** — `cmds.count_cat(command) >= 1`.
- **sed-read** — `cmds.count_sed_read(command) >= 1` (`sed -n`, not `sed -i`).

`sed -i` (edits), `head`/`tail`/`less`/`more`, `pytest`/`npm`/`node`/`make`/`ospec`,
and everything else are **not** recognized and never counted.

### 3.3 New shared helpers (add to `experiment/lib/cmds.py`)
```python
def only_tool_and_echo(command: str, tool: str) -> bool:
    """True if the action's only real programs are `tool` (one or more) plus
    echo/separators. Generalizes only_qi_and_echo to 'cat' and 'grep'
    (and 'sed_read'). Basis for the homogeneous token gate."""

def action_tool(command: str, tool_col: str) -> str | None:
    """Return 'qi'|'grep'|'cat'|'sed_read' if exactly one recognized tool is
    used, 'mixed' if two or more, or None if the action uses none of the four
    recognized exploration tools (excluded from all charts)."""
```
`only_qi_and_echo`, `count_cat`, `count_sed_read`, `count_sed_edit` already exist
and are reused. No `explore_bucket` / `read_kind` is added — that model is retired.

### 3.4 Segment palette (consistent across all new charts)
| segment | role | suggested color |
|---|---|---|
| qi | new tool | `#2a7fb8` (treatment blue) |
| grep | search | `#dd8452` (orange) |
| cat | whole-file read | `#8c8c8c` (grey) |
| sed_read | line-range read | `#c4b07a` (tan) |
| mixed | ≥2 recognized tools (call chart only) | hatched grey |

Define once as a module constant (`EXPLORE_COLORS`) shared in spirit between the
single- and cross-instance analyzers; identical names/labels in both scopes.

---

## 4. Chart specifications

### 4.1 Chart 1 — call counts (the headline)
**Single:** `analyze_pro_stats.py`, new `chart_explore_stack()` →
`11_stack_explore_calls.png`.

- Two stacked bars: `control`, `treatment`.
- Segment order bottom → top: **grep, cat, sed_read, mixed** (the manual-search
  block), then **qi** on top, visually set apart (treatment blue).
- Y-axis: recognized-tool **actions per run** (segment action-count ÷ n runs of
  that arm).
- Reads-at-a-glance: control = tall grep+cat+sed-read block, no qi; treatment =
  shorter manual block with a qi block added on top.
- Light-gray horizontal gridlines (`color="0.85"`, `set_axisbelow(True)`).
- Optional: annotate the manual-search sub-total (grep+cat+sed_read) on each bar
  so the shrink is quantified.
- Title: "Recognized-tool calls per arm (actions/run)".

**Cross:** `cross_batch_compare.py`, new `explore_stack_chart()` →
`explore_calls.png`.

- One row per instance, two stacked bars (control, treatment), same segments.
- **Normalize each instance to its control total = 100%** (call counts span a
  wide range across instances), same approach as `cumulative_cost_chart`.
- Dotted 100% reference line; gridlines; legend; instances sorted alphabetically.
- Title: "Recognized-tool calls (per instance, % of control total)".

### 4.2 Chart 2 — token output, homogeneous-gated
**Single:** `analyze_pro_stats.py`, new `chart_explore_tokens()` →
`12_stack_explore_tokens.png`.

- Two stacked bars: `control`, `treatment`. Segments: **qi, grep, cat, sed_read**
  (no `mixed` — mixed actions have no clean token attribution).
- Y-axis: **tokens per run**, summed only over **homogeneous** actions
  (`only_tool_and_echo(command, tool)`), divided by n runs of that arm.
- Same palette/gridlines/legend conventions as Chart 1.
- Title/subtitle must say "homogeneous single-tool actions only" so the exclusion
  of mixed actions is explicit (the stack is **not** a total).

**Cross:** `cross_batch_compare.py`, new `explore_tokens_chart()` →
`explore_tokens.png`, normalized to control total = 100%, same layout as 4.1 cross.

### 4.3 Per-call means (ESSENTIALS report, not a chart)
Extend `report_qi_commands.py`: for each of qi / grep / cat / sed_read compute,
from homogeneous actions only, **total output/run** and **per-call mean/median**
using the existing `_percall` division (an action `qi a; echo ===; qi b` splits
its output across its 2 qi sub-calls; same for one-or-many cat/grep). qi already
gets this treatment; extend to grep/cat/sed_read and tighten grep to
homogeneous-only.

### 4.4 Chart 3 — the efficiency radar (the hero) — BUILT
The single marketable synthesis of §1b. A **pentagon radar** with grey **control =
baseline (100% on every axis)** enclosing the blue **treatment** polygon; every
axis is oriented "**smaller = leaner**" so the enclosed shape reads as "more
efficient." Aggregated with the **geometric mean** of per-instance
treatment/control ratios (the correct, outlier-resistant average for ratios).

**The 5 axes** (`RADAR_AXES`):
1. **log size** — transcript KB (context volume)
2. **log variance** — std of log KB across reps (consistency / the guardrail win)
3. **turns** — steps (kept even though it's ~flat / can go the wrong way)
4. **patch lines** — edit size
5. **grep+cat calls** — the **mechanism** axis: qi *displaces* grep+cat calls,
   which is *why* the other four fall. Pure usage detection (`count grep + count
   cat` per action) — **not** the discarded `read` partition, so `pytest | head`
   is never counted. sed-read is deliberately excluded: including it flips
   qutebrowser to "stacks"; grep+cat alone is the cleaner, stronger signal (−21%
   vs −15% pooled) and the truer displacement story.

Axes 1–4 are outcomes; axis 5 is the mechanism behind them, so log-size and
grep+cat are causally linked (not independent). That linkage is the *point*
("here's *why* it got leaner"); a radar doesn't average the axes into one score,
so this is storytelling, not double-counting. Do not add these into a single
composite number — correlated axes would fake breadth (see §12).

**One chart, nested polygons** (not side-by-side panels — direct comparison is
easier when the subset is drawn *inside*):
- grey **control** baseline (100%),
- dark-blue **treatment — all instances** (pooled geomean, the larger polygon),
- light-green **treatment — qi good-fit** drawn *inside* it, with the non-source
  instances (`RADAR_NON_SOURCE = {openlibrary, flipt}`) scoped out. The legend just
  says "qi good-fit"; *why* those two are excluded (majority non-source patch) is
  explained in the surrounding prose, not crammed into the chart.
- Each axis label carries **both** deltas, `"log size  −10% / −14%"` (all /
  source-nav).

The subset is principled, **not** "drop the losers": those two are the only
instances whose gold patch is **majority non-source** (openlibrary 2/8 source —
rest templates/CSS/SVG/i18n; flipt 1/5 source — rest proto/generated/swagger/
CHANGELOG). qi indexes source code, so it mechanically can't help navigate them —
a coherent scope, verified from gold-patch composition. The inner polygon must be
shown *with* that justification, never presented as the headline alone.

**Single-instance analog:** `analyze_pro_stats.chart_radar` →
`13_radar_efficiency.png` (one instance, treatment/control ratio, no geomean).
**Cross:** `cross_batch_compare.radar_chart` → `radar_efficiency.png` (2 panels).

Pooled results (geomean, all 7 → 5 source-nav): log variance −32% → **−47%**,
patch lines −16% → −22%, log size −10% → −14%, grep+cat calls −21% → −21%,
turns −1% → −2%.

---

## 5. Data plumbing

### 5.1 Single-instance (`analyze_pro_stats.py`)
Add a dedicated loader `load_explore(run_dir)` returning, per `(arm, run_id)`:
- `calls[segment]` over all successful actions (`action_tool`), and
- `tokens[tool]` over homogeneous successful actions (`only_tool_and_echo`).

Keep `load_qi_commands` untouched (it still feeds the existing, unmodified
`08_box_qi_grep` chart). Charts 1 and 2 aggregate the loader's per-run values to
per-arm means (per run).

### 5.2 Cross-instance (`cross_batch_compare.py`)
Add a small reader inside each new chart fn (mirroring `qi_grep_chart`) that walks
each batch's `qi_commands.csv`, applies §3.2 detection, sums per arm, divides by
run count, then normalizes to the control total. No `load_batch` change.

---

## 6. Implementation steps (ordered)

1. **lib/cmds.py:** add `only_tool_and_echo()` and `action_tool()`; scratch-check
   invariants (`pytest … | head` → None; `sed -i` → None; `cat a; sed -n b` →
   mixed; `cat a; cat b` → cat; `qi a; echo ===; qi b` → qi and homogeneous).
2. **analyze_pro_stats.py:** add `load_explore()`, `EXPLORE_COLORS`/labels,
   `chart_explore_stack()` (#11) and `chart_explore_tokens()` (#12); wire into
   `make_charts()`; self-skip when `qi_commands.csv` is missing.
3. **cross_batch_compare.py:** add matching constants and `explore_stack_chart()`
   → `explore_calls.png` + `explore_tokens_chart()` → `explore_tokens.png`; call
   both in `main()`'s chart section; self-skip when no batch has data.
4. **report_qi_commands.py:** extend the homogeneous per-tool token totals and
   per-call means to grep/cat/sed_read (§4.3).
5. **Regenerate & eyeball:** single (`pro_pilot_tutanota2_sonnet5`,
   `pro_pilot_flipt_haiku`), cross (the 7-batch `_cross_adhoc_sonnet` set).
6. **Verify** (see §7).

Do not touch the existing `chart_qi_grep` / `qi_grep_chart` / `search_output_chart`
functions (see §9).

---

## 7. Verification

- **Detection invariants** (scratch): the four §6.1 cases classify as stated;
  `action_tool` returns None for non-recognized actions.
- **Manual-block direction (behavior axis only):** grep+cat+sed_read calls/run
  drop ctl→trt on tutanota and nodebb; qutebrowser stacks (rises). This contrast
  validates the *call* chart — but is **not** a proxy for the outcome; do not read
  "stacks" as "no benefit" (qutebrowser stacks yet wins biggest on §1b/§4.4).
- **Outcome axis is separate:** confirm the §4.4 chart's per-instance direction
  against the runs CSV (qutebrowser cost/tokens/log all fall ~−37…−68% despite the
  stacked call block), and that the per-run spread — not just the mean — is
  rendered (control's blow-up reps must be visible).
- **Homogeneous token gate:** every action contributing to a token segment passes
  `only_tool_and_echo`; the sum of homogeneous-tool tokens is ≤ that tool's raw
  per-action total (gate only removes actions).
- **Cross normalization:** each control stack sums to 100%; treatment stack height
  is its own total ÷ control total.
- **Per-call means (ESSENTIALS):** `_percall` sum-preserving — total/run unchanged
  vs pre-`_percall`; only means/medians shift where sub-call batching occurs.
- `py_compile` all edited files; regenerate without exceptions.

---

## 8. Edge cases & decisions

- **`mixed`** appears only in the call-count chart (its output cannot be cleanly
  attributed). It is small-to-moderate (tutanota ~3, nodebb ~5–9 actions/run) and
  is the honest home for `cat … | grep …`, `qi …; cat …`, etc.
- **grep-in-pipe** (`cat file | grep x`) uses two recognized tools → `mixed`, not
  grep. A bare `grep … | head` uses one recognized tool (grep) → grep.
- **`sed -i`, head/tail/less/more, pytest/npm/node/make** are not recognized and
  never counted — no bucket, no exclusion rule needed.
- **Errored actions** excluded (`is_error == 0`).
- **Per-call vs total:** call-count chart is per-action; token chart is per-run
  totals; per-call means (ESSENTIALS) use `_percall`. Each states its unit.

---

## 9. Disposition of the old charts

`chart_qi_grep` (single `08_box_qi_grep.png`), `qi_grep_chart` (cross
`qi_grep.png`), and `search_output_chart` (cross `search_output.png`) are **left
as-is, unmodified, but deprecated in place** — no longer cited. Note in particular
`search_output.png`'s "qi returns compact results" subtitle is now known-wrong
(per-call qi output is *not* smaller than grep). Deletion is deferred to a
separate pass once Charts 1–2 prove out.

---

## 10. Files touched (summary)

| File | Change |
|---|---|
| `experiment/lib/cmds.py` | add `only_tool_and_echo()`, `action_tool()` (no `explore_bucket`/`read_kind`) |
| `experiment/analysis/analyze_pro_stats.py` | `load_explore()`, `chart_explore_stack()` → `11_…`, `chart_explore_tokens()` → `12_…`; radar (`RADAR_AXES`, `_geomean`, `radar_metrics`, `radar_ratios`, `_radar_on_ax`, `chart_radar()` → `13_radar_efficiency.png`); retired `chart_qi_grep` + wall-time (`04`) chart calls |
| `experiment/analysis/cross_batch_compare.py` | `explore_stack_chart()` → `explore_calls.png`, `explore_tokens_chart()` → `explore_tokens.png`, `radar_chart()` → `radar_efficiency.png` (2 panels, `RADAR_NON_SOURCE`); retired `qi_grep_chart` call |
| `experiment/analysis/report_qi_commands.py` | homogeneous per-tool token totals + `_percall` means for qi/grep/cat/sed-read |

No CSV schema change (all derivable from existing CSV columns). Retired old charts
(`08_box_qi_grep`, cross `qi_grep.png`, wall-time `04`) have their *calls*
commented out; functions left in place.

---

## 11. Post-mortem: why the first draft was wrong

The abandoned draft classified each action into exactly one mutually-exclusive
bucket via a "primary tool" precedence (qi > grep > read > other), with a `read`
branch that fell through to `other_read` for any read-tool action lacking cat/sed.
Because a trailing `| head`/`| tail` makes `count_tools` label an action as
`read`, **test-execution commands** (`pytest … | head`, `npm test … | tail`)
landed in `other_read` and were charted as "exploration/reading":

| instance | other_read that was actually test execution |
|---|---|
| qutebrowser | 76% |
| tutanota | 93% |
| nodebb | 48% |

That inflated the "net exploration output drops" story and manufactured a
**−31%** drop on qutebrowser that **vanishes** (→ ~0%) once test runs are excluded.
Only tutanota's drop (~−12%) survived. The fix is architectural, not a patch:
stop partitioning actions; only detect genuine tool *usage* — commands we don't
recognize as exploration tools are simply not counted.

---

## 12. Outcome chart — decisions (RESOLVED → became §4.4)

The outcome chart became the **efficiency radar** (§4.4). Decisions and the
reasoning that got there, so a future session doesn't relitigate:

- **Radar, not dumbbell/strip/cumulative.** The goal shifted from "show variance
  collapse in one metric" to "one hero that carries the whole story." A radar is
  the PC-ranking-style multi-dimensional visual that reads as "better across the
  board."
- **Geometric mean, not median or arithmetic mean.** Median was robust but the
  two-panel narrative fell *flat* (removing 2 of 7 barely moved it). Arithmetic
  mean was dramatic but *outlier-driven* (one openlibrary 1.87× swung it) — the
  exact "pick the stat that flatters" move to avoid. Geomean is the correct
  average for ratios, outlier-resistant, **and** still moves on a principled
  exclusion. Best of both.
- **NO single composite score.** A PC-guide-style averaged "final score" was
  considered and **rejected**: our axes aren't independent (log-size, grep+cat,
  and — if we'd added them — tokens/cost/files are all facets of one thing), so
  averaging fakes breadth. The radar keeps the multi-dim *visual* without
  inventing a number that hides the correlation. Resolution stays a *gate*, never
  an averaged ingredient.
- **5th axis = grep+cat calls (mechanism), not files_touched / distinct-files-
  read / errors / test-runs.** files_touched was a patch-lines twin;
  distinct-files-read reintroduced the discarded command-classification problem
  (it needs `tool=="read"` + file-arg parsing, and `grep -r` reads a whole tree);
  errored-commands and test-runs were noisy. grep+cat calls is clean usage
  detection, strong (−21%), and doubles as the mechanism for the other axes.
- **Both scopes built:** cross 2-panel (pooled) + single-instance.
