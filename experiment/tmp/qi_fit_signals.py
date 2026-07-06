#!/usr/bin/env python3
"""Quantify 'qi good-fit' signals for SWE-bench Pro instances.  [WIP -- experiment/tmp]

Prior sessions defined a POOR qi fit in three prose notions (INSTANCE_QI_FIT.md,
NEW_QI_VS_GREP_CAT_STORY_IN_CHARTS.md).  This script turns each into a number so
the rule can live in code instead of a doc:

  prose "poor fit"                     ->  signal (higher = better qi fit)
  ---------------------------------------------------------------------------
  1. "files are config / non-source"   ->  src_frac= n_src / n_files  [DEMOTED
                                            2026-07-05: descriptive]  share of
                                            gold files qi can index.  The
                                            8-instance retrospective broke it as
                                            a gate: openlibrary (0.25) fails it
                                            yet won -22% tokens, flipt (0.2)
                                            fails it and lost -- no threshold
                                            separates them.  A minimal n_src>=1
                                            floor remains (qi must be able to
                                            index SOMETHING relevant).
  2. "small number of files"           ->  n_src   [floor only]  indexable source
                                            files the agent must touch.
                                            NOTE: gold-patch scope OVERSTATES the
                                            real task -> see n_graded below.
  3. "you know the files beforehand"   ->  disc    [DESCRIPTIVE, not gated]
                                            = 1 - path_leak_frac, share of gold
                                            files NOT named in the problem
                                            statement.  Cleanly measures the
                                            notion but does NOT predict the qi
                                            benefit (qutebrowser disc=0.33 was the
                                            biggest winner), so it is not a gate.
  4. "required scope << gold patch"    ->  n_graded [DEMOTED 2026-07-05:
                                            descriptive]  gold SOURCE files the
                                            added TESTS actually exercise (the
                                            honest scope).  Does not rank
                                            outcomes (qutebrowser won -46% at
                                            the n_graded=2 floor; nodebb won
                                            -16% at n_graded=0).

What DOES rank the observed token lift (8-instance retrospective, 2026-07-05):
regression breadth (n_p2p: the three biggest wins were exactly the three
instances with p2p>0, in rank order) x navigation surface (repo_defs: vuls had
p2p-in-Go-terms but a 4.7k-def surface and stayed weak).  Mechanism, observed in
the qutebrowser command traces: --toc replaces full-file reads (needs surface),
--usage replaces repeated grep-the-call-graph (needs regression breadth).
Selection now lives in pro_select.py --screen (four failure-mode screens);
this script is the deep gold-patch analysis + the --validate loop that keeps
every signal honest against observed outcomes as batches accumulate.

Supporting dims (context, not fit signals): n_dirs (cross-module spread), n_tsx
(degraded-index count), n_gen (generated files excluded from source), n_f2p,
n_p2p (regression protection), patch_lines, lang.

Grounding decisions (verified, not assumed):
  * Indexable extensions are the union of <lang>/config/file_extensions.txt in the
    SourceMinder tree (see EXT_* below).  .tsx IS indexed (empirically confirmed:
    plain funcs/classes/methods/imports parse; only typed arrow-const React
    components degrade to VAR+LAM) -- so .tsx counts as source with a tracked
    n_tsx sub-count, NOT excluded.
  * fail_to_pass / pass_to_pass are Python-repr strings, not JSON -> parse_repr_list.
  * path-leak matches a gold file's full path, its basename+ext, OR a *distinctive*
    stem (camelCase/PascalCase, or snake_case len>4).  Bare generic stems
    (rooms, index, consts) are NOT matched -- too many false positives.

Usage:
  P=experiment/.venv_pro/bin/python
  $P experiment/tmp/qi_fit_signals.py --repo element-web --rank fit
  $P experiment/tmp/qi_fit_signals.py --explain 3ff19cf7c4    # per-file breakdown
  $P experiment/tmp/qi_fit_signals.py --lang go --min-src 2 --csv > cands.csv
  $P experiment/tmp/qi_fit_signals.py --validate              # signals vs outcomes
"""
from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

import pyarrow.parquet as pq

TMP_DIR = Path(__file__).resolve().parent
EXPERIMENT_DIR = TMP_DIR.parent
ANALYSIS_DIR = EXPERIMENT_DIR / "analysis"
DEFAULT_PARQUET = EXPERIMENT_DIR / "data" / "swebench_pro" / "test.parquet"
# Per-instance code indexes; DB filename == f"{instance_id}.db".  These carry the
# only faithful measure of qi's navigation surface (indexed files + definitions),
# so the repo-size columns are sourced here rather than from raw repo bytes.
DBS_DIR = EXPERIMENT_DIR / "dbs"

