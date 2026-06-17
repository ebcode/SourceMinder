#!/bin/bash
# Run both arms of the qi experiment on a single SWE-bench Lite instance.
# Usage: bash experiment/compare.sh <instance_id>
# Example: bash experiment/compare.sh django__django-11099
#
# Prerequisites:
#   - Docker image already pulled (see experiment/docker_images.txt)
#   - code-index.db already built (run experiment/index_instance.sh first)
#   - DEEPSEEK_API_KEY set in environment
set -euo pipefail

# -----------------------------------------------------------------------------
# [LEGACY/RETIRED] This was the original single-instance A/B runner (Lite era,
# current.db-symlink delivery). It is superseded by run_experiment.py /
# run_pilot.py + the analysis/ pipeline. Kept for reference only.
# Set COMPARE_SH_FORCE=1 to run it anyway.
# -----------------------------------------------------------------------------
if [[ "${COMPARE_SH_FORCE:-0}" != "1" ]]; then
    echo "compare.sh is retired. Use:" >&2
    echo "  python3 experiment/run_pilot.py --arm <control|treatment> --instance <id>" >&2
    echo "  python3 experiment/run_experiment.py --instances-file experiment/verified_instance_ids.txt --reps N" >&2
    echo "(set COMPARE_SH_FORCE=1 to override.)" >&2
    exit 2
fi

INSTANCE_ID="${1:?Usage: $0 <instance_id>}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DB_PATH="$REPO_ROOT/experiment/dbs/${INSTANCE_ID}.db"

if [ ! -f "$DB_PATH" ]; then
    echo "ERROR: DB not found: $DB_PATH"
    echo "Run first: bash experiment/index_instance.sh $INSTANCE_ID"
    exit 1
fi

IMAGE="swebench/sweb.eval.x86_64.${INSTANCE_ID//__/_1776_}:latest"
if ! docker image inspect "$IMAGE" > /dev/null 2>&1; then
    echo "ERROR: Docker image not found: $IMAGE"
    echo "Pull it: docker pull $IMAGE"
    exit 1
fi

CMD="./experiment/.venv/bin/mini-extra swebench-single"
ARGS="--subset lite --split test -i $INSTANCE_ID -m deepseek/deepseek-v4-flash -l 0.4 --exit-immediately -y"

echo "=== $INSTANCE_ID ==="
echo ""

# Symlink the correct DB for treatment arm
ln -sf "$DB_PATH" "$REPO_ROOT/experiment/dbs/current.db"

# Control arm
echo "--- CONTROL ---"
timeout 300 $CMD $ARGS \
    -c swebench.yaml \
    -c experiment/config/control.yaml \
    2>&1 | tail -3
cp "$HOME/.config/mini-swe-agent/last_swebench_single_run.traj.json" \
   "$REPO_ROOT/experiment/logs/${INSTANCE_ID}_control.traj.json"
echo ""

# Treatment arm
echo "--- TREATMENT ---"
timeout 300 $CMD $ARGS \
    -c swebench.yaml \
    -c experiment/config/treatment.yaml \
    2>&1 | tail -3
cp "$HOME/.config/mini-swe-agent/last_swebench_single_run.traj.json" \
   "$REPO_ROOT/experiment/logs/${INSTANCE_ID}_treatment.traj.json"
echo ""

# Quick stats
echo "--- TOKEN COUNTS ---"
./experiment/.venv/bin/python3 -c "
import json
for arm in ['control', 'treatment']:
    path = '$REPO_ROOT/experiment/logs/${INSTANCE_ID}_' + arm + '.traj.json'
    try:
        with open(path) as f:
            traj = json.load(f)
        tp = tc = pk = turns = qi = 0
        for m in traj.get('messages', []):
            if m.get('role') == 'assistant':
                u = m.get('extra', {}).get('response', {}).get('usage', {})
                tp += u.get('prompt_tokens', 0)
                tc += u.get('completion_tokens', 0)
                pk = max(pk, u.get('prompt_tokens', 0))
                turns += 1
                for a in m.get('extra', {}).get('actions', []):
                    cmd = a.get('command', '').strip()
                    if 'qi ' in cmd or cmd.startswith('qi '):
                        qi += 1
        st = traj.get('info', {}).get('exit_status', '?')
        print(f'{arm:10s} {st:15s} {turns:2d}t  {tp:>8,}p  {pk:>6,}pk  qi={qi}')
    except:
        print(f'{arm:10s} ERROR reading trajectory')
"
echo ""
echo "Logs: experiment/logs/${INSTANCE_ID}_*.traj.json"
