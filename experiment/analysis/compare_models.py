#!/usr/bin/env python3
"""Cross-model comparison layer for the SourceMinder experiment.

Where ``analyze_stats.py`` compares arms *within* one model, this script compares
the qi **treatment effect across models** -- e.g. Claude Haiku 4.5 vs DeepSeek V4
Flash run on the *same* instance sample. The question it answers: is "qi helps,
and helps more on larger instances" a property of the tool x task structure, or
an artifact of one model?

It is **additive and import-only**: every per-instance computation (loading, the
n_files join, per-instance medians, the size interaction, the clustered
bootstrap, Wilson CIs) is imported from ``analyze_stats`` and reused. There is no
second copy of the CSV parser or the per-instance pivot -- one definition of each
computation, per the reuse contract in COMPARE_MODELS_SCRIPT_DESIGN.md s9.

The load-bearing methodological rule (design s1.3): **compare effects (ratios /
% change), never raw token counts, across models.** Different tokenizers plus the
~4 chars/token approximation make absolute token counts non-comparable between
models; the quantity that *travels* is the normalized within-model effect. Every
cross-model number here is a ratio or a percentage. Raw-token cross-model
comparison is explicitly out of scope.

This is **replication, not meta-analysis** (design s1.2): with k=2 models you
cannot estimate between-study heterogeneity, so there is no random-effects
pooling. Everything is side-by-side replication + cross-model consistency,
labelled as pilot/exploratory. At k=2 models x 5 instances the model x arm
interaction is doubly underpowered; no inferential test is run on it.

Usage:
  # point at a combined run directory (reads runs_with_success.csv from it)
  python3 experiment/analysis/compare_models.py --dir experiment/results/runs/<ts>

  # or name explicit CSVs; repeat --csv to concatenate per-model run dirs
  python3 experiment/analysis/compare_models.py \
      --csv .../haiku/runs_with_success.csv \
      --csv .../deepseek/runs_with_success.csv --dir .../out

  # defaults: newest results/runs/<ts>/runs_with_success.csv, output beside it
  python3 experiment/analysis/compare_models.py
"""
from __future__ import annotations

import argparse
import itertools
import json
import math
import sys
from pathlib import Path

import numpy as np
from scipy import stats as scipy_stats

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # -> experiment/
from analysis import analyze_stats as A  # the single source of every per-instance computation


# --------------------------------------------------------------------------- #
# Input resolution (mirrors the fixed analyze_stats --dir/--csv semantics, plus
# repeatable --csv -> concatenate per-model run dirs)
# --------------------------------------------------------------------------- #
def resolve_csv_paths(args) -> list[Path]:
    """Explicit --csv (repeatable) wins; else <dir>/runs_with_success.csv; else newest."""
    if args.csv:
        return [Path(c) for c in args.csv]
    run_dir = args.dir or (A.paths.batch_run_dir(args.batch) if getattr(args, "batch", None) else None)
    if run_dir is not None:
        return [run_dir / "runs_with_success.csv"]
    default = A._default_csv()
    return [default] if default else []


def load_rows(csv_paths: list[Path], n_files_map: dict[str, int]) -> list[A.Row]:
    """Load + concatenate rows from one or more CSVs.

    Rows already carry ``model``; concatenation is a plain extend. If the same
    ``(model, arm, instance_id, rep)`` identity appears in two files, keep the
    first and warn -- two run dirs should never describe the same run, so a
    collision means a bookkeeping mistake worth surfacing (design s2.2).
    """
    seen: set[tuple] = set()
    rows: list[A.Row] = []
    for path in csv_paths:
        for r in A.load(path, n_files_map):
            key = (r.get("model", ""), r.get("arm"), r.get("instance_id"),
                   r.get("run_id") or r.get("rep"))
            if key in seen:
                print(f"WARNING: duplicate run {key} in {path} -- keeping first",
                      file=sys.stderr)
                continue
            seen.add(key)
            rows.append(r)
    return rows


