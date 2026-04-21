# Performance & Best Practices

## Index Size & Performance

**Typical sizes: Project: index.db size, time to index**
- Small (100 files): ~10MB, ~2s
- Med   (1K files): ~100MB, ~10s
- Large (10K files): ~500MB, ~30s

**Optimization:**
- Use `--silent` for large projects
- Run indexers in parallel with WAL mode
- Use `--exclude-dir` for dependency folders
- Index only source directories (exclude tests, docs)
- Write and read code-index.db to/from a ram disk (--db-file /dev/shm/index.db)

## Query Performance

**Fast queries:**
- Exact matches: `qi getUserById`
- Prefix matches: `qi get*`
- With filters: `qi user -i func -f .c`

**Slower queries:**
- Wildcard everywhere: `qi '*user*'`
- No filters: `qi '*'`
- Complex AND queries

**Optimization:**
- Start specific, broaden if needed
- Use `--limit` and `--limit-per-file` while exploring
- Combine context and file filters
- Use `--def` to filter out usages

## Best Practices

**Indexing:**
1. Start small - index one directory first
2. Use daemon mode for active development
3. Exclude build artifacts in ignore list

**Querying:**
1. Start with `-x noise` to exclude comments/strings (and consider placing this line in ~/.smconfig)
2. Use `-e` and `-C` to see definitions and context
3. Use `--limit` while exploring
4. Two-step workflow: discover (--toc, -i func), then expand (-e)
5. Combine filters for precision

**Example workflow:**
```bash
# Step 1: Discover (fast, targeted)
qi handle* -i func --def --limit-per-file 2 --limit 10

# Step 2: See table of contents for selected file
qi -f c_language.c --toc

# Step 3: Expand (show code) for selected function
qi handle_do_statement -f c_language.c --def -e
```

## For Claude Code

```bash
# Code symbols - use qi
qi CursorManager
qi *manager* -i class

# Text/messages - use grep
grep "Error: connection failed" *.c

# Exclude noise for cleaner results
qi user -x noise

# Compact output (default) vs full names
qi user           # Shows: VAR, FUNC, etc.
qi user --full    # Shows: VARIABLE, FUNCTION, etc.
```