# Reuse the canonical repr-list parser (the fail_to_pass gotcha lives there) and
# the shared repo-size map (extracted to lib/repo_size.py 2026-07-05 so
# pro_select.py's --screen and this script agree on the surface numbers).
sys.path.insert(0, str(ANALYSIS_DIR))
sys.path.insert(0, str(EXPERIMENT_DIR))
from pro_select import parse_repr_list, count_patch_lines  # noqa: E402
from lib.repo_size import build_repo_size_map  # noqa: E402

# Indexable source extensions == union of <lang>/config/file_extensions.txt.
EXT_SOURCE = {
    ".c", ".h", ".def",                       # c
    ".ts", ".tsx", ".js", ".jsx", ".mjs",     # typescript
    ".php", ".phtml",                          # php
    ".go",                                     # go
    ".py", ".pyw", ".pyi",                     # python
    ".rs",                                     # rust
    ".pl", ".pm", ".t",                        # perl
}
# .tsx degrades (typed arrow-const components) but still indexes -> tracked, not excluded.
EXT_DEGRADED = {".tsx"}

# Test-file detection (path or basename). Precedence over source classification.
_TEST_DIR_RE = re.compile(r"(^|/)(tests?|__tests__|spec|specs|testing)(/|$)", re.I)
_TEST_BASE_RE = re.compile(
    r"(^test_.*|.*_test\.[a-z]+$|.*\.test\.[a-z]+$|.*\.spec\.[a-z]+$|.*test\.(go|py|ts|js)$)",
    re.I,
)

# Generated / codegen files: their extension is a source ext, but the bytes are
# machine-written boilerplate qi can't help navigate (e.g. flipt's flipt.pb.go =
# 356 getter/reflect funcs).  Classified as non-source so they don't inflate
# src_frac.  Matched on basename.
_GENERATED_RE = re.compile(
    r"("
    r"\.pb\.(go|cc|h|ts|js)$"                 # protobuf compiled
    r"|\.pb\.d\.ts$|_pb\.js$"                 # ts/js protobuf
    r"|_pb2(_grpc)?\.pyi?$"                   # python protobuf (+stubs)
    r"|_grpc\.pb\.go$"                        # grpc go
    r"|\.gen\.go$|\.g\.go$"                   # go codegen
    r"|_generated\.(go|ts|js|py)$|\.generated\.(ts|js|go)$"
    r"|\.min\.(js|css)$"                      # minified bundles
    r")", re.I)


def patch_files(patch: str) -> list[str]:
    """Gold-patch target paths, from `diff --git a/<path> b/<path>` headers."""
    return re.findall(r"^diff --git a/(.+?) b/", patch or "", re.M)


def classify_file(path: str) -> str:
    """One of: test | generated | source | nonsource."""
    low = path.lower()
    base = os.path.basename(low)
    if _TEST_DIR_RE.search(low) or _TEST_BASE_RE.match(base):
        return "test"
    if _GENERATED_RE.search(base):
        return "generated"
    ext = os.path.splitext(low)[1]
    return "source" if ext in EXT_SOURCE else "nonsource"


def _distinctive_stem(stem: str) -> bool:
    """True for names unlikely to false-positive as prose words:
    camelCase/PascalCase (an interior uppercase) or snake_case with len>4."""
    if any(c.isupper() for c in stem[1:]):
        return True
    if "_" in stem and len(stem) > 4:
        return True
    return False


def leaked_files(files: list[str], problem: str) -> dict[str, str]:
    """{path: how} for every gold file NAMED in the problem statement.

    how in {path, base, stem}.  A file the problem statement points at directly
    needs no qi discovery.  Bare generic stems are ignored (see module docstring)."""
    p = problem or ""
    out: dict[str, str] = {}
    for f in files:
        base = os.path.basename(f)
        stem = os.path.splitext(base)[0]
        if f in p:
            out[f] = "path"
        elif base in p:
            out[f] = "base"
        elif _distinctive_stem(stem) and re.search(r"\b" + re.escape(stem) + r"\b", p):
            out[f] = "stem"
    return out


