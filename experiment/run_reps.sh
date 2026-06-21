#!/usr/bin/env bash
# Run N reps of a single SWE-bench Pro instance on both arms (control + treatment)
# at the upstream-faithful temperature (0.0), to average out provider-side
# nondeterminism. Each rep gets its own --run-id (so trajectories/preds never
# overwrite) AND its own console log file (so the live model stream for each run
# is reviewable after the fact instead of interleaved in one terminal).
#
# Usage:  bash experiment/run_reps.sh [N_REPS]   (default 5)
#
# Per-run console logs land in:  logs/pro_pilot/reps_<timestamp>/<arm>_repNN.log
# The terminal itself shows only a one-line-per-run progress board.
set -uo pipefail

cd "$(dirname "$0")"

INSTANCE=instance_qutebrowser__qutebrowser-f91ace96223cac8161c16dd061907e138fe85111-v059c6fdc75567943479b23ebca7c07b5e9a7f34c
ARMS=(swebp_control swebp_treatment)
N=${1:-5}
PY=.venv_pro/bin/python

LOGDIR="logs/pro_pilot/reps_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$LOGDIR"

echo "instance: $INSTANCE"
echo "arms:     ${ARMS[*]}"
echo "reps:     $N  (temp 0.0, upstream-faithful)"
echo "logs:     $LOGDIR/<arm>_repNN.log"
echo "========================================================================"

for arm in "${ARMS[@]}"; do
  for i in $(seq -w 1 "$N"); do
    rid="rep${i}"
    log="$LOGDIR/${arm}_${rid}.log"
    printf '%-26s %s  starting  -> %s\n' "$arm $rid" "$(date '+%H:%M:%S')" "$log"
    # Each run's full live model stream goes to its own log file, not the terminal.
    "$PY" run_pro_one.py --instance "$INSTANCE" --arm "$arm" --run-id "$rid" \
      > "$log" 2>&1
    rc=$?
    # Surface the runner's own summary line (exit_status + patch_chars) on the board.
    summary=$(grep -E '^exit_status=' "$log" | tail -1)
    printf '%-26s %s  done rc=%s  %s\n' "$arm $rid" "$(date '+%H:%M:%S')" "$rc" "$summary"
  done
done

echo "========================================================================"
echo "ALL REPS DONE ($(date '+%H:%M:%S')) — per-run logs in $LOGDIR"
