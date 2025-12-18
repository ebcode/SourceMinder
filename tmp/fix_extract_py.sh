#!/bin/bash
# Fix all extract_node_text calls in Python indexer

FILE="python/python_language.c"

# First, remove the local extract_node_text function (lines 20-31)
sed -i '20,31s/.*/\/* Removed: Now using safe_extract_node_text() from shared\/string_utils.h *\//' "$FILE"
sed -i '21,31d' "$FILE"

# Python uses extract_node_text(node, source_code, buffer, size)
# Need to swap to safe_extract_node_text(source_code, node, buffer, size, filename)

sed -i 's/extract_node_text(\([a-z_]*\), source_code, \([a-z_]*\), sizeof(\2))/safe_extract_node_text(source_code, \1, \2, sizeof(\2), filename)/g' "$FILE"

echo "Replaced all extract_node_text calls with safe_extract_node_text"
echo "Remaining unsafe calls:"
grep "extract_node_text(" "$FILE" | grep -v "safe_" | wc -l