def graded_files(src_files: list[str], test_patch: str) -> dict[str, str]:
    """{src_path: how} for gold source files the graded tests actually exercise.

    The gold `patch` overstates required scope (a many-file gold patch can have
    a single tested file).  The honest scope comes from what the added tests
    touch.  Two language-aware rules, unioned:

      how="ref"  the source path/stem is referenced in the test_patch body
                 (js/ts/py: tests live under test/ and IMPORT the source).
      how="pkg"  the source file shares a directory with a test file
                 (go: _test.go is same-package, imports nothing).
    """
    tp = test_patch or ""
    test_dirs = {os.path.dirname(f)
                 for f in re.findall(r"^diff --git a/(.+?) b/", tp, re.M)}
    # Python/dotted imports ('from a.b.c import', 'import a.b.c') reference module
    # paths, not file paths -> convert a.b.c to a/b/c and suffix-match (handles the
    # lib/ prefix: gold lib/ansible/.../powershell endswith ansible/.../powershell).
    dotted = {d.replace(".", "/")
              for d in re.findall(r"(?:from|import)\s+([\w.]+)", tp) if "." in d}
    out: dict[str, str] = {}
    for f in src_files:
        noext = os.path.splitext(f)[0]
        base = os.path.basename(f)
        stem = os.path.splitext(base)[0]
        if noext in tp or base in tp:
            out[f] = "ref"
        elif any(noext.endswith(dp) for dp in dotted):
            out[f] = "ref"
        elif _distinctive_stem(stem) and re.search(r"\b" + re.escape(stem) + r"\b", tp):
            out[f] = "ref"
        elif os.path.dirname(f) in test_dirs:
            out[f] = "pkg"
    return out


def signals(row: dict, repo_size: dict[str, tuple[int, int]] | None = None) -> dict:
    """All qi-fit signals + supporting dims for one parquet row.

    repo_size (repo -> (files, defs)) supplies the DESCRIPTIVE repo-scale columns;
    None/absent -> repo_files/repo_defs stay None (rendered NULL, not 0)."""
    files = patch_files(row["patch"])
    cats = [classify_file(f) for f in files]
    src_files = [f for f, c in zip(files, cats) if c == "source"]
    n_files = len(files)
    n_src = len(src_files)
    n_test = cats.count("test")
    n_gen = cats.count("generated")
    n_non = cats.count("nonsource")
    n_tsx = sum(1 for f in src_files if os.path.splitext(f.lower())[1] in EXT_DEGRADED)
    n_dirs = len({os.path.dirname(f) for f in src_files})

    leaks = leaked_files(files, row.get("problem_statement", ""))
    src_leaks = sum(1 for f in src_files if f in leaks)

    graded = graded_files(src_files, row.get("test_patch", ""))
    n_graded = len(graded)

    src_frac = n_src / n_files if n_files else 0.0
    # discovery over the SOURCE files (those qi could help find); 1.0 if none named.
    disc = (1 - src_leaks / n_src) if n_src else 0.0

    f2p = parse_repr_list(row["fail_to_pass"])
    p2p = parse_repr_list(row["pass_to_pass"])

    # Repo-scale navigation surface (descriptive; NULL when the repo has no local
    # index). qi's value scales with how much structure there is to navigate.
    rf, rd = (repo_size or {}).get(row["repo"], (None, None))

    return {
        "instance_id": row["instance_id"],
        "repo": row["repo"],
        "lang": row["repo_language"],
        "repo_files": rf,
        "repo_defs": rd,
        "n_files": n_files,
        "n_src": n_src,
        "n_tsx": n_tsx,
        "n_test": n_test,
        "n_gen": n_gen,
        "n_non": n_non,
        "n_dirs": n_dirs,
        "src_frac": round(src_frac, 2),
        "n_graded": n_graded,
        "disc": round(disc, 2),
        "src_leaks": src_leaks,
        "n_f2p": len(f2p),
        "n_p2p": len(p2p),
        "patch_lines": count_patch_lines(row["patch"]),
        "_files": files,
        "_leaks": leaks,
        "_graded": graded,
        "_cats": cats,
    }


# DEMOTED 2026-07-05: the discovery gates (src_frac, n_graded) did not rank the
# 8-instance retrospective outcomes (see module docstring), so the defaults are
# now a minimal floor: n_src >= 1 (qi must be able to index something relevant).
# The old thresholds remain reachable via --min-src-frac/--min-graded for
# reproducing earlier selections.  Ranking/screening for NEW batches lives in
# pro_select.py --screen (resolvable + p2p breadth + navigation surface).
def is_fit(s: dict, min_src: int, min_src_frac: float, min_graded: int,
           need_p2p: bool) -> bool:
    return (s["n_src"] >= min_src
            and s["src_frac"] >= min_src_frac
            and s["n_graded"] >= min_graded
            and (not need_p2p or s["n_p2p"] > 0))


