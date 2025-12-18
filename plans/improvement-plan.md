# Query-Index Tool: Improvement Plan

## Context

This document captures reflections and ideas from dog-fooding the `query-index` tool during actual development sessions.
The goal is to identify ways to make the tool more useful and compact, particularly for AI assistants and developers.

---

## What Makes query-index Better Than Grep/Glob

### 1. Context Filtering is a Game-Changer
- `-x comment string` to exclude noise is incredibly powerful
- With grep, you get tons of false positives from comments/strings
- Context types (VARIABLE vs STRING vs COMMENT) immediately tell you what you're looking at
- Semantic understanding without needing to read the code

### 2. Structured, Token-Efficient Output
- Table format is much cleaner than grep's raw output
- File grouping is automatic and helpful
- Direct line numbers ready for `Read`/`Edit` tools
- Reduces tokens needed for AI assistants to understand results

### 3. Semantic Understanding
- Seeing "filename" as VARIABLE, PROPERTY, ARGUMENT, STRING tells you HOW it's used
- This is information grep simply cannot provide
- Makes code navigation much faster

### 4. Integration-Friendly
- Relative paths work directly with Read/Edit tools
- No need to parse grep output or filter noise
- Perfect for tooling and automation

---

## Recently Completed Features ✅

### Directory/Path-Based Filtering (Session: 2025-10-31)
**Status:** ✅ **IMPLEMENTED**

Extended `-f/--file` flag to support directory + filename patterns:
```bash
qi "clue" -f "./go/go_language.c"     # Explicit relative path
qi "clue" -f "./go/%"                 # All files in ./go/
qi "clue" -f "go/%.c"                 # All .c files in any go/ directory
qi "%" -f "go/"                       # Directory-only (trailing slash = wildcard)
```

Implementation uses boundary matching (`%/`) to prevent false matches.

---

### Search Scope Feedback (Session: 2025-11-01)
**Status:** ✅ **IMPLEMENTED**

Shows file count in search scope:
```bash
qi "clue" -f "%.c"
# Output: Filtering by file: %.c (58 files)
```

Helps distinguish between:
- Pattern matched 0 files (pattern is wrong)
- Pattern matched files but found 0 symbols (search term is wrong)

---

### Type Filtering for Function Parameters (Session: 2025-11-01)
**Status:** ✅ **IMPLEMENTED**

Query parameters by their type:
```bash
qi "%" -i argument -t "FileFilterList *"  # All FileFilterList* parameters
qi "%" -i argument -t "const char *"      # All const char* parameters
qi "%" -i argument -t "%*"                # All pointer parameters
```

Useful for:
- Refactoring: Find all parameters using old type names
- Type hunting: Show me all `const char*` parameters
- Migration verification: Confirm no old types remain

---

### Context Lines (Session: 2025-10-31)
**Status:** ✅ **IMPLEMENTED**

Grep-style context lines with highlighting:
```bash
qi "cursor" -A 3          # Show 3 lines after match
qi "cursor" -B 3          # Show 3 lines before match
qi "cursor" -C 5          # Show 5 lines before and after match
```

Features:
- Line number prefixes (grep-style format)
- Highlights matching patterns with green background
- Eliminates 80%+ of follow-up Read commands
- Major token savings for AI assistants

---

### Compact Context Names (Session: 2025-10-31)
**Status:** ✅ **IMPLEMENTED**

Default output now uses abbreviated context types:
- `VAR` instead of `VARIABLE`
- `FUNC` instead of `FUNCTION`
- `PROP` instead of `PROPERTY`

Use `--full` flag to show full names when needed.

---

### Smart Filter Shortcuts (Session: 2025-10-31)
**Status:** ✅ **IMPLEMENTED**

`-x noise` expands to `-x comment string` automatically.

---

### AND Refinement / Same-Line Matching (Session: 2025-10-31)
**Status:** ✅ **IMPLEMENTED**

