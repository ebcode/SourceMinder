#!/usr/bin/env python3
"""Per-arm summary over the per-command CSV from extract_qi_commands.py.

Reads qi_commands.csv and prints a by-arm comparison aimed at the prompt
study: does teaching --limit/--limit-per-file in the prompt actually shrink qi
output, and how does the command vocabulary / error rate differ between arms?

Everything here is DESCRIPTIVE: the prompt study has a handful of runs per arm,
far too few for inferential claims. Read the medians as direction, not proof.

Usage:
  python3 experiment/analysis/report_qi_commands.py
  python3 experiment/analysis/report_qi_commands.py --csv results/runs/prompt_study/qi_commands.csv
"""
from __future__ import annotations

import argparse
import csv
import re
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # -> experiment/
from lib import paths
from analysis.extract_qi_commands import QI_FLAG_COLS, QI_ANTIPATTERN_COLS


def _median(vals: list[float]):
    return statistics.median(vals) if vals else None


def _pct(vals: list[float], p: float):
    """Linear-interpolated percentile (p in [0,1]); None if empty."""
    if not vals:
        return None
    s = sorted(vals)
    k = (len(s) - 1) * p
    lo = int(k)
    hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def _fmt(x, nd=0):
    return "n/a" if x is None else f"{x:,.{nd}f}"


def load(csv_path: Path) -> list[dict]:
    with csv_path.open(newline="") as f:
        rows = list(csv.DictReader(f))
    for r in rows:
        for k in ("turn_idx", "cmd_idx", "output_chars",
                  "output_tokens_approx", "qi_pure"):
            r[k] = int(r[k]) if r[k] not in ("", None) else 0
        r["is_error"] = int(r["is_error"]) if r["is_error"] not in ("", None) else None
        r["qi_results"] = int(r["qi_results"]) if r.get("qi_results") not in ("", None) else None
        # r.get(): tolerate CSVs written before these columns existed.
        for k in (*QI_FLAG_COLS, *QI_ANTIPATTERN_COLS):
            r[k] = int(r[k]) if r.get(k) not in ("", None) else 0
    return rows


def report_arm(arm: str, rows: list[dict], p) -> None:
    p(f"\n{'=' * 70}\nARM: {arm}\n{'=' * 70}")
    n_runs = len({(r["instance"], r["run_id"]) for r in rows})
    qi = [r for r in rows if r["tool"] == "qi"]
    grep = [r for r in rows if r["tool"] == "grep"]
    read = [r for r in rows if r["tool"] == "read"]
    explore = len(qi) + len(grep) + len(read)
    p(f"runs={n_runs}  commands={len(rows)}")

    # 1. Tool mix
    p("\n--- Tool usage ---")
    p(f"  qi={len(qi)}  grep={len(grep)}  read={len(read)}  other={len(rows)-explore}")
    if explore:
        p(f"  qi share of exploration (qi/qi+grep+read): {len(qi)/explore:.0%}")
    if n_runs:
        p(f"  per run: {len(rows)/n_runs:.1f} commands, {len(qi)/n_runs:.1f} qi, "
          f"{len(grep)/n_runs:.1f} grep")

    # 2. qi output response size (clean qi-only commands)
    pure = [r for r in qi if r["qi_pure"]]
    sizes = [r["output_tokens_approx"] for r in pure]
    p("\n--- qi output size (qi_pure commands, tokens ~= chars/4) ---")
    p(f"  n_pure={len(pure)} of {len(qi)} qi commands")
    p(f"  mean={_fmt(statistics.mean(sizes)) if sizes else 'n/a'}  "
      f"median={_fmt(_median(sizes))}  p90={_fmt(_pct(sizes, 0.9))}  "
      f"max={_fmt(max(sizes) if sizes else None)}")
    if n_runs:
        p(f"  total qi output per run: {_fmt(sum(sizes)/n_runs)} tokens")

    # 3. Limit-flag adoption and effect
    p("\n--- Limit flags ---")
    if qi:
        limited = [r for r in qi if r["qi_limit"] or r["qi_limit_per_file"]]
        p(f"  qi commands using -l/--limit or -lpf/--limit-per-file: "
          f"{len(limited)}/{len(qi)} ({len(limited)/len(qi):.0%})")
        lim_sizes = [r["output_tokens_approx"] for r in pure
                     if r["qi_limit"] or r["qi_limit_per_file"]]
        unlim_sizes = [r["output_tokens_approx"] for r in pure
                       if not (r["qi_limit"] or r["qi_limit_per_file"])]
        p(f"  median qi output -- limited={_fmt(_median(lim_sizes))} "
          f"(n={len(lim_sizes)})  unlimited={_fmt(_median(unlim_sizes))} "
          f"(n={len(unlim_sizes)})")
    else:
        p("  (no qi commands)")

    # 4. Error / misuse rate
    scored = [r for r in qi if r["is_error"] is not None]
    if scored:
        errs = sum(r["is_error"] for r in scored)
        p(f"\n--- qi errors ---\n  returncode != 0: {errs}/{len(scored)} "
          f"({errs/len(scored):.0%})")

    # 5. Flag vocabulary
    if qi:
        p("\n--- qi flag vocabulary (% of qi commands using each) ---")
        for col in QI_FLAG_COLS:
            n = sum(r[col] for r in qi)
            if n:
                p(f"  {col[3:]:16s} {n:>3}/{len(qi)} ({n/len(qi):.0%})")

    # 6. grep output size, for contrast
    if grep:
        gsizes = [r["output_tokens_approx"] for r in grep]
        p(f"\n--- grep output size (tokens) ---\n  "
          f"median={_fmt(_median(gsizes))}  max={_fmt(max(gsizes))}  n={len(grep)}")


