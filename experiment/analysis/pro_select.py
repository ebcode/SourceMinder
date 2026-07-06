#!/usr/bin/env python3
"""Canonical SWE-bench Pro instance-selection CLI.

Selecting experiment instances meant writing throwaway pandas scripts against
test.parquet -- and re-hitting the same parsing bug each time, because
fail_to_pass / pass_to_pass are stored as **Python-repr strings, not JSON**
(single-quoted entries; json.loads chokes). This tool centralizes that gotcha in
one parser and emits a filtered, ranked table plus the docker `image_name`s, so
the scan is one command.

Computed selection axes per instance:
  n_files       gold-patch file count (diff --git lines in `patch`)
  patch_lines   +/- body lines in `patch`
  n_f2p         len(fail_to_pass)   -- NOT comparable across languages (see below)
  n_p2p         len(pass_to_pass)
  n_test_files  distinct test files holding the FAILING tests, parsed from
                fail_to_pass: file before ' | ' (js/ts) or before '::' (python).
                Go fail_to_pass carries only func names (no file), so go falls
                back to distinct top-level Test* funcs. Always <= n_f2p; the
                coarsest cross-language-comparable "how much to understand" axis.
  n_p2p_files   same parse applied to pass_to_pass: distinct files holding the
                REGRESSION tests. This is the impact-analysis surface -- what
                qi --usage has to bite on. Added 2026-07-05 after the 8-instance
                retrospective: the three biggest treatment token wins were
                exactly the three instances with p2p breadth > 0; the discovery
                signals (src_frac/disc/n_graded) did not rank them.
  resolve_rate  frontier-model resolve rate joined from
                data/pro_resolve_rates.csv (n_resolved/n_scored across ~9
                frontier models, Scale scoring). NULL when the file or the
                instance is missing. An instance no frontier model resolves
                measures task impossibility, not qi's effect.
  repo_files/   repo-level navigation surface from the local code indexes in
  repo_defs     experiment/dbs (see lib/repo_size.py). NULL when the repo has
                no local DB, never 0.
  runs          how many results/pro_runs/* batches already ran this instance

Two honesty guardrails are built in:
  * n_files / patch_lines are GOLD-PATCH PROXIES that overstate required scope
    (a 4-file instance was solved touching 1 file). The honest axis -- mapping
    each fail_to_pass test to the files it exercises -- is the separate "required
    scope computer" idea, not this tool.
  * n_f2p is NOT comparable across languages (Go counts coarse top-level Test*
    funcs; py/js count per-case). Ranking by it across languages is flagged;
    n_test_files is the cross-language-safe alternative and is the default rank.

--screen applies the four known failure-mode screens in one flag (candidates
for a NEW batch): not yet run, frontier-resolvable, regression breadth > 0,
navigation surface above a floor. Each screen exists because an instance
failing it has been observed to produce an unusable or weak measurement --
see the per-screen notes in apply_screen().

Usage:
  experiment/.venv_pro/bin/python experiment/analysis/pro_select.py \
      --lang go --n-files 4-6 --limit 15
  ... pro_select.py --rank n_f2p --lang python --csv > candidates.csv
  ... pro_select.py --lang go --screen --rank n_p2p_files

Reads test.parquet via pyarrow (the natural parquet reader; .venv_pro has it).
"""
from __future__ import annotations

import argparse
import ast
import csv
import re
import sys
from pathlib import Path

import pyarrow.parquet as pq

ANALYSIS_DIR = Path(__file__).resolve().parent
EXPERIMENT_DIR = ANALYSIS_DIR.parent
DEFAULT_PARQUET = EXPERIMENT_DIR / "data" / "swebench_pro" / "test.parquet"
DEFAULT_PRO_RUNS = EXPERIMENT_DIR / "results" / "pro_runs"
DEFAULT_RESOLVE_RATES = EXPERIMENT_DIR / "data" / "pro_resolve_rates.csv"
DEFAULT_DBS_DIR = EXPERIMENT_DIR / "dbs"

sys.path.insert(0, str(EXPERIMENT_DIR))
from lib.repo_size import build_repo_size_map  # noqa: E402

# Strip a trailing -v<sha> (or -vnan) so run-result, eval-only, and parquet
# instance_ids all normalize to instance_<org>__<repo>-<commit>.
_VSUFFIX = re.compile(r"-v[0-9a-zA-Z]+$")


def norm_iid(iid: str) -> str:
    return _VSUFFIX.sub("", iid or "")


