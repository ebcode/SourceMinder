#!/usr/bin/env python3
"""Evaluate SWE-bench **Pro** patches in parallel -> eval_results.csv.

Pro analog of evaluate_patches.py. Instead of the Princeton swebench harness it
drives the vendored Scale evaluator's ``eval_with_docker`` as a LIBRARY (the
vendor tree stays read-only; no ``cd vendor/... && ... ../../`` dance). It walks
the Pro logs for each run's ``.pred``, runs every (arm, rep) patch through the
Pro Docker eval, and writes one row per run keyed (model, arm, instance_id, rep)
so merge_results.py can join it onto runs.csv.

Two hazards in the upstream evaluator are handled here by giving every run its
own ``output_dir``:
  * its eval_results.json is keyed by instance_id alone, so N reps of one
    instance would collapse to a single result; and
  * ``prepare_run`` puts each run's workspace in a SHARED ``<out>/<uid>/workspace``,
    so parallel reps of the same instance would clobber each other.
Per-run isolated output dirs (``<dir>/eval/<arm>/<run_id>/``) fix both.

Outputs two CSVs:
  * eval_results.csv -- one row per run. Beyond the binary ``resolved`` it now
    carries the granular signal the verdict hides: ``failure_mode``
    (resolved / bug_not_fixed / regression / both); ``pass_rate`` with
    ``required_passed/required_total`` -- the fraction of all required tests
    (FAIL_TO_PASS + PASS_TO_PASS pooled) that passed, so "1 test away" (~0.98) is
    distinguishable from "20 away" (~0.60); and the per-category counts
    ``f2p_total/f2p_passed/p2p_total/p2p_passed`` (FAIL_TO_PASS = did the fix
    land; PASS_TO_PASS = did it break anything); and ``reason`` -- a
    human-readable explanation for the non-scored outcomes
    (inconclusive/error/empty_patch/missing_instance), e.g. the concrete compile
    error behind an ``inconclusive`` verdict, so a patch-caused build failure
    (re-eval is futile) is distinguishable from a flaky harness abort (re-eval
    may help) without reopening stdout.log.
  * eval_test_failures.csv -- long format, one row per required test that did NOT
    pass: (model, arm, instance_id, rep, kind, test_name, status). Lets you see
    exactly which tests failed without re-reading the per-rep output.json.

``resolved`` is unchanged: (FAIL_TO_PASS u PASS_TO_PASS) all PASSED. One exception:
if the vendored harness aborted the whole suite before the required tests ran --
signaled by a synthetic ``"Build/Runtime Error: ..."`` ERROR entry from
``parser.py``, or by every required test coming back with no reported status at
all -- the run is scored ``outcome="inconclusive"`` / ``failure_mode="inconclusive"``
instead of ``unresolved``. An inconclusive verdict can be EITHER a docker-eval
artifact (a flaky fixture or host contention aborting the harness -- not evidence
the patch failed, re-run it) OR a genuine agent failure whose patch doesn't
compile/import, so the suite aborted before any test ran (re-eval is futile; the
patch on disk is broken). The ``reason`` column tells the two apart -- read it
before deciding whether to re-run.

Usage:
  experiment/.venv_pro/bin/python experiment/analysis/evaluate_pro_patches.py \
      --dir experiment/results/pro_runs/<batch> --workers 5 [--run-prefix oldprompt_]

  # Target exactly one run (add --redo to force a re-eval of it):
  experiment/.venv_pro/bin/python experiment/analysis/evaluate_pro_patches.py \
      --dir experiment/results/pro_runs/<batch> --arm swebp_treatment --rep rep01 --redo
"""
from __future__ import annotations

import argparse
import concurrent.futures as cf
import csv
import importlib.util
import json
import re
import sys
import threading
from pathlib import Path

EXPERIMENT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(EXPERIMENT_DIR))
from lib import paths  # noqa: E402

