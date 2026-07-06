"""InteractiveAgent hardened against hallucinated multi-turn XML sessions.

The vendored scaffold's parse_action() (vendor/swebench_pro_mini/src/minisweagent/
agents/default.py) scans the WHOLE response for ```bash fences and accepts it if
exactly one is found -- it has no opinion about anything else in the response.

Confirmed on instance_tutao__tutanota-fbdb72a2bd... rep01 (swebp_treatment,
claude-haiku-4-5): a single 43KB completion fabricated ~113 <function_calls>/
<invoke> blocks simulating an entire fake multi-turn session (fake tool calls,
fake "thinking" reactions to results it never received, a bogus 7-file
implementation summary) and then happened to end with one real
```bash echo COMPLETE_TASK_AND_SUBMIT_FINAL_OUTPUT``` fence. Because that was
the only real fence in the text, parse_action() accepted it without error and
the task terminated as "Submitted" after step 1 -- with zero real edits made.
The "patch" that got collected was just the pre-existing test_patch diff from
container startup, not agent work.

The system prompt already tells the model "NEVER use XML (<function_calls>)
syntax", but that's advisory only -- nothing enforces it. This subclass makes
it a real FormatError, forcing a retry turn instead of a silent false-positive
completion.
"""
from __future__ import annotations

import re

from minisweagent.agents.default import FormatError
from minisweagent.agents.interactive import InteractiveAgent

_XML_TOOL_CALL_RE = re.compile(r"<function_calls>|<invoke\b")


class GuardedInteractiveAgent(InteractiveAgent):
    def parse_action(self, response: dict) -> dict:
        content = response.get("content", "")
        if _XML_TOOL_CALL_RE.search(content):
            raise FormatError(
                "Your response contains <function_calls>/<invoke> XML tags. "
                "The harness does NOT execute XML tool-call syntax, and it does "
                "NOT simulate multiple turns in one response -- you fabricated "
                "tool results and reasoning about output you were never given. "
                "Issue exactly ONE command in a single ```bash``` fenced block "
                "and stop; you will receive the REAL output before your next turn."
            )
        return super().parse_action(response)
