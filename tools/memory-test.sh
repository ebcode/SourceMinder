#!/bin/bash
# Test 1: OLD binary (from /usr/local/bin) - Run with massif
  rm -f code-index.db
  valgrind --tool=massif --massif-out-file=massif-old.out \
      /usr/local/bin/index-c ./shared ./c --once --verbose

  # Test 2: NEW binary (current directory) - Run with massif
  rm -f code-index.db
  valgrind --tool=massif --massif-out-file=massif-new.out \
      ./index-c ./shared ./c --once --verbose

  # Compare the results
  echo "=== OLD Binary Memory Usage ==="
  ms_print massif-old.out | head -60

  echo ""
  echo "=== NEW Binary Memory Usage ==="
  ms_print massif-new.out | head -60

  # Look for FileList specifically in the old version
  echo ""
  echo "=== OLD: FileList allocation (should be ~97 MB) ==="
  ms_print massif-old.out | grep -A3 "file_walker\|FileList" | head -20

  # Look for file_list in the new version
  echo ""
  echo "=== NEW: FileList allocation (should be much smaller) ==="
  ms_print massif-new.out | grep -A3 "file_list\|add_file_to_list" | head -20
