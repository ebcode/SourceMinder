#!/usr/bin/env python3
"""
Estimate experiment cost from model pricing and configuration.

Two modes:
  estimate  — given instances × runs, project total cost
  budget    — given a budget, find the max runs (or instances)

Uses per-run token estimates derived from prior pilot data (or explicit
--tokens/--output-tokens overrides).

Usage:
  # Estimate: 20 instances × 10 runs × 2 arms with DeepSeek v4 Flash
  python3 experiment/analysis/estimate_cost.py deepseek/deepseek-v4-flash \\
      --instances 20 --runs 10

  # Budget mode: how many runs can I afford with Haiku for $20?
  python3 experiment/analysis/estimate_cost.py anthropic/claude-haiku-4-5-20251001 \\
      --budget 20 --instances 5

  # Price per run (single-run cost)
  python3 experiment/analysis/estimate_cost.py deepseek/deepseek-v4-flash --per-run

  # Compare two models
  python3 experiment/analysis/estimate_cost.py deepseek/deepseek-v4-flash \\
      --instances 20 --runs 10 \\
      --compare anthropic/claude-haiku-4-5-20251001

  # Use explicit token counts instead of pilot-derived averages
  python3 experiment/analysis/estimate_cost.py deepseek/deepseek-v4-flash \\
      --instances 20 --runs 10 --input-tokens 500000 --output-tokens 20000

  # Recompute: refine the estimate with REAL per-arm instance_cost from the
  # trajectory logs where they exist (opportunistic — falls back to the
  # pricing-table estimate for any arm/model with no logs on disk).
  python3 experiment/analysis/estimate_cost.py anthropic/claude-haiku-4-5-20251001 \\
      --instances 5 --runs 5 --from-logs --budget 40
"""

import argparse
import csv
import json
import sys
from collections import defaultdict
from pathlib import Path
from typing import Optional

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # -> experiment/
from lib import paths
from lib.model import model_dir, normalize_model
from lib.trajmeta import ARMS, batch_of

# ── Model pricing (per MILLION tokens) ──────────────────────────────────────
# Key: litellm model identifier prefix
# Values: (input_price_per_MTok, output_price_per_MTok, cache_read_price_per_MTok)

PRICING = {
    "deepseek/deepseek-v4-flash":      (0.14, 0.28,  0.014),
    "deepseek/deepseek-v4":            (0.28, 0.56,  0.028),
    "deepseek/deepseek-v3.2":          (0.28, 0.40,  0.040),
    "deepseek/deepseek-chat":          (0.28, 0.42,  0.028),
    "deepseek/deepseek-reasoner":      (0.28, 0.42,  0.028),

    "anthropic/claude-haiku-4-5":      (1.00, 5.00,  0.10),
    "anthropic/claude-sonnet-4-5":     (3.00, 15.00, 0.30),
    "anthropic/claude-sonnet-4-6":     (3.00, 15.00, 0.30),
    "anthropic/claude-opus-4-8":       (5.00, 25.00, 0.50),

    "xiaomi_mimo/mimo-v2-flash":       (0.14, 0.28,  0.0028),
    "xiaomi_mimo/mimo-v2.5-pro":       (0.435, 0.87, 0.0036),

    "openai/gpt-4.1-mini":             (0.15, 0.60,  0.075),
    "openai/gpt-4.1":                  (2.00, 8.00,  1.00),
}

# Where to find cached input tokens (DeepSeek reports these in the
# prompt_tokens_details dict; they're a subset of total_input_tokens that
# was served from cache at a discount). Anthropic has a similar concept.
CACHE_READ_FRACTION = 0.80  # ~80% of input tokens are typically cache hits


# ── Token estimates derived from pilot data ─────────────────────────────────

def _find_default_csv() -> Path | None:
    """Find the most recent runs_with_success.csv under results/runs/."""
    candidates = sorted(paths.RUNS_DIR.glob("*/runs_with_success.csv"))
    return candidates[-1] if candidates else None


