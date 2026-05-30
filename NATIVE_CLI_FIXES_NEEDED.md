# Native CLI Fixes Needed

Bugs discovered during web bridge review that also exist in the native
CLI (`query-index.c`). These are deferred from the web bridge branch.

## 7. `build_toc_query()` buffer truncation unchecked

- **File:** `shared/toc.c:54-174`
- **Symptom:** TOC SQL is built into a static 8 KB buffer via `snprintf()` with
  manual `pos`/`remaining` tracking.  If the file-pattern, context, or symbol
  list causes the SQL to exceed the buffer, `snprintf` silently truncates the
  output and the remaining `pos`/`remaining` arithmetic drifts.  The caller
  receives a truncated (and likely invalid) SQL string with no error indication.
- **Fix:** After each `snprintf` call, check `if (written >= remaining)` and
  return NULL (or set an error).  Alternatively, replace the static buffer
  with a `SqlQueryBuilder` as done in the web bridge.

## 8. `add_entry_to_file()` uninitialized slot after failed strdup

- **File:** `shared/toc.c:189-206`
- **Symptom:** When creating a new file group, `realloc(*files, ...)` succeeds
  and `*files = temp` is assigned, but `try_strdup_ctx(filepath)` fails.  The
  function returns -1 without incrementing `*file_count`, leaving the enlarged
  array with an uninitialized trailing slot (`entries`, `entry_count`,
  `entry_capacity` are garbage).  Callers iterate only `*file_count` entries,
  so the garbage isn't accessed, but the slot is an integrity hazard.
- **Fix:** `memset(target_file, 0, sizeof(*target_file))` before the `strdup`
  call.  Fixed in web: `shared-web/toc-web.c:255` (`memset(f, 0, sizeof(*f))`).

## 9. Filepath construction without separator normalization

- **File:** `shared/toc.c:445`
- **Symptom:** `snprintf(filepath, "%s%s", directory, filename)` blindly
  concatenates directory and filename without checking whether directory
  already ends with `/`.  The indexer guarantees a trailing slash, but the
  formatter should not depend on that implicit contract — a bare directory
  value collapses different files into the same group key.
- **Fix:** Insert `"/"` between directory and filename if directory is non-empty
  and doesn't already end with `/`.  Fixed in web: `shared-web/toc-web.c:418-424`.

## 10. File grouping is O(files × rows)

- **File:** `shared/toc.c:182-186`
- **Symptom:** `add_entry_to_file()` linearly scans all previously-created file
  groups for every row.  With N rows spread across F files, this is O(N·F)
  string comparisons.  Since TOC rows arrive ordered by `(directory, filename,
  line)`, consecutive rows almost always share the same file.
- **Fix:** Cache the index of the last-matched file and check it first before
  falling back to linear scan.  This makes the common case O(1).  Fixed in web:
  `shared-web/toc-web.c:239-242` (`last_idx` cache).

## 11. Import deduplication is O(n²)

- **File:** `shared/toc.c:301-314`
- **Symptom:** `print_imports()` deduplicates import symbols by linearly
  scanning all previously-collected imports with `strcmp()`.  While imports
  per file are typically small (tens), generated or dependency-heavy files
  can produce hundreds, making the quadratic scan noticeable.
- **Fix:** Keep the dedup array sorted and use `bsearch()` for O(log n) lookup;
  insert new entries in sorted position.  Fixed in web:
  `shared-web/toc-web.c:346-375`.

## 12. Per-dot `printf` loop

- **File:** `shared/toc.c:281-283`
- **Symptom:** `print_section()` emits padding dots one `printf(".")` call at
  a time.  With up to ~65 dots per line, this means dozens of `printf` calls
  per TOC entry just for formatting.
- **Fix:** Use a single `printf("  %s %.*s %d\n", symbol, dots, dots_string, line)`
  with a pre-built dot string and the `%.*s` precision specifier.  Fixed in
  web: `shared-web/toc-web.c:325-329`.