def parse_repr_list(s: str) -> list:
    """The one place the fail_to_pass/pass_to_pass repr-string gotcha lives.

    These columns are Python reprs (mixed ' and " quoting), not JSON. Use
    ast.literal_eval; fall back to [] on anything unparseable so a single bad
    row never aborts a scan."""
    if not s:
        return []
    try:
        val = ast.literal_eval(s)
        return list(val) if isinstance(val, (list, tuple)) else []
    except (ValueError, SyntaxError):
        return []


def test_file_of(entry: str) -> str:
    """Test file holding a failing test, from a fail_to_pass entry.

    Three dataset formats:
      js/ts   '<file> | <desc>::<case>'         -> file before ' | '
      python  '<file>::<Class>::<test>'         -> file before '::'
      go      'TestParseResourcePath'           -> no file; the func name is the
                                                   unit (go carries no path)."""
    if " | " in entry:
        return entry.split(" | ", 1)[0].strip()
    if "::" in entry:
        return entry.split("::", 1)[0].strip()
    return entry.strip()


def count_patch_files(patch: str) -> int:
    return sum(1 for ln in (patch or "").splitlines() if ln.startswith("diff --git "))


def count_patch_lines(patch: str) -> int:
    n = 0
    for ln in (patch or "").splitlines():
        if (ln.startswith("+") and not ln.startswith("+++")) or \
           (ln.startswith("-") and not ln.startswith("---")):
            n += 1
    return n


def load_runs_index(pro_runs: Path) -> dict[str, set[str]]:
    """{normalized_instance_id: {batch_name, ...}} across all pro_runs/* dirs.

    Reads runs_with_success.csv (run-result form) and eval_results.csv (the
    shorter eval-only form); both normalize to the same key via norm_iid."""
    idx: dict[str, set[str]] = {}
    if not pro_runs.is_dir():
        return idx
    for batch in pro_runs.iterdir():
        if not batch.is_dir() or batch.name == "_cross":
            continue
        for fname in ("runs_with_success.csv", "eval_results.csv"):
            path = batch / fname
            if not path.is_file():
                continue
            with path.open(newline="") as fh:
                for row in csv.DictReader(fh):
                    iid = norm_iid(row.get("instance_id", ""))
                    if iid:
                        idx.setdefault(iid, set()).add(batch.name)
    return idx


def load_resolve_rates(path: Path) -> dict[str, dict]:
    """{normalized_instance_id: {'resolve_rate': float, 'n_resolved': int,
    'n_scored': int}} from data/pro_resolve_rates.csv (frontier-model 0/1
    outcomes, Scale scoring). Missing file -> {} (every rate renders NULL)."""
    idx: dict[str, dict] = {}
    if not path.is_file():
        return idx
    with path.open(newline="") as fh:
        for row in csv.DictReader(fh):
            iid = norm_iid(row.get("instance_id", ""))
            if not iid:
                continue
            try:
                idx[iid] = {
                    "resolve_rate": float(row["resolve_rate"]),
                    "n_resolved": int(row["n_resolved"]),
                    "n_scored": int(row["n_scored"]),
                }
            except (KeyError, ValueError):
                continue
    return idx


def parse_range(spec: str):
    """'4-6' -> (4,6); '4' -> (4,4); '4-' -> (4,None); '-6' -> (None,6)."""
    spec = spec.strip()
    if "-" not in spec:
        v = int(spec)
        return (v, v)
    lo, hi = spec.split("-", 1)
    return (int(lo) if lo else None, int(hi) if hi else None)


def in_range(val: int, rng) -> bool:
    lo, hi = rng
    return (lo is None or val >= lo) and (hi is None or val <= hi)


def build_rows(parquet: Path, runs_idx: dict[str, set[str]],
               resolve_idx: dict[str, dict],
               size_map: dict[str, tuple[int, int]] | None = None) -> list[dict]:
    table = pq.read_table(parquet, columns=[
        "instance_id", "repo", "repo_language", "patch",
        "fail_to_pass", "pass_to_pass", "image_name",
    ])
    rows = []
    for r in table.to_pylist():
        f2p = parse_repr_list(r["fail_to_pass"])
        p2p = parse_repr_list(r["pass_to_pass"])
        test_files = {test_file_of(e) for e in f2p if e}
        p2p_files = {test_file_of(e) for e in p2p if e}
        batches = runs_idx.get(norm_iid(r["instance_id"]), set())
        rr = resolve_idx.get(norm_iid(r["instance_id"]))
        size = (size_map or {}).get(r["repo"])
        rows.append({
            "instance_id": r["instance_id"],
            "repo": r["repo"],
            "lang": r["repo_language"],
            "n_files": count_patch_files(r["patch"]),
            "patch_lines": count_patch_lines(r["patch"]),
            "n_f2p": len(f2p),
            "n_p2p": len(p2p),
            "n_test_files": len(test_files),
            "n_p2p_files": len(p2p_files),
            "resolve_rate": rr["resolve_rate"] if rr else None,
            "repo_files": size[0] if size else None,
            "repo_defs": size[1] if size else None,
            "runs": len(batches),
            "_batches": sorted(batches),
            "image_name": r["image_name"],
        })
    return rows