# --------------------------------------------------------------------------- #
# Cross-model analyses -- all on effects (ratios / % change), never raw tokens
# --------------------------------------------------------------------------- #
def per_model_effects(rows_by_model: dict[str, list[A.Row]], metric: str,
                      iters: int, rng: np.random.Generator) -> dict:
    """Per model: T/C ratio + clustered CI, median per-instance %change, size rho.

    Reuses ``clustered_bootstrap_diff`` (ratio + clustered ratio CI) and
    ``size_interaction`` (per-instance %change + Spearman(n_files, %change)).
    Only ratios/percentages are retained -- no raw token columns travel across
    models (design s1.3).
    """
    out: dict[str, dict] = {}
    for model, rows in rows_by_model.items():
        if not A.has_metric(rows, metric):
            continue
        boot = A.clustered_bootstrap_diff(rows, metric, iters, rng)
        si = A.size_interaction(rows, metric)
        pcts = [r["pct_change"] for r in si["per_instance"]
                if r["pct_change"] is not None]
        out[model] = {
            "point_ratio": boot["point_ratio"],
            "ratio_ci": boot["ratio_ci"],
            "median_pct_change": float(np.median(pcts)) if pcts else None,
            "spearman_size": si["spearman"],
            "per_instance": si["per_instance"],
        }
    return out


def crossmodel_correlation(rows_by_model: dict[str, list[A.Row]],
                           metric: str) -> list[dict]:
    """Pairwise Spearman of per-instance %change across models (on shared instances).

    For each model pair, pair the per-instance %change on ``instance_id`` and
    correlate. High positive rho => the *same* instances benefit in both models
    => the effect is instance-driven and consistent. At k=2 this is one pair; the
    pairwise loop generalizes to k>2 without special-casing (design s3.3).
    """
    pct_by_model: dict[str, dict[str, float]] = {}
    for model, rows in rows_by_model.items():
        if not A.has_metric(rows, metric):
            continue
        si = A.size_interaction(rows, metric)
        pct_by_model[model] = {r["instance_id"]: r["pct_change"]
                               for r in si["per_instance"]
                               if r["pct_change"] is not None}
    results = []
    for a, b in itertools.combinations(sorted(pct_by_model), 2):
        shared = sorted(set(pct_by_model[a]) & set(pct_by_model[b]))
        dropped = sorted((set(pct_by_model[a]) ^ set(pct_by_model[b])))
        rec = {"model_a": a, "model_b": b, "n_shared": len(shared),
               "instances": shared, "dropped": dropped,
               "rho": None, "pvalue": None, "note": None}
        if len(shared) < 3:
            rec["note"] = f"n={len(shared)} shared -- too few for a correlation"
        else:
            xs = [pct_by_model[a][i] for i in shared]
            ys = [pct_by_model[b][i] for i in shared]
            sr = scipy_stats.spearmanr(xs, ys)
            rec["rho"], rec["pvalue"] = float(sr.statistic), float(sr.pvalue)
        results.append(rec)
    return results


def success_parity(rows_by_model: dict[str, list[A.Row]], iters: int,
                   rng: np.random.Generator) -> dict:
    """Per model: per-arm success rate (+ Wilson CI) and clustered success diff.

    Haiku was a tie both arms (qi changes search, not resolution). Replicated
    parity across models is a cleaner non-inferiority story than either run alone
    (design s3.4).
    """
    out: dict[str, dict] = {}
    for model, rows in rows_by_model.items():
        arms = {}
        for arm in A.ARMS:
            sv = A.fvals(A._arm(rows, arm), "task_success")
            succ, n = int(sv.sum()), int(sv.size)
            rate, lo, hi = A.wilson_ci(succ, n)
            arms[arm] = {"resolved": succ, "n": n, "rate": rate,
                         "wilson_ci": [lo, hi]}
        out[model] = {"arms": arms,
                      "clustered": A.clustered_bootstrap_success(rows, iters, rng)}
    return out


