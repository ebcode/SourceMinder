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

# Treatment qi index DB: the original is bind-mounted read-only as a seed, then
# copied into the container's /dev/shm ramdisk so qi serves queries from RAM and
# has a writable home for the WAL -wal/-shm sidecars (see prepare_treatment_mounts
# and seed_ramdisk_db).
DB_SEED_PATH = "/code-index.db.seed"
RAMDISK_DB_PATH = "/dev/shm/code-index.db"

# Directory inside the agent container where indexer configs are mounted.
# Daemon runs from here so file paths are stored as absolute /app/... (outside
# this dir), matching the seed DB which was indexed from outside /app.
INDEXER_CWD = "/sm-config"
DAEMON_PID_FILE = "/dev/shm/sm-indexer.pid"
DAEMON_LOG_FILE = "/dev/shm/sm-indexer.log"

# Maps pool_pro.csv repo_language to the indexer binary basename (without -static).
# js uses the TypeScript indexer (covers .js/.jsx/.mjs).
LANG_TO_INDEXER = {
    "python": "index-python",
    "go":     "index-go",
    "ts":     "index-ts",
    "js":     "index-ts",
}

# Maps indexer binary basename to its config subdirectory name under the repo root.
INDEXER_CONFIG_SUBDIR = {
    "index-python": "python",
    "index-go":     "go",
    "index-ts":     "typescript",
}


def get_repo_language(instance_id: str, experiment_dir: Path) -> str | None:
    """Return repo_language for instance_id from pool_pro.csv, or None."""
    pool = experiment_dir / "data" / "pool_pro.csv"
    if not pool.exists():
        return None
    for line in pool.read_text().splitlines()[1:]:  # skip header
        parts = line.split(",")
        if parts and parts[0] == instance_id:
            return parts[2] if len(parts) > 2 else None
    return None


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


def prepare_treatment_mounts(config, arm, instance_id, experiment_dir):
    """If this is a treatment (non-control) arm, verify qi prerequisites,
    prepare a DB copy, and inject Docker volume mounts for qi-static,
    code-index.db, .smconfig, the language indexer binary, and indexer configs
    into config['environment']['run_args'].

    Returns (cleanup_callable, indexer_name) where indexer_name is the binary
    basename (e.g. 'index-python') for the daemon launch, or None for control.
    """
    if arm == "swebp_control":
        return lambda: None, None

    repo_root = experiment_dir.parent

    # --- Prerequisites ---
    qi_static = repo_root / "build" / "qi-static"
    if not qi_static.is_file():
        print(f"ERROR: qi-static not found: {qi_static}", file=sys.stderr)
        sys.exit(2)

    db_path = experiment_dir / "dbs" / f"{instance_id}.db"
    if not db_path.exists():
        print(f"ERROR: DB not found: {db_path}\n"
              f"  Index it first: bash experiment/index_instance_pro.sh {instance_id}",
              file=sys.stderr)
        sys.exit(2)

    # --- Language indexer for watch daemon ---
    lang = get_repo_language(instance_id, experiment_dir)
    indexer_name = LANG_TO_INDEXER.get(lang) if lang else None
    if indexer_name:
        indexer_static = repo_root / "build" / f"{indexer_name}-static"
        if not indexer_static.is_file():
            print(f"WARNING: indexer not found: {indexer_static} -- watch daemon disabled",
                  file=sys.stderr)
            indexer_name = None
    else:
        print(f"WARNING: unknown repo_language {lang!r} for {instance_id} "
              f"-- watch daemon disabled", file=sys.stderr)

    sys.path.insert(0, str(experiment_dir))
    from lib.dbcheck import integrity_ok
    ok, detail = integrity_ok(db_path, timeout=30, retries=2)
    if not ok:
        print(f"ERROR: DB integrity check failed for {db_path}: {detail}", file=sys.stderr)
        sys.exit(3)
    print(f"DB integrity: {detail}  ({db_path.name})", flush=True)

    smconfig = experiment_dir / "config" / f"{arm}.smconfig"
    if not smconfig.exists():
        print(f"WARNING: no .smconfig found for arm '{arm}' at {smconfig}"
              f" -- qi will use built-in defaults", file=sys.stderr)
        smconfig = None

    # --- Inject mounts ---
    # The original DB is mounted read-only as a seed and copied into the
    # container's /dev/shm ramdisk at startup (seed_ramdisk_db). No host-side temp
    # copy is needed: the read-only seed is shareable across concurrent reps, and
    # each container's own ramdisk copy provides the per-rep writable working DB.
    #
    # Size /dev/shm to hold: 64M base + the DB copy + a WAL allowance. We now write
    # the DB at runtime (reconcile_startup_changes + the watch daemon's per-edit
    # re-indexing), so the -wal sidecar lives in the ramdisk too. The indexer runs
    # journal_mode=WAL with the default 1000-page autocheckpoint and no
    # journal_size_limit, so the WAL is not hard-bounded: short qi reads colliding
    # with checkpoints over a long session can let it grow toward the DB's own size
    # before it truncates. We therefore reserve a WAL allowance equal to the DB
    # size (worst case: a full rewrite before checkpoint), floored at 128M for
    # small DBs. The -shm sidecar is negligible (~tens of KB). --shm-size is a
    # ceiling on a lazy tmpfs, not a reservation, so the headroom only costs RAM if
    # the WAL actually grows -- overflow on a ramdisk is a hard failure ("database
    # disk image is malformed" / "disk I/O error"), so we err generous.
    db_bytes = db_path.stat().st_size
    db_mb = (db_bytes + 1048575) // 1048576
    wal_mb = max(128, db_mb)
    shm_mb = 64 + db_mb + wal_mb

    run_args = config.setdefault("environment", {}).setdefault("run_args", [])
    run_args.extend(["-v", f"{qi_static}:/usr/local/bin/qi:ro"])
    run_args.extend(["--shm-size", f"{shm_mb}m"])
    # Seed mount lives outside the git repo so `git diff` in /app never sees it.
    run_args.extend(["-v", f"{db_path}:{DB_SEED_PATH}:ro"])

    if smconfig:
        run_args.extend(["-e", "HOME=/root"])
        run_args.extend(["-v", f"{smconfig}:/root/.smconfig:ro"])

    # Indexer binary + language/shared configs for the watch daemon.
    # Configs mount at INDEXER_CWD so the daemon finds them relative to its cwd,
    # matching the path layout used at pre-index time.
    if indexer_name:
        config_subdir = INDEXER_CONFIG_SUBDIR[indexer_name]
        lang_config = repo_root / config_subdir / "config"
        shared_config = repo_root / "shared" / "config"
        run_args.extend(["-v", f"{indexer_static}:/usr/local/bin/{indexer_name}:ro"])
        run_args.extend(["-v", f"{lang_config}:{INDEXER_CWD}/{config_subdir}/config:ro"])
        run_args.extend(["-v", f"{shared_config}:{INDEXER_CWD}/shared/config:ro"])

    daemon_info = f" + indexer daemon ({indexer_name})" if indexer_name else " (no indexer daemon)"
    print(f"Treatment mounts: qi + DB seed ({(db_bytes + 1023) // 1024}K, "
          f"ramdisk /dev/shm={shm_mb}M) + "
          f"{'smconfig' if smconfig else '(no smconfig)'}{daemon_info}", flush=True)
    return lambda: None, indexer_name