def pilot_averages(pilot_csv: Optional[Path] = None) -> tuple[float, float, float]:
    """Return (mean_input_tokens, mean_output_tokens, mean_reasoning_tokens)
    from the most recent pilot CSV, or use baked-in defaults."""
    if pilot_csv is None:
        pilot_csv = _find_default_csv()
    if pilot_csv is None or not pilot_csv.exists():
        return (1_518_000, 21_277, 12_118)

    input_toks: list[int] = []
    output_toks: list[int] = []
    reason_toks: list[int] = []

    try:
        with open(pilot_csv, newline="") as f:
            reader = csv.DictReader(f)
            for row in reader:
                try:
                    input_toks.append(int(row.get("total_input_tokens", 0)))
                except (ValueError, TypeError):
                    pass
                try:
                    output_toks.append(int(row.get("total_completion_tokens", 0)))
                except (ValueError, TypeError):
                    pass
                try:
                    reason_toks.append(int(row.get("total_reasoning_tokens", 0)))
                except (ValueError, TypeError):
                    pass
    except (OSError, csv.Error) as exc:
        print(f"WARNING: could not read {pilot_csv} ({exc}); using baked-in "
              "token defaults.", file=sys.stderr)

    if not input_toks:
        return (1_518_000, 21_277, 12_118)

    return (
        sum(input_toks) / len(input_toks),
        sum(output_toks) / len(output_toks),
        sum(reason_toks) / len(reason_toks),
    )


# ── Pricing lookup ──────────────────────────────────────────────────────────

def lookup_pricing(model: str) -> tuple[float, float, float]:
    """Return (input_$_per_MTok, output_$_per_MTok, cache_read_$_per_MTok)."""
    # Exact match
    if model in PRICING:
        return PRICING[model]
    # Prefix match (strip date suffix: anthropic/claude-haiku-4-5-20251001 → ...haiku-4-5)
    for prefix, prices in PRICING.items():
        if model.startswith(prefix) or prefix.startswith(model):
            print(f"NOTE: no exact pricing for '{model}'; matched prefix "
                  f"'{prefix}'.", file=sys.stderr)
            return prices
    print(f"WARNING: no pricing for '{model}' — using Haiku 4.5 rates as a "
          "fallback; cost numbers are unreliable.", file=sys.stderr)
    return (1.00, 5.00, 0.10)


# ── Cost calculation ────────────────────────────────────────────────────────

def cost_per_run(model: str, input_tokens: float, output_tokens: float,
                 reasoning_tokens: float = 0.0) -> float:
    """Estimate cost for a single run."""
    in_price, out_price, cache_price = lookup_pricing(model)

    # Output tokens (including reasoning) billed at the output rate
    out_total = output_tokens + reasoning_tokens

    # Approximate cache-read fraction (DeepSeek / Anthropic both cache)
    cached = input_tokens * CACHE_READ_FRACTION
    uncached = input_tokens - cached

    in_cost = uncached * in_price / 1_000_000 + cached * cache_price / 1_000_000
    out_cost = out_total * out_price / 1_000_000

    return in_cost + out_cost


def total_cost(model: str, instances: int, runs_per_instance: int,
               arms: int = 2, input_tokens: float = 0, output_tokens: float = 0,
               reasoning_tokens: float = 0, pilot_csv: Optional[Path] = None) -> tuple[float, float, float, float]:
    """Return (total_, per_run, input_mean, output_mean)."""
    if input_tokens <= 0:
        input_mean, output_mean, reason_mean = pilot_averages(pilot_csv)
    else:
        input_mean, output_mean, reason_mean = input_tokens, output_tokens, reasoning_tokens

    total_runs = instances * arms * runs_per_instance
    per = cost_per_run(model, input_mean, output_mean, reason_mean)
    return per * total_runs, per, input_mean, output_mean + reason_mean


# ── Budget-constrained solver ───────────────────────────────────────────────

def solve_budget(model: str, budget: float, instances: int,
                 arms: int = 2, input_tokens: float = 0,
                 output_tokens: float = 0, reasoning_tokens: float = 0,
                 pilot_csv: Optional[Path] = None) -> int:
    """Maximum runs-per-instance that fit within `budget`."""
    if input_tokens <= 0:
        input_mean, output_mean, reason_mean = pilot_averages(pilot_csv)
    else:
        input_mean, output_mean, reason_mean = input_tokens, output_tokens, reasoning_tokens

    per = cost_per_run(model, input_mean, output_mean, reason_mean)
    total_runs_allowed = int(budget / per)
    runs_per_instance = total_runs_allowed // (instances * arms)
    return max(1, runs_per_instance)


# ── Opportunistic actuals from trajectory logs ──────────────────────────────
# These refine the pricing-table estimate when real runs exist on disk; the
# estimator never *depends* on them. If the logs are gone, every consumer falls
# back to the token-pricing path above.