def mechanism_summary(rows_by_model: dict[str, list[A.Row]]) -> dict:
    """Per model x arm: mean qi/grep/file invocations per run.

    Surfaces treatment *fidelity* -- whether each model actually substituted qi
    for grep under treatment, or merely added qi on top (design s2.6). A
    treatment arm that still greps heavily dilutes any qi-specific effect.
    """
    out: dict[str, dict] = {}
    for model, rows in rows_by_model.items():
        out[model] = {}
        for arm in A.ARMS:
            sub = A._arm(rows, arm)
            out[model][arm] = {
                m: (float(np.mean(A.fvals(sub, m))) if A.fvals(sub, m).size else None)
                for m in A.MECHANISM_METRICS
            }
    return out


def per_instance_pct_table(effects_by_metric: dict[str, dict],
                           models: list[str]) -> dict:
    """Per-instance %change side by side across models, per metric, + direction agreement.

    The reconciled form of CROSS_MODEL_PLAN.md s2.1 -- expressed in **%change**,
    not raw token Delta (which is not cross-model comparable). 'Direction agrees'
    counts instances where every model's %change shares a sign (all save or all
    cost). At n=5 a count like '4/5 agree' is a defensible descriptive statement.
    """
    table: dict[str, dict] = {}
    for metric, per_model in effects_by_metric.items():
        # instance_id -> {model: pct_change}
        by_inst: dict[str, dict[str, float]] = {}
        nfiles: dict[str, int | None] = {}
        for model in models:
            for r in per_model.get(model, {}).get("per_instance", []):
                if r["pct_change"] is None:
                    continue
                by_inst.setdefault(r["instance_id"], {})[model] = r["pct_change"]
                nfiles[r["instance_id"]] = r["n_files"]
        rows = []
        agree = 0
        for inst in sorted(by_inst):
            cells = by_inst[inst]
            present = [cells[m] for m in models if m in cells]
            agrees = len(present) == len(models) and (
                all(p < 0 for p in present) or all(p > 0 for p in present))
            agree += int(agrees)
            rows.append({"instance_id": inst, "n_files": nfiles.get(inst),
                         "pct_change": {m: cells.get(m) for m in models},
                         "direction_agrees": agrees})
        complete = [r for r in rows if all(r["pct_change"][m] is not None
                                           for m in models)]
        table[metric] = {"rows": rows, "n_agree": agree,
                         "n_complete": len(complete)}
    return table


# --------------------------------------------------------------------------- #
# Text summary
# --------------------------------------------------------------------------- #
def _fmt_ratio(x) -> str:
    return "n/a" if x is None else f"{x:.3f}"


def _fmt_pct(x) -> str:
    return "n/a" if x is None else f"{x:+.1f}%"


