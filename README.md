# SourceMinder

Multi-language code indexer (written in C11) for semantic search, built on SQLite and tree-sitter.

**Languages currently implemented:** C, Go, PHP, Python, TypeScript

**Database:** Creates `code-index.db` in current working directory

**Note:** This tool is designed for indexing **source code** (functions, classes, variables, ...),
not prose or documentation files (`.md`, `.txt`, `.log`, ...).
While you can index any text file, the tool extracts code symbols and won't be useful for natural language text.

## Quick Reference

| Flag | Purpose | Example |
|------|---------|---------|
| `-i <type...>` | Include context types (OR) | `qi user -i func var prop` |
| `-x <type...>` | Exclude context types (OR) | `qi user -x comment string` |
| `--list-types` | Display full list of context types | `qi --list-types` |
| `-x noise` | Exclude comments & strings | `qi user -x noise` |
| `-f <pattern...>` | Filter by file paths (OR) | `qi user -f .c .h` or `qi user -f src/* lib/*` |
| `-p <pattern>` | Filter by parent symbol | `qi count -p patterns` finds `patterns->count` |
| `-t <pattern>` | Filter by type annotation | `qi '*' -i arg -t 'int *'` finds int pointer args |
| `-m <pattern>` | Filter by modifier | `qi '*' -i func -m static` finds static functions |
| `-s <pattern>` | Filter by scope | `qi '*' -s public` finds public members |
| `-e` | Expand full definitions | `qi getUserById -i func -e` |
| `-C <n>` | Show n context lines | `qi user -C 3` |
| `-A <n>` | Show n lines after | `qi user -A 5` |
| `-B <n>` | Show n lines before | `qi user -B 5` |
| `--and` | Multi-pattern same-line | `qi fprintf stderr --and` |
| `--and <n>` | Multi-pattern within n lines | `qi malloc free --and 10` |
| `--def` | Only definitions | `qi getUserById --def` |
| `--usage` | Only usages | `qi User --usage` |
| `--within <sym>` | Search within function/class | `qi malloc --within handle_request` |
| `--limit <n>` | Limit results | `qi '*' --limit 20` |
| `--toc` | Table of contents | `qi '*' -f file.c --toc` |
| `--columns` | Custom columns | `qi user --columns line symbol context parent` |
| `-v` | Verbose (all columns) | `qi user -v` |
| `--full` | Full context names | `qi user --full` |

**IMPORTANT: Multi-Value Arguments (Non-Standard UNIX Pattern)**

Unlike most UNIX tools, `qi` accepts **multiple values** for both the main pattern and subsequent flags:

```bash
# Multiple search patterns (OR logic)
qi user session token --limit 20             # Find user OR session OR token

# Multiple context types (OR logic)
qi user -i func var prop                   # Functions OR variables OR properties

# Multiple file patterns (OR logic)
qi symbol -f file1.c file2.c utils.h         # In any of these files
qi malloc -f *.c *.h                         # All .c OR .h files

# Combine multiple values across arguments
qi malloc free -i func call -f memory.c allocator.c buffer.c
```

**Comparison with standard UNIX tools:**
- Standard: `grep -f pattern.txt file.c`      (one file per `-f`)
- qi: `qi symbol -f file1.c file2.c file3.c`  (multiple files per `-f`)

**Pattern wildcards:**
- `*` = any characters (e.g., `*Manager`, `get*`, `*user*`)
- `.` = single character (e.g., `.etUser` matches `getUser`, `setUser`)
- No wildcards = exact match (case-insensitive)
- Note: `%` and `_` also work (SQL LIKE syntax) but `*` and `.` are recommended

**Escaping special characters:**
- `\*` = literal asterisk (e.g., `operator\*` finds `operator*`)
- `\.` = literal period (e.g., `file\.c` finds `file.c`)
- `\--flag` = search for flag-like symbols (e.g., `\--help` finds `--help`)

Examples:
```bash
qi 'get*' -i func          # Wildcard: finds getUser, getData, etc.
qi 'get\*' -i func         # Literal: finds "get*" symbol if it exists
qi '.etUser'               # Wildcard: finds getUser, setUser (. = single char)
qi '\--help'               # Literal: finds "--help" string
```

## Why Use This?

**For Developers:**
- Browse a project's vocabulary (like a book index)
- Find symbols by context (classes, functions, variables, etc.)
- Track relationships with parent symbols (e.g., `obj.method()`)
- Filter by access modifiers (public, private, protected)
- Query with SQL wildcards for partial matches

