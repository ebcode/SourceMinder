# QA Plan for Multi-Language Code Indexer

## Overview
Systematic testing plan to validate all indexers and query functionality before beta release.

## 1. Language-Specific Indexer Testing

### For Each Language (C, TypeScript, PHP, Go, Python)

#### 1.1 Function/Method Body Indexing
- [ ] Local variables inside functions are indexed
- [ ] Local variables inside methods are indexed
- [ ] String literals inside functions are indexed
- [ ] Comments inside functions are indexed
- [ ] Function calls inside functions are indexed
- [ ] Nested function calls are indexed correctly

**Test command:**
```bash
# Create test file with local variables
./index-{lang} ./tmp
./qi local_variable -f test-file
```

#### 1.2 Context Type Coverage
Test that all context types work for each language:
- [ ] `FUNC` - Function/method definitions indexed
- [ ] `VAR` - Variables indexed (top-level and local)
- [ ] `ARG` - Function parameters indexed
- [ ] `ARG` - Function call arguments indexed correctly
- [ ] `CLASS` - Classes/structs indexed
- [ ] `PROP` - Properties/fields indexed
- [ ] `IMP` - Imports indexed
- [ ] `EXP` - Exports indexed (where applicable)
- [ ] `TYPE` - Type definitions indexed
- [ ] `STR` - String literals indexed
- [ ] `COM` - Comments indexed
- [ ] `CALL` - Function/method calls indexed

**Test command:**
```bash
./qi % -f test-file -i func
./qi % -f test-file -i var
./qi % -f test-file -i arg
# ... etc for each context type
```

#### 1.3 No Duplicate Entries
- [ ] No duplicate ARG/VAR on same line for function parameters
- [ ] No duplicate ARG/VAR on same line for function call arguments
- [ ] No duplicate entries for any symbol on same line

**Test command:**
```bash
# Create file with parameter used in function call
sqlite3 code-index.db "SELECT line_number, symbol, context_type FROM code_index WHERE filename='test-file' AND symbol='param_name' ORDER BY line_number, context_type;"
# Should see only ONE entry per line
```

#### 1.4 Language-Specific Features

**C:**
- [ ] Struct members indexed
- [ ] Enum values indexed
- [ ] Typedef indexed
- [ ] Static functions indexed with modifier
- [ ] Preprocessor macros indexed

**TypeScript:**
- [ ] Interface members indexed
- [ ] Generics indexed
- [ ] Decorators indexed
- [ ] Arrow functions indexed
- [ ] Type aliases indexed
- [ ] Export statements indexed

**PHP:**
- [ ] Trait methods indexed
- [ ] Magic methods indexed
- [ ] Namespaces indexed correctly
- [ ] Property promotion indexed
- [ ] Abstract/final modifiers indexed
- [ ] Static properties/methods indexed

**Go:**
- [ ] Interface methods indexed
- [ ] Goroutines indexed (function calls)
- [ ] Defer statements indexed
- [ ] Channel operations indexed
- [ ] Struct embedding indexed

**Python:**
- [ ] Decorators indexed
- [ ] Class methods indexed
- [ ] Static methods indexed
- [ ] Property decorators indexed
- [ ] Async/await functions indexed
- [ ] List comprehensions indexed
- [ ] Lambda functions indexed

## 2. Query Tool (./qi) Testing

### 2.1 Pattern Matching
- [ ] Exact match works: `./qi exact_symbol`
- [ ] Wildcard prefix: `./qi get%`
- [ ] Wildcard suffix: `./qi %Manager`
- [ ] Wildcard both: `./qi %user%`
- [ ] Single character wildcard: `./qi get_ser`
- [ ] Pattern with special chars escaped: `./qi "\--flag"`

### 2.2 File Filtering (-f)
- [ ] By filename: `./qi symbol -f database.c`
- [ ] By extension: `./qi symbol -f .py`
- [ ] By directory: `./qi symbol -f src/`
- [ ] With wildcards: `./qi symbol -f shared/%.c`
- [ ] Multiple patterns: `./qi symbol -f .c .h`
- [ ] Non-existent extension shows warning with indexed extensions list