def write_summary(models: list[str], effects: dict, correlation: dict,
                  parity: dict, mechanism: dict, pct_table: dict, fh,
                  n_instances: int = 0) -> None:
    def w(line=""):
        fh.write(line + "\n")

    w("=" * 78)
    w("CROSS-MODEL COMPARISON  (qi treatment effect across models)")
    w("=" * 78)
    w()
    w(f"Models ({len(models)}): " + ", ".join(models))
    w()
    w("REPLICATION, not meta-analysis: k=2 models cannot estimate between-study")
    w("heterogeneity. Everything below is descriptive / exploratory.")
    w("Cross-model quantities are RATIOS or %CHANGE -- raw token counts are not")
    w("comparable across tokenizers and are never compared here.")
    w()

    # --- s3.1 side-by-side effects table ---
    w("--- Effects side by side (treatment vs control, per model) ---")
    w("    ratio = median(T)/median(C) with clustered 95% CI; <1 => qi saves.")
    w("    med%Delta = median per-instance %change; rho = Spearman(n_files, %change).")
    for metric, per_model in effects.items():
        w(f"\n  [{metric}]")
        w(f"    {'model':32} {'ratio':>7} {'ratio 95% CI':>22} {'med%Delta':>10} "
          f"{'size rho':>9} {'p':>6}")
        for model in models:
            e = per_model.get(model)
            if not e:
                continue
            lo, hi = e["ratio_ci"]
            ci = (f"[{_fmt_ratio(lo)}, {_fmt_ratio(hi)}]")
            sp = e["spearman_size"]
            rho = "n/a" if sp["rho"] is None else f"{sp['rho']:+.2f}"
            pv = "n/a" if sp["pvalue"] is None else f"{sp['pvalue']:.3f}"
            w(f"    {model:32} {_fmt_ratio(e['point_ratio']):>7} {ci:>22} "
              f"{_fmt_pct(e['median_pct_change']):>10} {rho:>9} {pv:>6}")

    # --- s3.3 cross-model effect correlation ---
    w("\n--- Cross-model per-instance effect correlation (shared instances) ---")
    w("    Spearman of per-instance %change between models. rho>0 => the same")
    w("    instances benefit in both models (effect is instance-driven).")
    for metric, recs in correlation.items():
        for rec in recs:
            if rec["note"]:
                detail = rec["note"]
            else:
                detail = f"rho={rec['rho']:+.2f} p={rec['pvalue']:.3f}"
            w(f"  [{metric}] {rec['model_a']} vs {rec['model_b']}: "
              f"n={rec['n_shared']} shared -- {detail}")
            if rec["dropped"]:
                w(f"      dropped (not in both): {', '.join(rec['dropped'])}")

    # --- per-instance %change table + direction agreement (reconciled s2.1) ---
    w("\n--- Per-instance %change by model (negative => qi saves) ---")
    for metric, tab in pct_table.items():
        w(f"\n  [{metric}]  ({tab['n_agree']}/{tab['n_complete']} instances agree on direction)")
        w(f"    {'instance':28} {'n_files':>7} " +
          " ".join(f"{m.split('--')[-1][:14]:>15}" for m in models) + "  agree")
        for r in tab["rows"]:
            cells = " ".join(f"{_fmt_pct(r['pct_change'][m]):>15}" for m in models)
            nf = "" if r["n_files"] is None else str(int(r["n_files"]))
            flag = "Y" if r["direction_agrees"] else "n"
            w(f"    {r['instance_id']:28} {nf:>7} {cells}  {flag}")

    # --- s3.4 success parity ---
    w("\n--- Success parity (per model x arm) ---")
    w(f"    {'model':32} {'control':>14} {'treatment':>14} {'Delta pp (clustered CI)':>26}")
    for model in models:
        p = parity.get(model)
        if not p:
            continue
        def cell(a):
            x = p["arms"][a]
            return f"{x['resolved']}/{x['n']}={100*x['rate']:.0f}%"
        cl = p["clustered"]
        dpp = cl["point_diff_pp"]
        clo, chi = cl["diff_ci_pp"]
        dstr = ("n/a" if dpp is None else
                f"{100*dpp:+.0f} [{100*clo:+.0f},{100*chi:+.0f}]")
        w(f"    {model:32} {cell('control'):>14} {cell('treatment'):>14} {dstr:>26}")

    # --- s2.6 mechanism / treatment fidelity ---
    w("\n--- Mechanism: mean invocations per run (treatment fidelity) ---")
    w("    A treatment arm that still greps heavily is 'qi+grep', diluting effect.")
    for model in models:
        m = mechanism.get(model, {})
        w(f"  {model}")
        for arm in A.ARMS:
            vals = m.get(arm, {})
            cells = "  ".join(
                f"{k.split('_')[0]}={'n/a' if vals.get(k) is None else f'{vals[k]:.1f}'}"
                for k in A.MECHANISM_METRICS)
            w(f"    {arm:10} {cells}")

    w("\n--- Caveats ---")
    w("  * Replication at k=2 models -- no meta-analysis / random-effects pooling.")
    w("  * Cross-model comparisons use effects (ratios/%); raw tokens never crossed.")
    n_str = str(n_instances) if n_instances else "?"
    w(f"  * N={n_str} instances per model; model x arm interaction underpowered at any k=2.")
    w("  * Asymmetric censoring (LimitsExceeded) biases token ratios; read with")
    w("    per-model censoring rates from analyze_stats.py alongside.")
    w("  * %change conditions on each instance being run in both arms of a model;")
    w("    instances unresolved in an arm still count (tokens, not success).")