VENDOR_EVAL = EXPERIMENT_DIR / "vendor" / "swebench_pro_os" / "swe_bench_pro_eval.py"
DEFAULT_LOGS = paths.LOGS_DIR / "pro_pilot"
DEFAULT_SUBSET = EXPERIMENT_DIR / "data" / "swebench_pro"
DEFAULT_SCRIPTS = EXPERIMENT_DIR / "vendor" / "swebench_pro_os" / "run_scripts"
DEFAULT_DOCKERHUB = "jefzda"
DEFAULT_ARMS = ["swebp_control", "swebp_treatment"]
RAW_FIELDS = ["instance_id", "repo", "before_repo_set_cmd",
              "selected_test_files_to_run", "base_commit",
              "fail_to_pass", "pass_to_pass"]

_print_lock = threading.Lock()


def load_vendor_eval():
    spec = importlib.util.spec_from_file_location("sbp_eval", VENDOR_EVAL)
    mod = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(VENDOR_EVAL.parent))
    spec.loader.exec_module(mod)
    return mod


def norm_model(s: str) -> str:
    return (s or "").split("/")[-1].strip().lower()


def parse_run_id(path: Path, instance_id: str) -> str:
    stem = path.name[: -len(".pred")] if path.name.endswith(".pred") else path.stem
    if stem == instance_id:
        return "base"
    if stem.startswith(instance_id + "."):
        return stem[len(instance_id) + 1 :]
    return stem


def to_set(v) -> set:
    """fail_to_pass / pass_to_pass come as a list or its string repr."""
    if isinstance(v, (list, tuple, set)):
        return set(v)
    return set(eval(v)) if v else set()


def is_inconclusive(status_by: dict, required: set) -> bool:
    """True if the harness aborted before actually running the required tests,
    rather than running them and finding the fix incomplete.

    Two signals, either one sufficient:
      * a synthetic ``"Build/Runtime Error: ..."`` ERROR entry -- parser.py's own
        convention for "the suite crashed before finishing" (e.g. an unhandled
        exception in an unrelated test fixture aborting the whole run).
      * every required (FAIL_TO_PASS + PASS_TO_PASS) test is missing from
        status_by entirely -- none of them got a real PASSED/FAILED verdict, so
        there is no test signal to score, only a collection failure.
    A run where SOME required tests ran and failed is left as a genuine
    unresolved/bug_not_fixed verdict even if others are missing.
    """
    if any(name.startswith("Build/Runtime Error") for name in status_by):
        return True
    return bool(required) and required.isdisjoint(status_by)


def _clip(s: str, n: int = 200) -> str:
    s = " ".join(s.split())
    return s if len(s) <= n else s[: n - 1] + "…"


# A compiler diagnostic line: "<file.ext>:<line>[:col]: <message>" where the
# message is one of the phrasings a build/collection failure prints. Matches Go
# (undefined:, unknown field), C/C++, and generic "syntax error"/"expected".
_COMPILE_DIAG = re.compile(
    r"[\w./-]+\.\w+:\d+(?::\d+)?:\s+"
    r"(?:undefined:|unknown field|cannot use|not enough|too many|"
    r"syntax error|expected |declared (?:and|but) not used|"
    r"missing return|redeclared|imported and not used|.*\bundeclared\b)",
    re.IGNORECASE,
)
_PY_IMPORT_ERR = re.compile(
    r"\b(ModuleNotFoundError|ImportError|SyntaxError|NameError|IndentationError|"
    r"TabError|AttributeError):.*")


