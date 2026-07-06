#!/usr/bin/env python3
"""Wall-time stats for a SWE-bench **Pro** rep batch, from the run ledger.

The ledger (``logs/run_pro_ledger.jsonl``) is the only source with timing: each
``.traj.json`` carries ``instance_cost`` + ``api_calls`` but no wall clock. The
ledger records ``started_at`` / ``finished_at`` per rep attempt, keyed by
``batch_id`` / ``arm`` / ``rep``.

Reports per-rep durations, per-arm aggregates, and the true batch wall clock
(max finished - min started, which accounts for parallel workers). Retries are
de-duplicated to the latest attempt per (arm, rep) so durations aren't
double-counted, but the wall-clock span uses every attempt.

Usage:
  experiment/.venv_pro/bin/python experiment/analysis/wall_time.py \\
      --batch pro_pilot_ansible_ds_v4_flash_v2 \\
      [--dir experiment/results/pro_runs/<batch>]   # also writes wall_time.csv

With ``--dir`` it writes one row per rep to ``<dir>/wall_time.csv``; the
aggregates always go to stdout.
"""
from __future__ import annotations

import argparse
import collections
import csv
import json
import sys
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # -> experiment/
from lib import paths  # noqa: E402

DEFAULT_LEDGER = paths.LOGS_DIR / "run_pro_ledger.jsonl"

CSV_FIELDS = ["batch_id", "model", "arm", "rep", "instance_id",
              "started_at", "finished_at", "duration_sec", "exit_status", "ok"]


def parse(ts: str) -> datetime:
    return datetime.fromisoformat(ts)


def fmt(secs: float) -> str:
    m, s = divmod(int(secs), 60)
    return f"{m}m{s:02d}s" if m else f"{s}s"


def load_batch(ledger: Path, batch: str) -> list[dict]:
    """Ledger rows for ``batch`` that have both timestamps."""
    rows = []
    for line in ledger.read_text().splitlines():
        try:
            d = json.loads(line)
        except json.JSONDecodeError:
            continue
        if d.get("batch_id") != batch:
            continue
        if not (d.get("started_at") and d.get("finished_at")):
            continue
        rows.append(d)
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--batch", required=True,
                    help="Ledger batch_id to report (run_pro_reps.py --batch-id)")
    ap.add_argument("--dir", type=Path, default=None,
                    help="If given, also write wall_time.csv here "
                         "(e.g. results/pro_runs/<batch>)")
    ap.add_argument("--ledger", type=Path, default=DEFAULT_LEDGER,
                    help=f"Run ledger path (default: {DEFAULT_LEDGER})")
    args = ap.parse_args()

    if not args.ledger.is_file():
        print(f"WARNING: ledger not found: {args.ledger} — skipping wall-time",
              file=sys.stderr)
        return 0

    rows = load_batch(args.ledger, args.batch)
    if not rows:
        # Non-fatal: older/renamed batches may not be in the ledger, and this is
        # a supplementary metric — don't abort a pipeline over it.
        print(f"WARNING: no ledger entries for batch {args.batch!r} — "
              "skipping wall-time", file=sys.stderr)
        return 0

    # Keep the latest attempt per (arm, rep) so retries don't double-count
    # durations (ledger is append-order, so last wins).
    latest: dict[tuple[str, str], dict] = {}
    for d in rows:
        latest[(d["arm"], d["rep"])] = d
    attempts = sorted(rows, key=lambda d: d["started_at"])
    final = sorted(latest.values(), key=lambda d: (d["arm"], d["rep"]))

    def dur(d: dict) -> float:
        return (parse(d["finished_at"]) - parse(d["started_at"])).total_seconds()

    print(f"batch: {args.batch}   "
          f"({len(final)} reps, {len(attempts)} attempts incl. retries)\n")
    print(f"{'arm':<16} {'rep':>6} {'dur':>8} {'exit':>12} {'ok':>4}")
    per_arm: dict[str, list[float]] = collections.defaultdict(list)
    csv_rows = []
    for d in final:
        sec = dur(d)
        per_arm[d["arm"]].append(sec)
        print(f"{d['arm']:<16} {d['rep']:>6} {fmt(sec):>8} "
              f"{str(d.get('exit_status')):>12} {str(d.get('ok')):>4}")
        csv_rows.append({
            "batch_id": d.get("batch_id", ""),
            "model": d.get("model", ""),
            "arm": d["arm"],
            "rep": d["rep"],
            "instance_id": d.get("instance_id", ""),
            "started_at": d["started_at"],
            "finished_at": d["finished_at"],
            "duration_sec": round(sec, 3),
            "exit_status": d.get("exit_status"),
            "ok": d.get("ok"),
        })

    print(f"\n{'arm':<16} {'reps':>5} {'sum':>9} {'mean':>8} {'min':>7} {'max':>7}")
    for arm, ds in sorted(per_arm.items()):
        print(f"{arm:<16} {len(ds):>5} {fmt(sum(ds)):>9} "
              f"{fmt(sum(ds) / len(ds)):>8} {fmt(min(ds)):>7} {fmt(max(ds)):>7}")

    # Sum of durations (compute) vs true wall clock (parallel workers overlap).
    all_dur = sum(dur(d) for d in final)
    starts = [parse(d["started_at"]) for d in attempts]
    ends = [parse(d["finished_at"]) for d in attempts]
    wall = (max(ends) - min(starts)).total_seconds()
    print(f"\nsum of rep durations (compute): {fmt(all_dur)}")
    print(f"batch wall clock (elapsed):     {fmt(wall)}   "
          f"[{min(starts):%H:%M:%S} -> {max(ends):%H:%M:%S}]")
    if wall > 0:
        print(f"parallelism factor:             {all_dur / wall:.1f}x")

    if args.dir is not None:
        args.dir.mkdir(parents=True, exist_ok=True)
        out_path = args.dir / "wall_time.csv"
        with out_path.open("w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=CSV_FIELDS)
            w.writeheader()
            w.writerows(csv_rows)
        print(f"\nWrote {len(csv_rows)} rep(s) -> {out_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
