#!/usr/bin/env bash
# Run the full analysis pipeline for one named batch.
#
# Usage:
#   bash experiment/scripts/run_pipeline.sh <batch_id> [--no-charts]
#
# Steps: analyze_trajectories → evaluate_patches → merge_results →
#        analyze_stats → compare_models
#
# All outputs land under experiment/results/runs/<batch_id>/.
# Pass --no-charts to skip matplotlib rendering (useful in headless envs).
set -euo pipefail

BATCH="${1:?Usage: $0 <batch_id> [--no-charts]}"
shift
CHARTS_FLAG=""
for arg in "$@"; do
    [[ "$arg" == "--no-charts" ]] && CHARTS_FLAG="--no-charts"
done

EXPERIMENT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PYTHON="$EXPERIMENT_DIR/.venv/bin/python3"
if [[ ! -x "$PYTHON" ]]; then
    PYTHON="$(command -v python3)"
fi

echo "=== Batch: $BATCH ==="
echo "=== Output: $EXPERIMENT_DIR/results/runs/$BATCH/ ==="
echo ""

echo "=== analyze_trajectories ==="
"$PYTHON" "$EXPERIMENT_DIR/analysis/analyze_trajectories.py" \
    --logs "$EXPERIMENT_DIR/logs" --batch "$BATCH"

echo ""
echo "=== evaluate_patches ==="
"$PYTHON" "$EXPERIMENT_DIR/analysis/evaluate_patches.py" \
    --logs "$EXPERIMENT_DIR/logs" --batch "$BATCH" --workers 2 --max-workers 3

echo ""
echo "=== merge_results ==="
"$PYTHON" "$EXPERIMENT_DIR/analysis/merge_results.py" \
    --batch "$BATCH"

echo ""
echo "=== analyze_stats ==="
"$PYTHON" "$EXPERIMENT_DIR/analysis/analyze_stats.py" \
    --batch "$BATCH" $CHARTS_FLAG

echo ""
echo "=== compare_models ==="
"$PYTHON" "$EXPERIMENT_DIR/analysis/compare_models.py" \
    --batch "$BATCH" $CHARTS_FLAG

echo ""
echo "Done: $EXPERIMENT_DIR/results/runs/$BATCH/"
