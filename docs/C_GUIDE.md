# C Developer's Guide to qi

A practical guide for using qi to search and analyze C codebases. This guide focuses on C-specific patterns and workflows that leverage qi's semantic understanding of C code.

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Core Concepts](#core-concepts)
3. [Finding Definitions](#finding-definitions)
4. [Memory Management](#memory-management)
5. [Control Flow & Error Handling](#control-flow--error-handling)
6. [Preprocessor Directives](#preprocessor-directives)
7. [Structs and Types](#structs-and-types)
8. [Common Workflows](#common-workflows)
9. [Advanced Queries](#advanced-queries)
10. [Tips & Tricks](#tips--tricks)

---

## Quick Start

### Index Your C Project

```bash
# Index current directory
./index-c .

# Index specific directory
./index-c ./src

# Index multiple directories
./index-c ./src ./lib ./include
```

### Basic Search

```bash
# Find all uses of a symbol
qi "getUserById"

# Find function definition
qi "getUserById" -i func

# Find with code context
qi "getUserById" -i func -C 10

# Exclude noise (comments/strings)
qi "error" -x noise
```

### Your First Steps with qi (Recommended Workflow)

**Start with structure, then drill down:**

```bash
# 1. Get file overview (always start here!)
qi '*' -f 'database.c' --toc

# 2. See a specific function's implementation
qi "init_database" -i func -e

# 3. Find where it's used
qi "init_database" -i call --limit 20

# 4. Understand what happens inside
qi 'conn' --within 'init_database' -x noise

# 5. Explore error handling patterns
qi 'cleanup' -i goto --within 'init_database'
```

**Why this order?**
- `--toc` gives you the map (functions, types, macros, imports)
- `-e` shows you the code (full definitions inline)
- `--within` reveals what a function does
- `-i goto` finds error handling patterns
- `-p` shows you how structs are accessed

This is **way more efficient** than reading files or using grep!

---

## Core Concepts

### Context Types

qi understands these C-specific contexts:

| Context | Abbreviation | Description |
|---------|--------------|-------------|
| `function` | `func` | Function definitions |
| `variable` | `var` | Variables and constants |
| `argument` | `arg` | Function parameters |
| `type` | `type` | Struct, enum, union, typedef |
| `property` | `prop` | Struct/union fields |
| `call` | `call` | Function calls |
| `import` | `imp` | #include directives |
| `enum` | `enum` | Enum definitions |
| `case` | `case` | Enum values |
| `label` | `label` | Labels for goto |
| `goto` | `goto` | Goto statements |
| `comment` | `com` | Words from comments |
| `string` | `str` | Words from string literals |
| `filename` | `file` | Filename without extension |

### Modifiers

qi tracks C-specific modifiers:

| Modifier | Meaning |
|----------|---------|
| `static` | Static function or variable |
| `const` | Const-qualified variable |
| `inline` | Inline function |
| `extern` | External linkage |
| `volatile` | Volatile-qualified variable |

---

## Finding Definitions

### Functions

```bash
# Find function definition
qi "process_request" -i func

# Find all functions in a file
qi '%' -i func -f '%handler.c'

# Find static (private) functions
qi '%' -i func -m static --limit 20

# Find inline functions
qi '%' -i func -m inline

# Expand function to see full code
qi "init_database" -i func -e
```

### Variables and Constants

```bash
# Find variable definition
qi "MAX_BUFFER_SIZE" -i var

# Find all static variables (file-scope)
qi '%' -i var -m static --limit 20

# Find const variables
qi '%' -i var -m const

# Find all global variables
qi '%' -i var --def -f '%.c' --limit 30
```

### Types (Structs, Enums, Unions, Typedefs)

```bash
# Find struct definition
qi "Connection" -i type

# Find enum definition
qi "ErrorCode" -i enum

# Find all types in a file
qi '%' -i type -f '%database.h'

# Find enum values
qi '%' -i case -f '%constants.h'

# Expand type definition
qi "ParseResult" -i type -e
```

---

## Memory Management

### Finding Allocation Patterns

```bash
# Find all malloc calls
qi 'malloc' -i call

# Find all memory allocation functions
qi 'malloc' 'calloc' 'realloc' -i call --limit 30

# Find allocations in specific file
qi 'malloc' 'calloc' -i call -f '%memory.c'

# Show allocation context
qi 'malloc' -i call -C 5
```

### Finding Deallocation Patterns

```bash
# Find all free calls
qi 'free' -i call

# Find free calls in specific file
qi 'free' -i call -f '%utils.c'

# Show deallocation context
qi 'free' -i call -C 3
```

### Memory Audit Workflow

```bash
# 1. Find all allocations
qi 'malloc' 'calloc' 'realloc' -i call --columns line,symbol,parent

# 2. Find all deallocations
qi 'free' -i call --columns line,symbol

# 3. Check for NULL checks
qi 'malloc' 'NULL' --and 3 -x noise

# 4. Find goto cleanup patterns (common error handling)
qi 'malloc' 'cleanup' --and 10 -f '%.c' --limit 20

# 5. Check specific function's memory management
qi 'malloc' 'free' --within 'parse_config' -C 3
```

### Finding Memory Leaks

```bash
# Find functions with malloc but no free
qi 'malloc' --within 'process_data'
qi 'free' --within 'process_data'

# Find functions with early returns (potential leaks)
qi 'return' --within 'init_connection' -x noise --limit 10
qi 'free' --within 'init_connection'
```

---

## Control Flow & Error Handling

### Goto and Labels (Error Handling Pattern)

```bash
# Find all goto statements
qi '%' -i goto

# Find cleanup labels
qi 'cleanup' 'error' 'fail' 'done' -i label

# Find goto cleanup patterns
qi 'cleanup' -i goto --limit 20

# Find error handling in specific function
qi 'cleanup' 'error' --within 'main' -C 5

# Show full error handling flow
qi 'goto' 'cleanup' --and 3 -f '%.c' --limit 15
```

### Common Error Handling Patterns

```bash
# Find goto cleanup pattern
qi 'cleanup' -i goto -i label --limit 20

# Find error returns with goto
qi 'goto' 'cleanup' --and 2 -C 5

# Find NULL checks before goto
qi 'NULL' 'goto' --and 5 -x noise --limit 15

# Find functions using cleanup pattern
qi 'cleanup' -i label --columns line,parent
```

### Switch Statements

```bash
# Find case labels in switch
qi '%' -i case -f '%.c' --limit 30

# Find switch patterns
qi 'case' 'default' --and 20 -f '%dispatch.c'

# Find enum switch patterns
qi 'CONTEXT_%' -i case --limit 20
```

### Conditional Patterns

```bash
# Find NULL checks
qi 'NULL' -x noise --limit 30

# Find error checks
qi 'err' 'error' --and 2 -x noise --limit 20

# Find return value checks
qi 'ret' 'return' --and 3 --within 'main'
```

---

## Preprocessor Directives

### Finding Macros and Includes

```bash
# Find #include directives
qi '%' -i imp -f '%.h' --limit 30

# Find specific header includes
qi 'stdio.h' -i imp
qi 'stdlib.h' -i imp

# Find system includes vs local includes
qi '<%.h>' -i imp         # System headers
qi '"%.h"' -i imp         # Local headers

# Find all imports in a file
qi '*' -f 'database.c' --toc -i imp
```

### Macro Patterns

```bash
# Note: #define is filtered by default as it's a keyword
# Instead, find macro usage patterns:

# Find common macro patterns
qi 'MAX_%' -i var
qi 'MIN_%' -i var
qi '%_SIZE' -i var

# Find macro-like constants (uppercase)
qi 'MAX_BUFFER_SIZE' 'MAX_PATH' 'BUFFER_SIZE' -i var
```

---

## Structs and Types

### Finding Struct Definitions

```bash
# Find struct type
qi "Connection" -i type

# Find all struct types in file
qi '%' -i type -f '%database.h'

# Expand struct to see fields
qi "ParseResult" -i type -e

# Find struct definitions across project
qi '%Result' '%Config' '%Context' -i type --limit 30
```

### Finding Struct Fields

```bash
# Find struct fields by name
qi "count" "capacity" -i prop --limit 20

# Find fields in specific struct (via parent)
qi '%' -i prop --columns line,symbol,parent --limit 30

# Find pointer fields
qi '%' -i prop --columns line,symbol,type --limit 20

# Find fields accessed in code
qi 'count' -i var -p '%' --limit 20
```

### Struct Field Access Patterns

```bash
# Find field access on specific struct
qi '%' -p 'result' --limit 20

# Find specific field access
qi 'count' -p 'list' -C 3

# Find nested field access
qi '%' -p 'config' --columns line,symbol,parent --limit 30

# Show field usage context
qi 'symbols' -p 'filter' -C 5
```

### Type Analysis

```bash
# Find all types
qi '%' -i type -f '%.h' --limit 30

# Find pointer types
qi '%' -i type --columns line,symbol,type | grep '\*'

# Find typedef patterns
qi '%_t' -i type         # Common typedef suffix
qi '%Ptr' '%Ref' -i type # Pointer type patterns
```

---

## Common Workflows

### Understanding a New Codebase (START HERE!)

**Always start with Table of Contents:**

```bash
# 1. Get the entry point structure
qi '*' -f 'main.c' --toc

# Shows:
# - What's included (dependencies)
# - What functions are defined
# - What types are defined (with line ranges!)

# 2. Explore key modules by pattern
qi '*' -f '%handler.c' --toc
qi '*' -f '%parser.c' --toc
qi '*' -f 'shared/%.c' --toc

# 3. Find main entry points
qi 'main' 'init' 'run' -i func

# 4. See implementations with -e
qi 'main' -i func -e
qi 'init_database' -i func -e
```

**Why TOC first?**
- Instant overview without opening files
- Shows line ranges (jump directly to code)
- Reveals module structure and dependencies
- Faster than reading entire files

### Understanding a Function

**Complete workflow from discovery to understanding:**

```bash
# 1. Find function (location)
qi "parse_config" -i func

# 2. See full implementation (best approach!)
qi "parse_config" -i func -e

# 3. Find what the function does with specific variables
qi 'config' --within 'parse_config' -x noise
qi 'error' --within 'parse_config' -x noise

# 4. Check error handling
qi 'goto' --within 'parse_config'
qi 'cleanup' -i label --within 'parse_config'

# 5. Find all calls to this function
qi "parse_config" -i call --limit 20

# 6. See call context
qi "parse_config" -i call -C 5

# 7. Check memory management
qi 'malloc' 'free' --within 'parse_config'
```

**Pro tip:** Use `-e` instead of reading files! It shows code inline.

### Tracing Memory Management

```bash
# 1. Find allocation
qi "init_context" -i func -e

# 2. Find all allocations in function
qi 'malloc' 'calloc' --within 'init_context' -C 3

# 3. Find deallocations
qi 'free' --within 'init_context' -C 3

# 4. Check error paths (goto cleanup)
qi 'cleanup' --within 'init_context'

# 5. Find NULL checks
qi 'NULL' --within 'init_context' -x noise

# 6. Verify cleanup label
qi 'cleanup' -i label -f '%context.c'
```

### Finding Error Handling Patterns

```bash
# Find all functions using goto cleanup
qi 'cleanup' -i goto --columns line,parent --limit 30

# Find cleanup labels
qi 'cleanup' 'error' 'fail' -i label

# See full error flow in function
qi 'goto' 'cleanup' --within 'db_connect' -C 5

# Find error message patterns
qi 'fprintf' 'stderr' --and -x noise --limit 20

# Find perror usage
qi 'perror' -i call -C 3
```

### Refactoring a Struct

```bash
# 1. Find struct definition
qi "Connection" -i type -e

# 2. Find all fields
qi '%' -i prop --columns line,symbol,parent | grep Connection

# 3. Find all variable declarations of this type
qi '%' -i var --columns line,symbol,type | grep Connection

# 4. Find all field accesses
qi '%' -p 'conn' --limit 30
qi '%' -p 'connection' --limit 30

# 5. Find all function parameters of this type
qi '%' -i arg --columns line,symbol,type | grep Connection

# 6. Find functions that operate on this type
qi 'Connection' -i type --columns line,parent
```

### Finding API Boundaries

```bash
# Find all non-static (exported) functions
qi '%' -i func --def -f '%.c' --limit 50 | grep -v static

# Find function prototypes in headers
qi '%' -i func -f '%.h' --limit 30

# Find public API functions (common prefixes)
qi 'db_%' 'parse_%' 'init_%' -i func --def

# Find static (internal) helper functions
qi '%' -i func -m static --limit 30
```

---

## Power User Combinations

These combinations unlock qi's full potential by stacking filters creatively for C code.

### Combining Memory + Error Handling + Within

```bash
# What allocations happen in a function?
qi 'malloc' 'calloc' --within 'parse_file' -C 3

# Check cleanup for allocations
qi 'malloc' 'cleanup' --and 10 --within 'parse_file'

# Find free calls in cleanup section
qi 'free' --within 'parse_file' -C 3

# Check NULL handling
qi 'malloc' 'NULL' --and 3 --within 'parse_file' -x noise
```

### Combining Goto + Labels + Context

```bash
# Find all error handling labels
qi 'cleanup' 'error' 'fail' -i label --columns line,parent

# Find gotos to specific labels
qi 'cleanup' -i goto -C 3

# Find functions with most gotos (complex error handling)
qi '%' -i goto --columns parent | tail -n +3 | sort | uniq -c | sort -rn | head -10

# See full error flow
qi 'goto' 'cleanup' --and 5 -C 10 --limit 5
```

### Combining Static + Functions + File

```bash
# Find all static functions in module
qi '%' -i func -m static -f '%database.c'

# Find helper functions (often static)
qi '%helper' '%internal' '%impl' -i func -m static

# Find file-scoped variables
qi '%' -i var -m static -f '%.c' --limit 30

# Sample static functions across codebase
qi '%' -i func -m static --limit-per-file 3
```

### Combining Struct + Fields + Parent

```bash
# Find how config struct is used
qi '%' -p 'config' --limit 30 -C 2

# Find specific field accesses
qi 'timeout' 'max_connections' -p 'config'

# Find nested struct access
qi '%' -p 'result' --columns line,symbol,parent --limit 20

# See field usage patterns
qi 'count' -p '%' --columns line,parent --limit 30
```

### Creative Sampling

```bash
# Sample memory allocations across codebase
qi 'malloc' -i call --limit-per-file 2

# Sample error handling per file
qi 'cleanup' -i goto --limit-per-file 3

# Sample API functions per module
qi '%_init' '%_create' -i func --limit-per-file 2

# Compare distribution
qi 'free' -i call --limit 20              # Might all be in one file
qi 'free' -i call --limit-per-file 3      # Distributed across files
```

---

## Advanced Queries

### Parent Symbol Filtering (`-p`)

The `-p` flag filters by the parent symbol - extremely useful for understanding struct field access patterns.

```bash
# Find all accesses on db connection objects
qi '%' -p 'conn' --limit 20

# Find specific field access
qi 'handle' -p 'db'
qi 'socket' -p 'conn'

# Find method-like patterns (struct with function pointers)
qi '%' -i call -p 'parser' --limit 20

# Show parent relationships
qi '%' -p '%' --columns line,symbol,parent --limit 30
```

**Use cases:**
- Understanding how structs are used
- Finding field access patterns
- Debugging struct interactions
- API exploration

**Note:** Parent is populated for field **access**, not field definitions in struct declarations.

### Scoped Search (`--within`)

The `--within` flag searches only within specific function definitions - fantastic for understanding function behavior.

```bash
# What does parse_config do with 'buffer'?
qi 'buffer' --within 'parse_config' -C 3

# What allocations happen in a function?
qi 'malloc' 'calloc' --within 'init_database'

# Find all local variables in a function
qi '%' -i var --within 'process_request'

# Multiple functions (OR logic)
qi 'error' --within 'parse' 'process' 'validate'

# Find error handling within function
qi 'goto' 'cleanup' --within 'db_connect' -C 5

# Exclude noise for cleaner results
qi 'conn' --within 'init_database' -x noise
```

**Use cases:**
- Understanding function behavior
- Finding local error handling
- Debugging specific functions
- Code review (checking what a function touches)

**Why better than grep:** Understands function boundaries! grep can't tell where a function starts and ends.

### Related Patterns (`--and`)

Find lines where **multiple patterns** co-occur, either on same line or within N lines.

```bash
# Find 'malloc' and 'NULL' on the same line
qi 'malloc' 'NULL' --and

# Find 'malloc' and 'free' within 10 lines
qi 'malloc' 'free' --and 10

# Find error handling patterns
qi 'if' 'goto' --and 2 -x noise

# Find cleanup patterns
qi 'free' 'cleanup' --and 5

# Exclude noise for cleaner results
qi 'conn' 'db' --and -x noise

# With context to see the pattern
qi 'malloc' 'sizeof' --and 2 -C 3
```

**Use cases:**
- Finding related variables/patterns
- Understanding error handling
- Finding memory management patterns (malloc + free, malloc + NULL)
- Resource management (open + close, lock + unlock)

**Performance tip:** Smaller ranges are faster. `--and` (same line) is fastest.

### Sampling Results (`--limit-per-file`)

Limit results to N matches per file for distributed sampling across codebase.

```bash
# Sample malloc calls per file
qi 'malloc' -i call --limit-per-file 3

# Sample error handling
qi 'cleanup' -i goto --limit-per-file 2

# Sample static functions
qi '%' -i func -m static --limit-per-file 3

# Sample struct usage
qi '%' -p 'config' --limit-per-file 2
```

**Use cases:**
- Quick overview across codebase
- Preventing one file from dominating
- Understanding pattern distribution
- Getting diverse examples

**Difference from `--limit`:**
- `--limit 10` = first 10 matches total (might all be from one file)
- `--limit-per-file 3` = up to 3 matches from EACH file (distributed)

### Multi-Column Display

```bash
# Show type annotations
qi '%' -i var --columns line,symbol,type --limit 20

# Show modifiers (static, const, etc.)
qi '%' -i func --columns line,symbol,modifier --limit 20

# Show parent for field access
qi '%' -i var --columns line,symbol,parent --limit 20

# Show definition vs usage
qi 'parse_config' --columns line,symbol,is_definition

# Verbose mode (all columns)
qi 'malloc' -i call -v --limit 5
```

### File Filtering

```bash
# Specific file
qi "handler" -f "server.c"

# All C files in directory
qi '%' -i func -f 'src/%.c'

# Multiple file patterns
qi "Config" -i type -f '%.h' '%.c'

# Header files only
qi '%' -i func -f '%.h' --limit 30

# Implementation files only
qi '%' -i func -f '%.c' --limit 30
```

### Excluding Noise

```bash
# Exclude comments and strings
qi "error" -x noise

# Exclude specific contexts
qi "test" -x comment -x string

# Focus on code only
qi "TODO" -x noise
qi "FIXME" -x noise
```

### Context Display

```bash
# Show N lines after match
qi "malloc" -A 5

# Show N lines before match
qi "free" -B 3

# Show N lines before and after
qi "parse_config" -C 20

# Maximum context
qi "error_handler" -C 50
```

### Limiting Results

```bash
# Show first N matches
qi '%' -i func --limit 20

# Quick overview
qi '%' -i type --limit 10

# Sample functions
qi '%_init' '%_create' -i func --limit 15
```

### Table of Contents

```bash
# File overview
qi '*' -f 'database.c' --toc

# Multiple files
qi '*' -f '%handler.c' --toc

# Show only functions
qi '*' -f 'utils.c' --toc -i func

# Show only types
qi '*' -f 'types.h' --toc -i type
```

---

## Tips & Tricks

### Search Patterns

```bash
# Wildcard at start
qi '%_init' -i func      # Ends with _init

# Wildcard at end
qi 'db_%' -i func        # Starts with db_

# Wildcard both sides
qi '%handler%' -i func   # Contains handler

# Match anything
qi '%' -i func           # All functions
```

### Common C Naming Patterns

```bash
# Initialization functions
qi 'init_%' '%_init' -i func

# Cleanup/destroy functions
qi 'free_%' '%_free' 'destroy_%' '%_destroy' -i func

# Create/new functions
qi 'create_%' '%_create' 'new_%' '%_new' -i func

# Getter functions
qi 'get_%' -i func

# Setter functions
qi 'set_%' -i func

# Error codes
qi 'ERR_%' 'ERROR_%' -i var
qi '%_ERROR' '%_ERR' -i case

# Private functions (static)
qi '%' -i func -m static

# Constants (uppercase)
qi 'MAX_%' 'MIN_%' '%_SIZE' -i var
```

### Combining with Shell Tools

```bash
# Count functions per file
qi '%' -i func --columns line | cut -d: -f1 | sort | uniq -c | sort -rn | head -10

# Find most-called functions
qi '%' -i call --columns symbol | tail -n +3 | sort | uniq -c | sort -rn | head -10

# List all unique type names
qi '%' -i type --columns symbol | tail -n +2 | sort -u

# Find files with most gotos
qi '%' -i goto --columns line | cut -d: -f1 | sort | uniq -c | sort -rn | head -10

# Find files with most malloc calls
qi 'malloc' -i call --columns line | cut -d: -f1 | sort | uniq -c | sort -rn
```

### Performance Tips

```bash
# Use file filters to narrow scope
qi "handler" -f '%/src/%' -i func

# Use context filters
qi "test" -i func                    # Only functions

# Exclude noise for faster results
qi "error" -x noise

# Limit results for quick checks
qi '%' -i func --limit 50
```

### Debugging Queries

```bash
# Use verbose mode
qi "symbol" -v

# Check what's indexed
qi '%' --limit 20 -v

# Verify context types
qi '%' -i func --limit 5
qi '%' -i var --limit 5
qi '%' -i type --limit 5

# See all columns
qi "test" -v --limit 3
```

---

## Real-World Examples

### Example 1: Finding Memory Leaks

```bash
# Step 1: Find all allocations in module
qi 'malloc' 'calloc' 'realloc' -i call -f '%parser.c'

# Step 2: Find all frees
qi 'free' -i call -f '%parser.c'

# Step 3: Check specific function
qi 'malloc' --within 'parse_config' -C 3
qi 'free' --within 'parse_config' -C 3

# Step 4: Look for early returns (potential leaks)
qi 'return' --within 'parse_config' -x noise --limit 10

# Step 5: Verify cleanup label exists
qi 'cleanup' -i label -f '%parser.c'
qi 'cleanup' -i goto -f '%parser.c'
```

### Example 2: Auditing Error Handling

```bash
# Find all functions using goto cleanup
qi 'cleanup' -i goto --columns parent | tail -n +3 | sort -u

# Find cleanup labels
qi 'cleanup' 'error' 'fail' 'done' -i label

# Find functions with cleanup but no label
# (manually verify each has corresponding label)
qi 'cleanup' -i goto -f '%database.c'
qi 'cleanup' -i label -f '%database.c'

# Check error message patterns
qi 'fprintf' 'stderr' --and -f '%database.c'

# Verify NULL checks before dereferencing
qi 'NULL' 'if' --and 2 -f '%parser.c' -x noise
```

### Example 3: Understanding Struct Usage

```bash
# Find struct definition
qi "Connection" -i type -e

# Find all fields
qi '%' -i prop --columns line,symbol,parent | grep Connection

# Find how struct is used
qi '%' -p 'conn' --limit 30
qi '%' -p 'connection' --limit 30

# Find initialization pattern
qi 'Connection' 'malloc' --and 5 -C 3

# Find cleanup pattern
qi 'connection' 'free' --and 5 -C 3
```

### Example 4: API Surface Analysis

```bash
# Find all non-static functions in module
qi '%' -i func -m static -f '%database.c'  # Internal functions
# (Then search all functions and compare)

# Find public API (in headers)
qi '%' -i func -f 'database.h'

# Find all functions with specific prefix (API convention)
qi 'db_%' -i func --def

# Find functions called from outside
qi '%' -i call -f '%main.c' | grep db_
```

### Example 5: Refactoring Type Safety

```bash
# Find all uses of old type
qi 'int' -i type --columns line,symbol,type -f '%handler.c'

# Find all function signatures using type
qi '%' -i func --columns line,symbol,type | grep 'int handle'

# Find all variable declarations
qi '%' -i var --columns line,symbol,type | grep 'int'

# Find all function arguments
qi '%' -i arg --columns line,symbol,type | grep 'int'
```

### Example 6: Finding Similar Code (Copy-Paste Detection)

```bash
# Find functions with same structure
qi 'malloc' 'NULL' 'goto' --and 10 -C 5 --limit 10

# Find similar error messages
qi 'fprintf' 'stderr' --and -C 3 | grep "Error:"

# Find duplicate pattern usage
qi 'strcmp' 'if' --and 2 -C 3 --limit 20
```

---

## Troubleshooting

### Pattern Not Matching

```bash
# Problem: Symbol not found
qi "MyFunction" -i func
# No results

# Solution 1: Try wildcard
qi "%function%" -i func

# Solution 2: Search without context filter
qi "MyFunction"

# Solution 3: Try case variations
qi "myfunction" -i func
```

### Too Many Results

```bash
# Problem: Thousands of matches
qi "test"
# Found 5000 matches

# Solution 1: Add context filter
qi "test" -i func

# Solution 2: Exclude noise
qi "test" -x noise

# Solution 3: Add file filter
qi "test" -i func -f '%handler.c'

# Solution 4: Use limit
qi "test" -i func --limit 20
```

### Symbol is a Keyword

```bash
# Problem: Keyword filtered
qi "return"
# Note: 'return' is in keywords.txt

# Solution: Use wildcard
qi "%return%"

# Or search in specific context
qi "return" -i var  # Variables named return
```

### Need to Search for Reserved Words

```bash
# Many C keywords are filtered (struct, sizeof, return, etc.)
# Use wildcards to find related symbols:

qi "%struct%" -i type   # Find types with "struct" in name
qi "%size%" -i var      # Find size-related variables
qi "%return%" -i func   # Find return-related functions
```

---

## Best Practices & Learning Path

### Progressive Learning Path

**Level 1: Basics (Start here)**
```bash
qi "symbol"                    # Find a symbol
qi "symbol" -i func            # Filter by context
qi "symbol" -x noise           # Exclude comments/strings
```

**Level 2: Structure First (Game changer!)**
```bash
qi '*' -f 'file.c' --toc       # Always start with TOC
qi "func" -i func -e           # See implementations with -e
qi "Connection" -i type -e     # Expand type definitions
```

**Level 3: C-Specific Patterns**
```bash
qi '%' -i goto                 # Find goto statements
qi 'cleanup' -i label          # Find cleanup labels
qi 'malloc' 'free' -i call     # Find memory operations
qi '%' -i func -m static       # Find static functions
```

**Level 4: Advanced Filtering**
```bash
qi 'count' -i var -p 'list'    # Filter by parent
qi 'malloc' --within 'init'    # Scoped search
qi 'err' 'NULL' --and          # Related patterns
```

**Level 5: Power Combinations**
```bash
qi 'malloc' 'cleanup' --within 'parse_file' --and 10
qi '%' -p 'config' --limit-per-file 2
qi 'cleanup' -i goto --columns parent | sort | uniq -c
```

**Level 6: Master Workflows**
- Combine TOC + -e + --within for complete understanding
- Use --and for error handling analysis (malloc + NULL, goto + cleanup)
- Stack filters for memory leak detection
- Use --limit-per-file for distributed sampling

### Common Pitfalls to Avoid

**Don't do this:**
```bash
# Reading files instead of using TOC
cat database.c | less

# Using grep for semantic search
grep -r "init_database" .

# Forgetting to exclude noise
qi "error"  # Includes comments/strings

# Not using -e to see code
qi "func" -i func  # Just shows line numbers

# Ignoring memory patterns
# (Not checking malloc + free together)
```

**Do this instead:**
```bash
# Use TOC for structure
qi '*' -f 'database.c' --toc

# Use qi for semantic search
qi "init_database" -i func

# Always exclude noise for code symbols
qi "error" -x noise

# See code inline
qi "func" -i func -e

# Check memory patterns
qi 'malloc' 'free' --within 'function' -C 3
```

### Efficiency Tips

1. **Start with TOC**: `qi '*' -f 'file.c' --toc` shows structure instantly
2. **Use -e liberally**: Seeing code inline is faster than jumping to files
3. **Check error handling**: `-i goto` and `-i label` reveal cleanup patterns
4. **Use --within**: Understanding what a function does without reading entire file
5. **Sample with --limit-per-file**: Get distributed examples across codebase
6. **Exclude noise by default**: Add `-x noise` to most queries
7. **Look for patterns**: Use `--and` to find malloc + NULL, goto + cleanup, etc.

### When to Use What

| Task | Best Tool |
|------|-----------|
| File structure | `qi '*' -f 'file.c' --toc` |
| See function code | `qi "func" -i func -e` |
| Memory allocation | `qi 'malloc' -i call` |
| Memory deallocation | `qi 'free' -i call` |
| Error handling | `qi 'cleanup' -i goto -i label` |
| Static functions | `qi '%' -i func -m static` |
| Struct fields | `qi '%' -i prop` |
| Field access | `qi '%' -p 'struct_name'` |
| What function does | `qi '%' --within 'function'` |
| Related patterns | `qi 'a' 'b' --and 5` |
| Sample across files | `qi '%' --limit-per-file 3` |
| Literal text search | `grep` (not qi) |

### C-Specific Best Practices

**For Memory Management:**
```bash
# Always check pairs
qi 'malloc' --within 'function'
qi 'free' --within 'function'

# Verify NULL checks
qi 'malloc' 'NULL' --and 3 --within 'function'

# Check cleanup paths
qi 'cleanup' -i label -i goto -f 'file.c'
```

**For Error Handling:**
```bash
# Find error pattern
qi 'cleanup' -i goto --columns parent

# Verify cleanup labels exist
qi 'cleanup' -i label
```

**For API Design:**
```bash
# Find public functions (non-static)
qi '%' -i func --def -f '%.h'

# Find internal helpers
qi '%' -i func -m static --limit 30
```

---

## Comparison with Other Tools

### qi vs grep

```bash
# grep: Text-based, many false positives
grep -r "init_database" .

# qi: Semantic, finds only real functions
qi "init_database" -i func

# grep: Can't distinguish contexts
grep -r "Connection" .

# qi: Finds only type definitions
qi "Connection" -i type
```

### qi vs cscope/ctags

```bash
# cscope: Symbol database, but less flexible queries
# qi: Rich semantic queries with filtering

# cscope: Find function definition
cscope -d -L1 init_database

# qi: Find function + see code + find usage
qi "init_database" -i func -e
qi "init_database" -i call --limit 20

# qi advantages:
qi '%' -i func -m static               # Find all static functions
qi 'malloc' --within 'parse_config'    # Scoped search
qi 'cleanup' -i goto -i label          # Error handling patterns
```

### qi vs IDE "Find References"

```bash
# IDE: Great for active development
# qi: Great for codebase-wide analysis, scriptable

# IDE: Find usages of function
# (click, wait, see results in window)

# qi: Find all usages + patterns + sampling
qi "process_request" -i call
qi '%' -i goto --columns parent        # All error handling
qi 'malloc' --limit-per-file 2         # Sample across files

# qi advantage: Scriptable and composable
qi '%' -i goto | wc -l                 # Count goto statements
```

---

## Glossary

**Context**: What a symbol *is* (function, variable, type, etc.)

**Clue**: How a symbol is being *used* (currently minimal in C indexer)

**Definition**: The location where a symbol is declared (--def or is_definition=1)

**Usage**: A reference to a symbol (--usage or is_definition=0)

**Modifier**: Declaration modifiers (static, const, inline, extern, volatile)

**Parent**: The containing struct for field access (e.g., `conn->socket` has parent `conn`)

**Scope**: Visibility (file-local via static, or external)

**Type**: Variable type, function return type, struct definition, etc.

---

## Getting Help

```bash
# Show full help
qi --help

# List available columns
qi "test" --columns invalid   # Shows available columns in error

# Check version
qi --version

# Report issues
# https://github.com/your-repo/indexer-c/issues
```

---

## Quick Command Reference

| Task | Command |
|------|---------|
| File structure | `qi '*' -f 'file.c' --toc` |
| Find function | `qi "name" -i func` |
| Expand definition | `qi "name" -i func -e` |
| Find goto statements | `qi '%' -i goto` |
| Find labels | `qi 'cleanup' -i label` |
| Find malloc calls | `qi 'malloc' -i call` |
| Find free calls | `qi 'free' -i call` |
| Static functions | `qi '%' -i func -m static` |
| Struct fields | `qi '%' -i prop` |
| Field access | `qi 'field' -p 'struct'` |
| Search within function | `qi 'malloc' --within 'init'` |
| Related patterns (same line) | `qi 'malloc' 'NULL' --and` |
| Related patterns (N lines) | `qi 'malloc' 'free' --and 10` |
| Limit per file | `qi '%' --limit-per-file 3` |
| Show context | `qi "name" -C 20` |
| Exclude noise | `qi "name" -x noise` |
| Limit results | `qi '%' --limit 50` |
| Custom columns | `qi '%' --columns line,symbol,type` |

---

*This guide covers qi with C-specific support (goto, labels, static modifier, preprocessor directives, struct field tracking). Last updated: December 2025.*