Find lines where ALL patterns co-occur:
```bash
qi fprintf stderr --and        # Both symbols on same line
qi malloc free --and           # Memory management patterns
qi user session token --and    # All three symbols together
```

---

### Extension Warning System (Session: 2025-11-01)
**Status:** ✅ **IMPLEMENTED**

Warns when searching for non-indexed file extensions:
```bash
qi "test" -f "%.md"
# Warning: Extension '.md' is not indexed.
# Note: This tool is designed for indexing source code, not prose or documentation files.
# Indexed extensions: .c .h .ts .tsx .js .jsx .php .phtml .go
```

Distinguishes between:
- **Prose/documentation files** (`.md`, `.txt`, `.log`) - shows design note
- **Source code files** (`.py`, `.rs`, `.java`) - suggests using appropriate indexer

---

### Lambda/Anonymous Function Support (Session: 2025-11-16)
**Status:** ✅ **IMPLEMENTED**

Added first-class lambda indexing across all languages:
```bash
qi % -i lambda -f lambdas.ts           # Find all lambdas
qi % -i lambda -e                      # Show full lambda code
qi % -i lambda -m async                # Find async lambdas
qi % -i func lambda                    # Functions and lambdas together
```

**Implementation:**
- New `CONTEXT_LAMBDA` context type
- Handlers for: `arrow_function`, `function_expression` (TypeScript/JS), `func_literal` (Go), `anonymous_function`/`arrow_function` (PHP), `lambda` (Python)
- Parameters properly indexed with types (e.g., `...args: number[]`)
- Nested lambdas supported (lambdas passed as function arguments)

**Critical bugs fixed:**
- TypeScript `handle_variable_declaration` wasn't calling `process_children()`, preventing lambdas in assignments
- TypeScript `handle_call_expression` wasn't traversing into arguments, missing nested lambdas
- All language handlers now properly extract parameters and traverse children

**Languages supported:** Go ✅, PHP ✅, TypeScript ✅, Python ✅, JavaScript ✅ (shares TypeScript parser)

---

### Dynamic TOC Context Types (Session: 2025-11-16)
**Status:** ✅ **IMPLEMENTED**

Replaced hardcoded context type lists with dynamic configuration:
```c
// In shared/toc.h
static const char * const TOC_ALLOWED_CONTEXTS[] = {
    "FILE", "CLASS", "FUNC", "ENUM", "TYPE", "IMP", NULL
};
```

**Benefits:**
- Single source of truth for TOC-allowed types
- Better error messages when using unsupported types with `--toc`
- Future-proof: update array to add/remove types (no code duplication)

**Error message improved:**
```bash
qi -f file.c --toc -i var
# Before: No definitions found matching the criteria.
# After:  --toc does not support all requested context types.
#         Allowed context types for --toc: CLASS, FUNC, ENUM, TYPE, IMP
#         To see all symbols in a file, use without --toc: qi % -f <file>
```

---

### Files-Only Output Mode (Session: Prior to 2025-11-16)
**Status:** ✅ **IMPLEMENTED**

Show only unique file paths (like `grep -l`):
```bash
qi lowercase_symbol --files
# Output:
# ./shared/database.c
# ./shared/database.h
# ./shared/parse_result.c
# ./query-index.c
```

Perfect for refactoring workflows:
- Quick scan of affected files
- Verify changes are complete
- See scope before making edits

---

## Active Pain Points / Missing Features

### 0. **Better "No Results" Error Messages**
**Priority:** 🔥 HIGH

Currently, "No results" provides no explanation:
```bash
qi "context" -f "query-index.c" -i func
# Output: No results
```

**Problem:** User has to guess what went wrong:
- Wrong file filter? (forgot the `./` prefix)
- Wrong context type?
- Symbol is filtered?
- File not indexed?

**Proposed solution:**
```bash
qi "context" -f "query-index.c" -i func
# Output:
# No results for "context" -f "query-index.c" -i func
#
# Diagnostics:
# ✗ No files matched pattern "query-index.c"
#   Indexed files: ./query-index.c (note the ./ prefix)
#   Try: -f "./query-index.c" or -f "%.c"
```