def diagnose_inconclusive(status_by: dict, required: set,
                          stdout: str = "", stderr: str = "") -> str:
    """Explain WHY a run produced no required-test statuses -- the human-readable
    string for the eval_results.csv ``reason`` column.

    ``outcome="inconclusive"`` only means "none of the required tests reported a
    verdict"; it does NOT say why. The two causes need opposite responses:
      * the SUBMITTED PATCH doesn't compile / import, so the suite aborted before
        any test ran -- this is a genuine agent failure and re-eval is pointless
        (the patch on disk is broken); or
      * an unrelated harness/infra flake aborted the run -- re-eval may help.
    This scans the raw logs for the concrete signature (compile error, build
    failed, collection error, panic) so the reader can tell them apart at a
    glance instead of re-opening stdout.log by hand.
    """
    for name in status_by:
        if name.startswith("Build/Runtime Error"):
            return _clip(name)

    lines = (stdout + "\n" + stderr).splitlines()

    # Go's build cache suppresses the per-file compiler diagnostic on re-runs,
    # leaving only the "FAIL <pkg> [build failed]" summary -- so compile_diag is a
    # best-effort detail (present on a fresh build) and the deduped build_failed
    # package set is the robust signal. Logs are outer+workspace concatenations,
    # hence the dedupe.
    compile_diag = next((l.strip() for l in lines if _COMPILE_DIAG.search(l)), None)
    build_failed = {l.strip() for l in lines if "[build failed]" in l}
    if compile_diag or build_failed:
        detail = compile_diag or (sorted(build_failed)[0] if build_failed else "")
        extra = (f" (+{len(build_failed) - 1} more pkgs)"
                 if compile_diag is None and len(build_failed) > 1 else
                 f" ({len(build_failed)} pkgs build failed)" if build_failed else "")
        return _clip(f"build failed -- patch does not compile: {detail}{extra}")

    py_err = next((_PY_IMPORT_ERR.search(l) for l in lines
                   if _PY_IMPORT_ERR.search(l)), None)
    if py_err:
        return _clip(f"collection error -- patch broke import: {py_err.group(0).strip()}")
    if re.search(r"error[s]? during collection", stdout + stderr, re.IGNORECASE):
        return "pytest collection error -- suite aborted before required tests ran"

    panic = next((l.strip() for l in lines
                  if l.startswith(("panic:", "fatal error:"))), None)
    if panic:
        return _clip(f"runtime abort: {panic}")

    if not status_by:
        return "empty test report -- harness produced no test results (infra/flake, re-eval may help)"
    if required and required.isdisjoint(status_by):
        return (f"none of the {len(required)} required tests ran; "
                f"{len(status_by)} unrelated status(es) reported (parser misfire or flake)")
    return "harness aborted before required tests completed -- cause not found in logs"


def classify_failure(f2p_total: int, f2p_passed: int,
                     p2p_total: int, p2p_passed: int) -> str:
    """Refine the binary verdict into a failure mode (what 'unresolved' hides):
      resolved      -- every FAIL_TO_PASS and PASS_TO_PASS test passes
      bug_not_fixed -- a FAIL_TO_PASS test still fails (the fix didn't land)
      regression    -- the bug is fixed but a PASS_TO_PASS test broke
      both          -- failures on both sides
    """
    f2p_ok = f2p_passed == f2p_total
    p2p_ok = p2p_passed == p2p_total
    if f2p_ok and p2p_ok:
        return "resolved"
    if not f2p_ok and not p2p_ok:
        return "both"
    return "bug_not_fixed" if not f2p_ok else "regression"