# qi command "kind" by primary flag (mutually exclusive, precedence order).
# Output size is dominated by which subcommand is used, so the mix must be seen
# alongside any average -- toc is tiny, def -e on a class can be a whole file.
QI_KINDS = ("toc", "usage", "def", "expand", "search")


def _run_groups(rows: list[dict]) -> dict:
    groups: dict = {}
    for r in rows:
        groups.setdefault((r["instance"], r["run_id"]), []).append(r)
    return groups


def _successful(rows: list[dict], tool: str, pure: bool = False) -> list[dict]:
    """Calls of `tool` that returned 0. Errors emit tiny outputs and would drag
    any average down, so size metrics must exclude them."""
    out = [r for r in rows if r["tool"] == tool and r["is_error"] == 0]
    return [r for r in out if r["qi_pure"]] if pure else out


def _qi_kind(r: dict) -> str:
    if r["qi_toc"]:
        return "toc"
    if r["qi_usage"]:
        return "usage"
    if r["qi_def"]:
        return "def"
    if r["qi_expand"]:
        return "expand"
    return "search"


def _acol(arms) -> int:
    """Width of one arm column: longest arm name + a 2-space gap (min 12)."""
    return max(max((len(str(a)) for a in arms), default=0) + 2, 12)


def essentials(by_arm: dict[str, list[dict]], p) -> None:
    p(f"\n{'=' * 70}\nESSENTIALS (side-by-side)\n{'=' * 70}")
    arms = list(by_arm)
    w = _acol(arms)
    p(f"  {'metric':34s}" + "".join(f"{a:>{w}}" for a in arms))

    def line(label, fn):
        p(f"  {label:34s}" + "".join(f"{fn(by_arm[a]):>{w}}" for a in arms))

    def qi(rs): return [r for r in rs if r["tool"] == "qi"]
    def grep(rs): return [r for r in rs if r["tool"] == "grep"]
    def read(rs): return [r for r in rs if r["tool"] == "read"]
    def nruns(rs): return len(_run_groups(rs))

    def scored(rs):  # qi calls with a parseable returncode
        return [r for r in qi(rs) if r["is_error"] is not None]

    line("runs", lambda rs: str(nruns(rs)))
    line("qi calls", lambda rs: str(len(qi(rs))))
    line("grep calls", lambda rs: str(len(grep(rs))))
    line("qi calls / run",
         lambda rs: _fmt(len(qi(rs)) / nruns(rs), 1) if nruns(rs) else "n/a")
    line("qi / grep ratio",
         lambda rs: _fmt(len(qi(rs)) / len(grep(rs)), 2) if grep(rs) else "n/a")
    line("qi share of exploration",
         lambda rs: (f"{len(qi(rs))/(len(qi(rs))+len(grep(rs))+len(read(rs))):.0%}"
                     if (len(qi(rs)) + len(grep(rs)) + len(read(rs))) else "n/a"))
    line("% runs using qi",
         lambda rs: (f"{sum(1 for g in _run_groups(rs).values() if any(r['tool']=='qi' for r in g))/nruns(rs):.0%}"
                     if nruns(rs) else "n/a"))
    line("mean qi output/call* (tok)",
         lambda rs: (_fmt(statistics.mean([r["output_tokens_approx"] for r in _successful(rs, "qi", pure=True)]))
                     if _successful(rs, "qi", pure=True) else "n/a"))
    line("median qi output/call* (tok)",
         lambda rs: _fmt(_median([r["output_tokens_approx"] for r in _successful(rs, "qi", pure=True)])))
    line("mean grep output/call* (tok)",
         lambda rs: (_fmt(statistics.mean([r["output_tokens_approx"] for r in _successful(rs, "grep")]))
                     if _successful(rs, "grep") else "n/a"))
    line("total qi output / run (tok)",
         lambda rs: (_fmt(sum(r["output_tokens_approx"] for r in _successful(rs, "qi", pure=True)) / nruns(rs))
                     if nruns(rs) else "n/a"))
    line("qi error rate",
         lambda rs: (f"{sum(1 for r in scored(rs) if r['is_error'])/len(scored(rs)):.0%}"
                     if scored(rs) else "n/a"))

    def searched(rs):  # qi search calls with a parseable match count
        return [r for r in qi(rs) if r["qi_results"] is not None]

    line("qi zero-result rate",
         lambda rs: (f"{sum(1 for r in searched(rs) if r['qi_results'] == 0)/len(searched(rs)):.0%}"
                     if searched(rs) else "n/a"))
    line("limit-flag adoption (% qi)",
         lambda rs: (f"{sum(1 for r in qi(rs) if r['qi_limit'] or r['qi_limit_per_file'])/len(qi(rs)):.0%}"
                     if qi(rs) else "n/a"))

    def qi_rate(rs, col):  # % of qi calls with a 1 in `col`
        q = qi(rs)
        return f"{sum(r[col] for r in q)/len(q):.0%}" if q else "n/a"

    line("-p / --parent adoption (% qi)", lambda rs: qi_rate(rs, "qi_parent"))
    line("dotted-name misuse (% qi)", lambda rs: qi_rate(rs, "qi_dotted_name"))
    line("quoted-phrase misuse (% qi)", lambda rs: qi_rate(rs, "qi_quoted_phrase"))
    line("abs-path -f filter (% qi)", lambda rs: qi_rate(rs, "qi_abs_path"))
    p("\n  * output/call = SUCCESSFUL calls only (errors return tiny outputs);")
    p("    qi uses qi_pure (no compound/pipe) so output isn't mis-attributed.")