**Real session impact:** Caused confusion multiple times. Better errors would have saved significant time.

**Estimated effort:** ~50-75 lines (track and explain query failures)

---

### 1. **Symbol-Based Indexing Limitations**

Only finds indexed symbols, not arbitrary text:
- Strings are tokenized: "Usage: query-index" becomes separate words
- Can't find literal string contents
- Grep searches exact file contents as-is

This is by design but can be confusing.

---

### 2. **Scope-Based Search (Search Within a Symbol)**
**Priority:** 🔥 HIGH

**Problem:** Can't limit search to within a specific function or class. When debugging a large function, want to find all uses of a variable ONLY within that function.

```bash
# Current workaround (clunky):
qi "parent" -f typescript/ts_language.c --lines 1813-1920

# Desired:
qi "parent" -f typescript/ts_language.c --scope handle_assignment_expression
qi "%" -i var --scope "TextContainer"  # All vars within TextContainer class
```

**Use cases:**
- Debugging: Find variable uses within a specific function
- Refactoring: See how a symbol is used within a class
- Code review: Focus on specific function internals

**Real session impact (2025-11-30):** When debugging `handle_assignment_expression`, wanted to see all `parent` variable uses within just that function. Had to manually scan output or use line ranges.

**Estimated effort:** ~40-60 lines (filter by parent_symbol or use line ranges from function definitions)

---

### 3. **Suggest Grep Alternative for Non-Code Files**
**Priority:** ⚠️ LOW

When qi can't help (non-indexed files), suggest the grep equivalent:

```bash
qi "install" -f "Makefile"
# Output:
# Note: Makefile is not a source code file and is not indexed.
# Try: grep -n 'install' Makefile
```

**Benefits:**
- Teaches users when to use which tool
- Reduces frustration with "file not found" errors
- Provides immediate actionable alternative

**Estimated effort:** ~20-25 lines

---

### 4. **Can't Search by Function Signature / Parameters**
**Priority:** ⚠️ MEDIUM

Can find parameters, but not which functions have them:
```bash
# Want: Which FUNCTIONS have a file_filter parameter?
qi "file_filter" -i argument -f "query-index.c"
# Got: All arguments named file_filter ✅
# But no way to show parent functions
```

**Proposed solution:**
```bash
qi "%" -i function --has-param "file_filter"
qi "%" -i function --has-param "file_filter:FileFilterList"
```

**Estimated effort:** ~50-75 lines (use existing parent_symbol column)

---

### 5. **Line Number Range Filtering**
**Status:** ✅ **IMPLEMENTED**

Filter symbols by line number or range:
```bash
qi "%" -f "query-index.c" --lines 616        # Single line
qi "%" -f "query-index.c" --lines 610-620    # Line range
```

Useful for:
- Investigating compiler errors
- Exploring specific function bodies
- Narrowing search to code sections

---



### 7. **Verbose Output: All or Nothing**
**Priority:** ⚠️ LOW

Default is too sparse, `-v` is too much. Want medium verbosity.

**Proposed solution:** Column presets:
```bash
qi "clue" --preset brief      # line symbol context (default)
qi "clue" --preset standard   # line symbol context parent scope
qi "clue" --preset full       # all columns (current -v)
```

Or allow aliases in `~/.query-indexrc`:
```bash
[presets]
standard = --columns line symbol context parent scope
```

**Estimated effort:** ~40-50 lines

---

### 8. **Show Internal Columns (--columns for all DB columns)**
**Priority:** 🔥 HIGH

Can't inspect internal database columns like `source_location` or `is_definition`:
```bash
qi "PatternList" -i type
# Shows: LINE | SYMBOL | CONTEXT
# Want to see: source_location, is_definition values
```

**Problem:** When debugging why features don't work (e.g., `-e` not expanding), can't verify what's in the database without dropping to raw SQL.