TABLE_KEYS = ["repo", "lang", "repo_files", "repo_defs", "n_files", "n_src",
              "n_graded", "n_tsx", "n_dirs", "src_frac", "disc", "n_f2p", "n_p2p",
              "patch_lines", "fit", "short"]
CSV_KEYS = ["instance_id", "repo", "lang", "repo_files", "repo_defs", "n_files",
            "n_src", "n_graded", "n_tsx", "n_gen", "n_dirs", "src_frac", "disc",
            "src_leaks", "n_f2p", "n_p2p", "patch_lines", "fit"]


def _cell(v):
    """Render a signal value for display: None -> NULL (never blank or 0)."""
    return "NULL" if v is None else v


def short_id(iid: str) -> str:
    m = re.search(r"-([0-9a-f]{7,40})(-v.*)?$", iid)
    return m.group(1)[:10] if m else iid[-12:]


def print_table(rows: list[dict]) -> None:
    disp = []
    for s in rows:
        d = {k: _cell(s.get(k, "")) for k in TABLE_KEYS}
        d["short"] = short_id(s["instance_id"])
        d["fit"] = "Y" if s["fit"] else "-"
        disp.append(d)
    widths = {k: len(k) for k in TABLE_KEYS}
    for d in disp:
        for k in TABLE_KEYS:
            widths[k] = max(widths[k], len(str(d[k])))
    print("  ".join(k.ljust(widths[k]) for k in TABLE_KEYS))
    print("  ".join("-" * widths[k] for k in TABLE_KEYS))
    for d in disp:
        print("  ".join(str(d[k]).ljust(widths[k]) for k in TABLE_KEYS))


def explain(s: dict) -> None:
    print(f"instance : {s['instance_id']}")
    print(f"repo/lang: {s['repo']} / {s['lang']}")
    print(f"repo-size: repo_files={_cell(s['repo_files'])} repo_defs={_cell(s['repo_defs'])} "
          f"(indexed navigation surface; NULL if repo has no local DB)")
    print(f"signals  : n_src={s['n_src']} n_graded={s['n_graded']} "
          f"src_frac={s['src_frac']} disc={s['disc']} n_dirs={s['n_dirs']} "
          f"n_tsx={s['n_tsx']} n_gen={s['n_gen']} f2p={s['n_f2p']} p2p={s['n_p2p']} "
          f"pl={s['patch_lines']}  fit={'Y' if s['fit'] else '-'}")
    print("gold files (class | leak | graded):")
    for f, c in zip(s["_files"], s["_cats"]):
        how = s["_leaks"].get(f)
        tag = f"LEAK:{how}" if how else "hidden"
        g = s["_graded"].get(f)
        gtag = f"GRADED:{g}" if g else "ungraded"
        deg = " [tsx-degraded]" if os.path.splitext(f.lower())[1] in EXT_DEGRADED else ""
        print(f"   {c:<9} {tag:<10} {gtag:<12} {f}{deg}")


CROSS_CSV = EXPERIMENT_DIR / "results" / "pro_runs" / "_cross" / "cross_instance.csv"
MANIFEST = ANALYSIS_DIR / "cross_instance_manifest.txt"

# Signals worth checking against outcomes. n_p2p and repo_defs are the
# mechanism pair (usage breadth x toc surface); the rest ride along so a
# surprise correlation gets noticed rather than assumed away.
VALIDATE_SIGNALS = ["n_p2p", "repo_defs", "repo_files", "src_frac", "n_graded",
                    "disc", "n_src", "n_dirs", "n_f2p", "patch_lines"]


def _avg_ranks(vals: list[float]) -> list[float]:
    """Average ranks (1-based) with ties sharing their mean rank."""
    order = sorted(range(len(vals)), key=lambda i: vals[i])
    ranks = [0.0] * len(vals)
    i = 0
    while i < len(order):
        j = i
        while j + 1 < len(order) and vals[order[j + 1]] == vals[order[i]]:
            j += 1
        mean_rank = (i + j) / 2 + 1
        for k in range(i, j + 1):
            ranks[order[k]] = mean_rank
        i = j + 1
    return ranks