### 2.3 Context Filtering
- [ ] Include single: `./qi symbol -i func`
- [ ] Include multiple: `./qi symbol -i func var`
- [ ] Exclude single: `./qi symbol -x comment`
- [ ] Exclude multiple: `./qi symbol -x comment string`
- [ ] Noise shortcut: `./qi symbol -x noise` (excludes COM + STR)
- [ ] Include takes precedence over exclude
- [ ] Abbreviated context types: `./qi symbol -i func arg var`
- [ ] Full context types: `./qi symbol -i function argument variable`

### 2.4 Code Viewing
- [ ] Expand definitions: `./qi symbol -i func -e`
- [ ] Context before: `./qi symbol -B 3`
- [ ] Context after: `./qi symbol -A 3`
- [ ] Context both: `./qi symbol -C 5`
- [ ] Combine expand + context: `./qi symbol -e -C 3`
- [ ] Default context (no number): `./qi symbol -C`

### 2.5 Table of Contents (--toc)
- [ ] Show all definitions: `./qi % -f file.c --toc`
- [ ] Filter by function: `./qi % -f file.c --toc -i func`
- [ ] Filter by imports: `./qi % -f file.c --toc -i imp`
- [ ] Filter by types: `./qi % -f file.c --toc -i type`
- [ ] Works with extension: `./qi % -f .c --toc`
- [ ] Line ranges shown correctly
- [ ] Imports deduplicated and on one line

### 2.6 AND Refinement (--and)
- [ ] Two patterns same line: `./qi fprintf stderr --and`
- [ ] Multiple patterns same line: `./qi user session token --and`
- [ ] With range: `./qi malloc free --and 10`
- [ ] With context filter: `./qi pattern1 pattern2 --and -i func`
- [ ] With file filter: `./qi pattern1 pattern2 --and -f .c`

### 2.7 Definition vs Usage
- [ ] Only definitions: `./qi symbol --def`
- [ ] Only usages: `./qi symbol --usage`
- [ ] Short form -d 1: `./qi symbol -d 1`
- [ ] Short form -d 0: `./qi symbol -d 0`

### 2.8 Column Selection
- [ ] Default columns work
- [ ] Custom columns: `./qi symbol --columns line context symbol`
- [ ] With clue: `./qi symbol -c`
- [ ] Verbose (all columns): `./qi symbol -v`
- [ ] Full names: `./qi symbol --full`
- [ ] Compact names: `./qi symbol --compact`

### 2.9 Output Control
- [ ] Limit results: `./qi symbol --limit 10`
- [ ] Limit per file: `./qi symbol --limit-per-file 5`
- [ ] Files only: `./qi symbol --files`
- [ ] Debug mode: `./qi symbol --debug`

### 2.10 Type/Scope/Modifier Filtering
- [ ] Filter by type: `./qi % -i arg -t "int"`
- [ ] Filter by scope: `./qi % -i func -s public`
- [ ] Filter by modifier: `./qi % -i func -m static`
- [ ] Filter by parent: `./qi % -p ClassName`
- [ ] Filter by namespace: `./qi % -ns App\\Models`
- [ ] Filter by clue: `./qi % -c functionName`

## 3. Cross-Language Testing

### 3.1 Mixed-Language Projects
- [ ] Index multiple languages in same project
- [ ] Query across languages: `./qi symbol` (no file filter)
- [ ] Query specific language: `./qi symbol -f .py`
- [ ] Concurrent indexing: Run multiple indexers in parallel
  ```bash
  ./index-ts ./src &
  ./index-c ./lib &
  ./index-php ./app &
  wait
  ```

### 3.2 Symbol Name Conflicts
- [ ] Same symbol name in different languages indexed separately
- [ ] Same symbol name in different files shown correctly
- [ ] File paths disambiguate symbols

## 4. Edge Cases & Error Handling

### 4.1 Empty/Minimal Files
- [ ] Empty file indexed without errors
- [ ] File with only comments
- [ ] File with only imports
- [ ] Single-line file

### 4.2 Large Files
- [ ] File with 10k+ lines
- [ ] File with 1k+ functions
- [ ] Very long lines (>2000 chars)
- [ ] Deeply nested structures (10+ levels)

### 4.3 Special Characters
- [ ] Unicode in identifiers (where language allows)
- [ ] Symbols with numbers: `var123`
- [ ] Symbols with underscores: `my_variable`
- [ ] Special naming (Python: `__init__`, PHP: `$_POST`)

