"""Map a trajectory file path to its (model, arm, instance, rep) identity.

The orchestrator writes trajectories to
``logs/<model>/<arm>/<instance>/<run_id>.traj.json``. Both analysis scripts
(``analyze_trajectories.py``, ``evaluate_patches.py``) must agree on how to read
that layout back; this is the single source for that contract (previously copied
verbatim in both, which caused the ``infer_arm_instance`` -> ``infer_path_meta``
drift).
"""
from __future__ import annotations

import json
from pathlib import Path

ARMS = ("control", "treatment")


def infer_path_meta(path: Path) -> tuple[str, str, str, str]:
    """Derive (model, batch_id, arm, instance_id) from the path.

    Supported layouts (the model is always the directory directly under
    ``logs/``; a named batch inserts one level between the model and the arm)::

        logs/<model>/<batch>/<arm>/<instance>/<run_id>.traj.json   (named batch)
        logs/<model>/<arm>/<instance>/<run_id>.traj.json           (no batch)
        logs/<arm>/<instance>/<run_id>.traj.json                   (legacy no-model)
        <instance>_<arm>.traj.json                                 (legacy flat)

    ``batch_id`` is ``""`` for every layout except the named-batch one.
    """
    parts = path.parts
    arm = next((p for p in parts if p in ARMS), "")
    # nested layout: .../[<model>/[<batch>/]]<arm>/<instance>/<run_id>.traj.json
    if arm and path.parent.parent.name == arm:
        instance = path.parent.name
        arm_parent = path.parent.parent.parent       # <model> or <batch>
        if arm_parent.name == "logs":                # legacy logs/<arm>/...
            return "", "", arm, instance
        grandparent = path.parent.parent.parent.parent
        # The model sits directly under logs/. If the directory above the arm's
        # parent is itself a real directory other than the logs root, then the
        # arm's parent is a batch folder and the model is one level higher.
        if grandparent.name and grandparent.name != "logs":
            return grandparent.name, arm_parent.name, arm, instance
        return arm_parent.name, "", arm, instance
    # flat layout: <instance>_<arm>.traj.json (no model dir)
    stem = path.name.replace(".traj.json", "")
    for a in ARMS:
        if stem.endswith("_" + a):
            return "", "", a, stem[: -(len(a) + 1)]
    return "", "", arm, stem


def rep_of(path: Path) -> str:
    """Run id (rep) for a trajectory: the filename stem, e.g. ``1`` or ``3``."""
    return path.name.replace(".traj.json", "")


def _read_manifest(path: Path) -> dict:
    manifest = path.with_name(path.name.replace(".traj.json", ".manifest.json"))
    if manifest.exists():
        try:
            return json.loads(manifest.read_text())
        except (json.JSONDecodeError, OSError):
            pass
    return {}


def batch_of(path: Path) -> str:
    """Batch id for a trajectory.

    The directory layout is authoritative (``logs/<model>/<batch>/<arm>/...``);
    fall back to the manifest's ``batch_id`` field for trajectories that carry a
    batch tag in their manifest without a batch subdirectory.
    """
    return infer_path_meta(path)[1] or _read_manifest(path).get("batch_id", "")


def n_files_of(path: Path) -> str:
    """Read n_files from the corresponding manifest, or '' if missing."""
    return _read_manifest(path).get("n_files", "")


def patch_files_of(diff: str) -> int:
    """Count unique file paths touched by a unified diff. Returns 0 for empty."""
    if not diff.strip():
        return 0
    files: set[str] = set()
    for line in diff.splitlines():
        if line.startswith("--- a/") or line.startswith("+++ b/"):
            path_str = line.split("/", 1)[1] if "/" in line else line[6:]
            files.add(path_str.rstrip("\t"))
    return len(files)