def spearman(xs: list[float], ys: list[float]) -> float | None:
    """Spearman rank correlation; None when either side is constant."""
    n = len(xs)
    if n < 3:
        return None
    rx, ry = _avg_ranks(xs), _avg_ranks(ys)
    mx, my = sum(rx) / n, sum(ry) / n
    cov = sum((a - mx) * (b - my) for a, b in zip(rx, ry))
    vx = sum((a - mx) ** 2 for a in rx)
    vy = sum((b - my) ** 2 for b in ry)
    if vx == 0 or vy == 0:
        return None
    return cov / (vx * vy) ** 0.5


def load_observed_effects(metric: str) -> dict[str, float]:
    """{instance_id: pct_change} for the canonical cross set.

    cross_instance.csv keys rows by a short label (e.g. 'qutebrowser'); the
    manifest's batch dirs recover each label's full instance_id (the label is a
    substring of its batch dir name, and the batch's eval_results.csv names the
    instance). Negative pct_change = treatment used less (a qi win)."""
    import csv as _csv
    label_effect: dict[str, float] = {}
    with CROSS_CSV.open(newline="") as fh:
        for r in _csv.DictReader(fh):
            if r["metric"] == metric and r["instance"] != "__POOLED__":
                label_effect[r["instance"]] = float(r["pct_change"])
    batches = []
    for line in MANIFEST.read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            batches.append((ANALYSIS_DIR / line).resolve())
    out: dict[str, float] = {}
    for label, eff in label_effect.items():
        dirs = [b for b in batches if label in b.name]
        if len(dirs) != 1:
            print(f"validate: label {label!r} matches {len(dirs)} manifest "
                  f"batches -- skipped", file=sys.stderr)
            continue
        ev = dirs[0] / "eval_results.csv"
        with ev.open(newline="") as fh:
            row = next(_csv.DictReader(fh), None)
        if row:
            out[row["instance_id"]] = eff
    return out