def output_by_type(by_arm: dict[str, list[dict]], p) -> None:
    p(f"\n{'=' * 70}\nqi OUTPUT BY COMMAND TYPE (successful + qi_pure; median tok)\n{'=' * 70}")
    arms = list(by_arm)
    w = _acol(arms)
    p(f"  {'kind':10s}" + "".join(f"{a:>{w}}" for a in arms))
    for kind in QI_KINDS:
        cells = []
        for a in arms:
            calls = [r for r in _successful(by_arm[a], "qi", pure=True)
                     if _qi_kind(r) == kind]
            cells.append(f"{_median([r['output_tokens_approx'] for r in calls]):>8,.0f} (n={len(calls)})"
                         if calls else "-")
        p(f"  {kind:10s}" + "".join(f"{c:>{w}}" for c in cells))


def _qi_never_after_grep(rows: list[dict]) -> tuple[int, int]:
    """Runs where qi is *never* used after the first grep command (count, total)."""
    count = 0
    total = 0
    for grp in _run_groups(rows).values():
        total += 1
        grep_turns = [g["turn_idx"] for g in grp if g["tool"] == "grep"]
        if not grep_turns:
            continue
        first_grep = min(grep_turns)
        if not any(g["tool"] == "qi" and g["turn_idx"] > first_grep for g in grp):
            count += 1
    return count, total


