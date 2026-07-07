"""The interactive log header must visibly mark turns whose action was recovered
from reasoning_content (empty content field), so that when reading/analyzing logs
these "read its mind" turns aren't conflated with normal operation.

The marker is driven by extra["content_source"] == "reasoning_content", which
LitellmModel.query() sets. The "mini-swe-agent (step N, $C):" prefix is preserved
so existing log parsers still match.
"""

import io
from unittest.mock import patch

from rich.console import Console

from minisweagent.agents.interactive import InteractiveAgent
from minisweagent.environments.local import LocalEnvironment
from minisweagent.models.test_models import DeterministicModel

MARKER = "recovered-from-reasoning_content"


def _capture_header(extra) -> str:
    """Return the fully *rendered* log text (markup resolved), not the raw markup
    string -- so this catches Rich markup-eating bugs (e.g. [..] parsed as tags)."""
    agent = InteractiveAgent(
        model=DeterministicModel(outputs=[""]), env=LocalEnvironment()
    )
    buf = io.StringIO()
    cap = Console(file=buf, highlight=False, force_terminal=False, width=200)
    with patch("minisweagent.agents.interactive.console", cap):
        kwargs = {"extra": extra} if extra is not None else {}
        agent.add_message("assistant", "body text", **kwargs)
    return buf.getvalue()


def test_marker_present_for_reasoning_sourced_turn():
    header = _capture_header({"content_source": "reasoning_content"})
    assert MARKER in header
    assert "mini-swe-agent" in header and "step" in header  # prefix preserved


def test_no_marker_for_normal_content_turn():
    header = _capture_header({"content_source": "content"})
    assert MARKER not in header
    assert "mini-swe-agent" in header and "step" in header


def test_no_marker_when_extra_absent():
    """Human-mode / synthetic turns pass no extra; they must not be marked."""
    header = _capture_header(None)
    assert MARKER not in header