**Proposed solution:**
```bash
qi "PatternList" --columns symbol line source_location definition
# Output:
# LINE | SYMBOL      | SOURCE_LOCATION | DEF
# -----+-------------+-----------------+----
#   20 | PatternList | 19:0 - 22:1     | 1
```

**Real session impact:** Had to use `sqlite3` directly to debug typedef struct issue. Would have been instantly visible with this feature.

**Estimated effort:** ~30-40 lines (extend existing --columns to support all DB columns)

---

### 9. **Debug/Verbose Mode (--debug flag)**
**Priority:** ⚠️ MEDIUM

Can't see what qi is doing internally:
```bash
qi "user" -f "%.c" -i func
# Want to see:
# - SQL query generated
# - Which files matched filter
# - Why symbols included/excluded
```

**Proposed solution:**
```bash
qi "user" -f "%.c" --debug
# Output:
# [DEBUG] SQL: SELECT * FROM code_index WHERE lowercase_symbol LIKE ? AND context_type = 'FUNCTION' AND (directory LIKE '%.c' OR filename LIKE '%.c')
# [DEBUG] Files matched: ./main.c, ./parser.c (2 files)
# [DEBUG] Found 5 symbols
```

**Real session impact:** Would have helped debug file filter issues and understand query behavior.

**Estimated effort:** ~40-50 lines

---


### 11. **Fuzzy Search / Suggestions**
**Priority:** ⚠️ LOW

When no exact match, suggest similar symbols:
```bash
qi "PrintContext"
# Output:
# No results for "PrintContext"
# Did you mean: print_context_lines, print_table_header?
```

**Estimated effort:** ~100-150 lines (need symbol similarity algorithm)

---

### 12. **Explain Mode for Filtered Words**
**Priority:** ⚠️ MEDIUM

When searching for filtered words, explain why:
```bash
qi "function" --explain
# Output:
# Note: 'function' is in typescript/data/ts-keywords.txt and is not indexed.
# Workaround: Use wildcards: qi "%function%"
```

Currently says "not indexed" but doesn't say WHY or WHERE it's filtered.

**Estimated effort:** ~30-40 lines

---

### 13. **Index Freshness Warnings**
**Priority:** ⚠️ LOW

Warn when querying files modified since last index:
```bash
qi "newFunction" --check-freshness
# Warning: ./auth.c modified 5 minutes ago, last indexed 2 hours ago
# Suggestion: Re-run ./index-c
```

**Estimated effort:** ~60-75 lines (need to track index timestamps)

---

### 14. **Show What Files Matched (--show-files)**
**Priority:** ⚠️ LOW

Confirm file filter worked:
```bash
qi "%" -f "%.c" --show-files
# Searching in 58 files:
#   ./query-index.c
#   ./shared/database.c
#   ./shared/file_utils.c
#   ...
```

**Estimated effort:** ~25-30 lines

---


### Color-Coded Output
- Functions in blue
- Variables in green
- Classes in yellow
- Comments in gray

Makes visual scanning faster. Disable with `--no-color` or when piping.

---

### Cross-Reference Queries
```bash
qi "cursor" --used-with "bounds"   # Find where both are used together
qi "Manager" --calls "initialize"  # Find Managers that call initialize
```

---

### Statistics and Analysis
```bash
qi "%" --stats
# Total symbols: 15,432
# Functions: 3,421
# Variables: 8,234
# Classes: 234
# Most common: cursor (234 uses), manager (156 uses)
```

---

### JSON Output Mode
```bash
qi "cursor" --json
```

Perfect for tool integration and programmatic use.

---

### Minimal Output Mode
```bash
qi "cursor" --minimal
# Output:
# query-index.c:284
# scratch/test.c:45
# tools/editor.c:123
```

Just `filename:line` for piping to other tools.

---

## Session Notes / Learnings

### What Worked Really Well (Session: 2025-11-01)

**Type filtering is incredibly powerful:**
```bash
qi "%" -i argument -t "FileFilterList *"  # Find all params of this type
qi "%" -i argument -t "%*"                # Find ALL pointer params
```