### 4.4 File System
- [ ] Files with spaces in path
- [ ] Deep directory structures (10+ levels)
- [ ] Symlinks to files/directories
- [ ] Files outside indexed directory
- [ ] Non-existent database file shows helpful error

### 4.5 Database Operations
- [ ] Re-indexing same file updates entries
- [ ] Indexing new file adds entries
- [ ] Database file permissions
- [ ] WAL mode enabled (check for -wal file)
- [ ] Concurrent queries while indexing

## 5. Help Text & Documentation

### 5.1 Help Text Accuracy
- [ ] All examples in `./qi --help` actually work
- [ ] Python examples present (`.py` files)
- [ ] All supported languages mentioned
- [ ] Extension list in warnings is complete

### 5.2 Example Commands
Run every example from README.md and help text:
- [ ] Quick start examples work
- [ ] Common workflow examples work
- [ ] Pattern syntax examples work
- [ ] All command combinations work

## 6. Performance Testing

### 6.1 Indexing Performance
- [ ] Index 100 files completes in reasonable time
- [ ] Index 1000+ files completes
- [ ] Memory usage stays reasonable
- [ ] Progress indication works (if enabled)

### 6.2 Query Performance
- [ ] Query returns in <100ms for typical search
- [ ] Query with wildcards performs adequately
- [ ] Query with --and performs adequately
- [ ] Large result sets (1000+ matches) handled

## 7. Regression Testing

### 7.1 Known Fixed Bugs
- [ ] Function bodies are indexed (C, TS, PHP, Python)
- [ ] No duplicate ARG/VAR on same line (C, Python)
- [ ] Python shows ARG for function call arguments (not VAR)
- [ ] Python extension recognized in warnings
- [ ] Help text shows Python examples

## 8. Configuration Files

### 8.1 Data Files
For each language, verify:
- [ ] `config/file-extensions.txt` is correct
- [ ] `config/keywords.txt` filters properly
- [ ] `config/ignore_files.txt` works
- [ ] Shared `config/stopwords.txt` filters properly
- [ ] Shared `config/regex-patterns.txt` filters properly

### 8.2 User Config
- [ ] `~/.smconfig` is read
- [ ] `[qi]` section flags applied
- [ ] CLI flags override config file
- [ ] Invalid config handled gracefully

## 9. Error Messages & User Experience

### 9.1 Helpful Messages
- [ ] Searching filtered word shows which file contains it
- [ ] No results suggests partial match retry
- [ ] Unknown extension lists indexed extensions
- [ ] Pattern with 0 results tries `%pattern%` automatically
- [ ] File filter with 0 matches suggests alternatives

### 9.2 Daemon Mode (Indexers)
- [ ] Indexer runs in daemon mode by default
- [ ] `--quiet` suppresses output
- [ ] `--verbose` shows preflight checks
- [ ] Exit codes correct (0 = success, non-zero = error)
- [ ] Ctrl+C handled gracefully

## 10. Test Data Organization

### Suggested Test File Structure
```
scratch/
  sources/
    c/test-locals.c           # Local variables
    c/test-strings.c          # String literals
    c/test-duplicates.c       # Parameter usage
    typescript/test-locals.ts
    php/test-locals.php
    go/test-locals.go
    python/test-locals.py
```

## Testing Workflow

1. **Per-language smoke test**: Test basic indexing + querying
2. **Context type sweep**: Test all context types for one language
3. **Query tool sweep**: Test all query flags/options
4. **Edge case sweep**: Test edge cases systematically
5. **Cross-cutting concerns**: Test combinations
6. **Performance check**: Test with realistic codebase
7. **Documentation verification**: Run all examples

## Automated Testing Suggestions

Consider creating:
- Shell script that runs key test cases
- Expected output files to diff against
- Test database with known contents
- Bats tests for critical paths (following project convention)

## Sign-off Criteria

Before beta release, ensure:
- [ ] All 5 language indexers pass smoke tests
- [ ] All context types work in at least 2 languages
- [ ] Top 10 query patterns from help text work
- [ ] No duplicate bug reproductions
- [ ] Help text examples verified
- [ ] Performance acceptable on 100-file project
- [ ] At least 3 edge cases tested per category