# The four known failure-mode screens for picking a NEW batch instance. Each
# exists because instances failing it produced unusable or weak measurements:
#   already_run    -- re-running measures nothing new (and pre-fix logs got
#                     replaced once already; see the 59%-baseline incident)
#   unresolvable   -- no frontier model solves it: both arms flail, the result
#                     measures task impossibility, not qi (navidrome series)
#   no_p2p         -- zero regression breadth: nothing for qi --usage to trace;
#                     every weak/negative canonical instance had n_p2p_files=0
#   small_surface  -- tiny navigation surface: nothing for qi --toc to save
#                     (vuls at 4.7k defs managed only -6.9%). Floor is
#                     PROVISIONAL, set between vuls (weak) and qutebrowser
#                     (strong); revisit as batches accumulate.
#   no_local_db    -- repo_defs unknown (no local index): surface can't be
#                     screened; index the repo first rather than guessing
def apply_screen(rows: list[dict], min_defs: int) -> tuple[list[dict], dict]:
    kept, excluded = [], {"already_run": 0, "unresolvable": 0, "unknown_resolve": 0,
                          "no_p2p": 0, "small_surface": 0, "no_local_db": 0}
    for r in rows:
        if r["runs"] > 0:
            excluded["already_run"] += 1
        elif r["resolve_rate"] is None:
            excluded["unknown_resolve"] += 1
        elif r["resolve_rate"] <= 0:
            excluded["unresolvable"] += 1
        elif r["n_p2p_files"] == 0:
            excluded["no_p2p"] += 1
        elif r["repo_defs"] is None:
            excluded["no_local_db"] += 1
        elif r["repo_defs"] < min_defs:
            excluded["small_surface"] += 1
        else:
            kept.append(r)
    return kept, excluded


RANK_KEYS = ("n_test_files", "n_files", "n_f2p", "n_p2p", "n_p2p_files",
             "resolve_rate", "repo_defs", "patch_lines", "runs")
# instance_id and image_name are wide; print them last / via --csv only.
TABLE_KEYS = ["repo", "lang", "n_files", "patch_lines", "n_f2p", "n_p2p",
              "n_test_files", "n_p2p_files", "resolve_rate", "repo_defs",
              "runs", "instance_id"]
CSV_KEYS = ["instance_id", "repo", "lang", "n_files", "patch_lines", "n_f2p",
            "n_p2p", "n_test_files", "n_p2p_files", "resolve_rate",
            "repo_files", "repo_defs", "runs", "image_name"]


def render(v) -> str:
    return "NULL" if v is None else str(v)


def print_table(rows: list[dict]) -> None:
    widths = {k: len(k) for k in TABLE_KEYS}
    for row in rows:
        for k in TABLE_KEYS:
            widths[k] = max(widths[k], len(render(row[k])))
    print("  ".join(k.ljust(widths[k]) for k in TABLE_KEYS))
    print("  ".join("-" * widths[k] for k in TABLE_KEYS))
    for row in rows:
        print("  ".join(render(row[k]).ljust(widths[k]) for k in TABLE_KEYS))


