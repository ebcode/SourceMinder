#!/bin/bash
echo "=== Test 1: No slash (backwards compatible) ==="
./query-index "test" -f "query-index.c" --limit 2

echo -e "\n=== Test 2: Explicit relative path ==="
./query-index "clue" -f "./go/go_language.c" --limit 2

echo -e "\n=== Test 3: Path without ./ prefix (boundary matching) ==="
./query-index "clue" -f "go/go_language.c" --limit 2

echo -e "\n=== Test 4: Wildcard filename ==="
./query-index "clue" -f "go/%" --limit 2

echo -e "\n=== Test 5: Directory only (trailing slash) ==="
./query-index "%" -f "go/" --limit 2

echo -e "\n=== Test 6: Multiple file patterns ==="
./query-index "%" -f "query-index.c" "go_language.c" --limit 5

echo -e "\n=== Test 7: Prevent false matches (go/ vs mygo/) ==="
# This should only match ./go/ not ./mygo/ (if it existed)
./query-index "%" -f "go/%" --limit 2
