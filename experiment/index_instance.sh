#!/bin/bash
# Pre-index one or more SWE-bench instances' source code.
# Usage:
#   bash experiment/index_instance.sh <instance_id>
#   bash experiment/index_instance.sh --file experiment/n18_verified_instance_ids.txt
#
# Prerequisites:
#   1. Docker image must already be pulled locally.
#      Pull all images: see experiment/docker_images.txt
#      Pull one:
#        docker pull swebench/sweb.eval.x86_64.django_1776_django-11099:latest
#   2. Build static binaries:
#        ./configure --enable-all && make
#        bash experiment/build_qi_static.sh
#        bash experiment/build_index_python_static.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

index_one() {
    local INSTANCE_ID="$1"
    local IMAGE_NAME="swebench/sweb.eval.x86_64.${INSTANCE_ID//__/_1776_}:latest"
    local CONTAINER_NAME="sm-index-${INSTANCE_ID}"
    local DB_PATH="${REPO_ROOT}/experiment/dbs/${INSTANCE_ID}.db"

    if [ -f "$DB_PATH" ]; then
        echo "SKIP $INSTANCE_ID (already exists: $(du -h "$DB_PATH" | cut -f1))"
        return 0
    fi

    echo "=== Indexing $INSTANCE_ID ==="

    # Verify image exists locally
    if ! docker image inspect "$IMAGE_NAME" > /dev/null 2>&1; then
        echo "ERROR: Image not found locally: $IMAGE_NAME"
        echo "Pull it first: docker pull $IMAGE_NAME"
        echo "All images listed in: experiment/docker_images.txt"
        return 1
    fi

    # Start container in background
    echo "Starting container..."
    docker run -d --name "$CONTAINER_NAME" "$IMAGE_NAME" sleep 3600 > /dev/null

    cleanup() {
        docker rm -f "$CONTAINER_NAME" > /dev/null 2>&1 || true
    }
    trap cleanup RETURN

    # Copy static indexer binary and config files into container
    echo "Copying indexer and config..."
    docker cp "$REPO_ROOT/build/index-python-static" "$CONTAINER_NAME:/usr/local/bin/index-python"
    docker exec "$CONTAINER_NAME" chmod +x /usr/local/bin/index-python

    # Config files must be at paths relative to /testbed (the indexer's cwd)
    docker exec "$CONTAINER_NAME" mkdir -p /testbed/shared/config /testbed/python/config
    docker cp "$REPO_ROOT/python/config/." "$CONTAINER_NAME:/testbed/python/config/"
    docker cp "$REPO_ROOT/shared/config/." "$CONTAINER_NAME:/testbed/shared/config/"

    # Run the indexer
    echo "Running index-python on /testbed..."
    docker exec "$CONTAINER_NAME" bash -c 'cd /testbed && index-python /testbed --once --silent'

    # Copy the database out
    mkdir -p "$REPO_ROOT/experiment/dbs"
    docker cp "$CONTAINER_NAME:/testbed/code-index.db" "$DB_PATH"
    echo "Saved $(du -h "$DB_PATH" | cut -f1)"

    # Clean up container now (not on script exit)
    cleanup
    trap - RETURN
    echo "=== Done: $INSTANCE_ID ==="
}

# ── Entry point ──────────────────────────────────────────────────────────────

if [ "${1:-}" = "--file" ] || [ "${1:-}" = "-f" ]; then
    INSTANCE_FILE="${2:?Usage: $0 --file <file>}"
    if [ ! -f "$INSTANCE_FILE" ]; then
        echo "ERROR: file not found: $INSTANCE_FILE"
        exit 1
    fi
    count=0
    while IFS= read -r line; do
        [[ -z "$line" ]] && continue
        [[ "$line" =~ ^[[:space:]]*# ]] && continue
        instance_id="${line%% *}"
        index_one "$instance_id" || true
        ((count++)) || true
    done < "$INSTANCE_FILE"
    echo "=== Indexed $count instances ==="
    exit 0
fi

INSTANCE_ID="${1:?Usage: $0 <instance_id> or $0 --file <instances_file>}"
index_one "$INSTANCE_ID"
