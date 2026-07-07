# SourceMinder

Multi-language code indexer and querent, built on SQLite and tree-sitter, written in C11.

qi (query-index) is a semantic search tool; it tells you where, and, more usefully, *what* a symbol is.

**Languages currently implemented:** C, Go, Perl, PHP, Python, Rust, TypeScript

**Database:** Creates `code-index.db` in current working directory

**Note:** This tool is designed for indexing **source code** (functions, classes, variables, ...),
not prose or documentation files (`.md`, `.txt`, `.log`, ...).

**For Programmers:** Coming up to speed on an unfamiliar codebase is often one of the most time-consuming
tasks. IDE's have long provided a "Structure" or "Outline" view of a file, so that you can see a list
of functions, and quickly navigate to them. qi's --toc flag provides a similar view from the command line.
In addition, the CTX (context) column in search results shows you *how* the result is being used on that line.
I can't count the number of times I've searched for relevant code, only to find that the code was commented
out. SourceMinder solves that problem.

**For LLMs:** qi is a faster, more token-efficient alternative to grep + cat for code navigation.
`--toc` is a file's "table of contents" overview,
`-e` shows (expands) full definitions without opening the file, and 
`--raw` prints exact source lines (hides line #:) to anchor edits.

## Build & Install

### Prerequisites

On an apt-based Linux (like Debian), install the following dependencies.
MacOS users, see MACOS_SETUP.md. Windows users, see MSYS2_SETUP.md.
I have not yet tested on non-apt Linux, so any help here would be appreciated.

```
apt install libtree-sitter-dev libtree-sitter0.25 libsqlite3-dev
```

**Note:** You may need to change libtree-sitter0.25 to libtree-sitter0, or some other version, depending on your system.

Clone the repo:

```
git clone https://github.com/ebcode/SourceMinder.git && cd SourceMinder
```

Clone the tree-sitter grammars (at least one):

```
git clone https://github.com/tree-sitter/tree-sitter-c.git
git clone https://github.com/tree-sitter/tree-sitter-go.git
git clone https://github.com/tree-sitter-perl/tree-sitter-perl.git
git clone https://github.com/tree-sitter/tree-sitter-php.git
git clone https://github.com/tree-sitter/tree-sitter-python.git
git clone https://github.com/tree-sitter/tree-sitter-rust.git
git clone https://github.com/tree-sitter/tree-sitter-typescript.git
```

### Configure, Compile & Install

Select which languages you want to build (all disabled by default):

```
./configure --enable-all                                 # All languages (recommended for testing)
./configure --enable-c --enable-typescript --enable-php  # Specific languages
./configure --enable-all --disable-php                   # All but PHP
CC=clang ./configure --enable-c                          # Custom compiler, only C
```

```
make                    # Build indexers and query tool
sudo make install       # Install to /usr/local/bin
```

**Installed binaries:** `index-c`, `index-go`, `index-perl`, `index-php`, `index-python`, `index-rust`, `index-ts`, `qi`
**Config files:** `/usr/local/share/sourceminder/<language>/config/`

## Five-Minute Tour

**1. Index a codebase** (SourceMinder itself, here):

```
index-c . --once --verbose
```

**2. Get a file's overview — always start here:**

```
qi '*' -f shared/indexer_main.c --toc
```

**3. Find a symbol:**

```
qi db_init -x noise
```

```
Searching for: db_init
Excluding context types: COM STR

LINE | SYM     | CTX
-----+---------+-----
./query-index.c:
4005 | db_init | CALL

./shared/database.c:
78   | db_init | FUNC

./shared/indexer_main.c:
491  | db_init | CALL
```

The **CTX** column is what makes qi powerful: `FUNC` is the definition, `CALL` rows are
the call sites. No more hunting down code only to find it's commented out.

**4. Drill down:**

```
qi db_init -i func -e             # Expand the full definition inline
qi db_init --usage -C 3           # Call sites with 3 lines of context
qi malloc --within db_init        # What db_init does with malloc internally
```

**5. Keep the index fresh** while you work:

```
index-c ./src &                   # Daemon: watches for changes, re-indexes
index-c ./src --once              # Or: index once and exit (CI, one-off analysis)
```

## Core Concepts

### What a symbol is: context types (`-i` include / `-x` exclude)

| Type | Short | Example |
|------|-------|---------|
| `function` | `func` | `function getUserById() { }` |
| `class` | - | `class UserManager { }` |
| `interface` | `iface` | `interface Storable { }` |
| `argument` | `arg` | `function foo(userId: string)` |
| `variable` | `var` | `const cursor = ...` |
| `property` | `prop` | `this.name = "Alice"` |
| `type` | - | `type UserID = string` |
| `macro` | - | `#define MAX_LEN 256` |
| `import` / `export` | `imp` / `exp` | `import { User } from ...` |
| `call` | - | `getUserById(123)` |
| `lambda` | - | `(x) => x * 2` |
| `enum` / `case` | - | `enum Status { Active = 1 }` |
| `namespace` | `ns` | `namespace Utils { }` |
| `trait` | - | `trait Serializable { }` (PHP, Rust) |
| `comment` | `com` | words from comments |
| `string` | `str` | words from string literals |
| `filename` | `file` | file named `user.ts` |

Run `qi --list-types` for the complete list.

```
qi user -i func var            # Only functions or variables
qi user -x noise               # Exclude comments and strings (-x comment string)
```

### Symbol metadata: filterable columns

Every symbol also carries metadata you can filter on (add `-v` to see all columns).
These filters are fuzzy by default — `-m stat` matches `static` — see Matching semantics below.

| Concept | Flag | Meaning | Example |
|---------|------|---------|---------|
| **Parent** | `-p` | Containing type, receiver, or accessed object | `qi count -p patterns` finds `patterns->count` |
| **Type** | `-t` | Type annotation | `qi '*' -i arg -t 'int *'` |
| **Modifier** | `-m` | Behavioral annotation | `-m static`, `-m async`, `-m const` |
| **Scope** | `-s` | Visibility | `-s public`; Rust uses literal `pub`, `pub(crate)` |
| **Clue** | `-c` | How a symbol is used | `-c '@property'` (decorators), `-c go` (goroutines) |
| **Definition** | `--def` / `--usage` | Definitions vs. usages | `qi getUserById --def` |

See [docs/ADVANCED_USAGE.md](docs/ADVANCED_USAGE.md) for depth on each filter.

### Matching semantics: exact symbols, fuzzy filters

Patterns are case-insensitive. The **symbol pattern** is exact by default — add
wildcards to fuzz it: `*` or `%` = any characters, `.` or `_` = one character.
If an exact pattern finds nothing, qi auto-retries as `*PATTERN*`.

The **metadata filters** (`-p`, `-t`, `-m`, `-s`, `-c`) are the opposite: fuzzy
by default (values are auto-wrapped in `%`), so `-m stat` matches `static` and
`-p pattern` matches `patterns`. (`-f` file filtering has its own path-matching
rules and is neither.)

```
qi 'get*' -i func          # getUser, getData, ... (quote * to prevent shell expansion)
qi '.etUser'               # getUser, setUser
qi 'get\*'                 # Escape for a literal: finds the symbol "get*"
qi '\--help'               # Escape a leading dash: finds "--help"
```

### Why qi is non-standard (developer ergonomics)

Most Unix tools repeat flags for multiple values: `grep -e pat1 -e pat2`.
qi instead treats everything before the first flag as search patterns (OR logic),
so that a single flag may accept multiple values without repetition — this saves typing:

```
qi malloc free calloc                        # Any of these symbols
qi user -i func var prop                     # Any of these contexts
qi symbol -f file1.c file2.c utils.h         # Any of these files
qi fprintf stderr --and                      # AND: all patterns on the same line
qi malloc free --and 10                      # AND: within 10 lines of each other
```

Because SourceMinder was designed specifically to work with both human and AI
users, some decisions were made to accommodate an AI-assisted workflow. The
exact/fuzzy asymmetry is one of them: a searcher usually knows the symbol name
*exactly* (they just read it in the output), but only *approximately* knows
metadata values — was the modifier `static` or `static inline`? Exact-matching
filters turn those guesses into zero-match dead ends; fuzzy filters turn them
into hits.

## Everyday Flags

| Flag | Purpose | Example |
|------|---------|---------|
| `-e` | Expand full definitions | `qi getUserById -i func -e` |
| `-C` / `-A` / `-B <n>` | Context lines (around/after/before) | `qi user -C 3` |
| `--toc` | File table of contents | `qi '*' -f file.c --toc` |
| `--within <sym>` | Search inside a function/class | `qi malloc --within handle_request` |
| `--and [n]` | All patterns on same line (or within n lines) | `qi fprintf stderr --and` |
| `--limit <n>`, `-lpf <n>` | Cap results (total, per file) | `qi '*' --limit 20` |
| `--files` | List matching files only (like `grep -l`) | `qi database --files` |
| `--raw` | Bare source lines (with `-e`/`-A`/`-B`/`-C`) — exact text for edit anchors | `qi db_init -i func -e --raw` |
| `-v` | All metadata columns | `qi user -v` |
| `-q` | Drop banner/footer chrome | `qi user -q` |

This is the everyday subset — `qi --help` is the complete, always-current reference.

## When to Use qi vs grep

qi is a symbol navigator; grep is a text finder. The golden rule:

> **Symbol? Use qi. Text? Use grep.**

```
Looking for CODE SYMBOLS (function, variable, class, type)?    → qi
Exploring CODE STRUCTURE (imports, relationships)?             → qi
Two or more terms appearing within a given line range?         → qi --and [N]
Need REGEX patterns or complex text matching?                  → grep
Searching NON-CODE FILES (markdown, config, logs)?             → grep
```

When in doubt, try qi first — it's easier to fall back to grep than to wade
through grep noise for code symbols. See [docs/QI_VS_GREP.md](docs/QI_VS_GREP.md)
for the full comparison and hybrid workflows.

## Indexing

**Folder mode** (recommended): `index-c ./src ./lib` recursively indexes matching
files, respects per-language ignore lists, and runs as a daemon watching for
changes. Add `--once` to index and exit instead (CI, one-off analysis). Stop a
daemon with `kill`/`killall`.

**File mode:** `index-c main.c utils.c` indexes exactly those files, ignores
ignore lists, and always runs once.

**Options:** `--once`, `--silent`, `--quiet-init` (quiet initial pass, noisy on
changes), `--verbose`, `--exclude-dir DIR [DIR...]`.

**What gets indexed:** files matching `<language>/config/file-extensions.txt`,
minus folders in the ignore list. Symbols are extracted via tree-sitter AST
parsing, with parent tracking (`this.target.getBounds()`), access modifiers, and
noise filtering (stopwords, language keywords, short symbols, pure numbers).
Paths are stored relative to the current working directory.

**Concurrent indexing:** multiple language indexers can run in parallel on the
same database — SQLite WAL mode is enabled automatically on first run.

```
index-ts ./src & index-c ./lib & index-php ./app &
```

## Configuration

Config files (file extensions, ignored folders, stopwords, keywords) live under
`<language>/config/` locally and `/usr/local/share/sourceminder/<language>/config/`
system-wide. Every binary also carries built-in defaults compiled in at build
time, used only when no config file is found on disk — so the tools work even
with nothing installed. See [docs/CONFIGURATION.md](docs/CONFIGURATION.md).

**Recommended:** set qi defaults in `~/.smconfig` so every query skips comment/string noise:

```
[qi]
-x noise
-q
```

CLI flags always override config-file defaults.

## Going Further

- [docs/GENERAL_GUIDE.md](docs/GENERAL_GUIDE.md) — newcomer-friendly qi workflow guide
- Language guides: [C](docs/C_GUIDE.md) · [Go](docs/GO_GUIDE.md) · [Python](docs/PYTHON_GUIDE.md) · [Rust](docs/RUST_GUIDE.md)
- [docs/ADVANCED_USAGE.md](docs/ADVANCED_USAGE.md) — parent/type/modifier/scope filters, `--within`, combining filters
- [docs/QI_VS_GREP.md](docs/QI_VS_GREP.md) — when to use which, hybrid workflows
- [docs/PERFORMANCE.md](docs/PERFORMANCE.md) — index sizes, query performance, best practices
- [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) — locked databases, missing symbols, build issues