def seed_ramdisk_db(env) -> None:
    """Copy the read-only seed DB into the container's /dev/shm ramdisk.

    Run once after the container starts and before the agent, so every qi query
    is served from RAM. The copy also gives WAL-mode SQLite a writable directory
    for its -wal/-shm sidecars (a read-only mount would fail to open). /dev/shm
    is mounted at container creation (sized via --shm-size in prepare_treatment_mounts),
    so it is available immediately. Fails loudly: a missing ramdisk DB would make
    every qi call error.
    """
    res = env.execute(f"cp {DB_SEED_PATH} {RAMDISK_DB_PATH}")
    if res.get("returncode", 0) != 0:
        raise RuntimeError(
            f"failed to seed ramdisk DB ({DB_SEED_PATH} -> {RAMDISK_DB_PATH}): "
            f"{res.get('output', '')}")


def reconcile_startup_changes(env, indexer_name: str) -> None:
    """Index files mutated by container startup into the ramdisk DB.

    The seed DB was built at the base commit, but the startup env_startup_command
    runs before_repo_set_cmd, whose `git checkout <fix> -- <test files>` adds and
    modifies the test_patch files. The watch daemon launched next runs with
    --watch-only (skips initial indexing), and git's atomic writes do not reliably
    fire the inotify events the daemon re-indexes on -- so without this pass qi
    would miss every startup-added test file (confirmed empirically on the ansible
    instance: the new test symbols returned 0 matches until reconciled).

    Scope is `git status --porcelain` -- exactly the files startup touched. By
    construction that is the test_patch set; the gold solution is never written to
    the working tree (its files stay at base content on disk), so this never
    indexes solution code. Paths are absolutized to /app/... so DB rows match the
    seed's path format. Best-effort: a failure degrades qi coverage for the
    startup files but must not abort the run.
    """
    cmd = (
        "cd /app && CHANGED=$(git status --porcelain | awk '{print $2}' | "
        "sort -u | sed 's#^#/app/#'); "
        f"[ -z \"$CHANGED\" ] || ( cd {INDEXER_CWD} && {indexer_name} $CHANGED "
        f"--db-file {RAMDISK_DB_PATH} --silent )"
    )
    res = env.execute(cmd)
    if res.get("returncode", 0) != 0:
        print(f"WARNING: startup reconcile index failed (qi may miss startup-added "
              f"files): {res.get('output', '')}", file=sys.stderr)
    else:
        print("Reconciled startup-added files into ramdisk DB", flush=True)


