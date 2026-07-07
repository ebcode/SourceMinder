#!/bin/sh
# fetch-grammars.sh - fetch the seven tree-sitter grammar repos at pinned commits.
#
# SourceMinder compiles each grammar's generated C source (src/parser.c,
# src/scanner.c) directly into the indexer binaries, so grammar versions are
# part of the build. The pins below are the exact commits the indexers and
# golden tests were validated against; bumping one requires re-validating
# that language's indexer.
#
# Usage: ./fetch-grammars.sh [--force]
#   Clones each grammar into ./tree-sitter-<lang> (shallow, single commit).
#   Existing checkouts already at the pinned commit are left alone.
#   Existing checkouts at a different commit are reported and skipped;
#   --force re-fetches them to the pinned commit (local changes are lost).

set -eu

FORCE=0
case "${1:-}" in
    "") ;;
    --force) FORCE=1 ;;
    *) echo "Usage: $0 [--force]" >&2; exit 1 ;;
esac

# name|url|pinned commit
#
# All pins ship generated src/parser.c. Perl's main branch does not, so its
# pin is on upstream's `release` branch: commit 67887d64 is the deploy of
# main commit 8917c6e9 (the validated grammar), with parser.c included.
GRAMMARS='
tree-sitter-c|https://github.com/tree-sitter/tree-sitter-c.git|ae19b676b13bdcc13b7665397e6d9b14975473dd
tree-sitter-go|https://github.com/tree-sitter/tree-sitter-go.git|2346a3ab1bb3857b48b29d779a1ef9799a248cd7
tree-sitter-perl|https://github.com/tree-sitter-perl/tree-sitter-perl.git|67887d64e479493888dba5a4daae4be5a84e8218
tree-sitter-php|https://github.com/tree-sitter/tree-sitter-php.git|3f2465c217d0a966d41e584b42d75522f2a3149e
tree-sitter-python|https://github.com/tree-sitter/tree-sitter-python.git|26855eabccb19c6abf499fbc5b8dc7cc9ab8bc64
tree-sitter-rust|https://github.com/tree-sitter/tree-sitter-rust.git|77a3747266f4d621d0757825e6b11edcbf991ca5
tree-sitter-typescript|https://github.com/tree-sitter/tree-sitter-typescript.git|75b3874edb2dc714fb1fd77a32013d0f8699989f
'

fetch_pinned() {
    dir=$1; url=$2; sha=$3
    rm -rf "$dir"
    git init -q "$dir"
    # GitHub allows fetching an arbitrary commit; depth 1 keeps it small.
    git -C "$dir" fetch -q --depth 1 "$url" "$sha"
    git -C "$dir" checkout -q "$sha"
    echo "  $dir: fetched $sha"
}

failures=0
while IFS='|' read -r dir url sha; do
    [ -n "$dir" ] || continue
    if [ -e "$dir" ] && [ ! -d "$dir/.git" ]; then
        echo "  $dir: exists but is not a git checkout -- move it aside and re-run" >&2
        exit 1
    fi
    if [ -d "$dir/.git" ]; then
        head=$(git -C "$dir" rev-parse HEAD)
        if [ "$head" = "$sha" ]; then
            echo "  $dir: already at pinned commit"
            continue
        fi
        if [ "$FORCE" -eq 0 ]; then
            echo "  $dir: at $head, pinned $sha -- skipped (use --force to re-fetch)" >&2
            failures=1
            continue
        fi
    fi
    fetch_pinned "$dir" "$url" "$sha"
done <<EOF
$GRAMMARS
EOF

if [ "$failures" -ne 0 ]; then
    echo "Some grammars are not at their pinned commits (see above)." >&2
    exit 1
fi
echo "Done. Next: ./configure --enable-all && make"
