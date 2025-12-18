#!/bin/bash
# Fix all extract_node_text calls in TypeScript indexer

FILE="typescript/ts_language.c"

# Replace all variations systematically
sed -i 's/extract_node_text(source_code, \([a-z_]*\), \([a-z_]*\), sizeof(\2))/safe_extract_node_text(source_code, \1, \2, sizeof(\2), filename)/g' "$FILE"

# Handle special cases with different variable names
sed -i 's/extract_node_text(source_code, \([a-z_]*\), \([a-z_]*\), buf_size)/safe_extract_node_text(source_code, \1, \2, buf_size, filename)/g' "$FILE"

echo "Replaced all extract_node_text calls with safe_extract_node_text"
echo "Remaining unsafe calls:"
grep -c "extract_node_text(source_code" "$FILE" | grep -v "safe_"
