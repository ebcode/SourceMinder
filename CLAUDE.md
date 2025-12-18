- when running a test involving qi, always search for the word "test": $ qi test --limit 5
- the code index database is named code-index.db
- prefer the qi tool to manual sqlite3 queries when testing the index
- you can use the qi tool to query multiple words by separating them with a space: qi symbol1 symbol2
- always fix compiler warnings
- never 'fix later' -- especially compiler warnings
- when testing the indexers, bundle up individual, targeted commands to more inclusive commands. qi % -v --limit 10 (for example)
- when running qi % , use: qi % -x noise -- unless specifically searching for symbols in comments or strings
- ALWAYS ask the user to compile (run make) and re-index

## Error Handling
- In main(): use `try_strdup_ctx()`, check for NULL, use `goto cleanup` pattern
- In utilities/parsers: use `safe_strdup_ctx()` which calls exit(1) - fail fast on unrecoverable errors
- NEVER use bare `return 1` in main() - always `goto cleanup` to avoid leaks
- NEVER combine `count++` with fallible operations: check first, then increment
- See docs/ARCHITECTURE.md for full details