Much more precise than grep, impossible to do with text search alone.

---

**Modifier filtering enables powerful queries:**
```bash
qi "%" -i function -m "static"            # Find 329 static helper functions!
qi "%" -i function -m "inline"            # Performance analysis
```

---

**File count feedback builds confidence:**
```bash
qi "clue" -f "%.c"
# Filtering by file: %.c (58 files)
```

Confirms pattern worked and shows actual search scope.

---

**Context flags eliminate query → Read workflow:**

Before: Query → see line number → Read file
After: Query with `-C` → see code immediately

Saved 15+ Read commands in one session = major token savings.

---

### Token Efficiency Comparison

**query-index:** ~50-100 tokens per query result
**grep:** ~200-500 tokens (need to parse, filter, explain)
**Savings:** 50-80% fewer tokens with query-index

---

### What Worked Really Well (Session: 2025-11-06 - Implementing `-e` flag)

**Dog-fooding while building the feature:**
- Used qi to search for `handle_struct_specifier` while debugging struct expansion
- Used qi to test the `-e` flag immediately after implementing it
- Found bugs faster because the tool helped debug itself

**Context-aware filtering prevented noise:**
- `-i func` to find only function definitions, not strings/comments
- `-x noise` excluded false positives automatically
- Saved countless grep false positive filtering

**The -A flag eliminated Read operations:**
- `./qi "parse_source_location" -i func -A 50` showed entire implementation
- Much faster than: search → get line → Read file → scroll to section
- Estimated 10-15 Read operations avoided in this session alone

**Instant cross-language testing:**
- Tested `-e` flag across C, TypeScript, PHP, and Go in seconds
- Would have taken much longer with grep/find
- `./qi "%" -i func -f "%.php" -e --limit 1` instantly validated PHP support

---

### Pain Points Encountered (Session: 2025-11-06)

**1. File filter confusion (HIGH IMPACT):**
- `./qi "print_context_lines" -f "query-index.c"` → "No results"
- Required user to tell me: "remove the -f flag"
- Issue: File was indexed as `./query-index.c` not `query-index.c`
- Hit this multiple times during session

**2. Couldn't inspect source_location column (HIGH IMPACT):**
- When debugging typedef struct issue, had to drop to raw SQL
- `sqlite3 code-index.db "SELECT source_location..."` was the only option
- Would have been instantly visible with `--columns source_location`

**3. is_definition type confusion caused segfault:**
- Treated `is_definition` as string when it's actually int
- Caused: `strcmp(is_definition, "1")` → segfault
- Better inline docs or --debug mode would have prevented this

**4. No visibility into what qi was doing:**
- When getting "No results", couldn't see:
  - What SQL was generated
  - Which files matched the filter
  - Why no symbols were found
- Had to guess and try different approaches

---

### Key Insights from This Session

**qi is genuinely better than grep for code search:**
- Context awareness (func vs string vs comment) is transformational
- Can't do `qi "%" -i func -s public` with grep
- Structured output is immediately actionable

**The `-e` flag is a major win:**
- Shows complete function/struct/enum definitions
- Combined with context filtering: `qi "auth" -i func -e`
- Eliminates the "how many context lines do I need?" guessing game

**Observability is the #1 missing feature:**
- When things don't work, qi is a black box
- Need `--debug`, `--columns source_location`, better error messages
- Would have saved 30+ minutes in this session

**Token efficiency is real:**
- Avoided 15+ Read operations with `-A` flag
- Structured output uses ~50% fewer tokens than grep
- Perfect for AI assistant workflows

---

### Dog-Fooding Score: 9/10

**What made it great:**
- Found and fixed bugs using the tool itself
- Context filtering eliminated noise
- Cross-language testing was trivial
- Actually faster than grep workflow

**What lost the point:**
- Observability gaps when debugging
- File filter confusion
- Had to drop to SQL once

**Bottom line:** qi is production-ready and genuinely useful. The remaining improvements are about debugging/observability, not core functionality.

---

## Overall Assessment

