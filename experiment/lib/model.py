"""The model under test and its filesystem-safe directory slug.

``run_experiment.py`` and ``run_pilot.py`` both carried a literal ``MODEL`` plus
the ``replace("/", "--")`` slug; the orchestrator's default had to be kept in
sync with the child's by hand. This is the single source.
"""
from __future__ import annotations

# litellm model identifier: provider/model-name.
MODEL = "deepseek/deepseek-v4-flash"


def model_dir(model: str = MODEL) -> str:
    """Filesystem/log-dir-safe slug, e.g. ``deepseek--deepseek-v4-flash``."""
    return normalize_model(model).replace("/", "--")


def normalize_model(model: str = MODEL) -> str:
    """Canonical litellm id (``provider/model``) from either form.

    Accepts the dir-slug form (e.g. ``deepseek--deepseek-v4-flash``, as it appears
    under ``logs/<model>/``) and restores the provider ``/`` so the value is a
    valid litellm model id. The slug only ever replaces the single provider ``/``
    (model names use single ``-``), so restoring the first ``--`` is unambiguous.
    """
    if "/" in model:
        return model
    if "--" in model:
        return model.replace("--", "/", 1)
    return model


def provider_of(model: str = MODEL) -> str:
    """Provider prefix from a litellm id *or* its dir-slug.

    ``deepseek/deepseek-v4-flash`` -> ``deepseek``;
    ``deepseek--deepseek-v4-flash`` -> ``deepseek`` (slug form, e.g. copied from a
    logs path).
    """
    return normalize_model(model).split("/", 1)[0]


def api_key_var(model: str = MODEL) -> str:
    """Env var holding the API key for ``model``'s provider.

    e.g. ``DEEPSEEK_API_KEY`` / ``ANTHROPIC_API_KEY``.
    """
    return f"{provider_of(model).upper()}_API_KEY"
