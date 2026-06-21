#!/usr/bin/env bash
# Run the full analysis pipeline for one SWE-bench Pro rep batch.
#
# Usage:
#   bash experiment/scripts/run_pro_pipeline.sh <batch> [options]
#
# Options:
#   --run-prefix P   only include runs whose run_id starts with P (scopes a rep
#                    batch, e.g. 'oldprompt_'); matches run_pro_reps.py --run-id-prefix
#   --workers N      parallel Docker eval workers (default: 5)
#   --no-charts      skip matplotlib rendering (headless envs)
#
# Locating the input logs (default: logs/pro_pilot/). For runs produced with
# run_pro_reps.py --batch-id, the logs live under logs/<model>/<batch-id>/; point
# this pipeline at them with either:
#   --logs DIR              explicit logs root, OR
#   --batch-id ID [--model M]  reconstruct logs/<model>/<batch-id>/ (M default:
#                              deepseek/deepseek-v4-flash), using the same
#                              model_dir() slug run_pro_reps.py uses.
#
# Steps: analyze_pro_trajectories -> evaluate_pro_patches -> merge_results
#        -> analyze_pro_stats
#
# All outputs land under experiment/results/pro_runs/<batch>/.
# Pro analog of run_pipeline.sh; the Verified pipeline + scripts are untouched.
set -euo pipefail

BATCH="${1:?Usage: $0 <batch> [--logs DIR | --batch-id ID [--model M]] [--run-prefix P] [--workers N] [--no-charts]}"
shift

RUN_PREFIX=""
WORKERS="5"
CHARTS_FLAG=""
LOGS_OVERRIDE=""
BATCH_ID=""
MODEL="deepseek/deepseek-v4-flash"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --run-prefix) RUN_PREFIX="$2"; shift 2 ;;
        --workers)    WORKERS="$2"; shift 2 ;;
        --no-charts)  CHARTS_FLAG="--no-charts"; shift ;;
        --logs)       LOGS_OVERRIDE="$2"; shift 2 ;;
        --batch-id)   BATCH_ID="$2"; shift 2 ;;
        --model)      MODEL="$2"; shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done

EXPERIMENT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# The Pro stages need the Pro venv (datasets, docker SDK, pandas).
PYTHON="$EXPERIMENT_DIR/.venv_pro/bin/python"
if [[ ! -x "$PYTHON" ]]; then
    echo "ERROR: Pro venv not found at $PYTHON" >&2
    exit 1
fi

OUT_DIR="$EXPERIMENT_DIR/results/pro_runs/$BATCH"
ANALYSIS="$EXPERIMENT_DIR/analysis"

# Resolve the input logs root. Precedence: --logs > --batch-id > default.
if [[ -n "$LOGS_OVERRIDE" ]]; then
    LOGS_DIR="$LOGS_OVERRIDE"
elif [[ -n "$BATCH_ID" ]]; then
    # Reconstruct logs/<model_dir>/<batch-id>/ using run_pro_reps.py's own slug.
    LOGS_DIR="$("$PYTHON" -c "import sys; sys.path.insert(0,'$EXPERIMENT_DIR'); \
from lib.model import model_dir; \
print('$EXPERIMENT_DIR/logs/'+model_dir('$MODEL')+'/$BATCH_ID')")"
else
    LOGS_DIR="$EXPERIMENT_DIR/logs/pro_pilot"
fi

if [[ ! -d "$LOGS_DIR" ]]; then
    echo "ERROR: logs dir not found: $LOGS_DIR" >&2
    exit 1
fi

PREFIX_ARGS=()
[[ -n "$RUN_PREFIX" ]] && PREFIX_ARGS=(--run-prefix "$RUN_PREFIX")

echo "=== Pro batch: $BATCH ${RUN_PREFIX:+(run-prefix '$RUN_PREFIX')} ==="
echo "=== Logs:   $LOGS_DIR/ ==="
echo "=== Output: $OUT_DIR/ ==="
echo ""

echo "=== analyze_pro_trajectories ==="
"$PYTHON" "$ANALYSIS/analyze_pro_trajectories.py" \
    --logs "$LOGS_DIR" --dir "$OUT_DIR" "${PREFIX_ARGS[@]}"

echo ""
echo "=== evaluate_pro_patches ==="
"$PYTHON" "$ANALYSIS/evaluate_pro_patches.py" \
    --logs "$LOGS_DIR" --dir "$OUT_DIR" --workers "$WORKERS" "${PREFIX_ARGS[@]}"

echo ""
echo "=== merge_results (reused from the Verified pipeline) ==="
"$PYTHON" "$ANALYSIS/merge_results.py" --dir "$OUT_DIR"

echo ""
echo "=== analyze_pro_stats ==="
"$PYTHON" "$ANALYSIS/analyze_pro_stats.py" --dir "$OUT_DIR" $CHARTS_FLAG

echo ""
echo "Done: $OUT_DIR/"