**For semantic code search** (classes, functions, structure): `query-index` >> grep
**For exact text patterns** or **seeing context**: query-index + `-C` and `-e` flags now superior to grep
**For AI token efficiency**: query-index is fantastic
**For debugging/observability**: Still needs work (see HIGH priority items above)

---

## LLM Usage Reflection (November 2025)

Based on real-world usage by Claude during development session:

### Improvement Ideas for Human Users

1. **Fuzzy matching on failed searches** - "Did you mean: extract_parameter?" when searching for `extract_paramters`
2. **Show sample contexts in zero-results** - "cursor found in: VAR (50), CALL (20). Use -i to filter."
3. **Better stopword message** - Instead of just noting it's filtered, show: "Try: `qi %all% patterns matched --and`"
4. **Add `--phrase` flag** - Automatically split and add `--and`: `qi --phrase "all patterns matched"`

### Improvement Ideas for LLM Users

1. **JSON output mode** - `--json` flag for structured parsing (would save token-heavy formatting)
2. **Multi-query batch mode** - `qi cursor handle extract --batch` to search 3 patterns in one call (saves round-trips)
3. **Confidence scores** - When using wildcards, show match relevance: `extract_parameters (exact)` vs `extract_param_types (partial)`
4. **Context hints in output** - Show parent/scope inline: `cursor (in TextContainer, private)` without needing `-p -s`

### Quick Wins