def abandonment(by_arm: dict[str, list[dict]], p) -> None:
    p(f"\n{'=' * 70}\nTOOL TIMING -- onset & abandonment (turns ranked 1..T per run)\n{'=' * 70}")
    p("  first/last turn = ordinal of the first/last qi (or grep/sed) command;")
    p("  frac = turn / total turns. Low first = immediate adherence; low last")
    p("  frac = tool dropped early. 'in first 3' = qi used by turn 3.\n")
    arms = list(by_arm)
    w = _acol(arms)
    p(f"  {'metric':34s}" + "".join(f"{a:>{w}}" for a in arms))

    def stat(rows, tools):
        first_turns, last_turns, fracs, n_first3, n_never, n = [], [], [], 0, 0, 0
        for grp in _run_groups(rows).values():
            n += 1
            turns = sorted({g["turn_idx"] for g in grp})
            ordm = {t: i + 1 for i, t in enumerate(turns)}
            T = len(turns)
            ords = [ordm[g["turn_idx"]] for g in grp if g["tool"] in tools]
            if ords:
                first_turns.append(min(ords))
                last_turns.append(max(ords))
                fracs.append(max(ords) / T)
                if min(ords) <= 3:
                    n_first3 += 1
            else:
                n_never += 1
        return first_turns, last_turns, fracs, n_first3, n_never, n

    qi_s = {a: stat(by_arm[a], ("qi",)) for a in arms}
    gs_s = {a: stat(by_arm[a], ("grep", "read")) for a in arms}

    def line(label, fn):
        p(f"  {label:34s}" + "".join(f"{fn(a):>{w}}" for a in arms))

    line("median total turns / run",
         lambda a: _fmt(_median([len({g["turn_idx"] for g in grp})
                                 for grp in _run_groups(by_arm[a]).values()])))
    line("qi: median FIRST turn", lambda a: _fmt(_median(qi_s[a][0])))
    line("qi: % runs using qi in first 3",
         lambda a: f"{qi_s[a][3]/qi_s[a][5]:.0%}" if qi_s[a][5] else "n/a")
    line("qi: median last turn", lambda a: _fmt(_median(qi_s[a][1])))
    line("qi: median last-turn frac",
         lambda a: f"{_median(qi_s[a][2]):.0%}" if qi_s[a][2] else "n/a")
    line("qi: runs never using it", lambda a: f"{qi_s[a][4]}/{qi_s[a][5]}")
    line("grep/sed: median FIRST turn", lambda a: _fmt(_median(gs_s[a][0])))
    line("grep/sed: median last turn", lambda a: _fmt(_median(gs_s[a][1])))
    line("grep/sed: median last-turn frac",
         lambda a: f"{_median(gs_s[a][2]):.0%}" if gs_s[a][2] else "n/a")
    line("grep/sed: runs never using it", lambda a: f"{gs_s[a][4]}/{gs_s[a][5]}")

    grep_kills = {a: _qi_never_after_grep(by_arm[a]) for a in arms}
    line("qi never-after-first-grep",
         lambda a: (f"{grep_kills[a][0]}/{grep_kills[a][1]} "
                    f"({grep_kills[a][0]/grep_kills[a][1]:.0%})"
                    if grep_kills[a][1] else "n/a"))