**qi vs grep - Different tools for different jobs:**
- **qi** - **Recommended default** for code navigation: symbols (functions, classes, variables, types), structure exploration, precision filtering
- **grep** - Literal text (error messages, comments), regex patterns, non-code files

**A few reasons to prefer defaulting to qi in lieu of grep:**
- `--toc` gives instant file overview (like a book's "table of contents")
- Context filtering (`-i func`, `-x noise`) eliminates false positives
- `--within function` enables scoped search (impossible with grep)
- `-e` shows full definitions inline

**For Claude Code:**
- Faster workflow than grep + Read for code navigation
- Saves tokens with compact output
- Relative paths work directly with Read/Edit tools
- Context-aware filtering reduces noise
- Use `qi -f file.c --toc` before Read tool to understand structure
- Use `-e` to see full definitions without Read tool

## Advanced Filtering (Power User Features)

Beyond basic pattern matching, qi tracks rich metadata about every symbol. These filters enable greater precision:

### Parent Symbol (`-p` / `--parent`)
Find member access, method calls, and struct/class fields:

```bash
qi count -p patterns               # Find patterns->count or patterns.count
qi 'field%' -p 'config%'           # All fields accessed on config objects
qi init -p User                    # Find User->init or User.init methods
qi '*' -p 'request%' --limit 20    # All members of request-like objects
```

**How it works:** Tracks the parent in expressions like `obj->field`, `obj.method()`, `array[i].field`

### Type Annotation (`-t` / `--type`)
Filter by type declarations (for refactoring):

```bash
qi '*' -i arg -t 'int *'           # All int pointer arguments
qi '*' -i var -t 'char *'          # All char pointer variables
qi '*' -i arg -t 'OldType%'        # Find args using deprecated types
qi '*' -t 'uint32_t' --limit 20    # All uint32_t usage
```

**Use case:** Type-based refactoring, finding all uses of a specific type

### Modifier (`-m` / `--modifier`)
Filter by access modifiers and storage classes:

```bash
qi '*' -i func -m static --limit 10  # All static functions
qi '*' -i var -m const               # All const variables
qi '*' -m private                    # Private members
qi '*' -m inline -i func             # Inline functions
```

**Use case:** Understanding code visibility, finding optimization candidates

### Scope (`-s` / `--scope`)
Filter by visibility scope:

```bash
qi '*' -s public -i func             # Public API functions
qi '*' -s private -i prop            # Private properties
qi method -s protected               # Protected methods
```

**Use case:** API analysis, understanding encapsulation

### Scoped Search (`--within`)
Search only within specific functions or classes:

```bash
qi malloc --within handle_request    # malloc calls only in handle_request
qi fprintf --within 'debug%'         # fprintf in debug-related functions
qi strcmp --within parse_args        # strcmp only in parse_args
qi '*' -i var --within main          # All variables declared in main
```

**Use case:** Understanding local behavior, debugging specific functions

### Combining Filters
Stack filters for maximum precision:

```bash
# Find static int pointer arguments in memory functions
qi '*' -i arg -t 'int *' -m static --within 'mem*'

# Find private fields accessed on config objects
qi '*' -p config -s private -i prop

# Find all malloc calls in static helper functions
qi malloc -i call --within '*helper*' -m static
```

**Pro tip:** Use `-v` (verbose) to see all available columns, then craft precise queries

## Build & Install

### Prerequisites

On Linux, install the following dependencies. (MacOS users, see MACOS_SETUP.md)

```bash
apt install linux-tools-common libtree-sitter-dev libtree-sitter0 libsqlite3-dev
```
Clone the repo

```bash
git clone https://github.com/ebcode/SourceMinder.git && cd SourceMinder
```

Clone the tree-sitter grammars (at least one):

```bash
git clone https://github.com/tree-sitter/tree-sitter-c.git
git clone https://github.com/tree-sitter/tree-sitter-go.git
git clone https://github.com/tree-sitter/tree-sitter-php.git
git clone https://github.com/tree-sitter/tree-sitter-python.git
git clone https://github.com/tree-sitter/tree-sitter-typesript.git
```

### Configure

Select which languages you want to build (all disabled by default):

```bash
./configure --enable-all                                 # All languages (recommended for testing)
./configure --enable-c --enable-typescript --enable-php  # Specific languages
./configure --enable-all --disable-php                   # All but PHP
CC=clang ./configure --enable-c                          # Custom compiler, only C
```

### Compile & Install

```bash
make                    # Build indexers and query tool
sudo make install       # Install to /usr/local/bin
```

**Installed binaries:** `index-c`, `index-ts`, `index-php`, `index-go`, `index-python`, `qi`
**Config files:** `/usr/local/share/sourceminder/<language>/config/`

## Getting Started

**Step 1: Build and index the SourceMinder codebase**
```bash
index-c . --once --verbose
```

**Step 2: Query (single and multiple patterns)**
```bash
qi -f shared/indexer-main.c --toc       # Table-of-Contents output
qi handle_import_statement              # Exact match
qi '*user*'                             # Contains "user"
qi '*' -i func --def --limit 10         # First 10 function definitions (quote single * to prevent shell expansion)

# Multiple patterns (OR logic)
qi malloc free calloc                   # Find any of these
qi user session token -i var --limit 20 # Variables named user/session/token
```

**Step 3: View code**
```bash
qi getUserById -i func -e               # Expand full function
qi getUserById -i func call -e -C 3     # Expand full function, and show context around call sites
```

**Step 4: Background daemon**
```bash
index-c . &           # Watch for changes
```

### Understanding Output

```bash
qi 'symbol' --limit-per-file 1 --limit 5
```

Output:
```
Searching for: symbol

LINE | SYM    | CTX 
-----+--------+-----
./query-index.c:
67   | symbol | COM 

./benchmark/struct-layout-optimized.c:
26   | symbol | PROP

./benchmark/struct-layout-original.c:
17   | symbol | PROP

./c/c_language.c:
23   | Symbol | COM 

./go/go_language.c:
83   | Symbol | COM 

Found 351 matches (showing first 5)
Result breakdown: ARG (162), COM (68), VAR (52), STR (37), PROP (32)
Tip: Use -i <context> to narrow results

```

- **LINE**: Line number
- **SYM**:  The matched symbol (as it appears in the source)
- **CTX**:  Where/how the symbol appears (context)
- Informational notes at the end 


## Context Types Reference

Understanding context types is key to effective filtering:

| Type | Short | Definition | Example |
|------|-------|------------|---------|
| `class` | - | Class declarations | `class UserManager { }` |
| `interface` | `iface` | Interface declarations | `interface Storable { }` |
| `function` | `func` | Functions and methods | `function getUserById() { }` |
| `argument` | `arg` | Function parameters | `function foo(userId: string)` |
| `variable` | `var` | Variables and constants | `const cursor = ...` |
| `property` | `prop` | Class properties | `this.name = "Alice"` |
| `type` | - | Type definitions/annotations | `type UserID = string` |
| `import` | `imp` | Imported symbols | `import { User } from ...` |
| `export` | `exp` | Exported symbols | `export { User }` |
| `call` | - | Function/method calls | `getUserById(123)` |
| `lambda` | - | Lambda/arrow functions | `(x) => x * 2` |
| `enum` | - | Enum declarations | `enum Status { }` |
| `case` | - | Enum cases | `Active = 1` |
| `namespace` | `ns` | Namespace declarations | `namespace Utils { }` |
| `trait` | - | Trait declarations | `trait Serializable { }` (PHP) |
| `comment` | `com` | Words from comments | `// display user info` |
| `string` | `str` | Words from string literals | `"user name"` |
| `filename` | `file` | Filename without extension | File named `user.ts` |

**Usage:**
```bash
qi user -i func var            # Include Functions OR variables
qi user -x comment string      # Exclude comments AND strings
qi user -x noise               # Shorthand for -x comment string
qi user -i func var -x comment # Combine include and exclude
```

## Indexing

### Folder Mode vs File Mode

**Folder Mode** (recommended for projects):
- Recursively indexes all matching files
- Respects ignore lists in `<language>/config/ignore_files.txt`
- Runs in daemon by default, watching for changes
- Use for: Active development on a codebase

```bash
index-c ./src ./lib
```

**File Mode** (for specific files):
- Indexes only the specified files
- Ignores ignore lists
- Runs once and exits (no daemon)
- Use for: One-off indexing, testing, or specific file updates

```bash
index-c main.c utils.c helper.c
```

### Daemon Management

**Start daemon:**
```bash
index-c ./src &               # Background process
index-c ./src --silent &      # Silent
```

**One-time indexing:**
- Useful for one-time analysis, but not for querying and editing in the same session

```bash
index-c ./src --once          # Index once and exit
```

**Stop daemon:**
```bash
ps aux | grep index-c           # Find process ID
kill <PID>                      # Kill it
killall index-c index-ts        # Or kill all indexers
```

**When to use:**
- **Daemon**: Active development - auto-updates as you edit
- **--once**: CI/CD pipelines, one-time analysis, manual control

### Indexer Options

```bash
index-<language> <folders...> [options]
```

**Options:**
- `--once` - Run once and exit (no daemon)
- `--silent` - Silence output
- `--quiet-init` - Quiet initial indexing, noisy re-indexing on file change
- `--verbose` - Show preflight checks and progress
- `--exclude-dir DIR [DIR...]` - Exclude additional folders

**Examples:**
```bash
index-c ./src --verbose --once
index-c ./src ./lib --quiet-init &
index-c ../ --exclude-dir build dist node_modules
```

### What Gets Indexed

The indexer:
- Scans files matching `<language>/config/file-extensions.txt`
- Skips folders in `<language>/config/ignore_files.txt`
- Extracts symbols via tree-sitter AST parsing
- Tracks parent symbols for member expressions (e.g., `this.target.getBounds()`)
- Captures access modifiers (public, private, protected)
- Filters noise (stopwords, keywords, punctuation, short symbols, pure numbers)
- Stores relative paths from current working directory

### Concurrent Indexing (WAL Mode)

Multiple language indexers can run in parallel without database locks:

```bash
index-ts ./src &
index-c ./lib &
index-php ./app &
```

SQLite WAL mode is enabled automatically on first run.

## Querying

### Basic Queries

```bash
qi <pattern...> [options]    # One or more patterns (logical OR)
```

**Single pattern matching** (wildcard syntax):
```bash
qi cursor              # Exact: cursor
qi '*manager'          # Ends with Manager
qi 'get*'              # Starts with get
qi '*session*'         # Contains session
qi _etuser             # Matches: getUser, setUser
```

**Multiple pattern matching** (OR logic - finds any match):
```bash
qi malloc free calloc                  # Find any of these functions
qi user session token --limit 30       # Find user OR session OR token
qi '*error' '*Exception' '*Fail*'      # Any error-related symbols
qi 'init*' 'destroy*' 'cleanup*'       # Any lifecycle functions
```

### Context Filtering

```bash
qi '*manager' -i class iface   # Only classes or interfaces
qi user -i func var prop       # Functions, variables, or properties
qi user -x comment             # Exclude comments
qi user -x noise               # Exclude comments and strings (recommended default -- see how to configure below)
```

### File Filtering

**Single or multiple file patterns** (OR logic):
```bash
qi user -f .c                           # All .c files
qi user -f .c .h                        # .c OR .h files
qi user -f src/* lib/* include/*        # Multiple directories
qi user -f auth/*.c session/*.c         # .c files in multiple dirs
qi user -f database.c utils.c helpers.c # Specific files
```

### Definition vs Usage

```bash
qi getUserById --def           # Only definitions (is_definition=1)
qi User --usage                # Only usages (is_definition=0)
```

**Use cases:**
- **Jump to definition:** Find where a function is declared
- **Find all usages:** See everywhere a type is used
- **API exploration:** List all function definitions

### Multi-Pattern AND

Find lines where **ALL** patterns co-occur:

```bash
qi fprintf err_msg --and        # Both on same line
qi user session token --and     # All three on same line
qi malloc free --and 10         # Both within 10 lines of each other
```

**Incremental refinement:**
```bash
qi file_filter                  # Too many results
qi file_filter *build* --and    # Refine to lines also containing "build"
```

### Viewing Code

**Expand full definitions:**
```bash
qi getUserById -i func -e       # Show complete function
```

Output:
```
LINE | SYMBOL       | CONTEXT
-----+--------------+---------
auth.c:
  45 | validateUser | FUNC

  45 | bool validateUser(const char* username, const char* password) {
  46 |     if (!username || !password) {
  47 |         return false;
  48 |     }
  ...
  54 | }
```

**Show context lines:**
```bash
qi cursor -C 2                  # 2 lines before and after
qi cursor -A 5                  # 5 lines after
qi cursor -B 3                  # 3 lines before
```

**Two-step workflow (recommended):**
```bash
# Step 1: Discover
qi *auth* -i func --limit 10

# Step 2: Explore
qi auth -i func --def -e        # Expand definitions
```

### Table of Contents

Quick overview of all definitions in a file:

```bash
qi -f database.c --toc
qi -f c_language.c --toc -i func    # Only functions
qi -f .c --toc                      # Works with extension shorthand (WARNING: This command could produce a LOT of output!)
```

Output:
```
database.c:

IMPORTS: sqlite3.h, stdio.h, stdlib.h, string.h

FUNCTIONS:
  init_database (15-45)
  execute_query (47-89)
  close_database (91-98)

TYPES:
  DatabaseConfig (5-12)
```

### Customize Column Output and Order

```bash
qi database --columns line context symbol       # Basic columns
qi database --columns line context clue symbol  # Include clue
qi database --columns symbol line clue          # Reorder columns
```

**Available columns**: `line`, `context`, `parent`, `scope`, `modifier`, `clue`, `namespace`, `type`, `symbol`

**Default behavior:**
- Normal: `line`, `symbol`, `context` (abbreviated)
- With `-c/--clue`: Adds `clue` column
- With `-v/--verbose`: All columns
- With `--full`: Full context type names

### Limit Results

```bash
qi user --limit 10                    # First 10 matches
qi '*' --limit 20                     # First 20 symbols
qi '*' --limit-per-file 3             # First 3 symbols in each file
qi '*' --limit-per-file 3 --limit 10  # First 3 symbols in each file, up to 10 total
```

## Configuration

### Config File Locations

**Installed system-wide:** `/usr/local/share/sourceminder/<language>/config/`
**Local development:** `<language>/config/` (in the project directory)

The indexer checks both locations (local takes precedence).

### File Extensions

**Location:** `<language>/config/file-extensions.txt`
**Format:** One extension per line starting with a dot:

```
.ts
.tsx
.js
.jsx
```

No recompilation needed after editing config files. But you will need to re-index.

### Ignored Folders and Files

**Location:** `<language>/config/ignore_files.txt`
**Format:** One folder per line:

```
node_modules
dist
build
.git
vendor/legacy
```

Folders are ignored at any level. Use `--exclude-dir` for per-run exclusions.

### Stopwords & Keywords

**Stopwords** (shared): `shared/config/stopwords.txt`
**Keywords** (per-language): `<language>/config/<language>-keywords.txt`

## Performance & Best Practices

### Index Size & Performance

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

### Query Performance

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

### Best Practices

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

### For Claude Code

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

## Troubleshooting

### Common Issues

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

### Build Issues

**`undefined reference to ts_language_c`:**
- Run `./configure` first, then `make clean && make`

**`No such file: shared/config/stopwords.txt`:**
- Config files missing
- Run `sudo make install` or verify local config files exist

**Warning: `ENABLE_C` redefined:**
- Run `./configure` again, then `make clean && make`

### Debugging

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

## Quick Tips

### Pattern Matching
```bash
qi cursor       # Exact match
qi get*         # Starts with (wildcard)
qi *Manager     # Ends with (wildcard)
qi *user*       # Contains (wildcard)
qi .etUser      # Single char wildcard: getUser, setUser
qi 'get\*'      # Literal asterisk (escaped)
qi '\--help'    # Search for --help symbol
```

### File Filtering
```bash
qi user -f .c              # By extension
qi user -f src/*           # By directory
qi user -f ./auth/*.c      # Specific path
qi user -f .c .h           # Multiple patterns (OR)
```

### Common Workflows

**Find a function definition:**
```bash
qi getUserById -i func --def -e
```

**Explore a file:**
```bash
qi -f database.c --toc
```

**Find related code:**
```bash
qi user session --and 10 -C 3
```

**Refactor a type:**
```bash
qi '*' -i arg -t "OldType *"      # Find all parameters using old type
qi '*' -i arg -t "OldType*"       # Verify none remain after refactor
```

**Understand how a type is used:**
```bash
qi user --def -e                # See the definition
qi user --usage --limit 20      # See all usages
```

**Chain your workflow:**
```bash
qi '*' -f query-index.c --toc              # 1. Explore
qi *user* -i func --def --limit 10         # 2. Find interesting functions
qi validateUser -i func -e                 # 3. See implementation
qi validateUser --usage -C 3               # 4. Find usage examples
qi validate* login* --and 20 -i func call  # 5. Find related code
```