# --------------------------------------------------------------------------- #
# Charts
# --------------------------------------------------------------------------- #
_MARKERS = ("o", "s", "^", "D", "v")


def _model_styles(models: list[str]) -> dict[str, dict]:
    return {m: {"marker": _MARKERS[i % len(_MARKERS)], "label": m.split("--")[0]}
            for i, m in enumerate(models)}


def make_charts(models: list[str], effects: dict, correlation: dict,
                parity: dict, mechanism: dict, charts_dir: Path) -> list[str]:
    plt = A.plt
    if plt is None:
        return []
    from matplotlib.ticker import MaxNLocator
    charts_dir.mkdir(parents=True, exist_ok=True)
    styles = _model_styles(models)
    written: list[str] = []

    def save(fig, name):
        A._save(fig, charts_dir / name)
        written.append(name)

    metrics = list(effects.keys())

    # 01 size-interaction replication: per metric, overlay both models' points
    if metrics:
        fig, axes = plt.subplots(1, len(metrics), figsize=(5 * len(metrics), 4.5),
                                 squeeze=False)
        for ax, metric in zip(axes[0], metrics):
            for model in models:
                e = effects[metric].get(model)
                if not e:
                    continue
                pts = [(r["n_files"], r["pct_change"]) for r in e["per_instance"]
                       if r["n_files"] is not None and r["pct_change"] is not None]
                if not pts:
                    continue
                xs, ys = zip(*pts)
                sp = e["spearman_size"]
                rho = "" if sp["rho"] is None else f" (rho={sp['rho']:+.2f})"
                ax.scatter(xs, ys, marker=styles[model]["marker"], s=70,
                           alpha=0.8, label=styles[model]["label"] + rho)
            ax.axhline(0, color="gray", lw=0.8, ls="--")
            ax.set_title(metric)
            ax.set_xlabel("n_files (instance size)")
            ax.set_ylabel("% change (treatment vs control)")
            ax.xaxis.set_major_locator(MaxNLocator(integer=True))
            ax.legend(fontsize=8)
        fig.suptitle("Size-interaction replication: does 'qi saves more on larger "
                     "instances' hold across models?")
        save(fig, "01_size_interaction_replication.png")

    # 02 cross-model effect correlation: A %change vs B %change per metric
    #    (only drawn for the first model pair; matrix deferred until k>2)
    if len(models) == 2 and metrics:
        a, b = models
        fig, axes = plt.subplots(1, len(metrics), figsize=(5 * len(metrics), 4.5),
                                 squeeze=False)
        for ax, metric in zip(axes[0], metrics):
            ea = {r["instance_id"]: r["pct_change"]
                  for r in effects[metric].get(a, {}).get("per_instance", [])
                  if r["pct_change"] is not None}
            eb = {r["instance_id"]: r["pct_change"]
                  for r in effects[metric].get(b, {}).get("per_instance", [])
                  if r["pct_change"] is not None}
            shared = sorted(set(ea) & set(eb))
            xs = [ea[i] for i in shared]
            ys = [eb[i] for i in shared]
            ax.scatter(xs, ys, s=70, alpha=0.8, color="C3")
            for i, inst in enumerate(shared):
                ax.annotate(inst.split("__")[-1], (xs[i], ys[i]),
                            fontsize=7, alpha=0.8,
                            xytext=(4, 4), textcoords="offset points")
            lim = max([abs(v) for v in xs + ys] + [1]) * 1.15
            ax.plot([-lim, lim], [-lim, lim], color="gray", lw=0.8, ls="--")
            ax.axhline(0, color="gray", lw=0.5)
            ax.axvline(0, color="gray", lw=0.5)
            ax.set_xlim(-lim, lim)
            ax.set_ylim(-lim, lim)
            rec = next((r for r in correlation.get(metric, [])
                        if r["model_a"] == a and r["model_b"] == b), None)
            sub = (rec["note"] if rec and rec["note"]
                   else (f"rho={rec['rho']:+.2f} p={rec['pvalue']:.3f}"
                         if rec else ""))
            ax.set_title(f"{metric}\n{sub}")
            ax.set_xlabel(f"{styles[a]['label']} % change")
            ax.set_ylabel(f"{styles[b]['label']} % change")
        fig.suptitle("Cross-model per-instance effect correlation "
                     "(do the same instances benefit?)")
        save(fig, "02_crossmodel_effect_correlation.png")

    # 03 effects side by side: forest of T/C ratios with clustered CI
    if metrics:
        labels, ratios, los, his = [], [], [], []
        for metric in metrics:
            for model in models:
                e = effects[metric].get(model)
                if not e or e["point_ratio"] is None:
                    continue
                labels.append(f"{styles[model]['label']} / {metric}")
                ratios.append(e["point_ratio"])
                lo, hi = e["ratio_ci"]
                los.append(e["point_ratio"] - lo if lo is not None else 0)
                his.append(hi - e["point_ratio"] if hi is not None else 0)
        if labels:
            fig, ax = plt.subplots(figsize=(8, 0.5 * len(labels) + 2))
            y = range(len(labels))
            ax.errorbar(ratios, y, xerr=[los, his], fmt="o", capsize=4, color="C0")
            ax.axvline(1.0, color="red", lw=1, ls="--", label="ratio = 1 (no effect)")
            ax.set_yticks(list(y))
            ax.set_yticklabels(labels, fontsize=8)
            ax.set_xlabel("treatment / control ratio (<1 => qi saves)")
            ax.set_title("Effect ratios side by side (clustered 95% CI)")
            ax.legend(fontsize=8)
            save(fig, "03_effects_sidebyside.png")

    # 04 success parity: grouped bars success rate by (model x arm) + Wilson CI
    if parity:
        fig, ax = plt.subplots(figsize=(7, 4.5))
        width = 0.8 / max(len(models), 1)
        x = np.arange(len(A.ARMS))
        for i, model in enumerate(models):
            p = parity.get(model)
            if not p:
                continue
            rates = [p["arms"][a]["rate"] for a in A.ARMS]
            lo = [p["arms"][a]["rate"] - p["arms"][a]["wilson_ci"][0] for a in A.ARMS]
            hi = [p["arms"][a]["wilson_ci"][1] - p["arms"][a]["rate"] for a in A.ARMS]
            ax.bar(x + i * width, rates, width, yerr=[lo, hi], capsize=4,
                   label=styles[model]["label"])
        ax.set_xticks(x + width * (len(models) - 1) / 2)
        ax.set_xticklabels(A.ARMS)
        ax.set_ylabel("success rate")
        ax.set_ylim(0, 1)
        ax.set_title("Success parity by model x arm (Wilson 95% CI)")
        ax.legend(fontsize=8)
        save(fig, "04_success_parity.png")

    # 05 mechanism: grouped bars qi/grep/file mean per run by model x arm
    if mechanism:
        fig, ax = plt.subplots(figsize=(9, 4.5))
        groups = [(model, arm) for model in models for arm in A.ARMS]
        x = np.arange(len(A.MECHANISM_METRICS))
        width = 0.8 / max(len(groups), 1)
        for i, (model, arm) in enumerate(groups):
            vals = [mechanism[model][arm].get(m) or 0 for m in A.MECHANISM_METRICS]
            ax.bar(x + i * width, vals, width,
                   label=f"{styles[model]['label']}/{arm[:4]}")
        ax.set_xticks(x + width * (len(groups) - 1) / 2)
        ax.set_xticklabels([m.split("_")[0] for m in A.MECHANISM_METRICS])
        ax.set_ylabel("mean invocations / run")
        ax.set_title("Mechanism by model x arm (treatment fidelity)")
        ax.legend(fontsize=7, ncol=2)
        save(fig, "05_mechanism_cross_model.png")

    return written