def measured_costs_from_logs(model: str,
                             logs_dir: Optional[Path] = None,
                             batch_id: str = "") -> dict[str, list[tuple[str, float]]]:
    """Scan trajectory logs for this model and return litellm-reported actual
    costs, grouped by arm: {arm: [(instance_id, instance_cost), ...]}.

    Reads ``info.model_stats.instance_cost`` from each ``*.traj.json`` under
    ``logs/<model_dir>/<arm>/<instance>/``. Best-effort and side-effect-free:
    a missing directory, unreadable file, or absent cost field is skipped, so
    the caller always gets a (possibly empty) dict.

    When ``batch_id`` is given, only trajectories whose manifest ``batch_id``
    field matches are included.
    """
    base = (logs_dir or paths.LOGS_DIR) / model_dir(normalize_model(model))
    out: dict[str, list[tuple[str, float]]] = defaultdict(list)
    if not base.is_dir():
        return {}
    for arm in ARMS:
        # Match both layouts: logs/<model>/<arm>/<inst>/*.traj.json and the
        # named-batch logs/<model>/<batch>/<arm>/<inst>/*.traj.json.
        for traj in sorted(base.glob(f"**/{arm}/*/*.traj.json")):
            if batch_id and batch_of(traj) != batch_id:
                continue
            try:
                d = json.loads(traj.read_text())
                cost = d.get("info", {}).get("model_stats", {}).get("instance_cost")
            except (OSError, ValueError, AttributeError):
                continue
            if isinstance(cost, (int, float)) and cost > 0:
                out[arm].append((traj.parent.name, float(cost)))
    return dict(out)


# ── Output formatting ───────────────────────────────────────────────────────

def print_estimate(model: str, instances: int, runs_per_instance: int,
                   arms: int = 2, input_tokens: float = 0,
                   output_tokens: float = 0, reasoning_tokens: float = 0,
                   budget: float = 0, pilot_csv: Optional[Path] = None) -> None:
    total, per, input_mean, output_mean = total_cost(
        model, instances, runs_per_instance, arms,
        input_tokens, output_tokens, reasoning_tokens, pilot_csv,
    )
    total_runs = instances * arms * runs_per_instance
    in_price, out_price, cache_price = lookup_pricing(model)

    print(f"Model:         {model}")
    print(f"Pricing:       ${in_price:.2f} / M input, ${out_price:.2f} / M output"
          f"  (cache reads: ${cache_price:.3f} / M)")
    print(f"Configuration: {instances} instances × {arms} arms × {runs_per_instance} runs")
    print(f"               = {total_runs} total runs")
    print(f"Tokens/run:    ~{input_mean:,.0f} input + ~{output_mean:,.0f} output"
          f"  (mean from pilot)")
    print(f"Cost/run:      ${per:.4f}")
    print(f"───────────────")
    print(f"Total:         ${total:.2f}")
    if budget:
        pct = total / budget * 100
        print(f"               ({pct:.1f}% of ${budget:.0f} budget)")


def print_per_run(model: str, input_tokens: float = 0, output_tokens: float = 0,
                  reasoning_tokens: float = 0, pilot_csv: Optional[Path] = None) -> None:
    if input_tokens <= 0:
        input_mean, output_mean, reason_mean = pilot_averages(pilot_csv)
    else:
        input_mean, output_mean, reason_mean = input_tokens, output_tokens, reasoning_tokens

    per = cost_per_run(model, input_mean, output_mean, reason_mean)
    in_price, out_price, cache_price = lookup_pricing(model)

    print(f"Model:       {model}")
    print(f"Pricing:     ${in_price:.2f} / M input, ${out_price:.2f} / M output")
    print(f"Tokens/run:  ~{input_mean:,.0f} input + ~{(output_mean + reason_mean):,.0f} output")
    print(f"Cost/run:    ${per:.4f}")


def print_budget(model: str, budget: float, instances: int, arms: int = 2,
                 input_tokens: float = 0, output_tokens: float = 0,
                 reasoning_tokens: float = 0, pilot_csv: Optional[Path] = None) -> None:
    runs_per = solve_budget(model, budget, instances, arms,
                            input_tokens, output_tokens, reasoning_tokens, pilot_csv)
    total_runs = instances * arms * runs_per
    _, per, input_mean, output_mean = total_cost(
        model, instances, runs_per, arms,
        input_tokens, output_tokens, reasoning_tokens, pilot_csv,
    )
    actual_cost = per * total_runs
    in_price, out_price, cache_price = lookup_pricing(model)

    print(f"Model:         {model}")
    print(f"Pricing:       ${in_price:.2f} / M input, ${out_price:.2f} / M output")
    print(f"Budget:        ${budget:.2f}")
    print(f"Tokens/run:    ~{input_mean:,.0f} input + ~{output_mean:,.0f} output")
    print(f"Cost/run:      ${per:.4f}")
    print(f"───────────────")
    print(f"Max runs/inst: {runs_per}  ({instances} instances × {arms} arms × {runs_per} = {total_runs} total)")
    print(f"Total cost:    ${actual_cost:.2f}  ({actual_cost/budget*100:.0f}% of budget)")
    print(f"Headroom:      ${budget - actual_cost:.2f}")