def qi_dynamics(by_arm: dict[str, list[dict]], p) -> None:
    """Consecutive-qi streaks and qi intensity across session thirds."""
    p(f"\n{'=' * 70}\nqi DYNAMICS -- streaks & shape over the session\n{'=' * 70}")
    arms = list(by_arm)
    w = _acol(arms)

    def max_streak(grp):
        # max consecutive qi among exploration commands (grep/read break it;
        # 'other' commands like running tests are ignored, not breaks).
        seq = sorted((g for g in grp if g["tool"] in ("qi", "grep", "read")),
                     key=lambda g: (g["turn_idx"], g["cmd_idx"]))
        best = cur = 0
        for g in seq:
            cur = cur + 1 if g["tool"] == "qi" else 0
            best = max(best, cur)
        return best

    p(f"  {'metric':34s}" + "".join(f"{a:>{w}}" for a in arms))
    p(f"  {'max qi streak (median/run)':34s}"
      + "".join(f"{_fmt(_median([max_streak(g) for g in _run_groups(by_arm[a]).values()])):>{w}}"
                for a in arms))

    # qi intensity (qi calls per turn) by session third
    p("\n  qi calls per turn, by session third (early / mid / late):")
    p(f"  {'third':34s}" + "".join(f"{a:>{w}}" for a in arms))
    intensity = {a: ([0, 0, 0], [0, 0, 0]) for a in arms}  # (qi_calls, turns)
    for a in arms:
        qic, trn = intensity[a]
        for grp in _run_groups(by_arm[a]).values():
            turns = sorted({g["turn_idx"] for g in grp})
            T = len(turns)
            if not T:
                continue
            ordm = {t: i for i, t in enumerate(turns)}
            for i in range(T):
                trn[min(2, i * 3 // T)] += 1
            for g in grp:
                if g["tool"] == "qi":
                    trn_idx = ordm[g["turn_idx"]]
                    qic[min(2, trn_idx * 3 // T)] += 1
    for label, idx in (("early", 0), ("mid", 1), ("late", 2)):
        cells = []
        for a in arms:
            qic, trn = intensity[a]
            cells.append(_fmt(qic[idx] / trn[idx], 2) if trn[idx] else "n/a")
        p(f"  {label:34s}" + "".join(f"{c:>{w}}" for c in cells))


# --- Composability: batching (chained commands per action) and piping --------
# The composability prompt teaches bundling related lookups into ONE shell
# action ("qi a; qi b; qi c") and piping qi output through head/wc/grep to trim.
# Both cut round-trips, the dominant driver of total_input. Chaining lives INSIDE
# the command string (one action row), so it must be measured by splitting.
_CMD_SEP = re.compile(r"&&|\|\||;")          # command separators (NOT a single |)
_QI_SEG = re.compile(r"^\s*qi\b")            # a sub-command that invokes qi
_PIPE_FILTER = re.compile(r"\|\s*(head|tail|wc|grep|sort|uniq|sed|awk)\b")


def _subcmds(command: str) -> list[str]:
    """Split one shell action into chained sub-commands on ; && || (heuristic:
    does not parse quotes; qi commands rarely embed these separators)."""
    return [s.strip() for s in _CMD_SEP.split(command or "") if s.strip()]


def _qi_subcmds(command: str) -> list[str]:
    return [s for s in _subcmds(command) if _QI_SEG.match(s)]


def _chained(rows: list[dict]) -> list[int]:
    """Sub-commands per action, across all actions (batching)."""
    return [len(_subcmds(r["command"])) for r in rows]


def _qi_per_turn(rows: list[dict]) -> list[int]:
    """qi-call count per (instance, run, turn); only turns with >=1 qi call."""
    counts: dict = {}
    for r in rows:
        n = len(_qi_subcmds(r["command"]))
        if n:
            key = (r["instance"], r["run_id"], r["turn_idx"])
            counts[key] = counts.get(key, 0) + n
    return list(counts.values())


def _qi_pipe_rate(rows: list[dict]):
    """Fraction of qi invocations piped into a filter; None if no qi calls."""
    total = sum(len(_qi_subcmds(r["command"])) for r in rows)
    if not total:
        return None
    piped = sum(1 for r in rows for s in _qi_subcmds(r["command"])
                if _PIPE_FILTER.search(s))
    return piped / total


def _batch_per_run(rows: list[dict], nruns: int):
    """Turns with >1 qi call, normalized per run; None if no runs."""
    if not nruns:
        return None
    return sum(1 for c in _qi_per_turn(rows) if c > 1) / nruns


def composability(by_arm: dict[str, list[dict]], p) -> None:
    p(f"\n{'=' * 70}\nCOMPOSABILITY -- batching & piping (fewer round-trips)\n{'=' * 70}")
    p("  batching = sub-commands chained into one action (split on ; && ||);")
    p("  qi calls counted per sub-command so 'qi a; qi b' = 2. piping = qi")
    p("  output filtered through head/wc/grep/etc.\n")
    arms = list(by_arm)
    w = _acol(arms)
    p(f"  {'metric':34s}" + "".join(f"{a:>{w}}" for a in arms))

    def line(label, fn):
        p(f"  {label:34s}" + "".join(f"{fn(by_arm[a]):>{w}}" for a in arms))

    line("mean sub-cmds / action",
         lambda rs: _fmt(statistics.mean(_chained(rs)), 2) if rs else "n/a")
    line("median sub-cmds / action",
         lambda rs: _fmt(_median(_chained(rs)), 1))
    line("% qi calls piped to filter",
         lambda rs: f"{_qi_pipe_rate(rs):.0%}" if _qi_pipe_rate(rs) is not None else "n/a")
    line("turns with >1 qi call / run",
         lambda rs: _fmt(_batch_per_run(rs, len(_run_groups(rs))), 1)
                    if _run_groups(rs) else "n/a")
    line("% qi-turns with >1 qi call",
         lambda rs: (f"{sum(1 for c in _qi_per_turn(rs) if c > 1)/len(_qi_per_turn(rs)):.0%}"
                     if _qi_per_turn(rs) else "n/a"))


# Precision flags that scope a grep (file-type or word-boundary), separating a
# targeted "grep -rn --include='*.py' foo" from a naive "grep foo".
GREP_PRECISE_FLAGS = ("--include", "--exclude", "-w", "-l")


def grep_sophistication(by_arm: dict[str, list[dict]], p) -> None:
    p(f"\n{'=' * 70}\nGREP SOPHISTICATION (precise = scoped by {'/'.join(GREP_PRECISE_FLAGS)})\n{'=' * 70}")
    arms = list(by_arm)
    w = _acol(arms)
    p(f"  {'metric':34s}" + "".join(f"{a:>{w}}" for a in arms))

    def greps(a):
        return [r for r in by_arm[a] if r["tool"] == "grep"]

    def precise(a):
        return [r for r in greps(a)
                if any(f in r["command"] for f in GREP_PRECISE_FLAGS)]

    def line(label, fn):
        p(f"  {label:34s}" + "".join(f"{fn(a):>{w}}" for a in arms))

    line("grep calls", lambda a: str(len(greps(a))))
    line("% precise grep",
         lambda a: f"{len(precise(a))/len(greps(a)):.0%}" if greps(a) else "n/a")
    line("median naive-grep output (tok)",
         lambda a: _fmt(_median([r["output_tokens_approx"] for r in greps(a)
                                 if r not in precise(a)])))
    line("median precise-grep output (tok)",
         lambda a: _fmt(_median([r["output_tokens_approx"] for r in precise(a)])))


def cross_model(rows: list[dict], p) -> None:
    """One table per metric: model rows x arm columns.

    Only metrics that compare validly across models -- rates/proportions and the
    char-based output approximation (chars/4 is a fixed transform, NOT an API
    tokenizer count, so it applies identically to every model). Real API token
    counts (peak_prompt_tokens etc.) are NOT comparable across tokenizers and
    deliberately have no place here -- use compare_models.py for those.
    """
    p(f"\n{'=' * 70}\nCROSS-MODEL MATRIX (model rows x arm cols)\n{'=' * 70}")
    p("  cross-model-valid metrics only (rates + char-based sizes).")
    models = sorted({r["model"] for r in rows})
    arms = sorted({r["arm"] for r in rows if r["arm"]})
    w = _acol(arms)

    def qi(rs): return [r for r in rs if r["tool"] == "qi"]
    def grep(rs): return [r for r in rs if r["tool"] == "grep"]
    def read(rs): return [r for r in rs if r["tool"] == "read"]
    def nruns(rs): return len(_run_groups(rs))
    def scored(rs): return [r for r in qi(rs) if r["is_error"] is not None]
    def searched(rs): return [r for r in qi(rs) if r["qi_results"] is not None]

    metrics = [
        ("qi calls / run",
         lambda rs: _fmt(len(qi(rs)) / nruns(rs), 1) if nruns(rs) else "n/a"),
        ("qi / grep ratio",
         lambda rs: _fmt(len(qi(rs)) / len(grep(rs)), 2) if grep(rs) else "n/a"),
        ("qi share of exploration",
         lambda rs: (f"{len(qi(rs))/(len(qi(rs))+len(grep(rs))+len(read(rs))):.0%}"
                     if (len(qi(rs)) + len(grep(rs)) + len(read(rs))) else "n/a")),
        ("mean qi output/call (tok)",
         lambda rs: (_fmt(statistics.mean([r["output_tokens_approx"]
                                           for r in _successful(rs, "qi", pure=True)]))
                     if _successful(rs, "qi", pure=True) else "n/a")),
        ("qi zero-result rate",
         lambda rs: (f"{sum(1 for r in searched(rs) if r['qi_results'] == 0)/len(searched(rs)):.0%}"
                     if searched(rs) else "n/a")),
        ("qi error rate",
         lambda rs: (f"{sum(1 for r in scored(rs) if r['is_error'])/len(scored(rs)):.0%}"
                     if scored(rs) else "n/a")),
        ("limit-flag adoption (% qi)",
         lambda rs: (f"{sum(1 for r in qi(rs) if r['qi_limit'] or r['qi_limit_per_file'])/len(qi(rs)):.0%}"
                     if qi(rs) else "n/a")),
        ("mean sub-cmds / action",
         lambda rs: _fmt(statistics.mean(_chained(rs)), 2) if rs else "n/a"),
        ("% qi calls piped to filter",
         lambda rs: f"{_qi_pipe_rate(rs):.0%}" if _qi_pipe_rate(rs) is not None else "n/a"),
        ("turns with >1 qi call / run",
         lambda rs: _fmt(_batch_per_run(rs, nruns(rs)), 1) if nruns(rs) else "n/a"),
        ("% qi-turns with >1 qi call",
         lambda rs: (f"{sum(1 for c in _qi_per_turn(rs) if c > 1)/len(_qi_per_turn(rs)):.0%}"
                     if _qi_per_turn(rs) else "n/a")),
        ("qi never-after-first-grep",
         lambda rs: (f"{grep_kills[0]}/{grep_kills[1]} "
                     f"({grep_kills[0]/grep_kills[1]:.0%})"
                     if (grep_kills := _qi_never_after_grep(rs))[1] else "n/a")),
    ]

    def sub(model, arm):
        return [r for r in rows if r["model"] == model and r["arm"] == arm]

    for label, fn in metrics:
        p(f"\n  [{label}]")
        p(f"  {'model':20s}" + "".join(f"{a:>{w}}" for a in arms))
        for model in models:
            short = (model.split("--")[-1] or model)[:20]
            p(f"  {short:20s}"
              + "".join(f"{fn(sub(model, arm)):>{w}}" for arm in arms))


def report_block(rows: list[dict], p) -> None:
    """All per-arm and cross-arm sections for one set of rows (one model)."""
    arms = sorted({r["arm"] for r in rows if r["arm"]})
    by_arm = {a: [r for r in rows if r["arm"] == a] for a in arms}
    p(f"arms: {', '.join(arms)}   total commands: {len(rows)}")
    for arm in arms:
        report_arm(arm, by_arm[arm], p)
    essentials(by_arm, p)
    output_by_type(by_arm, p)
    abandonment(by_arm, p)
    qi_dynamics(by_arm, p)
    composability(by_arm, p)
    grep_sophistication(by_arm, p)


def main() -> int:
    default_csv = paths.new_run_dir(batch_id="prompt_study") / "qi_commands.csv"
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--csv", type=Path, default=default_csv,
                    help=f"per-command CSV from extract_qi_commands.py "
                         f"(default: {default_csv})")
    ap.add_argument("--model", default=None, metavar="SUBSTR",
                    help="only report models whose id contains SUBSTR "
                         "(e.g. v4-pro); default: every model in the CSV")
    ap.add_argument("--cross-model", action="store_true",
                    help="print an arm-by-model matrix of cross-model-valid "
                         "metrics instead of the per-model detail blocks")
    args = ap.parse_args()

    if not args.csv.is_file():
        print(f"ERROR: CSV not found: {args.csv}\n"
              f"  Run extract_qi_commands.py first.", file=sys.stderr)
        return 1

    rows = load(args.csv)
    models = sorted({r["model"] for r in rows})
    if args.model:
        models = [m for m in models if args.model in m]
        if not models:
            print(f"ERROR: no model matching {args.model!r} in {args.csv}",
                  file=sys.stderr)
            return 1

    rows = [r for r in rows if r["model"] in models]
    out = []
    p = out.append
    p(f"qi command report -- {args.csv}")
    p("\nDESCRIPTIVE ONLY: few runs per arm; read medians as direction.")
    if args.cross_model:
        cross_model(rows, p)
    else:
        # Arms are only comparable within a model (tokenizers/abilities differ),
        # so always split by model -- one self-contained report block each.
        for model in models:
            mrows = [r for r in rows if r["model"] == model]
            p(f"\n{'#' * 70}\n# MODEL: {model or '(unknown)'}  "
              f"({len(mrows)} commands)\n{'#' * 70}")
            report_block(mrows, p)
    print("\n".join(out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
