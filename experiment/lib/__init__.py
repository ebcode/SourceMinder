"""Shared helpers for the qi context-preservation experiment.

Importable from any experiment script once the ``experiment/`` directory is on
``sys.path``. Scripts bootstrap that with::

    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # -> experiment/
    from lib import paths

(top-level scripts in ``experiment/`` use ``parents[0]``).
"""
