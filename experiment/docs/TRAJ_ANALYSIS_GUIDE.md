# Trajectory Analysis Scripts Guide

Scripts in `tmp/` for analyzing mini-swe-agent `.traj.json` files. Each is self-contained — edit the `BASE` and `INST_DIR` variables at the top to point at your trajectories.

## Quick Start

```bash
# Edit the path in the script, or override inline:
python3 -c "
import json, re
# ... copy the relevant function and call it
"
# Or: run the script directly (edit BASE path first)
python3 tmp/analyze_traj.py
```

All scripts hardcode paths as module-level constants near the top. Change `BASE` (the log directory root) and `INST_DIR` (the instance subdirectory) to match your run.

## Script Reference

### 1. `analyze_traj.py` — First orientation

**Purpose:** Quick structural overview of a `.traj.json` file. Shows top-level keys, message counts by role, exit status, model stats, config keys. Use this first when you've never seen the file before.

**Output:** Message count table, exit status, submission preview, model_stats, config keys.

**Customize:** Change `BASE` (line ~7) and `INST_DIR` paths. The script iterates `TRAJ_PATHS` dict — add or remove arms.

### 2. `analyze_format_errors.py` — Why did format errors happen?

**Purpose:** Categorize every format error: was the model emitting XML `<tool_call>` without backticks? Multiple code blocks? Backticks without `bash` label? Shows error turn positions, gap analysis, consecutive error runs, error density by session third.

**Output:** Error categories with counts and samples, error turn positions, recovery patterns, efficiency metrics.

**Customize:** Change the `BASE` and path construction near the top. The `err_msg` constant at ~line 20 can be changed to match different format error templates.

### 3. `analyze_xml_bt.py` — Do XML tool_calls ever contain valid backticks?

**Purpose:** The definitive check: for each assistant turn, does XML `<tool_call>` coexist with a valid ```bash block? In MiMo's case, the answer is "never" — XML is always a wasted turn. Also shows action distribution by wrapper type (xml+bt, xml_no_bt, bt_only) and XML usage over time (by thirds).

**Output:** Format breakdown table, action distribution, recovery patterns, XML usage trend.

**Customize:** Same path constants. The `BACKTICK_RE` regex detects ```bash blocks — adjust if the expected format differs (` ```sh `, ` ```python `, etc.).

### 4. `analyze_final.py` — Full turn-by-turn action classification

**Purpose:** Classifies every assistant turn into one of: edit, test_run, qi, navigate, grep, write_tmp, git, format_error, misc. Also counts test pass/fail lines in user observations. This is the most complete single-script analysis.

**Output:** Turn breakdown, action distribution, efficiency metrics (edit rate, test rate), last-50-turns breakdown, test outcome counts, user observation types.

**Customize:** Path constants. The classification logic uses prefix matching on stripped commands — extend the pattern lists if your commands use different prefixes (e.g., `rg` instead of `grep`, `cargo test` instead of `pytest`). The `strip_cd()` function handles `cd /app &&` prefixes — change the regex if your working dir differs.

### 5. `analyze_traj_deep.py` — Earlier version of final analysis

**Purpose:** Predecessor to `analyze_final.py`. Classifies user messages and assistant actions. Less comprehensive classification (misses `cd /app &&` prefixes) but includes recovery tracking and qi-vs-grep comparison. Kept for reference — use `analyze_final.py` instead.

### 6. `analyze_qi_responses.py` — What did each qi query actually return?

**Purpose:** Parses the full output of every qi query in a trajectory. Shows the command, the number of output lines, and the actual response content (up to 8 lines for short results, first/last 3 for long). Identifies empty/near-empty results, overly broad queries, verbose-vs-raw usage, parent queries, and qi→grep transitions (qi+grep in the same command).

**Output:** Per-query: turn number, output line count, command, actual response content. Summary: empty queries, broad patterns, verbose vs raw count, parent query effectiveness, qi+grep same-command patterns.