def print_compare(model_a: str, model_b: str, instances: int, runs_per_instance: int,
                  arms: int = 2, input_tokens: float = 0, output_tokens: float = 0,
                  reasoning_tokens: float = 0, pilot_csv: Optional[Path] = None) -> None:
    print(f"Configuration: {instances} instances × {arms} arms × {runs_per_instance} runs")
    print()
    for model in (model_a, model_b):
        total, per, input_mean, output_mean = total_cost(
            model, instances, runs_per_instance, arms,
            input_tokens, output_tokens, reasoning_tokens, pilot_csv,
        )
        total_runs = instances * arms * runs_per_instance
        in_price, out_price, cache_price = lookup_pricing(model)
        ratio = lookup_pricing(model_a)[0] / in_price if model != model_a else 1.0
        print(f"  {model}")
        print(f"    ${in_price:.2f} / M input, ${out_price:.2f} / M output")
        print(f"    ${per:.4f}/run  →  ${total:.2f} total ({total_runs} runs)")
        if model != model_a:
            mult = total / (total_cost(model_a, instances, runs_per_instance, arms,
                                       input_tokens, output_tokens, reasoning_tokens, pilot_csv)[0])
            print(f"    ({mult:.1f}× vs {model_a})")
        print()


def print_recompute(model: str, instances: int, runs_per_instance: int,
                    arms: int = 2, input_tokens: float = 0, output_tokens: float = 0,
                    reasoning_tokens: float = 0, budget: float = 0,
                    pilot_csv: Optional[Path] = None,
                    logs_dir: Optional[Path] = None,
                    batch_id: str = "") -> None:
    """Project total cost, refining the pricing-table estimate with real
    per-arm ``instance_cost`` values from the logs where they exist.

    The pricing estimate is always shown as the baseline (it needs no logs).
    Each arm's projection then uses its measured mean if any runs are on disk,
    falling back to the estimate otherwise — so removing the logs degrades
    gracefully to the pure estimate rather than breaking.
    """
    total_runs = instances * arms * runs_per_instance
    runs_per_arm = instances * runs_per_instance
    est_per_run = cost_per_run(model, *(
        (input_tokens, output_tokens, reasoning_tokens) if input_tokens > 0
        else pilot_averages(pilot_csv)))

    print(f"Cost recompute — {model}")
    print(f"Configuration: {instances} instances × {arms} arms × {runs_per_instance} runs"
          f" = {total_runs} total runs")
    print()
    print("Baseline (pricing-table estimate, no logs needed):")
    print(f"  Cost/run:  ${est_per_run:.4f}")
    print(f"  Total:     ${est_per_run * total_runs:.2f}")
    print()

    measured = measured_costs_from_logs(model, logs_dir, batch_id=batch_id)
    if not measured:
        print("Measured from logs: none found for this model — using baseline above.")
        if budget:
            print(f"\n({est_per_run * total_runs / budget * 100:.0f}% of ${budget:.0f} budget)")
        return

    print("Measured from logs (opportunistic):")
    arm_means: dict[str, float] = {}
    for arm in ARMS:
        rows = measured.get(arm, [])
        if not rows:
            continue
        costs = [c for _, c in rows]
        mean = sum(costs) / len(costs)
        arm_means[arm] = mean
        insts = sorted({iid for iid, _ in rows})
        print(f"  {arm:10s} n={len(costs)}  mean ${mean:.4f}/run"
              f"  (range ${min(costs):.4f}–${max(costs):.4f}; instances: {', '.join(insts)})")
    print()

    # Blended projection: measured arm mean where available, estimate otherwise.
    print("Blended projection (measured arms where available, estimate elsewhere):")
    blended_total = 0.0
    arm_names = list(ARMS[:arms]) if arms <= len(ARMS) else [f"arm{i}" for i in range(arms)]
    for arm in arm_names:
        per = arm_means.get(arm, est_per_run)
        src = "measured" if arm in arm_means else "estimate"
        sub = per * runs_per_arm
        blended_total += sub
        print(f"  {arm:10s} {runs_per_arm} runs × ${per:.4f} = ${sub:7.2f}   ({src})")
    print("  ───────────────")
    print(f"  Total:     ${blended_total:.2f}")
    if budget:
        print(f"             ({blended_total / budget * 100:.0f}% of ${budget:.0f} budget)")
    print()
    print("Note: measured means cover only the instances run so far; instances not")
    print("yet measured fall back to the token-pricing estimate. Difficulty varies")
    print("by instance, so the blend tightens as more instances are run.")


