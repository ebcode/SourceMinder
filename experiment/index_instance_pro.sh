#!/bin/bash
# Pre-index one or more SWE-bench *Pro* instances' source code.
#
# Pro differs from Verified in three ways this script handles:
#   1. Images live at jefzda/sweap-images:<tag> (read from data/pool_pro.csv,
#      not derived from the instance id like Verified's swebench/sweb.eval.*).
#   2. The repo is checked out at /app, not /testbed.
#   3. Pro is multi-language: the right index-<lang>-static binary is chosen per
#      instance from the pool's repo_language column (js uses the TS indexer,
#      which also parses .js/.jsx/.mjs).
#
# Usage:
#   bash experiment/index_instance_pro.sh <instance_id>
#   bash experiment/index_instance_pro.sh --file experiment/pro_pool_ids.txt
#
# Prerequisites:
#   1. data/pool_pro.csv exists (run: python3 experiment/prep_pro_dataset.py)
#   2. The instance's image is pulled:  docker pull <image-from-pool>
#   3. Static indexers built:  build/index-{python,go,ts}-static
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
POOL="${REPO_ROOT}/experiment/data/pool_pro.csv"

# repo_language -> "<static-binary-basename> <config-subdir>".
# js shares the TypeScript indexer (its file_extensions.txt covers .js/.jsx/.mjs).
lang_to_indexer() {
    case "$1" in
        python) echo "index-python-static python" ;;
        go)     echo "index-go-static go" ;;
        ts|js)  echo "index-ts-static typescript" ;;
        *)      return 1 ;;
    esac
}

# Look up a field for an instance in the pool CSV.
# Columns: instance_id,repo,repo_language,n_files,image,base_commit
pool_field() {
    local instance_id="$1" col="$2"
    awk -F, -v id="$instance_id" -v c="$col" '
        $1 == id { print $c; found=1; exit }
        END { if (!found) exit 1 }
    ' "$POOL"
}

index_one() {
    local INSTANCE_ID="$1"
    local DB_PATH="${REPO_ROOT}/experiment/dbs/${INSTANCE_ID}.db"

    if [ -f "$DB_PATH" ]; then
        echo "SKIP $INSTANCE_ID (already exists: $(du -h "$DB_PATH" | cut -f1))"
        return 0
    fi

    local LANG IMAGE
    LANG="$(pool_field "$INSTANCE_ID" 3)" || { echo "ERROR: $INSTANCE_ID not in $POOL"; return 1; }
    IMAGE="$(pool_field "$INSTANCE_ID" 5)" || { echo "ERROR: no image for $INSTANCE_ID"; return 1; }

    local INDEXER CONFIG_SUBDIR
    read -r INDEXER CONFIG_SUBDIR < <(lang_to_indexer "$LANG") \
        || { echo "ERROR: unsupported repo_language '$LANG' for $INSTANCE_ID"; return 1; }

    local BIN="${REPO_ROOT}/build/${INDEXER}"
    if [ ! -x "$BIN" ]; then
        echo "ERROR: indexer not found: $BIN (build it first)"; return 1
    fi

    echo "=== Indexing $INSTANCE_ID  [lang=$LANG  indexer=$INDEXER] ==="

    if ! docker image inspect "$IMAGE" > /dev/null 2>&1; then
        echo "ERROR: image not found locally: $IMAGE"
        echo "Pull it first: docker pull $IMAGE"
        return 1
    fi

    local CONTAINER_NAME="sm-index-pro-${INSTANCE_ID//[^a-zA-Z0-9_.-]/_}"
    # The repo is self-contained; index offline for determinism (verified: no
    # network needed for source parsing).
    docker run -d --name "$CONTAINER_NAME" --network none \
        --entrypoint sleep "$IMAGE" 3600 > /dev/null

    cleanup() { docker rm -f "$CONTAINER_NAME" > /dev/null 2>&1 || true; }
    trap cleanup RETURN

    # Copy the indexer binary in.
    docker cp "$BIN" "$CONTAINER_NAME:/usr/local/bin/${INDEXER%-static}"
    docker exec "$CONTAINER_NAME" chmod +x "/usr/local/bin/${INDEXER%-static}"

    # Config is resolved relative to the indexer's cwd (/app): each binary looks
    # for <lang>/config and shared/config. The indexing container is disposable,
    # so writing these into /app does not affect the agent's container.
    docker exec "$CONTAINER_NAME" mkdir -p "/app/${CONFIG_SUBDIR}/config" /app/shared/config
    docker cp "${REPO_ROOT}/${CONFIG_SUBDIR}/config/." "$CONTAINER_NAME:/app/${CONFIG_SUBDIR}/config/"
    docker cp "${REPO_ROOT}/shared/config/." "$CONTAINER_NAME:/app/shared/config/"

    echo "Running ${INDEXER%-static} on /app..."
    docker exec "$CONTAINER_NAME" bash -c "cd /app && ${INDEXER%-static} /app --once --silent"

    mkdir -p "${REPO_ROOT}/experiment/dbs"
    docker cp "$CONTAINER_NAME:/app/code-index.db" "$DB_PATH"
    echo "Saved $(du -h "$DB_PATH" | cut -f1)"

    cleanup
    trap - RETURN
    echo "=== Done: $INSTANCE_ID ==="
}

# ── Entry point ──────────────────────────────────────────────────────────────

if [ ! -f "$POOL" ]; then
    echo "ERROR: pool not found: $POOL"
    echo "Generate it: python3 experiment/prep_pro_dataset.py"
    exit 1
fi

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
