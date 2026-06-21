#!/usr/bin/env python3
"""
Run ONE SWE-bench Pro instance through the official scaffold (the vendored Scale
mini-swe-agent fork), using it as a library so the vendor tree stays read-only.

Why this exists
---------------
Neither upstream CLI does what we need on its own:
  - `mini-extra swebench-single` streams the agent live (InteractiveAgent, yolo)
    but unpacks agent.run() as a 2-tuple and crashes *after* a successful solve,
    discarding the collected patch.
  - `mini-extra swebench` (batch) persists the patch but shows only a progress
    table -- no live transcript.

This runner gives BOTH the live transcript AND a clean, eval-ready patch by
calling the scaffold's own pieces and unpacking the 3-tuple
(exit_status, result, patch) correctly. It is the Pro analog of run_one.py, and
it does not modify anything under vendor/.

MUST be run with the Pro venv interpreter (the Scale fork is installed there):

    experiment/.venv_pro/bin/python experiment/run_pro_one.py \
        --instance instance_qutebrowser__qutebrowser-... \
        --arm control

Outputs (under <output>/<arm>/<instance_id>/):
    <instance_id>.traj.json   full trajectory (save_traj)
    <instance_id>.pred        JSON {instance_id, model_patch, ...}, consumable by
                              helper_code/gather_patches.py -> swe_bench_pro_eval.py
"""
from __future__ import annotations

import argparse
import json
import sys
import traceback
from pathlib import Path

EXPERIMENT_DIR = Path(__file__).resolve().parent
DEFAULT_SUBSET = str(EXPERIMENT_DIR / "data" / "swebench_pro")
DEFAULT_OUTPUT = EXPERIMENT_DIR / "logs" / "pro_pilot"
DEFAULT_MODEL = "deepseek/deepseek-v4-flash"
ENV_FILE = EXPERIMENT_DIR / ".env"


def load_env_file(path: Path) -> None:
    """Best-effort load of KEY=VALUE lines from experiment/.env into os.environ.

    The Scale fork only auto-loads ~/.config/mini-swe-agent/.env; our API keys
    live in experiment/.env. Don't override anything already set in the env.
    """
    import os
    if not path.exists():
        return
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, val = line.partition("=")
        key, val = key.strip(), val.strip().strip('"').strip("'")
        os.environ.setdefault(key, val)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--instance", required=True, metavar="INSTANCE_ID",
                        help="SWE-bench Pro instance id")
    parser.add_argument("--arm", default="swebp_control",
                        help="Arm name; selects config/<arm>.yaml and the output "
                             "subdir (default: swebp_control)")
    parser.add_argument("--config", default=None,
                        help="Config path (default: config/<arm>.yaml)")
    parser.add_argument("--model", default=DEFAULT_MODEL,
                        help=f"litellm model id (default: {DEFAULT_MODEL})")
    parser.add_argument("--subset", default=DEFAULT_SUBSET,
                        help="Dataset path or subset name (default: the local "
                             "data/swebench_pro)")
    parser.add_argument("--split", default="test", help="Dataset split")
    parser.add_argument("--output", default=str(DEFAULT_OUTPUT),
                        help=f"Output root dir (default: {DEFAULT_OUTPUT})")
    parser.add_argument("--run-id", default=None,
                        help="Optional run id; appended to filenames for repeats")
    args = parser.parse_args()

    load_env_file(ENV_FILE)

    # Imports happen after arg parsing so --help works without the Pro venv.
    try:
        import yaml
        from datasets import load_dataset
        from minisweagent.agents.interactive import InteractiveAgent
        from minisweagent.models import get_model
        from minisweagent.run.extra.swebench import DATASET_MAPPING, get_sb_environment
        from minisweagent.run.utils.save import save_traj
    except ModuleNotFoundError as e:
        print(f"ERROR: {e}.\nRun this with the Pro venv interpreter:\n"
              f"  {EXPERIMENT_DIR}/.venv_pro/bin/python {Path(__file__).name} ...",
              file=sys.stderr)
        return 2

    config_path = Path(args.config) if args.config else \
        EXPERIMENT_DIR / "config" / f"{args.arm}.yaml"
    if not config_path.exists():
        print(f"ERROR: config not found: {config_path}", file=sys.stderr)
        return 1

    # Load the instance (mirrors the scaffold's own subset->path resolution).
    dataset_path = DATASET_MAPPING.get(args.subset, args.subset)
    print(f"Loading {dataset_path} split={args.split} ...", flush=True)
    instances = {inst["instance_id"]: inst
                 for inst in load_dataset(dataset_path, split=args.split)}
    if args.instance not in instances:
        print(f"ERROR: instance {args.instance!r} not in dataset", file=sys.stderr)
        return 1
    instance = instances[args.instance]

    config = yaml.safe_load(config_path.read_text())
    # Non-interactive: don't prompt before exiting on submit (yolo mode runs the
    # agent unattended but still streams every step to the console).
    config.setdefault("agent", {})["confirm_exit"] = False

    out_dir = Path(args.output) / args.arm / args.instance
    out_dir.mkdir(parents=True, exist_ok=True)
    stem = args.instance + (f".{args.run_id}" if args.run_id else "")
    traj_path = out_dir / f"{stem}.traj.json"
    pred_path = out_dir / f"{stem}.pred"

    env = get_sb_environment(config, instance)
    agent = InteractiveAgent(
        get_model(args.model, config.get("model", {})),
        env,
        **({"mode": "yolo"} | config.get("agent", {})),
    )

    exit_status, result, patch, extra_info = None, None, "", None
    try:
        # The scaffold's agent.run() returns a 3-tuple; the upstream single-CLI
        # bug is unpacking only 2. We unpack all three so the patch survives.
        exit_status, result, patch = agent.run(instance["problem_statement"])
    except Exception as e:  # noqa: BLE001 -- persist whatever we have on failure
        exit_status, result = type(e).__name__, str(e)
        extra_info = {"traceback": traceback.format_exc()}
        print(f"\nERROR during run: {e}", file=sys.stderr)
    finally:
        save_traj(agent, traj_path, exit_status=exit_status, result=patch or result,
                  extra_info=extra_info)
        # .pred in the format gather_patches.py expects (JSON w/ model_patch).
        pred_path.write_text(json.dumps({
            "instance_id": args.instance,
            "model_patch": patch or "",
            "model_name_or_path": args.model,
        }) + "\n")

    n = len(patch or "")
    print(f"\nexit_status={exit_status}  patch_chars={n}")
    print(f"trajectory: {traj_path}")
    print(f"pred:       {pred_path}")
    return 0 if patch else 1


if __name__ == "__main__":
    raise SystemExit(main())