# ── Main ────────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("model", nargs="?", default=None,
                        help="litellm model identifier (e.g. deepseek/deepseek-v4-flash)")
    parser.add_argument("--instances", type=int, default=3,
                        help="Number of SWE-bench instances (default: 3)")
    parser.add_argument("--runs", type=int, default=10,
                        help="Repetitions per instance per arm (default: 10)")
    parser.add_argument("--arms", type=int, default=2,
                        help="Number of experimental arms (default: 2)")
    parser.add_argument("--budget", type=float, default=None,
                        help="Dollar budget ceiling (enables budget mode)")
    parser.add_argument("--per-run", action="store_true",
                        help="Print single-run cost only")
    parser.add_argument("--input-tokens", type=int, default=0,
                        help="Override per-run input token estimate")
    parser.add_argument("--output-tokens", type=int, default=0,
                        help="Override per-run output token estimate")
    parser.add_argument("--reasoning-tokens", type=int, default=0,
                        help="Override per-run reasoning token estimate")
    parser.add_argument("--pilot-csv", type=Path, default=None,
                        help="CSV to read token averages from "
                             "(default: latest results/runs/<ts>/runs_with_success.csv)")
    parser.add_argument("--compare", type=str, default=None,
                        help="Second model to compare against")
    parser.add_argument("--from-logs", action="store_true",
                        help="Refine the estimate with actual per-arm instance_cost "
                             "from trajectory logs (opportunistic; falls back to the "
                             "pricing estimate when logs are absent)")
    parser.add_argument("--logs-dir", type=Path, default=None,
                        help="Override the logs root scanned by --from-logs "
                             "(default: experiment/logs)")
    parser.add_argument("--batch", default="", metavar="BATCH_ID",
                        help="Filter --from-logs to trajectories whose manifest "
                             "batch_id matches (default: all runs)")
    parser.add_argument("--list-models", action="store_true",
                        help="List all models with known pricing and exit")
    args = parser.parse_args()

    if args.list_models:
        print(f"{'Model':50s}  {'Input $/M':>10s}  {'Output $/M':>10s}  {'Cache $/M':>10s}")
        print("-" * 86)
        for m, (i, o, c) in sorted(PRICING.items()):
            print(f"{m:50s}  ${i:8.2f}  ${o:9.2f}  ${c:8.3f}")
        return

    if not args.model:
        parser.error("model is required unless using --list-models")

    if args.from_logs:
        print_recompute(args.model, args.instances, args.runs, arms=args.arms,
                        input_tokens=args.input_tokens, output_tokens=args.output_tokens,
                        reasoning_tokens=args.reasoning_tokens,
                        budget=args.budget or 0, pilot_csv=args.pilot_csv,
                        logs_dir=args.logs_dir, batch_id=args.batch)
        return
    if args.per_run:
        print_per_run(args.model, args.input_tokens, args.output_tokens,
                      args.reasoning_tokens, args.pilot_csv)
    elif args.compare:
        print_compare(args.model, args.compare, args.instances, args.runs,
                      args.arms, args.input_tokens, args.output_tokens,
                      args.reasoning_tokens, args.pilot_csv)
    elif args.budget is not None:
        print_budget(args.model, args.budget, args.instances, args.arms,
                     args.input_tokens, args.output_tokens, args.reasoning_tokens,
                     args.pilot_csv)
    else:
        print_estimate(args.model, args.instances, args.runs, arms=args.arms,
                       input_tokens=args.input_tokens, output_tokens=args.output_tokens,
                       reasoning_tokens=args.reasoning_tokens,
                       pilot_csv=args.pilot_csv, budget=args.budget or 0)

    if not args.input_tokens and not args.pilot_csv:
        print("\nNote: token estimates are from DeepSeek pilot data. Other providers")
        print("may produce different token counts for the same task (different tokenizers).")


if __name__ == "__main__":
    main()