def start_indexer_daemon(env, indexer_name: str):
    """Launch the language indexer in --watch-only mode as a background daemon.

    The daemon watches /app for file changes and re-indexes on edits, keeping
    qi queries current after the agent modifies source files.  Runs from
    INDEXER_CWD (outside /app) so file paths in the DB stay as absolute
    /app/... entries -- matching the seed DB path format.

    Returns a stop callable that kills the daemon cleanly.
    """
    # Subshell: cd into INDEXER_CWD (outside /app for path consistency), then
    # exec the indexer so the subshell is replaced directly -- $! becomes the
    # indexer PID, and kill $pid cleanly terminates the indexer (not a bash wrapper).
    cmd = (
        f"( cd {INDEXER_CWD} && exec {indexer_name} /app --watch-only --silent "
        f"--db-file {RAMDISK_DB_PATH} ) > {DAEMON_LOG_FILE} 2>&1 "
        f"& echo $! > {DAEMON_PID_FILE}"
    )
    res = env.execute(cmd)
    if res.get("returncode", 0) != 0:
        print(f"WARNING: indexer daemon launch failed: {res.get('output', '')}",
              file=sys.stderr)
        return lambda: None

    print(f"Indexer daemon started ({indexer_name} --watch-only, "
          f"pid in {DAEMON_PID_FILE})", flush=True)

    def stop():
        env.execute(f"kill $(cat {DAEMON_PID_FILE} 2>/dev/null) 2>/dev/null || true")

    return stop


def recover_empty_patch(env, agent) -> str:
    """Re-collect a patch that came back empty due to a broken /dev/null.

    An empty patch can be a false negative: if an agent command unlinks the
    /dev/null device node (e.g. `go tool compile -o /dev/null` cleaning up its
    output file after a *failed* compile), the agent's collection step
    (`git add -A && git diff --cached`) fails with exit 128 and returns "" even
    though the edits are present in the repo. Recreate /dev/null and re-collect.
    A genuinely empty patch stays empty, so this is safe to always attempt.
    """
    try:
        env.execute("[ -c /dev/null ] || { rm -f /dev/null 2>/dev/null; "
                    "mknod -m 666 /dev/null c 1 3; }")
        return agent.collect_patch() or ""
    except Exception as e:  # noqa: BLE001 -- best-effort salvage; never raise
        print(f"\nWARNING: empty-patch recovery failed: {e}", file=sys.stderr)
        return ""


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

    treatment_cleanup, indexer_name = prepare_treatment_mounts(
        config, args.arm, args.instance, EXPERIMENT_DIR)

    out_dir = Path(args.output) / args.arm / args.instance
    out_dir.mkdir(parents=True, exist_ok=True)
    stem = args.instance + (f".{args.run_id}" if args.run_id else "")
    traj_path = out_dir / f"{stem}.traj.json"
    pred_path = out_dir / f"{stem}.pred"

    env = get_sb_environment(config, instance)
    daemon_stop = lambda: None  # noqa: E731
    if args.arm != "swebp_control":
        seed_ramdisk_db(env)
        if indexer_name:
            # Order matters: reconcile the startup-added files (before_repo_set_cmd's
            # test checkout) into the freshly-seeded ramdisk DB BEFORE the watch-only
            # daemon starts. --watch-only skips initial indexing and won't catch the
            # already-on-disk files, so the reconcile is what makes qi see them.
            reconcile_startup_changes(env, indexer_name)
            daemon_stop = start_indexer_daemon(env, indexer_name)
    agent = InteractiveAgent(
        get_model(args.model, config.get("model", {})),
        env,
        **({"mode": "yolo"} | config.get("agent", {})),
    )

    exit_status, result, patch, extra_info = None, None, "", None
    patch_recovered = False
    try:
        # The scaffold's agent.run() returns a 3-tuple; the upstream single-CLI
        # bug is unpacking only 2. We unpack all three so the patch survives.
        exit_status, result, patch = agent.run(instance["problem_statement"])
        # An empty patch may be a false negative from a broken /dev/null device
        # node (see recover_empty_patch); repair it and re-collect once.
        if not patch:
            patch = recover_empty_patch(env, agent)
            patch_recovered = bool(patch)
            if patch_recovered:
                print(f"\nRecovered empty patch ({len(patch)} chars) after "
                      "repairing /dev/null", file=sys.stderr)
    except Exception as e:  # noqa: BLE001 -- persist whatever we have on failure
        exit_status, result = type(e).__name__, str(e)
        extra_info = {"traceback": traceback.format_exc()}
        print(f"\nERROR during run: {e}", file=sys.stderr)
    finally:
        daemon_stop()
        if patch_recovered:
            extra_info = {**(extra_info or {}), "patch_recovered": True}
        save_traj(agent, traj_path, exit_status=exit_status, result=patch or result,
                  extra_info=extra_info)
        # .pred in the format gather_patches.py expects (JSON w/ model_patch).
        pred_path.write_text(json.dumps({
            "instance_id": args.instance,
            "model_patch": patch or "",
            "model_name_or_path": args.model,
            "patch_recovered": patch_recovered,
        }) + "\n")
        treatment_cleanup()

    n = len(patch or "")
    print(f"\nexit_status={exit_status}  patch_chars={n}")
    print(f"trajectory: {traj_path}")
    print(f"pred:       {pred_path}")
    return 0 if patch else 1


if __name__ == "__main__":
    raise SystemExit(main())
