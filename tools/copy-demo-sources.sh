#!/usr/bin/env bash
# Copy every indexed file into the web demo sources folder, preserving
# directory structure.  The file list comes from `qi % --files` (header and
# footer lines are filtered out; only ./-prefixed paths are used).
#
# Usage:
#   tools/copy-demo-sources.sh [dest-dir]              # runs `qi % --files` itself
#   qi % --files | tools/copy-demo-sources.sh - [dest-dir]   # '-' reads the list from stdin
#
# dest-dir defaults to ./html/sources/sourceminder
#
# Run from the repo root so the relative paths resolve.
set -euo pipefail

FROM_STDIN=0
if [ "${1:-}" = "-" ]; then
    FROM_STDIN=1
    shift
fi
DEST="${1:-./html/sources/sourceminder}"

if [ "$FROM_STDIN" = "1" ]; then
    list_cmd() { cat; }
else
    if ! command -v qi &>/dev/null; then
        echo "ERROR: qi not found on PATH (or pipe a list in with '-')" >&2
        exit 1
    fi
    list_cmd() { qi % --files; }
fi

mkdir -p "$DEST"

copied=0
missing=0
while IFS= read -r file; do
    [[ "$file" != ./* ]] && continue
    if [ -f "$file" ]; then
        cp --parents "$file" "$DEST/"
        copied=$((copied + 1))
    else
        echo "WARNING: indexed but not found on disk: $file" >&2
        missing=$((missing + 1))
    fi
done < <(list_cmd)

echo "Copied $copied file(s) to $DEST ($missing missing)"
