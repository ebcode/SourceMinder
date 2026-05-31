- the code index database is named code-index.db
- prefer the qi tool to manual sqlite3 queries when testing the index
- always fix compiler warnings
- never 'fix later' -- especially compiler warnings
- ALWAYS ask the user to compile (run make) and re-index

## Error Handling
- In main(): use `try_strdup_ctx()`, check for NULL, use `goto cleanup` pattern
- In utilities/parsers: use `safe_strdup_ctx()` which calls exit(1) - fail fast on unrecoverable errors
- NEVER use bare `return 1` in main() - always `goto cleanup` to avoid leaks
- NEVER combine `count++` with fallible operations: check first, then increment
- See docs/ARCHITECTURE.md for full details
