import json
import tempfile
from pathlib import Path
from unittest.mock import Mock, patch

import litellm
import pytest

from minisweagent.models import GLOBAL_MODEL_STATS
from minisweagent.models.litellm_model import LitellmModel


def test_authentication_error_enhanced_message():
    """Test that AuthenticationError gets enhanced with config set instruction."""
    model = LitellmModel(model_name="gpt-4")

    # Create a mock exception that behaves like AuthenticationError
    original_error = Mock(spec=litellm.exceptions.AuthenticationError)
    original_error.message = "Invalid API key"

    with patch("litellm.completion") as mock_completion:
        # Make completion raise the mock error
        def side_effect(*args, **kwargs):
            raise litellm.exceptions.AuthenticationError(
                "Invalid API key", llm_provider="openai", model="gpt-4"
            )

        mock_completion.side_effect = side_effect

        with pytest.raises(litellm.exceptions.AuthenticationError) as exc_info:
            model._query([{"role": "user", "content": "test"}])

        # Check that the error message was enhanced
        assert (
            "You can permanently set your API key with `mini-extra config set KEY VALUE`."
            in str(exc_info.value)
        )


def test_model_registry_loading():
    """Test that custom model registry is loaded and registered when provided."""
    model_costs = {
        "my-custom-model": {
            "max_tokens": 4096,
            "input_cost_per_token": 0.0001,
            "output_cost_per_token": 0.0002,
            "litellm_provider": "openai",
            "mode": "chat",
        }
    }

    with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
        json.dump(model_costs, f)
        registry_path = f.name

    try:
        with patch("litellm.utils.register_model") as mock_register:
            _model = LitellmModel(
                model_name="my-custom-model", litellm_model_registry=Path(registry_path)
            )

            # Verify register_model was called with the correct data
            mock_register.assert_called_once_with(model_costs)
    except Exception as e:
        print(e)
        raise e
    finally:
        Path(registry_path).unlink()


def test_model_registry_none():
    """Test that no registry loading occurs when litellm_model_registry is None."""
    with patch("litellm.register_model") as mock_register:
        _model = LitellmModel(model_name="gpt-4", litellm_model_registry=None)

        # Verify register_model was not called
        mock_register.assert_not_called()


def test_model_registry_not_provided():
    """Test that no registry loading occurs when litellm_model_registry is not provided."""
    with patch("litellm.register_model") as mock_register:
        _model = LitellmModel(model_name="gpt-4o")

        # Verify register_model was not called
        mock_register.assert_not_called()


def test_litellm_model_cost_tracking_ignore_errors():
    """Test that models work with cost_tracking='ignore_errors'."""
    model = LitellmModel(model_name="gpt-4o", cost_tracking="ignore_errors")

    initial_cost = GLOBAL_MODEL_STATS.cost

    with patch("litellm.completion") as mock_completion:
        mock_response = Mock()
        mock_response.choices = [Mock(message=Mock(content="Test response"))]
        mock_response.model_dump.return_value = {"test": "response"}
        mock_completion.return_value = mock_response

        with patch(
            "litellm.cost_calculator.completion_cost",
            side_effect=ValueError("Model not found"),
        ):
            messages = [{"role": "user", "content": "test"}]
            result = model.query(messages)

            assert result["content"] == "Test response"
            assert model.cost == 0.0
            assert model.n_calls == 1
            assert GLOBAL_MODEL_STATS.cost == initial_cost


def test_litellm_model_cost_validation_zero_cost():
    """Test that zero cost raises error when cost tracking is enabled."""
    model = LitellmModel(model_name="gpt-4o")

    with patch("litellm.completion") as mock_completion:
        mock_response = Mock()
        mock_response.choices = [Mock(message=Mock(content="Test response"))]
        mock_response.model_dump.return_value = {"test": "response"}
        mock_completion.return_value = mock_response

        with patch("litellm.cost_calculator.completion_cost", return_value=0.0):
            messages = [{"role": "user", "content": "test"}]

            with pytest.raises(RuntimeError) as exc_info:
                model.query(messages)

            assert "Cost must be > 0.0, got 0.0" in str(exc_info.value)
            assert "MSWEA_COST_TRACKING='ignore_errors'" in str(exc_info.value)


def _mock_query(model, *, content, reasoning_content):
    """Run model.query() with a mocked litellm response whose message has the
    given content / reasoning_content. Returns the result dict."""
    with patch("litellm.completion") as mock_completion:
        mock_response = Mock()
        mock_response.choices = [
            Mock(message=Mock(content=content, reasoning_content=reasoning_content))
        ]
        mock_response.model_dump.return_value = {"test": "response"}
        mock_completion.return_value = mock_response
        with patch(
            "litellm.cost_calculator.completion_cost",
            side_effect=ValueError("Model not found"),
        ):
            return model.query([{"role": "user", "content": "test"}])


def test_empty_content_folds_in_reasoning_content():
    """Reasoning models (DeepSeek) may leave content empty and put the fenced
    command in reasoning_content. query() must fold it into content so the action
    is parseable (and the turn isn't logged as a blank line)."""
    model = LitellmModel(model_name="deepseek-v4-flash", cost_tracking="ignore_errors")
    reasoning = "Let me look.\n```bash\ncat /app/foo.py\n```"
    result = _mock_query(model, content="", reasoning_content=reasoning)
    assert result["content"] == reasoning
    # folded turns are tagged so analysis can separate them from normal operation
    assert result["extra"]["content_source"] == "reasoning_content"


def test_blank_content_folds_in_reasoning_content():
    """Whitespace-only content is treated as empty and triggers the fallback."""
    model = LitellmModel(model_name="deepseek-v4-flash", cost_tracking="ignore_errors")
    reasoning = "```bash\nls -la\n```"
    result = _mock_query(model, content="   \n  ", reasoning_content=reasoning)
    assert result["content"] == reasoning
    assert result["extra"]["content_source"] == "reasoning_content"


def test_nonempty_content_ignores_reasoning_content():
    """A normal turn with real content must be untouched -- reasoning_content is
    not appended, so non-reasoning turns are unchanged."""
    model = LitellmModel(model_name="gpt-4o", cost_tracking="ignore_errors")
    result = _mock_query(
        model, content="```bash\necho hi\n```", reasoning_content="some private thoughts"
    )
    assert result["content"] == "```bash\necho hi\n```"
    # normal turns are tagged as content-sourced (not folded)
    assert result["extra"]["content_source"] == "content"


def test_empty_content_and_no_reasoning_stays_empty():
    """If both content and reasoning_content are empty, result stays empty (the
    turn legitimately produced no action and will format-error downstream)."""
    model = LitellmModel(model_name="deepseek-v4-flash", cost_tracking="ignore_errors")
    result = _mock_query(model, content="", reasoning_content=None)
    assert result["content"] == ""
    # still marked as a recovery attempt (content field was empty)
    assert result["extra"]["content_source"] == "reasoning_content"