def validate(rows: list[dict], metric: str) -> int:
    """Rank-correlate each signal against observed treatment effects.

    lift = -pct_change (positive = treatment saved), so POSITIVE rho means
    'higher signal -> bigger qi win'. This is the loop that keeps signals
    honest: disc and src_frac were both promoted on intuition and later broke
    on outcomes; every future signal earns gate status here instead."""
    effects = load_observed_effects(metric)
    by_norm = {re.sub(r"-v[0-9a-zA-Z]+$", "", s["instance_id"]): s for s in rows}
    joined = []
    for iid, eff in effects.items():
        s = by_norm.get(re.sub(r"-v[0-9a-zA-Z]+$", "", iid))
        if s:
            joined.append((s, -eff))
        else:
            print(f"validate: no parquet row for {iid} -- skipped", file=sys.stderr)
    if len(joined) < 3:
        print(f"validate: only {len(joined)} joined instances -- not enough",
              file=sys.stderr)
        return 1

    print(f"observed {metric} lift per canonical instance "
          f"(lift = -pct_change; positive = treatment saved):")
    for s, lift in sorted(joined, key=lambda t: -t[1]):
        sig = "  ".join(f"{k}={_cell(s[k])}" for k in
                        ("n_p2p", "repo_defs", "src_frac", "n_graded", "disc"))
        print(f"  {short_id(s['instance_id']):<12} {s['repo']:<28} "
              f"lift={lift:+6.1f}%  {sig}")

    print(f"\nSpearman rank correlation, signal vs lift (n={len(joined)}; "
          f"positive = higher signal predicts bigger win):")
    scored = []
    for key in VALIDATE_SIGNALS:
        pairs = [(float(s[key]), lift) for s, lift in joined if s[key] is not None]
        rho = spearman([p[0] for p in pairs], [p[1] for p in pairs]) \
            if len(pairs) >= 3 else None
        scored.append((key, rho, len(pairs)))
    for key, rho, n in sorted(scored, key=lambda t: -(abs(t[1]) if t[1] is not None else -1)):
        print(f"  {key:<12} rho={'--' if rho is None else f'{rho:+.2f}'}  (n={n})")
    print("\ncaveats: tiny n; instances differ by repo AND model (tutanota ran "
          "on sonnet); rho is rank-only. Signals earn or lose gate status here, "
          "not from intuition.")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--parquet", type=Path, default=DEFAULT_PARQUET)
    ap.add_argument("--repo", help="substring filter on repo")
    ap.add_argument("--lang", action="append", default=[])
    ap.add_argument("--explain", metavar="IID_SUBSTR",
                    help="print per-file class+leak breakdown for matching instance(s)")
    ap.add_argument("--rank", default="fit",
                    choices=["fit", "n_src", "n_graded", "disc", "src_frac",
                             "n_dirs", "n_p2p", "patch_lines", "n_files",
                             "repo_files", "repo_defs"],
                    help="sort key (default: fit then n_graded)")
    ap.add_argument("--fit-only", action="store_true", help="only rows where fit=Y")
    ap.add_argument("--min-src", type=int, default=1,
                    help="floor: min indexable source files (the only default gate "
                         "since the 2026-07-05 demotion)")
    ap.add_argument("--min-src-frac", type=float, default=0.0,
                    help="DEMOTED gate, default off; 0.5 reproduces pre-2026-07-05 "
                         "selections")
    ap.add_argument("--min-graded", type=int, default=0,
                    help="DEMOTED gate, default off; 2 reproduces pre-2026-07-05 "
                         "selections")
    ap.add_argument("--need-p2p", action="store_true",
                    help="gate: require pass_to_pass > 0 (regression protection)")
    ap.add_argument("--validate", action="store_true",
                    help="rank-correlate each signal against observed per-instance "
                         "treatment effects from results/pro_runs/_cross")
    ap.add_argument("--metric", default="total_tokens",
                    choices=["total_tokens", "cost", "duration_sec", "turn_count"],
                    help="outcome metric for --validate (default: total_tokens)")
    ap.add_argument("--limit", type=int, default=25, help="max rows (0 = all)")
    ap.add_argument("--csv", action="store_true")
    args = ap.parse_args()

    if not args.parquet.is_file():
        print(f"qi_fit_signals: no parquet at {args.parquet}", file=sys.stderr)
        return 1

    table = pq.read_table(args.parquet, columns=[
        "instance_id", "repo", "repo_language", "patch", "test_patch",
        "fail_to_pass", "pass_to_pass", "problem_statement"])
    raw_rows = table.to_pylist()
    repo_size = build_repo_size_map(
        ((r["instance_id"], r["repo"]) for r in raw_rows), DBS_DIR)
    rows = [signals(r, repo_size) for r in raw_rows]
    for s in rows:
        s["fit"] = is_fit(s, args.min_src, args.min_src_frac,
                          args.min_graded, args.need_p2p)

    if args.validate:
        return validate(rows, args.metric)

    langs = {l.lower() for l in args.lang}
    rows = [s for s in rows
            if (not args.repo or args.repo.lower() in s["repo"].lower())
            and (not langs or s["lang"].lower() in langs)]

    if args.explain:
        hits = [s for s in rows if args.explain.lower() in s["instance_id"].lower()]
        if not hits:
            print(f"no instance matches {args.explain!r}", file=sys.stderr)
            return 1
        for s in hits:
            explain(s)
            print()
        return 0

    if args.fit_only:
        rows = [s for s in rows if s["fit"]]

    # None (NULL repo-size) sorts last under reverse=True via a -inf coercion.
    def _sort_key(s):
        if args.rank == "fit":
            return (s["fit"], s["n_graded"], s["n_src"])
        v = s[args.rank]
        return float("-inf") if v is None else v
    rows.sort(key=_sort_key, reverse=True)
    if args.limit > 0:
        rows = rows[:args.limit]

    if args.csv:
        import csv
        w = csv.DictWriter(sys.stdout, fieldnames=CSV_KEYS)
        w.writeheader()
        for s in rows:
            w.writerow({k: _cell(s[k]) for k in CSV_KEYS})
        return 0

    if not rows:
        print("qi_fit_signals: no instances match", file=sys.stderr)
        return 0
    print_table(rows)
    print()
    print(f"{len(rows)} shown.  floor: n_src>={args.min_src}"
          f"{f' src_frac>={args.min_src_frac}' if args.min_src_frac else ''}"
          f"{f' n_graded>={args.min_graded}' if args.min_graded else ''}"
          f"{' p2p>0' if args.need_p2p else ''}"
          "  (discovery gates demoted 2026-07-05: descriptive only)")
    print("signals: n_src=gold source files | n_graded=source files the TESTS "
          "exercise (honest scope) | src_frac=source/all | "
          "disc=share of source NOT named in prompt -- all descriptive; "
          "selection screens live in pro_select.py --screen")
    print("repo-size: repo_files/repo_defs = per-repo indexed navigation surface; "
          "NULL when the repo has no local DB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