def write_csv(rows: list[dict]) -> None:
    w = csv.DictWriter(sys.stdout, fieldnames=CSV_KEYS)
    w.writeheader()
    for row in rows:
        w.writerow({k: ("" if row[k] is None else row[k]) for k in CSV_KEYS})


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--parquet", type=Path, default=DEFAULT_PARQUET)
    ap.add_argument("--pro-runs", type=Path, default=DEFAULT_PRO_RUNS,
                    help="dir of batch results, for the 'runs' already-run flag")
    ap.add_argument("--lang", action="append", default=[],
                    help="filter by repo_language (repeatable): go python js ts")
    ap.add_argument("--repo", help="substring filter on repo")
    ap.add_argument("--n-files", help="range filter, e.g. 4-6, 4, 4-, -6")
    ap.add_argument("--n-f2p", help="range filter on n_f2p")
    ap.add_argument("--fresh", action="store_true",
                    help="only instances not yet run (runs == 0)")
    ap.add_argument("--resolve-rates", type=Path, default=DEFAULT_RESOLVE_RATES,
                    help="frontier resolve-rate CSV (missing -> NULL rates)")
    ap.add_argument("--min-resolve", type=float,
                    help="only instances with resolve_rate >= X (excludes NULL)")
    ap.add_argument("--dbs-dir", type=Path, default=DEFAULT_DBS_DIR,
                    help="local code-index DBs for repo_files/repo_defs")
    ap.add_argument("--screen", action="store_true",
                    help="apply the four failure-mode screens: not yet run, "
                         "frontier-resolvable, n_p2p_files > 0, repo_defs >= "
                         "--min-defs; prints per-screen exclusion counts")
    ap.add_argument("--min-defs", type=int, default=10000,
                    help="--screen navigation-surface floor (PROVISIONAL; "
                         "between vuls 4726 weak and qutebrowser 20527 strong)")
    ap.add_argument("--rank", choices=RANK_KEYS, default="n_test_files",
                    help="sort key (default: n_test_files, cross-language safe)")
    ap.add_argument("--asc", action="store_true", help="ascending sort")
    ap.add_argument("--limit", type=int, default=20, help="max rows (0 = all)")
    ap.add_argument("--csv", action="store_true", help="emit CSV with image_name")
    args = ap.parse_args()

    if not args.parquet.is_file():
        print(f"pro_select: no parquet at {args.parquet}", file=sys.stderr)
        return 1

    runs_idx = load_runs_index(args.pro_runs)
    resolve_idx = load_resolve_rates(args.resolve_rates)
    if not resolve_idx:
        print(f"pro_select: no resolve rates at {args.resolve_rates} "
              f"-- resolve_rate renders NULL", file=sys.stderr)

    # Repo sizes need one instance_id per repo to probe the DBs; use the
    # parquet's own rows for the pairs (cheap: one sqlite open per local DB).
    table = pq.read_table(args.parquet, columns=["instance_id", "repo"])
    size_map = build_repo_size_map(
        ((r["instance_id"], r["repo"]) for r in table.to_pylist()), args.dbs_dir)

    rows = build_rows(args.parquet, runs_idx, resolve_idx, size_map)

    langs = {l.lower() for l in args.lang}
    nf_rng = parse_range(args.n_files) if args.n_files else None
    f2p_rng = parse_range(args.n_f2p) if args.n_f2p else None
    rows = [
        r for r in rows
        if (not langs or r["lang"].lower() in langs)
        and (not args.repo or args.repo.lower() in r["repo"].lower())
        and (nf_rng is None or in_range(r["n_files"], nf_rng))
        and (f2p_rng is None or in_range(r["n_f2p"], f2p_rng))
        and (not args.fresh or r["runs"] == 0)
        and (args.min_resolve is None
             or (r["resolve_rate"] is not None
                 and r["resolve_rate"] >= args.min_resolve))
    ]

    screen_stats = None
    if args.screen:
        rows, screen_stats = apply_screen(rows, args.min_defs)

    # NULL-safe sort: unknown values rank last regardless of direction.
    rows.sort(key=lambda r: ((r[args.rank] is None),
                             -(r[args.rank] or 0) if not args.asc else (r[args.rank] or 0)))
    if args.limit > 0:
        rows = rows[:args.limit]

    if args.csv:
        write_csv(rows)
        return 0

    if not rows:
        print("pro_select: no instances match the filters", file=sys.stderr)
        return 0

    print_table(rows)
    print()
    print(f"{len(rows)} shown.  rank={args.rank}  "
          f"(image_name available via --csv)")
    if screen_stats is not None:
        parts = "  ".join(f"{k}={v}" for k, v in screen_stats.items() if v)
        print(f"screen: excluded {parts or 'nothing'}  "
              f"(surface floor: repo_defs >= {args.min_defs})")
    if args.rank == "n_f2p" and len(langs) != 1:
        print("note: n_f2p is NOT comparable across languages (Go = coarse "
              "top-level Test* funcs; py/js = per-case). Rank by n_test_files, "
              "or restrict to one --lang.")
    print("note: n_files / patch_lines are gold-patch proxies that overstate "
          "required scope (a 4-file instance was solved touching 1 file).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