1. **Add `-e` reminder to help text** - Many times Read was used when `-e` would have worked
2. **Case-insensitive by default for `--and`** - Already works this way, just needs clarity
3. **Show total matches in every query** - ✅ DONE (Feature #7)
4. **Alias `-x noise` to `-xn`** - Common pattern deserves shortcut

### What Works Exceptionally Well

- **Context-aware filtering** (`-i func`, `-i var`) - incredibly powerful and precise
- **Token efficiency** - Compact output format vs grep's verbose output
- **The `-e` flag** - Expanding function definitions inline eliminates Read calls
- **Context summary** (#7) - `VAR (45582), CALL (21900)` instantly shows shape of results
- **Automatic wildcard retry** - Saved multiple failed searches

### Known Pain Points

1. **Muscle memory issues** - Users try multi-word patterns in quotes (now has helpful error message ✅)
2. **Stopword frustration** - Common words like "all", "by" can't be searched directly
3. **Discovery problem** - Only shows exact matches, unlike grep where you see symbols in passing
4. **Line number context unclear** - Need to remember to use `-C` flag for surrounding context

### Usage Recommendation

**Use qi for:**
- Finding function/class/type definitions across languages
- Understanding symbol usage patterns (definitions vs calls)
- Token-conscious codebase exploration
- Structural code understanding

**Still use grep/traditional tools for:**
- Free-form text search in comments/documentation
- Exploratory "I don't know what I'm looking for" searches
- Very first pass at unfamiliar codebase

**The killer insight:** qi shines when you know *what kind of thing* you're looking for. When you don't, traditional grep still has an edge.


### Latest feedback (2025-11-09)

What Was Genuinely Better (Beyond the Obvious)

1. File count feedback is underrated - "Filtering by file: query-index.c (1 files)" immediately validated my filter worked. With grep, I'd have
 to scan the output to verify. This saved mental effort repeatedly.
2. Automatic wildcard retry - When I searched for "Selection" and got no results, it automatically tried "%Selection%" and found "Selection:"
in strings. Grep just says "no match" and you're stuck.
3. Iterative refinement with --limit - I could scan first 5 results, then refine my query. With grep, you get everything dumped at once and
have to parse it visually.
4. Context filtering prevented false positives - Searching for lowercase_symbol with -x noise excluded comments like /* lowercase_symbol is 
used for... */. Grep would show every mention.

What Was Genuinely Worse

1. File filtering confusion was REAL - Even after we fixed it, I kept second-guessing myself. The improvement plan was right - this was a
legitimate usability issue that caused friction.
2. ~~No "list files only" mode~~ ✅ **NOW IMPLEMENTED** - `--files` flag added (like grep -l)
3. Can't search SQL string contents effectively - When looking for column names in SQL queries (which are string literals), I wanted to find
exact phrases like "lowercase_symbol TEXT NOT NULL" but could only search for individual words. The tokenization that makes general search
great makes literal string searching harder.

What Would Knock It Out of the Park

~~List affected files - A --files or --unique-files flag~~ ✅ **NOW IMPLEMENTED** - See "Files-Only Output Mode" in completed features above.

Maybe Useful:

Grouped output - When I got results across multiple files, they were interleaved by line number. A --group-by-file option might be clearer for
refactoring work, though the current format is fine for most uses.

Honestly? Nothing Major Missing:

The tool worked remarkably well for this session. The file filtering issue (which we fixed) was the only real pain point. Beyond that, it was
faster and more precise than grep/glob would have been.

~~The --files flag is the one feature I genuinely wished for~~ ✅ **NOW IMPLEMENTED** - everything else worked great once I learned the patterns.

---

## Session Reflection: TypeScript Indexer Bug Fix (2025-11-30)

### What Worked Really Well

1. **Table of Contents (`--toc`) was invaluable**
   - `qi % -f typescript/ts_language.c --toc -i func` showed all 39 functions instantly
   - Perfect for navigating unfamiliar code structure
   - Saved multiple grep/glob searches

2. **The `-e` flag eliminated Read operations**
   - `qi handle_assignment_expression -f typescript/ts_language.c -i func -e` showed entire function
   - Avoided ~5-10 Read operations in this session
   - Workflow: search → see code immediately (vs search → find line → Read → scroll)

3. **Verbose mode (`-v`) for debugging**
   - `qi name -f fixtures/typescript/basic-class.ts -p -v` revealed the bug instantly
   - Showed parent column was empty instead of `this`
   - Much better than raw SQL queries for investigation

4. **Context filtering prevented noise**
   - `-i func` to find only functions, not comments/strings
   - Semantic understanding is genuinely transformational
   - (Note: Forgot to use `-x noise`, but would have been helpful)

### Pain Points Encountered

1. **Couldn't use qi on Makefile/configure files** (by design)
   - Had to context-switch to `grep -n`
   - Not fixable without changing tool's purpose, but felt jarring
   - **Suggestion added:** Show grep alternative when encountering non-code files

2. **No scope-based search**
   - When debugging `handle_assignment_expression`, wanted to find `parent` uses ONLY within that function
   - Workaround: `--lines 1813-1920` (requires knowing line numbers)
   - **High-priority feature request added**

3. **Line range filtering requires manual lookup**
   - Had to Read file first to find function boundaries (lines 1813-1920)
   - `--scope` flag would eliminate this step entirely

### Feature That Would Have Been Amazing

**Incremental narrowing / Query refinement:**

```bash
qi parent -f typescript/ts_language.c -v
# Found 50 matches across 10 functions

qi --refine -i var
# Narrows previous results to just VARIABLEs (12 matches)

qi --refine --lines 1800-1920
# Further narrows to specific line range (3 matches)
```

**Why this would be transformational:**
- Mirrors natural search workflow: broad → narrow → narrow more
- Saves re-typing entire query with cumulative filters
- Could show "refinement path" for clarity
- Like a REPL for code search

**Estimated effort:** ~100-150 lines (query state management)
**Priority:** ⚠️ LOW (nice-to-have, not essential for core workflows)

### Bottom Line

**Session score: 8.5/10**

Would have been 9.5/10 with `--scope` flag. The tool genuinely made debugging faster - `--toc` and `-e` are killer features that saved significant time. Main gap is scope-based filtering for focusing on specific functions/classes.

**Priority adjustments recommended:**
- Scope-based search: LOW → 🔥 HIGH (most impactful missing feature)
- Line range filtering: ✅ Already implemented
- Grep alternative suggestions: NEW, ⚠️ LOW (quality-of-life improvement)
