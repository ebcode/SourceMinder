# Troubleshooting

## Common Issues

**Database locked:**
- Another indexer is writing to the database
- Wait or kill zombie processes: `killall index-c index-ts`
- If persists, delete `code-index.db`, (and `code-index.db-*` if present) and re-index

**No files being indexed:**
- Check file extensions: `cat <language>/config/file-extensions.txt`
- Check ignore list: `cat <language>/config/ignore_files.txt`
- Run with `--verbose` to see preflight checks

**Daemon not updating:**
- Check if running: `ps aux | grep index-c`
- Restart: `killall index-c && ./index-c ./src &`
- Use `--once` mode if daemon is problematic

**Too many results:**
- Add context filter: `-i func var`
- Add file filter: `-f .c` or `-f src/%`
- Exclude noise: `-x noise`
- Use definition filter: `--def`
- Use `--limit` and `--limit-per-file` to see samples first

**No results found:**
- Try with wildcards: `qi '*symbol*'`
- Verify file indexed: `qi '*' -f filename.c --limit 1`
- Check if symbol is filtered (qi will output a notice for words in stopwords.txt)

**Missing symbols:**
- Symbol might be filtered (stopword, keyword, or too short)
- Try wildcards: `qi *symbol*`
- Check keywords: `cat <language>/config/<language>-keywords.txt`

## Build Issues

**`undefined reference to ts_language_c`:**
- Run `./configure` first, then `make clean && make`

**`No such file: shared/config/stopwords.txt`:**
- Config files missing
- Run `sudo make install` or verify local config files exist

**Warning: `ENABLE_C` redefined:**
- Run `./configure` again, then `make clean && make`

## Debugging

```bash
# Check version
qi --version
./index-c --version

# Verbose output
./index-c ./src --verbose --once

# Test with minimal example
echo 'int test() { return 42; }' > /tmp/test.c
./index-c /tmp/test.c
qi test
```
