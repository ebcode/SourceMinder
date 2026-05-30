#!/usr/bin/env bash
#
# browser-snapshot.sh -- snapshot one project's index into a browser-safe DB.
#
# This script lives in html/sources/.  Given --project <name>, it reads the
# project's index from one folder deep:
#
#     html/sources/<name>/code-index.db
#
# and writes a non-WAL, compacted snapshot one folder up, alongside the other
# browser DBs the manifest serves:
#
#     html/code-index-<name>.browser.db
#
# <name> is the same folder name qi-web routes -e/-C source fetches to
# (manifest sourceBase "./sources/<name>/"), so one identifier drives both the
# source tree and the snapshot.  Index the project *in place* under
# html/sources/<name>/ so the paths it records resolve against that sourceBase.
#
# Why non-WAL: the browser loads the DB via sqlite3_deserialize(), which cannot
# open the -wal sidecar a WAL-mode database depends on -- a raw copy of a WAL DB
# throws SQLITE_CANTOPEN on the first query (in the browser and in the harness).
# VACUUM INTO folds the live WAL state in and writes a self-contained
# rollback-journal database; it also compacts, shrinking the download.
#
# Usage:
#     ./browser-snapshot.sh --project jinja
#
set -euo pipefail

SOURCES_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"   # html/sources
HTML_DIR="$(dirname "$SOURCES_DIR")"                          # html

usage() {
    echo "usage: $(basename "$0") --project <name>" >&2
    echo "  reads  html/sources/<name>/code-index.db" >&2
    echo "  writes html/code-index-<name>.browser.db  (non-WAL, compacted)" >&2
}

project=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --project)   project="${2:-}"; shift 2 ;;
        --project=*) project="${1#*=}"; shift ;;
        -h|--help)   usage; exit 0 ;;
        *)           echo "error: unknown argument '$1'" >&2; usage; exit 2 ;;
    esac
done

if [ -z "$project" ]; then
    echo "error: --project <name> is required" >&2
    usage
    exit 2
fi

# Guard against path traversal / nested names: a project is a single folder.
case "$project" in
    */*|.|..) echo "error: --project must be a single folder name, got '$project'" >&2; exit 2 ;;
esac

if ! command -v sqlite3 >/dev/null 2>&1; then
    echo "error: sqlite3 not found on PATH" >&2
    exit 1
fi

src="$SOURCES_DIR/$project/code-index.db"
dest="$HTML_DIR/code-index-$project.browser.db"

if [ ! -f "$src" ]; then
    echo "error: no index at $src" >&2
    echo "  index the project in place first (its code-index.db must live there)" >&2
    exit 1
fi

# VACUUM INTO refuses to overwrite, so clear any prior snapshot first.
rm -f "$dest"
sqlite3 "$src" "VACUUM INTO '$dest'"

mode="$(sqlite3 "$dest" 'PRAGMA journal_mode;')"
if [ "$mode" = "wal" ]; then
    echo "error: '$dest' is still WAL after VACUUM INTO -- not browser-safe" >&2
    exit 1
fi

bytes="$(wc -c < "$dest" | tr -d ' ')"
echo "wrote $dest"
echo "  journal_mode=$mode, ${bytes} bytes"
echo "  manifest entry: \"dbUrl\": \"./code-index-$project.browser.db\", \"sizeBytes\": $bytes"
