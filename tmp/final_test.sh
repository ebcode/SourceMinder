#!/bin/bash
echo "=== Test: Search across multiple directories with patterns ==="
./query-index "%" -f "go/%" "c/%" --limit 10 -x comment string

echo -e "\n=== Test: Boundary matching prevents false positives ==="
# Should ONLY match ./c/ not ./proto/c/ due to %/ prefix
./query-index "%" -f "c/%.c" --limit 5

echo -e "\n=== Test: Explicit path matching (no boundary) ==="
./query-index "%" -f "./proto/c/%.c" --limit 5