# --------------------------------------------------------------------------- #
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--csv", action="append", default=None,
                    help="runs_with_success.csv (repeatable -> concatenated). "
                         "Default: <dir>/runs_with_success.csv, or newest run dir.")
    ap.add_argument("--dir", type=Path, default=None,
                    help="Run directory: reads runs_with_success.csv from it and "
                         "writes outputs there (same meaning as analyze_stats.py).")
    ap.add_argument("--batch", default=None, metavar="BATCH_ID",
                    help="Resolve the run directory as results/runs/<batch>/ "
                         "(alias for --dir results/runs/<batch>)")
    ap.add_argument("--pool", type=Path, default=A.paths.DATA_DIR / "pool.csv",
                    help="Source for per-instance n_files (size moderator).")
    ap.add_argument("--bootstrap-iters", type=int, default=10000,
                    help="Clustered bootstrap resamples (default: 10000).")
    ap.add_argument("--seed", type=int, default=42,
                    help="RNG seed for the bootstrap (default: 42).")
    ap.add_argument("--models", nargs="+", default=None,
                    help="Optional filter to a subset of model names.")
    ap.add_argument("--no-charts", action="store_true",
                    help="Skip chart rendering even if matplotlib is available.")
    args = ap.parse_args()

    csv_paths = resolve_csv_paths(args)
    missing = [p for p in csv_paths if not p.is_file()]
    if not csv_paths or missing:
        where = missing or ["(none found)"]
        print(f"ERROR: runs_with_success.csv not found: "
              f"{', '.join(str(p) for p in where)}; pass --csv or --dir",
              file=sys.stderr)
        return 1

    run_dir = args.dir or (A.paths.batch_run_dir(args.batch) if args.batch else None)
    out_dir = (run_dir or csv_paths[0].parent).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    n_files_map = A.load_n_files(args.pool)
    rows = load_rows(csv_paths, n_files_map)
    print(f"Loaded {len(rows)} runs from {', '.join(str(p) for p in csv_paths)}")

    models = sorted({r["model"] for r in rows})
    if args.models:
        models = [m for m in models if m in set(args.models)]
        rows = [r for r in rows if r["model"] in set(models)]
    if len(models) < 2:
        print(f"need >=2 models to compare (found: {models or 'none'}). "
              f"Use analyze_stats.py for a single-model analysis.", file=sys.stderr)
        return 0

    rows_by_model = {m: [r for r in rows if r["model"] == m] for m in models}
    for m in models:
        print(f"  {m}: {len(rows_by_model[m])} runs, "
              f"{len(A._instances(rows_by_model[m]))} instances")

    rng = np.random.default_rng(args.seed)
    effects = {metric: per_model_effects(rows_by_model, metric,
                                         args.bootstrap_iters, rng)
               for metric in A.TOKEN_METRICS}
    effects = {k: v for k, v in effects.items() if v}  # drop metrics absent everywhere
    correlation = {metric: crossmodel_correlation(rows_by_model, metric)
                   for metric in effects}
    parity = success_parity(rows_by_model, args.bootstrap_iters, rng)
    mechanism = mechanism_summary(rows_by_model)
    pct_table = per_instance_pct_table(effects, models)

    n_instances = len(A._instances(rows))
    summary_path = out_dir / "model_comparison_summary.txt"
    with summary_path.open("w") as fh:
        write_summary(models, effects, correlation, parity, mechanism, pct_table, fh,
                      n_instances=n_instances)
    print(f"Wrote {summary_path}")

    payload = {
        "input_csvs": [str(p) for p in csv_paths],
        "models": models,
        "effects": effects,
        "correlation": correlation,
        "success_parity": parity,
        "mechanism": mechanism,
        "per_instance_pct": pct_table,
    }
    json_path = out_dir / "model_comparison.json"
    json_path.write_text(json.dumps(A._json_safe(payload), indent=2))
    print(f"Wrote {json_path}")

    if A.plt is not None and not args.no_charts:
        charts_dir = (out_dir / "charts" / "cross_model").resolve()
        written = make_charts(models, effects, correlation, parity, mechanism,
                              charts_dir)
        print(f"  wrote {len(written)} charts -> {charts_dir}")
    elif A.plt is None and not args.no_charts:
        print("WARNING: matplotlib unavailable -- charts skipped.", file=sys.stderr)

    # Echo the summary so the headline is visible without opening the file.
    print()
    print(summary_path.read_text())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
