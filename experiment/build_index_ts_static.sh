#!/bin/bash
# Build a statically-linked index-ts binary for use inside SWE-bench Docker containers.
# Must be run AFTER a successful `make` (which produces the .o files).
# Result: build/index-ts-static (fully static, no shared library dependencies)
set -euo pipefail

cd "$(dirname "$0")/.."

# Collect all .o files needed for index-ts (mirrors the Makefile's index-ts rule)
SHARED_OBJS=$(ls shared/*.o 2>/dev/null)
TS_OBJS="typescript/index-ts.o typescript/ts_language.o"
GRAMMAR_OBJS="tree-sitter-typescript/typescript/src/parser.o tree-sitter-typescript/typescript/src/scanner.o"

for obj in $TS_OBJS $GRAMMAR_OBJS; do
    if [ ! -f "$obj" ]; then
        echo "Error: $obj not found. Run './configure --enable-all && make' first."
        exit 1
    fi
done

# Static libraries (Ubuntu/Debian paths)
SQLITE3_A="/usr/lib/x86_64-linux-gnu/libsqlite3.a"
TREE_SITTER_A="/usr/local/lib/libtree-sitter.a"

for lib in "$SQLITE3_A" "$TREE_SITTER_A"; do
    if [ ! -f "$lib" ]; then
        echo "Error: $lib not found."
        echo "Install with: apt-get install libsqlite3-dev"
        echo "tree-sitter should be built from source in tree-sitter/lib/"
        exit 1
    fi
done

echo "Building static index-ts..."
gcc -O2 -Wall -o build/index-ts-static \
    $TS_OBJS $GRAMMAR_OBJS $SHARED_OBJS \
    "$SQLITE3_A" "$TREE_SITTER_A" \
    -lm -lpthread -ldl \
    -static

echo "Verifying..."
LDD_OUT=$(ldd build/index-ts-static 2>&1) || true
if echo "$LDD_OUT" | grep -q "not a dynamic executable"; then
    echo "OK: build/index-ts-static is fully static ($(du -h build/index-ts-static | cut -f1))"
else
    echo "ERROR: build/index-ts-static has dynamic dependencies:"
    echo "$LDD_OUT"
    exit 1
fi
