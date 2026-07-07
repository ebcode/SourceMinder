#!/bin/bash
# Build a statically-linked qi binary for use inside SWE-bench Docker containers.
# Must be run AFTER a successful `make` (which produces the .o files).
# Result: build/qi-static (fully static, no shared library dependencies)
set -euo pipefail

cd "$(dirname "$0")/.."

# Collect all .o files needed for qi
SHARED_OBJS=$(ls shared/*.o 2>/dev/null)
QUERY_OBJ="query-index.o"

if [ ! -f "$QUERY_OBJ" ]; then
    echo "Error: $QUERY_OBJ not found. Run 'make' first."
    exit 1
fi

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

echo "Building static qi..."
gcc -O2 -Wall -o build/qi-static \
    $QUERY_OBJ $SHARED_OBJS \
    "$SQLITE3_A" "$TREE_SITTER_A" \
    -lm -lpthread -ldl \
    -static

echo "Verifying..."
# ldd exits 1 for static binaries; capture output separately to avoid pipefail issues
LDD_OUT=$(ldd build/qi-static 2>&1) || true
if echo "$LDD_OUT" | grep -q "not a dynamic executable"; then
    echo "OK: build/qi-static is fully static ($(du -h build/qi-static | cut -f1))"
else
    echo "ERROR: build/qi-static has dynamic dependencies:"
    echo "$LDD_OUT"
    exit 1
fi
