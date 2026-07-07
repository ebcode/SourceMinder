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

# Tuple of known arm names used ONLY as a fallback when the structural layout
# detection below cannot determine the arm from directory nesting (e.g. legacy
# flat files named ``<instance>_<arm>.traj.json``). Since v2 the arm is detected
# structurally (the directory that contains the instance subdirectory), so this
# is a legacy guard, not an allow-list.
ARMS = ("control", "treatment")


def infer_path_meta(path: Path) -> tuple[str, str, str, str]:
    """Derive (model, batch_id, arm, instance_id) from the path.

    The arm is detected **structurally**: it is always the directory that
    contains the instance subdirectory.  This works for arbitrary arm names
    (``treatment_v1``, ``treatment_short``, etc.) without a hardcoded set.

    Supported layouts::

        logs/<model>/<batch>/<arm>/<instance>/<run_id>.traj.json   (named batch)
        logs/<model>/<arm>/<instance>/<run_id>.traj.json           (no batch)
        logs/<arm>/<instance>/<run_id>.traj.json                   (legacy no-model)
        <instance>_<arm>.traj.json                                 (legacy flat)

    ``batch_id`` is ``""`` for every layout except the named-batch one.
    """
    # instance is always the immediate parent of the .traj.json file
    instance = path.parent.name

    # arm is structurally the directory that contains the instance subdirectory.
    # In every nested layout (with or without batch / model dirs), the instance
    # dir sits directly inside the arm dir.
    arm_parent = path.parent.parent
    arm = arm_parent.name

    # Structural detection only makes sense when the trajectory is nested at
    # least two levels deep.  If the arm dir's parent is the file's own parent
    # (i.e. the trajectory sits next to instance subdirs — not a real layout),
    # fall through to flat-layout detection.
    if arm_parent != path.parent and arm_parent.name:
        # arm_parent.parent is one of: <batch>, <model>, or "logs"
        container = arm_parent.parent
        if container.name == "logs":
            # Legacy: logs/<arm>/<instance>/...  (no model directory)
            return "", "", arm, instance
        # The model sits directly under logs/.  If the directory two levels
        # above the arm is *also* a real directory that isn't "logs" itself,
        # then we have a batch layer: logs/<model>/<batch>/<arm>/...
        grandparent = container.parent
        if grandparent.name and grandparent.name != "logs":
            return grandparent.name, container.name, arm, instance
        # logs/<model>/<arm>/<instance>/...  (no batch)
        return container.name, "", arm, instance

    # Legacy flat layout: <instance>_<arm>.traj.json
    # Only the known arms are detectable here; unknown arm names resolve as "".
    stem = path.name.replace(".traj.json", "")
    for a in ARMS:
        if stem.endswith("_" + a):
            return "", "", a, stem[: -(len(a) + 1)]
    return "", "", "", stem


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