def result_row(model, arm, iid, rid, outcome, resolved, *, failure_mode=None,
               f2p_total="", f2p_passed="", p2p_total="", p2p_passed="",
               failures=None, suspect_test_only_patch=False,
               reparsed_stdout=False, reason="") -> dict:
    """Build one summary row. ``failures`` (per-test long-format rows) is stashed
    under ``_failures`` and split out before the summary CSV is written. For the
    non-test outcomes (error/empty/missing) failure_mode defaults to the outcome.

    required_total/required_passed pool FAIL_TO_PASS + PASS_TO_PASS, and pass_rate
    is the fraction passing across that pool -- partial credit that distinguishes
    "1 test away" (e.g. 0.98) from "20 away" (e.g. 0.60). pass_rate is "" when
    there are no required tests or for the non-test outcomes.

    suspect_test_only_patch flags a submitted patch whose files are entirely a
    subset of test_patch's files (set by run_pro_one.py) -- the agent was told
    not to edit tests, so a real fix can't look like this. Seen once so far: a
    hallucinated multi-turn session (see lib/guarded_agent.py) that terminated
    after step 1 and collected the container startup's test checkout as if it
    were the agent's own patch. A "resolved" verdict on such a row is almost
    certainly a docker-eval artifact, not a real fix -- treat it as suspect
    regardless of outcome.

    reparsed_stdout marks a verdict that came from lib/pro_test_parser.py's
    raw-log reparse rather than the vendored parser -- i.e. the vendored
    parser misfired (see is_inconclusive) but our own reparse of the raw
    stdout/stderr found the suite genuinely completed and could tell the
    required tests' real pass/fail apart from an unrelated flaky test.

    reason is a human-readable explanation for the non-scored outcomes
    (inconclusive/error/empty_patch/missing_instance) -- e.g. the concrete
    compile error behind an inconclusive verdict (see diagnose_inconclusive), or
    "submitted patch is empty". Blank for a normally-scored resolved/unresolved
    run. Lets the reader tell a patch-caused build failure (re-eval is futile)
    from a flaky harness abort (re-eval may help) without reopening stdout.log."""
    req_total = (f2p_total + p2p_total) if isinstance(f2p_total, int) else ""
    req_passed = (f2p_passed + p2p_passed) if isinstance(f2p_passed, int) else ""
    pass_rate = round(req_passed / req_total, 4) if isinstance(req_total, int) and req_total else ""
    return dict(
        model=model, arm=arm, instance_id=iid, rep=rid,
        outcome=outcome, resolved=resolved,
        failure_mode=failure_mode if failure_mode is not None else outcome,
        f2p_total=f2p_total, f2p_passed=f2p_passed,
        p2p_total=p2p_total, p2p_passed=p2p_passed,
        required_total=req_total, required_passed=req_passed, pass_rate=pass_rate,
        suspect_test_only_patch=int(suspect_test_only_patch),
        reparsed_stdout=int(reparsed_stdout),
        reason=reason,
        _failures=failures or [],
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--logs", type=Path, default=DEFAULT_LOGS, help=f"(default: {DEFAULT_LOGS})")
    ap.add_argument("--dir", type=Path, required=True,
                    help="Output dir for eval_results.csv (e.g. results/pro_runs/<batch>)")
    ap.add_argument("--arms", nargs="+", default=DEFAULT_ARMS)
    ap.add_argument("--run-prefix", default="", help="Only eval runs whose run_id starts with this")
    ap.add_argument("--exclude", nargs="+", default=[],
                    help="Skip these runs, named as <arm>_<run_id> (i.e. the per-run "
                         "log basename without .log), e.g. swebp_control_rep05")
    ap.add_argument("--workers", type=int, default=5, help="Parallel eval workers (default: 5)")
    ap.add_argument("--subset", type=Path, default=DEFAULT_SUBSET)
    ap.add_argument("--scripts-dir", type=Path, default=DEFAULT_SCRIPTS)
    ap.add_argument("--dockerhub-username", default=DEFAULT_DOCKERHUB)
    ap.add_argument("--redo", action="store_true", help="Re-run even if a prior output exists")
    ap.add_argument("--arm", default=None,
                    help="Restrict to exactly this one arm (e.g. swebp_treatment); "
                         "pair with --rep to target a single run")
    ap.add_argument("--rep", default=None,
                    help="Restrict to exactly this one rep/run_id (e.g. rep01), "
                         "exact match (not a prefix); pair with --arm to target a "
                         "single run. Combine with --redo to force a re-run of "
                         "that run; without --redo it's skipped if a prior "
                         "output already exists, same as any other run.")
    ap.add_argument("--block-network", action="store_true")
    args = ap.parse_args()

    if args.arm:
        args.arms = [args.arm]

    # The vendored evaluator reads dockerfiles via paths RELATIVE to its own dir
    # (swe_bench_pro_eval.py:58/62 -> "dockerfiles/base_dockerfile/<iid>/Dockerfile"),
    # so it only works with CWD == the vendor dir. Resolve all our paths to
    # absolute first, then chdir there -- the proper form of the old
    # `cd vendor/swebench_pro_os && ... ../../` dance.
    import os
    args.logs = args.logs.resolve()
    args.dir = args.dir.resolve()
    args.subset = Path(args.subset).resolve()
    args.scripts_dir = args.scripts_dir.resolve()
    os.chdir(VENDOR_EVAL.parent)

    sbp = load_vendor_eval()
    import pandas as pd
    from lib.pro_dataset import load_pro_dataset
    from lib.pro_test_parser import reparse_required_tests

    # --- gather predictions ---
    preds = []  # (arm, instance_id, run_id, patch, model, suspect_test_only_patch)
    for arm in args.arms:
        for p in sorted((args.logs / arm).rglob("*.pred")):
            rec = json.loads(p.read_text())
            iid = rec["instance_id"]
            rid = parse_run_id(p, iid)
            if not rid.startswith(args.run_prefix):
                continue
            if args.rep is not None and rid != args.rep:
                continue
            if f"{arm}_{rid}" in args.exclude:
                print(f"  excluding {arm}_{rid}")
                continue
            preds.append((arm, iid, rid,
                          rec.get("model_patch") or rec.get("patch") or "",
                          norm_model(rec.get("model_name_or_path", "")),
                          bool(rec.get("suspect_test_only_patch", False))))
    if not preds:
        if args.arm or args.rep:
            print(f"ERROR: no predictions found for arm={args.arm!r} rep={args.rep!r} "
                  f"under {args.logs}", file=sys.stderr)
        else:
            print("ERROR: no predictions found", file=sys.stderr)
        return 1
    print(f"{len(preds)} prediction(s) to evaluate across {args.workers} worker(s)")

    # --- raw sample rows (one Series per instance) ---
    ds = load_pro_dataset(str(args.subset), split="test")
    by_id = {r["instance_id"]: r for r in ds}

    eval_root = args.dir / "eval"

    def run_task(task):
        arm, iid, rid, patch, model, suspect = task
        row = by_id.get(iid)
        if row is None:
            return result_row(model, arm, iid, rid, "missing_instance", 0,
                              suspect_test_only_patch=suspect,
                              reason=f"instance_id {iid!r} not found in subset dataset")
        if not patch.strip():
            return result_row(model, arm, iid, rid, "empty_patch", 0,
                              suspect_test_only_patch=suspect,
                              reason="submitted patch is empty (agent produced no diff)")
        sample = pd.Series({k: row[k] for k in RAW_FIELDS})
        out_dir = eval_root / arm / rid
        out_dir.mkdir(parents=True, exist_ok=True)
        try:
            output = sbp.eval_with_docker(
                patch, sample, str(out_dir), args.dockerhub_username,
                str(args.scripts_dir), prefix=f"{arm}_{rid}",
                redo=args.redo, block_network=args.block_network)
            if not output:
                return result_row(model, arm, iid, rid, "error", 0,
                                  suspect_test_only_patch=suspect,
                                  reason="docker eval returned no output (harness/infra error)")
            status_by = {t["name"]: t["status"] for t in output["tests"]}
            passed = {n for n, s in status_by.items() if s == "PASSED"}
            f2p = to_set(row["fail_to_pass"])
            p2p = to_set(row["pass_to_pass"])
            required = f2p | p2p

            stdout_path = out_dir / iid / f"{arm}_{rid}_stdout.log"
            stderr_path = out_dir / iid / f"{arm}_{rid}_stderr.log"

            def _read_logs():
                # The outer *_stdout.log carries only the FAIL/[build failed]
                # summary; the concrete per-file compiler diagnostics (undefined:,
                # unknown field, ...) live in the inner workspace/stdout.log. Read
                # both so diagnose_inconclusive can surface the exact error line.
                ws = out_dir / iid / "workspace"
                paths = [stdout_path, stderr_path, ws / "stdout.log", ws / "stderr.log"]
                texts = [p.read_text() if p.exists() else "" for p in paths]
                so = "\n".join(t for t in (texts[0], texts[2]) if t)
                se = "\n".join(t for t in (texts[1], texts[3]) if t)
                return so, se

            reparsed = False
            if is_inconclusive(status_by, required):
                # The vendored parser only recognizes "All N assertions passed"
                # (the zero-failure phrasing); the instant one assertion fails
                # anywhere in an 8000+-assertion suite it prints "N out of M
                # assertions failed" instead, which the vendored parser doesn't
                # match at all -- it then blames the whole suite on the first
                # unrelated "Error:" line in stderr. Reparse the raw logs
                # ourselves to tell "an unrelated test flaked" apart from "the
                # suite actually crashed" (lib/pro_test_parser.py).
                if stdout_path.exists() and stderr_path.exists():
                    reparsed_status = reparse_required_tests(
                        stdout_path.read_text(), stderr_path.read_text(), required)
                    if reparsed_status is not None:
                        status_by = reparsed_status
                        passed = {n for n, s in status_by.items() if s == "PASSED"}
                        reparsed = True
            f2p_passed = len(f2p & passed)
            p2p_passed = len(p2p & passed)
            ok = required <= passed   # resolved verdict, unchanged
            # one long-format row per required test that did NOT pass (a true
            # FAILED, or MISSING if it never ran -- e.g. a collection error).
            failures = [
                dict(model=model, arm=arm, instance_id=iid, rep=rid, kind=kind,
                     test_name=t, status=status_by.get(t, "MISSING"))
                for kind, names in (("fail_to_pass", f2p), ("pass_to_pass", p2p))
                for t in sorted(names - passed)
            ]
            reason = ""
            if not ok and not reparsed and is_inconclusive(status_by, required):
                outcome, failure_mode = "inconclusive", "inconclusive"
                so, se = _read_logs()
                reason = diagnose_inconclusive(status_by, required, so, se)
            else:
                outcome = "resolved" if ok else "unresolved"
                failure_mode = classify_failure(len(f2p), f2p_passed,
                                                len(p2p), p2p_passed)
            return result_row(
                model, arm, iid, rid, outcome, int(ok),
                failure_mode=failure_mode,
                f2p_total=len(f2p), f2p_passed=f2p_passed,
                p2p_total=len(p2p), p2p_passed=p2p_passed,
                failures=failures, suspect_test_only_patch=suspect,
                reparsed_stdout=reparsed, reason=reason)
        except Exception as exc:  # noqa: BLE001 -- record, never crash the batch
            with _print_lock:
                print(f"  [{arm} {rid}] exception: {exc}", file=sys.stderr)
            return result_row(model, arm, iid, rid, "error", 0,
                              suspect_test_only_patch=suspect,
                              reason=_clip(f"exception during eval: {exc}"))

    results = []
    done = 0
    with cf.ThreadPoolExecutor(max_workers=args.workers) as pool:
        futs = {pool.submit(run_task, t): t for t in preds}
        for fut in cf.as_completed(futs):
            r = fut.result()
            results.append(r)
            done += 1
            with _print_lock:
                why = f"  --  {r['reason']}" if r.get("reason") else ""
                print(f"  [{done}/{len(preds)}] {r['arm']} {r['rep']}: "
                      f"{r['outcome']} (resolved={r['resolved']}){why}", flush=True)

    args.dir.mkdir(parents=True, exist_ok=True)

    # Split the per-test failure rows (long format) off the summary rows. Pop so
    # the summary DictWriter sees only its own fields.
    test_failures = []
    for r in results:
        test_failures.extend(r.pop("_failures", []))

    # Read-modify-write merge: refresh only the runs we just evaluated and keep
    # every other row intact, so a targeted re-eval (e.g.
    # --arms swebp_treatment --run-prefix rep01) updates a single run's row(s)
    # without clobbering the rest of the batch. A full-batch run refreshes every
    # key, reproducing the old whole-file behavior. (To start clean, delete the
    # CSVs first.) Runs are keyed by (arm, instance_id, rep).
    touched = {(r["arm"], r["instance_id"], r["rep"]) for r in results}

    out_path = args.dir / "eval_results.csv"
    fields = ["model", "arm", "instance_id", "rep", "outcome", "resolved",
              "failure_mode", "pass_rate", "required_passed", "required_total",
              "f2p_total", "f2p_passed", "p2p_total", "p2p_passed",
              "suspect_test_only_patch", "reparsed_stdout", "reason"]
    merged = list(results)
    if out_path.exists():
        with out_path.open(newline="") as f:
            for row in csv.DictReader(f):
                if (row["arm"], row["instance_id"], row["rep"]) in touched:
                    continue  # superseded by a freshly-evaluated row above
                row = {k: row.get(k, "") for k in fields}
                row["resolved"] = int(row["resolved"] or 0)
                # rows written before these columns existed default to 0
                row["suspect_test_only_patch"] = int(row["suspect_test_only_patch"] or 0)
                row["reparsed_stdout"] = int(row["reparsed_stdout"] or 0)
                try:
                    row["pass_rate"] = float(row["pass_rate"])
                except (ValueError, TypeError):
                    pass  # "" for empty/missing/error rows -- left as-is
                merged.append(row)
    merged.sort(key=lambda r: (r["arm"], r["rep"]))
    with out_path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(merged)

    # Separate long-format CSV: one row per required test that did not pass. Drop
    # every old failure row for the touched runs (a now-resolved run contributes
    # zero), then add the fresh ones.
    fail_path = args.dir / "eval_test_failures.csv"
    tf_fields = ["model", "arm", "instance_id", "rep", "kind", "test_name", "status"]
    merged_tf = list(test_failures)
    if fail_path.exists():
        with fail_path.open(newline="") as f:
            for row in csv.DictReader(f):
                if (row["arm"], row["instance_id"], row["rep"]) in touched:
                    continue
                merged_tf.append({k: row.get(k, "") for k in tf_fields})
    merged_tf.sort(key=lambda r: (r["arm"], r["rep"], r["kind"], r["test_name"]))
    with fail_path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=tf_fields)
        w.writeheader()
        w.writerows(merged_tf)

    from collections import Counter
    n_res = sum(r["resolved"] for r in merged)
    print(f"\nWrote {len(merged)} row(s) -> {out_path}  ({n_res} resolved; "
          f"{len(results)} refreshed this run)")
    print(f"Wrote {len(merged_tf)} failing-test row(s) -> {fail_path}")
    print(f"  failure modes: {dict(Counter(r['failure_mode'] for r in merged))}")
    for arm in sorted({r["arm"] for r in merged}):
        ar = [r for r in merged if r["arm"] == arm]
        if ar:
            res = sum(x["resolved"] for x in ar)
            # partial credit: mean pass_rate across required (F2P+P2P) tests
            rates = [x["pass_rate"] for x in ar if isinstance(x["pass_rate"], float)]
            mr = (sum(rates) / len(rates)) if rates else float("nan")
            print(f"  {arm:18s} resolved {res}/{len(ar)}  "
                  f"mean pass_rate: {mr:.0%} (n={len(rates)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
