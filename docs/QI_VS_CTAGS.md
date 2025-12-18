  Comprehensive Comparison: ctags vs qi

  ---
  1. What can qi do that ctags cannot?

  Usage/Reference Tracking:
  - qi tracks both definitions AND usages (function calls, variable references, etc.)
  - ctags only indexes definitions, not where symbols are used
  - Example: qi display_context found both the function definition (line 119) and its call site (line 584)

  Advanced Query Capabilities:
  - Wildcard pattern matching with SQL LIKE syntax (get_%, %Manager, %user%)
  - Multi-pattern proximity search: qi malloc free --and 10 finds both patterns within 10 lines
  - Context type filtering: -i func var (include) or -x noise (exclude comments/strings)
  - Definition vs usage filtering: --def or --usage flags
  - File pattern filtering: -f src/%, -f %.c

  Code Context Display:
  - Expand full definitions: -e flag shows complete function bodies
  - Context lines: -C, -A, -B flags (like grep) show surrounding code
  - Table of contents: --toc shows file structure with line ranges

  Granular Symbol Detection:
  - Tracks all variable occurrences (1583 vars in query-index.c vs ctags' 4 global vars only)
  - Tracks function calls, not just definitions
  - Tracks arguments, properties, imports, exports, comments, strings

  Database-Driven Queries:
  - SQLite backend enables complex filtering combinations
  - Can query by parent symbol, scope, namespace, access modifiers, type annotations
  - Custom column output: --columns line symbol context

  ---
  2. What can ctags do that qi cannot?

  Massive Language Support:
  - ctags supports 135 languages vs qi's 5 (C, TypeScript, PHP, Go, Python)
  - Includes: Java, Ruby, Perl, Shell, SQL, YAML, Markdown, Lua, R, Haskell, etc.

  Editor Integration:
  - Native vim/emacs integration via standardized tags file format
  - Jump-to-definition with Ctrl+] in vim works out of the box
  - Emacs etags format support (-e flag)
  - Search pattern format for editors: /^static const char* display_context(...$/

  Regex-Based Extensibility:
  - Define custom patterns with --regex-<LANG> for new languages
  - Create custom parsers without recompiling
  - --langdef for entirely new language definitions

  Specialized Output Formats:
  - Cross-reference format (-x): human-readable tabular output
  - JSON output: --output-format=json
  - Sorted tags for fast binary search

  Lighter Weight:
  - Plain text file (258K lines) vs 9.5MB SQLite database
  - No daemon required; one-time generation
  - Can be checked into version control

  Prototype Support:
  - Distinguishes function prototypes from definitions (--c-kinds=+p)
  - qi doesn't explicitly separate prototypes

  ---
  3. What programming tasks are best suited for ctags?

  1. Editor Navigation (Primary Use Case):
  - Jump to definition with Ctrl+] in vim
  - Code completion and symbol lookup in editors
  - Tag stack navigation (Ctrl+t to go back)
  - Quick reference while coding

  2. Multi-Language Polyglot Projects:
  - Projects using languages qi doesn't support (Java, Ruby, Rust, etc.)
  - Mixed-language codebases (e.g., Rails app with Ruby, JS, YAML, SQL)
  - Documentation generation from source code

  3. Quick Symbol Lookup:
  - Finding where a function/class is defined (not where it's used)
  - Generating symbol lists for documentation
  - Code auditing (finding all macros, enums, typedefs)

  4. CI/CD and Automation:
  - Lightweight indexing in build pipelines
  - Generating API documentation lists
  - Checking for symbol conflicts across files

  5. Legacy or Esoteric Language Support:
  - Working with older languages (COBOL, Fortran, Ada)
  - Domain-specific languages
  - Custom language support via regex patterns

  Best when:
  - You need editor integration for jump-to-definition
  - You're working with languages outside C/TS/PHP/Go/Python
  - You want a simple, portable text-based index
  - You don't need to track symbol usages

  ---
  4. What programming tasks are best suited for qi?

  1. Code Exploration & Understanding:
  - Finding all usages of a function/variable (refactoring, impact analysis)
  - Understanding call chains: "Where is this function called from?"
  - Exploring unfamiliar codebases with pattern searches

  2. Refactoring & Code Changes:
  - Finding all occurrences to rename: qi getUserById --usage
  - Type migration: qi % -i arg -t "OldType *" finds all function parameters
  - Verifying changes: qi OldType% confirms no old references remain

  3. Semantic Code Search:
  - Context-aware searches: qi user -i func var -x noise (exclude comments)
  - Proximity searches: qi malloc free --and 10 (memory leak hunting)
  - Finding patterns: qi %Manager -i class (all Manager classes)

  4. Code Analysis:
  - Finding all definitions in a file: qi % -f auth.c --toc
  - Analyzing variable usage patterns across files
  - Tracking relationships (parent symbols, member access)

  5. Working with Supported Languages Deeply:
  - TypeScript private members, decorators, interfaces
  - PHP traits, namespaces
  - Python classes, decorators
  - Go interfaces, struct methods
  - C structs, function pointers

  6. Token-Efficient LLM Queries (Claude Code):
  - Compact output saves tokens
  - Context filtering reduces noise
  - -e flag shows full code without separate Read calls
  - Relative paths work directly with Edit tools

  Best when:
  - You need to find usages, not just definitions
  - You're working with C, TypeScript, PHP, Go, or Python
  - You want semantic search with wildcards and filters
  - You need to see code context or full definitions
  - You're doing refactoring or impact analysis
  - You want a modern, queryable code database

  ---
  Summary Table

  | Feature            | ctags                     | qi                                |
  |--------------------|---------------------------|-----------------------------------|
  | Languages          | 135+                      | 5 (C, TS, PHP, Go, Python)        |
  | Tracks Usages      | ✗                         | ✓                                 |
  | Tracks Definitions | ✓                         | ✓                                 |
  | Editor Integration | ✓ Native                  | ✗ Manual                          |
  | Wildcard Search    | ✗ (via grep)              | ✓ Built-in                        |
  | Context Filtering  | Kind-based                | Rich (func/var/class/comment/etc) |
  | Show Code          | Single line               | Full defs + context               |
  | Storage            | Text file                 | SQLite DB                         |
  | Query Speed        | Fast (grep/binary search) | Fast (indexed SQL)                |
  | Proximity Search   | ✗                         | ✓                                 |
  | Granularity        | Definitions only          | All occurrences                   |
  | Extensibility      | Regex patterns            | Requires C code                   |
  | File Size          | Smaller (text)            | Larger (9.5MB DB)                 |
