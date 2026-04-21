# SourceMinder

Multi-language code indexer (written in C11) for semantic search, built on SQLite and tree-sitter.

**Languages currently implemented:** C, Go, Perl, PHP, Python, TypeScript

**Database:** Creates `code-index.db` in current working directory

**Note:** This tool is designed for indexing **source code** (functions, classes, variables, ...),
not prose or documentation files (`.md`, `.txt`, `.log`, ...).
While you can index any text file, the tool extracts code symbols and won't be useful for natural language text.

## Build & Install

### Prerequisites

On an apt-based Linux (like Debian), install the following dependencies. (MacOS users, see MACOS_SETUP.md)
I have not yet tested on non-apt Linux, so any help here would be appreciated.

```bash
apt install libtree-sitter-dev libtree-sitter0 libsqlite3-dev
```
Clone the repo

```bash
git clone https://github.com/ebcode/SourceMinder.git && cd SourceMinder
```

Clone the tree-sitter grammars (at least one):

```bash
git clone https://github.com/tree-sitter/tree-sitter-c.git
git clone https://github.com/tree-sitter/tree-sitter-go.git
git clone https://github.com/tree-sitter-perl/tree-sitter-perl.git
git clone https://github.com/tree-sitter/tree-sitter-php.git
git clone https://github.com/tree-sitter/tree-sitter-python.git
git clone https://github.com/tree-sitter/tree-sitter-typescript.git
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

**Installed binaries:** `index-c`, `index-ts`, `index-php`, `index-go`, `index-python`, `index-perl`, `qi`
**Config files:** `/usr/local/share/sourceminder/<language>/config/`


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
| `--raw` | Bare source lines only (no headers, line numbers, separators, or stats); use with `-e` or `-B`/`-A` | `qi new -f file.pl -e --raw` |

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

**qi vs grep** — see [docs/QI_VS_GREP.md](docs/QI_VS_GREP.md) for a full comparison. In short: use `qi` as the default for code navigation (symbols, structure, precise filtering); use `grep` for literal text, regex, and non-code files.

**For Claude Code:**
- Faster workflow than grep + Read for code navigation
- Saves tokens with compact output
- Relative paths work directly with Read/Edit tools
- Use `qi -f file.c --toc` before Read tool to understand structure
- Use `-e` to see full definitions without Read tool

## Getting Started

**Step 1: Index the SourceMinder codebase**
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
```

### Context & File Filtering

```bash
qi '*manager' -i class iface   # Only classes or interfaces
qi user -x noise               # Exclude comments and strings
qi user -f .c .h               # .c OR .h files
qi user -f src/                # All files in src/
qi user -f src/* lib/*         # Multiple directories
```

### Definition vs Usage

```bash
qi getUserById --def           # Only definitions
qi User --usage                # Only usages
```

### Multi-Pattern AND

Find lines where **ALL** patterns co-occur:

```bash
qi fprintf err_msg --and        # Both on same line
qi malloc free --and 10         # Both within 10 lines of each other
```

### Viewing Code

```bash
qi getUserById -i func -e       # Expand complete function definition
qi cursor -C 2                  # 2 lines before and after
qi cursor -A 5                  # 5 lines after
```

### Table of Contents

```bash
qi -f database.c --toc              # All definitions in file
qi -f c_language.c --toc -i func    # Only functions
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

### Limit Results

```bash
qi user --limit 10                    # First 10 matches
qi '*' --limit-per-file 3 --limit 10  # First 3 per file, up to 10 total
```

### Common Workflows

```bash
# Discover → explore → expand
qi '*' -f query-index.c --toc              # 1. Explore file structure
qi '*user*' -i func --def --limit 10       # 2. Find interesting functions
qi validateUser -i func -e                 # 3. See implementation
qi validateUser --usage -C 3               # 4. Find usage examples
```

## Advanced Filtering

qi also supports power-user filters for parent-symbol, type annotation, modifier, and scope-based queries, plus scoped search within a specific function or class. See [docs/ADVANCED_USAGE.md](docs/ADVANCED_USAGE.md) for details.

## Configuration

Config files (file extensions, ignored folders, stopwords, keywords) live under `<language>/config/` locally and `/usr/local/share/sourceminder/<language>/config/` system-wide. See [docs/CONFIGURATION.md](docs/CONFIGURATION.md) for the full reference.

## Performance & Best Practices

For index-size expectations, query-performance tips, and recommended workflows, see [docs/PERFORMANCE.md](docs/PERFORMANCE.md).

## Troubleshooting

Database-locked errors, missing symbols, build issues, and debugging tips are covered in [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).