**Customize:** Path constants. The `strip_cd()` function same as above. The response cleaning regex strips `<returncode>`, `<output>`, `<obs>` tags — adjust if your harness wraps output differently.

### 7. `compare_grep_qi.py` — What did each arm search for?

**Purpose:** Side-by-side comparison of grep/find commands (control) vs qi commands (treatment). Shows the actual commands and file reads per arm. Also does a cross-reference: extracts search terms from both arms and computes overlap.

**Output:** Per-arm: grep commands, qi commands, files read (cat), actual edits, fix scripts, test runs. Cross-reference table: terms searched by each arm, overlap count.

**Customize:** Path constants. The grep command detection looks for `grep`, `find`, `rg` — add more if needed. Qi detection looks for `qi ` prefix.

### 8. `compare_discoveries.py` — Per-symbol: what did each arm discover?

**Purpose:** For a fixed set of key symbols (e.g., `timedout`, `_UNSET`, `is_controller`), shows what control discovered using grep vs what treatment discovered using qi. Includes the actual user observation responses so you can see what the agent saw. Also identifies grep-only discoveries (Ellipsis, cross-cutting regex) and checks qi response quality.

**Output:** Per-symbol: control grep results with responses, treatment qi results with responses, treatment grep results (if qi wasn't used). Summary: critical grep-only discoveries, qi response emptiness check.

**Customize:** Path constants. Edit `KEY_SYMBOLS` list (around line 30) to match the symbols relevant to your instance.

## Common Customizations

### Point at a different run

All scripts have this pattern near the top:

```python
BASE = "experiment/logs/xiaomi_mimo--mimo-v2.5-pro/pro_pilot_ansible_mimo_v2.5-pro"
INST_DIR = "instance_ansible__ansible-6cc97447aac5816745278f3735af128afb255c81-v0f01c69f1e2528b935359cfe578530722bca2c59"
FNAME = f"{INST_DIR}.rep01.traj.json"
```

Change `BASE` to your log root and `INST_DIR` to your instance directory. The `INST_DIR` pattern is `<instance_id>-v<base_commit>f<patch_commit>`.

### Different format error message

The MiMo format error is:

```python
ERR_MSG = "Please always provide EXACTLY ONE action in triple backticks"
```

For a different model or harness, change this string to match the format error template in your config's `format_error_template`.

### Different command format

The analysis scripts look for triple-backtick bash blocks:

```python
BACKTICK_RE = re.compile(r"```bash\s*\n(.*?)\n```", re.DOTALL)
```

If your agent uses `<command>` XML tags, ` ```sh `, or some other format, change this regex.

### Different working directory

Commands often start with `cd /app &&`. The strip function is:

```python
def strip_cd(cmd):
    return re.sub(r"^(cd\s+/app\s*[;&]{1,2}\s*|cd /app && |cd /app ; )", "", cmd)
```

Change `/app` to your container's working directory.

### Different command classification

The action classifiers in `analyze_final.py` use prefix matching:

```python
if re.match(r"sed\s+-i\s", first):
    is_edit = True
elif "pytest" in first:
    test_runs += 1
```

Add patterns for your language's build system (`cargo test`, `go test`, `npm test`, etc.) and edit commands (`sd`, `awk`, etc.).

## Typical Workflow

1. Run `analyze_traj.py` first — confirm the file loads, check message counts, exit status.
2. Run `analyze_format_errors.py` — are there format errors? What causes them?
3. Run `analyze_xml_bt.py` — if XML tool_calls are present, do they ever coexist with backticks?
4. Run `analyze_final.py` — full action classification, efficiency metrics, test outcomes.
5. If treatment uses qi: run `analyze_qi_responses.py` to check qi output quality.
6. If comparing arms: run `compare_grep_qi.py` and `compare_discoveries.py` for the head-to-head.
