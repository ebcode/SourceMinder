"""parse_action() must accept ```bash fences, <command> tags, and Qwen/Hermes
<tool_call> blocks, so models that default to XML tool-calling conventions
(DeepSeek, Xiaomi MiMo) aren't penalized with a FormatError every turn.

The XML sample below is the *verbatim* emission captured from a real MiMo run:
  experiment/logs/xiaomi_mimo--mimo-v2.5-pro/.../swebp_control_rep01.log
"""

import pytest

from minisweagent.agents.default import DefaultAgent, FormatError
from minisweagent.environments.local import LocalEnvironment
from minisweagent.models.test_models import DeterministicModel


def make_agent() -> DefaultAgent:
    # Model outputs are unused here; we call parse_action() directly.
    return DefaultAgent(model=DeterministicModel(outputs=[""]), env=LocalEnvironment())


# --- the three accepted conventions, each carrying the same command ----------

BACKTICK = "Let me look.\n```bash\ncat /app/foo.py\n```"

COMMAND_TAG = "Let me look.\n<command>\ncat /app/foo.py\n</command>"

# Verbatim Qwen/Hermes tool_call as emitted by MiMo-v2.5-pro.
MIMO_TOOL_CALL = (
    "I'll inspect the test file.\n"
    "<tool_call>\n"
    '<function name="bash">\n'
    "<parameter=command>cat /app/foo.py</parameter>\n"
    "</function>\n"
    "</tool_call>"
)


@pytest.mark.parametrize(
    "content",
    [BACKTICK, COMMAND_TAG, MIMO_TOOL_CALL],
    ids=["backtick", "command_tag", "mimo_tool_call"],
)
def test_each_format_parses_to_same_command(content):
    agent = make_agent()
    parsed = agent.parse_action({"content": content})
    assert parsed["action"] == "cat /app/foo.py"
    # original response fields are preserved
    assert parsed["content"] == content


def test_multiline_command_body_preserved():
    agent = make_agent()
    body = "cd /app\ngit status\n./run.sh --flag"
    content = f"<parameter=command>{body}</parameter>"
    assert agent.parse_action({"content": content})["action"] == body


def test_zero_actions_raises_format_error():
    agent = make_agent()
    with pytest.raises(FormatError):
        agent.parse_action({"content": "Just prose, no command anywhere."})


def test_multiple_actions_raises_format_error():
    agent = make_agent()
    content = "```bash\necho one\n```\n```bash\necho two\n```"
    with pytest.raises(FormatError):
        agent.parse_action({"content": content})


def test_first_convention_wins_no_double_count():
    """A real ```bash command plus a stray <parameter=command> in reasoning text
    must not be counted as two actions: the highest-priority convention that
    matches is the only one consulted."""
    agent = make_agent()
    content = (
        "Note: tools take <parameter=command>...</parameter>.\n"
        "```bash\necho hi\n```"
    )
    assert agent.parse_action({"content": content})["action"] == "echo hi"


def test_xml_only_response_parses_without_any_fence():
    """The regression we are fixing: an XML-only response (no ```bash anywhere)
    used to raise FormatError; it must now parse."""
    agent = make_agent()
    assert "```bash" not in MIMO_TOOL_CALL
    assert agent.parse_action({"content": MIMO_TOOL_CALL})["action"] == "cat /app/foo.py"
