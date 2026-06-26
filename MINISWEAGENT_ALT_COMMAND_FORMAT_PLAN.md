# Mini-SWE-Agent Alternative Command Format Plan

**Goal:** Add an `action_format` config field so agents can use XML-style `<command>` tags instead of triple backticks for action parsing.

**Motivation:** Agents using non-Anthropic models (DeepSeek, Xiaomi MiMo) tend to output `<command>` tags, matching the training data of many open-weight models. Currently, `parse_action` only recognizes ```` ```bash ```` blocks, so these agents get FormatError on every turn.

---

## Files to Modify

### 1. `experiment/vendor/swebench_pro_mini/src/minisweagent/agents/default.py`

#### 1a. Add `action_format` to `AgentConfig` (line ~35)

```python
action_format: str = "backtick"
"""Action format the agent should use. 'backtick' for ```bash blocks, 'xml' for <command> tags."""
```

#### 1b. Add XML regex support to `parse_action` (line ~134)

Replace the current single-regex parse_action:

```python
def parse_action(self, response: dict) -> dict:
    """Parse the action from the message. Returns the action."""
    if self.config.action_format == "xml":
        actions = re.findall(
            r"<command>\s*\n?(.*?)\n?\s*</command>",
            response["content"], re.DOTALL
        )
    else:
        actions = re.findall(
            r"```bash\s*\n(.*?)\n```", response["content"], re.DOTALL
        )
    if len(actions) == 1:
        return {"action": actions[0].strip(), **response}
    raise FormatError(
        self.render_template(self.config.format_error_template, actions=actions)
    )
```

**Design notes:**
- `<command>...</command>` — handles optional whitespace/newlines around the command
- The `timeout_template` default already uses `<command>` XML format: `<command>{{action['action']}}</command>`. No change needed there. This was the hint that XML was already partially used.
- `has_finished` is unchanged — it checks output content, not action format.

---

### 2. `experiment/config/swebp_control.yaml`

#### 2a. Add `action_format: xml` to agent section

#### 2b. Replace triple-backtick references in templates with XML equivalents

The templates currently use ```` ```bash ```` blocks. For XML format:

**system_template** — replace:
```
Your response must contain exactly ONE bash code block with ONE command...
Format your response as shown in <format_example>.

<format_example>
Your reasoning and analysis here. Explain why you want to perform the action.

```bash
your_command_here
```
</format_example>
```
with:
```
Your response must contain exactly ONE <command> tag with ONE command (or commands connected with && or ||).
Include a THOUGHT section before your command where you explain your reasoning process.
Format your response as shown in <format_example>.

<format_example>
Your reasoning and analysis here. Explain why you want to perform the action.

<command>
your_command_here
</command>
</format_example>
```

**format_error_template** — replace:
```
Please always provide EXACTLY ONE action in triple backticks, found {{actions|length}} actions.
```
with:
```
Please always provide EXACTLY ONE action in <command> tags, found {{actions|length}} actions.
```

**instance_template** — replace the example:
```
<example_response>
THOUGHT: I need to understand the structure...

```bash
ls -la
```
</example_response>
```
with:
```
<example_response>
THOUGHT: I need to understand the structure...

<command>
ls -la
</command>
</example_response>
```

**"Useful command examples" section** — replace all ```` ```bash ```` blocks in the command examples with `<command>` blocks (create file, sed, view file, etc.)

---

### 3. `experiment/config/swebp_treatment.yaml`

Same changes as `swebp_control.yaml` plus treatment-specific templates that reference triple backticks.

---

### 4. `experiment/config/pro_shared.yaml`

Add `action_format: xml` to simplify switching between experiments. If set here, both arms inherit it. Can be overridden per-arm.

---

## Optional / Future

### XML-format config variants

Instead of modifying the existing control/treatment configs, create new ones:
- `config/swebp_control_xml.yaml`
- `config/swebp_treatment_xml.yaml`

Then run with `--arm swebp_control_xml` or similar. This keeps the backtick configs intact for comparison experiments.

### Backward compatibility

The default `action_format` is `"backtick"`, so existing configs without the field continue to work unchanged.

### Regex edge cases

The XML regex `r"<command>\s*\n?(.*?)\n?\s*</command>"` handles:
- `<command>ls</command>`
- `<command>\nls\n</command>`
- Leading/trailing whitespace stripped via `.strip()`

Does NOT handle:
- Nested XML (not a realistic concern for bash commands)
- Self-closing `<command/>` (handled by length check)

---

## Verification

| Check | Method |
|-------|--------|
| XML parse_action extracts `<command>ls -la</command>` | Unit test or manual agent step |
| XML parse_action rejects multiple `<command>` blocks | FormatError raised |
| XML parse_action rejects zero commands | FormatError raised |
| Backtick format unchanged | Existing configs still parse ```` ```bash ```` |
| Templates render without Jinja2 errors | `run_pro_one.py` dry run |
| `timeout_template` unchanged (already XML) | No modification needed |

---

## Files Touched Summary

| File | Change |
|------|--------|
| `vendor/swebench_pro_mini/src/minisweagent/agents/default.py` | Add `action_format` to `AgentConfig`, extend `parse_action` |
| `config/swebp_control.yaml` | Add `action_format: xml`, update templates |
| `config/swebp_treatment.yaml` | Add `action_format: xml`, update templates |
| `config/pro_shared.yaml` | Add `action_format: xml` (optional, for default inheritance) |
