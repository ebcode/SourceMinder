#!/bin/bash
# Build a statically-linked index-python binary for use inside SWE-bench Docker containers.
# Must be run AFTER a successful `make` (which produces the .o files).
# Result: build/index-python-static (fully static, no shared library dependencies)
set -euo pipefail

cd "$(dirname "$0")/.."

# Collect all .o files needed for index-python (mirrors the Makefile's index-python rule)
SHARED_OBJS=$(ls shared/*.o 2>/dev/null)
PYTHON_OBJS="python/index-python.o python/python_language.o"
GRAMMAR_OBJS="tree-sitter-python/src/parser.o tree-sitter-python/src/scanner.o"

for obj in $PYTHON_OBJS $GRAMMAR_OBJS; do
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

echo "Building static index-python..."
gcc -O2 -Wall -o build/index-python-static \
    $PYTHON_OBJS $GRAMMAR_OBJS $SHARED_OBJS \
    "$SQLITE3_A" "$TREE_SITTER_A" \
    -lm -lpthread -ldl \
    -static

echo "Verifying..."
# ldd exits 1 for static binaries; capture output separately to avoid pipefail issues
LDD_OUT=$(ldd build/index-python-static 2>&1) || true
if echo "$LDD_OUT" | grep -q "not a dynamic executable"; then
    echo "OK: build/index-python-static is fully static ($(du -h build/index-python-static | cut -f1))"
else
    echo "ERROR: build/index-python-static has dynamic dependencies:"
    echo "$LDD_OUT"
    exit 1
fi
