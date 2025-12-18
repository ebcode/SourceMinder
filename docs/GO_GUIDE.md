# Go Developer's Guide to qi

A practical guide for using qi to search and analyze Go codebases. This guide focuses on Go-specific patterns and workflows that leverage qi's semantic understanding of Go code.

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Core Concepts](#core-concepts)
3. [Finding Definitions](#finding-definitions)
4. [Concurrency Patterns](#concurrency-patterns)
5. [Channel Operations](#channel-operations)
6. [Control Flow](#control-flow)
7. [Type System](#type-system)
8. [Common Workflows](#common-workflows)
9. [Advanced Queries](#advanced-queries)
10. [Tips & Tricks](#tips--tricks)

---

## Quick Start

### Index Your Go Project

```bash
# Index current directory
./index-go .

# Index specific directory
./index-go ./cmd/myapp

# Index multiple packages
./index-go ./pkg ./cmd ./internal
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
qi '*' -f 'handler.go' --toc

# 2. See a specific function's implementation
qi "processRequest" -i func -e

# 3. Find where it's used
qi "processRequest" -i call --limit 20

# 4. Understand what happens inside
qi 'user' --within 'processRequest' -x noise

# 5. Explore concurrency patterns
qi '%' -c go --within 'processRequest'
```

**Why this order?**
- `--toc` gives you the map (types, functions, imports)
- `-e` shows you the code (full definitions inline)
- `--within` reveals what a function does
- `-c go/defer/send/receive` finds Go-specific patterns
- `-p` shows you how structs/interfaces are used

This is **way more efficient** than reading files or using grep!

---

## Core Concepts

### Context Types

qi understands these Go-specific contexts:

| Context | Abbreviation | Description |
|---------|--------------|-------------|
| `function` | `func` | Function and method definitions |
| `variable` | `var` | Variables, constants, parameters |
| `type` | `type` | Structs, interfaces, type aliases |
| `property` | `prop` | Struct fields |
| `call` | `call` | Function calls |
| `import` | `imp` | Import statements |
| `case` | `case` | Enum/const values |

### Clues (Go-Specific Usage Contexts)

qi uses "clues" to indicate how a symbol is being used:

| Clue | Meaning |
|------|---------|
| `go` | Launched as goroutine |
| `defer` | Deferred function call |
| `send` | Channel send operation |
| `receive` | Channel receive operation |
| `select` | Variable in select case |
| `range` | Variable in range loop |
| `macro` | C-style #define (in cgo) |

---

## Finding Definitions

### Functions and Methods

```bash
# Find function definition
./qi "processRequest" -i func

# Find all functions in a file
./qi "%" -i func -f "%handler.go"

# Find methods on a specific type (via parent)
./qi "%" -i func -p "UserService"

# Find all exported functions (uppercase first letter)
./qi "Get%" -i func
./qi "New%" -i func
```

### Variables and Constants

```bash
# Find variable definition
./qi "maxRetries" -i var --columns line,symbol,type,definition

# Find all variable definitions (def=1)
./qi "%" -i var -f "%config.go" --columns line,symbol,definition

# Find constants
./qi "%" -i case -f "%constants.go"

# Find all error variables
./qi "Err%" -i var
./qi "%Error" -i var
```

### Types (Structs, Interfaces)

```bash
# Find struct definition
./qi "User" -i type

# Find interface definition
./qi "Handler" -i type

# Find all types in a package
./qi "%" -i type -f "pkg/auth/%"

# Find struct fields
./qi "%" -i prop -p "User"
```

---

## Concurrency Patterns

### Finding Goroutines

```bash
# All goroutine launches
./qi "%" -i call -c go

# Specific function launched as goroutine
./qi "worker" -i call -c go

# Anonymous goroutines
./qi "func" -i call -c go

# Goroutines in a specific file
./qi "%" -i call -c go -f "%server.go"

# Show context (useful for seeing goroutine body)
./qi "%" -i call -c go -C 20
```

### Goroutine Anti-Patterns

```bash
# Find all goroutine launches to review for leaks
./qi "%" -i call -c go --columns line,symbol,parent

# Find functions commonly launched as goroutines
# (look for same function appearing multiple times)
./qi "processTask" -i call -c go

# Check if cleanup/done channels are used nearby
./qi "done" -i var -f "%worker.go"
```

---

## Channel Operations

### Finding Channel Send Operations

```bash
# All channel send operations
./qi "%" -i var -c send

# Specific channel sends
./qi "results" -i var -c send

# Sends in a specific file
./qi "%" -i var -c send -f "%worker.go"

# Show what's being sent (with context)
./qi "%" -i var -c send -C 5
```

### Finding Channel Receive Operations

```bash
# All channel receives
./qi "%" -i var -c receive

# Specific channel receives
./qi "messages" -i var -c receive

# Receives with ok check (look for two variables on same line)
./qi "%" -i var -c receive -C 2

# Find potential channel leaks (receives with no timeout)
./qi "%" -i var -c receive
```

### Channel Analysis Workflow

```bash
# 1. Find all sends to a channel
./qi "taskQueue" -i var -c send

# 2. Find all receives from that channel
./qi "taskQueue" -i var -c receive

# 3. Check for close() calls
./qi "close" -i call -f "%queue.go"

# 4. Look for range over channel
./qi "taskQueue" -c range
```

### Finding Unbuffered vs Buffered Channels

```bash
# Find channel variable definitions
./qi "%" -i var --columns line,symbol,type -f "%channels.go"

# Look for make(chan ...) in context
./qi "make" -i call -C 3 -f "%channels.go"
```

---

## Control Flow

### Defer Statements

```bash
# All defer statements
./qi "%" -i call -c defer

# Specific function deferred
./qi "Close" -i call -c defer
./qi "Unlock" -i call -c defer

# Anonymous functions deferred
./qi "func" -i call -c defer

# Defer in specific file
./qi "%" -i call -c defer -f "%handler.go"

# Check defer ordering (multiple defers in same function)
./qi "%" -i call -c defer -C 10
```

### Common Defer Patterns

```bash
# Find resource cleanup patterns
./qi "Close" -i call -c defer
./qi "Unlock" -i call -c defer
./qi "Done" -i call -c defer
./qi "cancel" -i call -c defer

# Find defer with recover (panic handling)
./qi "recover" -i call -C 5
```

### Select Statements

```bash
# All select case variables
./qi "%" -i var -c select

# Specific variable in select
./qi "msg" -i var -c select

# Select statements in file (via select variables)
./qi "%" -i var -c select -f "%server.go"

# Show full select context
./qi "%" -i var -c select -C 15
```

### Select Patterns

```bash
# Find timeout patterns (look for time.After in context)
./qi "%" -i var -c select -C 10 -f "%timeout.go"

# Find non-blocking receives (select with default)
./qi "default" -f "%select%.go"

# Fan-in patterns (multiple channels in select)
./qi "%" -i var -c select -C 20
```

---

## Type System

### Interfaces

```bash
# Find interface definitions
./qi "%er" -i type  # Common Go interface naming

# Find interface methods
./qi "%" -i func -p "Handler"
./qi "%" -i func -p "io.Reader"

# Find types that might implement an interface
# (search for matching method names)
./qi "Read" -i func
./qi "Write" -i func
```

### Struct Fields

```bash
# Find all fields in a struct
./qi "%" -i prop -p "Config"

# Find specific field
./qi "timeout" -i prop

# Find pointer fields (check type column)
./qi "%" -i prop --columns line,symbol,parent,type

# Find embedded fields
./qi "%" -i prop -p "Server" --columns line,symbol,type
```

### Type Parameters (Generics)

```bash
# Find generic functions
./qi "%" -i func --columns line,symbol,type | grep "\["

# Find uses of generic types
./qi "List" -i type
./qi "Map" -i type
```

---

## Common Workflows

### Understanding a New Codebase (START HERE!)

**Always start with Table of Contents:**

```bash
# 1. Get the entry point structure
qi '*' -f 'main.go' --toc

# Shows:
# - What's imported (dependencies)
# - What types are defined
# - What functions exist (with line ranges!)

# 2. Explore key packages by pattern
qi '*' -f '%handler.go' --toc
qi '*' -f '%service.go' --toc
qi '*' -f 'pkg/%' --toc

# 3. Find main entry points
qi 'main' 'init' 'run' -i func

# 4. See implementations with -e
qi 'main' -i func -e
qi 'Server' -i type -e
```

**Why TOC first?**
- Instant overview without opening files
- Shows line ranges (jump directly to code)
- Reveals package structure and dependencies
- Faster than reading entire files

### Understanding a Function

**Complete workflow from discovery to understanding:**

```bash
# 1. Find function (location)
qi "processRequest" -i func

# 2. See full implementation (best approach!)
qi "processRequest" -i func -e

# 3. Find what the function does with specific variables
qi 'request' --within 'processRequest' -x noise
qi 'error' --within 'processRequest' -x noise

# 4. Find all calls to this function
qi "processRequest" -i call --limit 20

# 5. Check if it's used as goroutine
qi "processRequest" -i call -c go

# 6. Check if it's deferred
qi "processRequest" -i call -c defer

# 7. See call context
qi "processRequest" -i call -C 5
```

**Pro tip:** Use `-e` instead of reading files! It shows code inline.

### Tracing Channel Communication

```bash
# 1. Find channel definition
./qi "resultChan" -i var --columns line,symbol,type,definition

# 2. Find all sends
./qi "resultChan" -i var -c send -C 3

# 3. Find all receives
./qi "resultChan" -i var -c receive -C 3

# 4. Check in select statements
./qi "resultChan" -i var -c select -C 10

# 5. Find close() calls
./qi "resultChan" -C 2 | grep close
```

### Finding Concurrency Issues

```bash
# Find all goroutines
./qi "%" -i call -c go

# Check for WaitGroups
./qi "WaitGroup" -i var
./qi "Wait" -i call
./qi "Done" -i call -c defer

# Check for context usage
./qi "context" -i var --columns line,symbol,type
./qi "ctx" -i var

# Look for channels without close
./qi "%" -i var -c send | wc -l  # count sends
./qi "close" -i call | wc -l      # count closes

# Find range over channels (need close)
./qi "%" -c range
```

### Refactoring a Type

```bash
# 1. Find type definition
./qi "User" -i type -C 20

# 2. Find all fields
./qi "%" -i prop -p "User"

# 3. Find all methods
./qi "%" -i func -p "User"

# 4. Find all variable declarations of this type
./qi "%" -i var --columns line,symbol,type | grep "User"

# 5. Find all function parameters of this type
./qi "%" -i arg --columns line,symbol,type | grep "User"
```

### Finding Error Handling Patterns

```bash
# Find error variable definitions
./qi "err" -i var

# Find error returns
./qi "%" -i func --columns line,symbol,type | grep "error"

# Find error wrapping
./qi "Wrap" -i call
./qi "Errorf" -i call

# Find error checks in context
./qi "err" -i var -C 3 | grep "if err"
```

---

## Power User Combinations

These combinations unlock qi's full potential by stacking filters creatively for Go code.

### Combining Goroutines + Context + Within

```bash
# What goroutines does a function launch?
qi '%' -i call -c go --within 'processRequest'

# What's being sent to channels within a function?
qi '%' -c send --within 'worker' -C 3

# Find defer statements within specific functions
qi '%' -c defer --within 'handleRequest' 'processData'
```

### Combining Channels + Parent + Context

```bash
# Find send operations on specific channels
qi '%' -c send -p 'taskQueue' -C 3

# Find receive operations with parent info
qi '%' -c receive -v --limit 20  # Shows parent channel

# Find all operations on a specific channel
qi 'dataChan' -c 'send' 'receive' 'select' --limit-per-file 3
```

### Combining Select + Range + Defer

```bash
# Find select statements with specific variables
qi 'done' -c select -C 10

# Find range over channels (should have close somewhere)
qi '%' -c range -C 5

# Find deferred cleanup in goroutines
qi '%' -c defer --within 'worker' -C 3
```

### Combining Multiple Filters for Precision

```bash
# Find goroutines launched within error handlers
qi '%' -i call -c go --within 'handleError' -x noise

# Find deferred unlocks within specific functions
qi 'Unlock' -i call -c defer --within 'processRequest'

# Find channel operations with context
qi '%' -c send -C 5 --limit-per-file 2
```

### Creative Sampling

```bash
# Sample goroutines across codebase
qi '%' -i call -c go --limit-per-file 2

# Sample defer patterns per file
qi '%' -c defer --limit-per-file 3

# Compare distribution
qi 'Lock' -i call --limit 20              # Might all be in one file
qi 'Lock' -i call --limit-per-file 3      # Distributed across files
```

---

## Advanced Queries

### Parent Symbol Filtering (`-p`)

The `-p` flag filters by the parent symbol - useful for finding method calls on specific objects/interfaces.

```bash
# Find all method calls on request objects
qi '%' -i call -p 'req' --limit 20

# Find methods called on context
qi '%' -i call -p 'ctx' --limit 20

# Find specific method on specific object
qi 'Lock' -i call -p 'mu'
qi 'Done' -i call -p 'wg'
```

**Use cases:**
- Understanding how interfaces are used
- Finding method call patterns
- Debugging object interactions

**Note:** Parent is populated for method **calls**, not method definitions.

### Scoped Search (`--within`)

The `--within` flag searches only within specific function definitions.

```bash
# What does processRequest do with 'err'?
qi 'err' --within 'processRequest' -x noise

# Find all goroutines launched within a function
qi '%' -c go --within 'handleRequest'

# Find all local variables in a function
qi '%' -i var --within 'processData'

# Multiple functions (OR logic)
qi 'lock' --within 'handler' 'processor' 'worker'
```

**Use cases:**
- Understanding function behavior
- Finding local concurrency patterns
- Debugging specific functions

**Why better than grep:** Understands function boundaries!

### Related Patterns (`--and`)

Find lines where **multiple patterns** co-occur.

```bash
# Find 'err' and 'nil' on the same line
qi 'err' 'nil' --and

# Find 'lock' and 'unlock' within 5 lines
qi 'lock' 'unlock' --and 5

# Find goroutine + waitgroup patterns
qi 'go' 'wg' --and 10 -C 3

# With context
qi 'chan' 'close' --and 10 -C 2
```

**Use cases:**
- Finding error handling patterns
- Understanding concurrency patterns
- Finding resource management (lock/unlock, open/close)

### Sampling Results (`--limit-per-file`)

Limit results to N matches per file for distributed sampling.

```bash
# Sample goroutines per file
qi '%' -i call -c go --limit-per-file 2

# Sample defer patterns
qi '%' -c defer --limit-per-file 3

# Sample channel operations
qi '%' -c send --limit-per-file 2
```

**Use cases:**
- Overview across codebase
- Preventing one file from dominating
- Understanding pattern distribution

### Multi-Column Display

```bash
# Comma-separated columns
./qi "%" -i func --columns line,symbol,type

# Space-separated columns
./qi "%" -i var --columns line symbol type definition

# Custom column combinations
./qi "%" -i func --columns line,symbol,parent,type
./qi "%" -i call -c go --columns line,symbol,clue
```

### File Filtering

```bash
# Specific file
./qi "handler" -f "server.go"

# Wildcard patterns
./qi "%" -i func -f "%_test.go"     # Test files
./qi "%" -i func -f "cmd/%"          # cmd directory
./qi "%" -i func -f "%/handler.go"   # All handler.go files

# Multiple patterns (combine with multiple -f flags)
./qi "Config" -i type -f "%config.go" -f "%settings.go"
```

### Excluding Noise

```bash
# Exclude comments and strings
./qi "error" -x noise

# Exclude specific contexts
./qi "test" -x comment -x string

# Focus on code only
./qi "TODO" -x noise
./qi "FIXME" -x noise
```

### Context Display

```bash
# Show N lines after match
./qi "func main" -A 10

# Show N lines before match
./qi "return" -B 5

# Show N lines before and after
./qi "processRequest" -C 20

# Maximum context (100 lines)
./qi "handler" -C 100
```

### Limiting Results

```bash
# Show first N matches
./qi "%" -i func --limit 20

# Quick overview
./qi "%" -i func --limit 5

# Sample usage
./qi "New%" -i func --limit 10
```

---

## Tips & Tricks

### Search Patterns

```bash
# Wildcard at start
./qi "%Manager" -i type      # Ends with Manager

# Wildcard at end
./qi "Get%" -i func          # Starts with Get

# Wildcard in middle
./qi "HTTP%Client" -i type   # Contains HTTP and ends with Client

# Match anything
./qi "%" -i func             # All functions
```

### Common Go Naming Patterns

```bash
# Constructors
./qi "New%" -i func

# Getters
./qi "Get%" -i func

# Setters
./qi "Set%" -i func

# Handlers
./qi "Handle%" -i func
./qi "%Handler" -i func

# Interfaces (often end in 'er')
./qi "%er" -i type
./qi "%able" -i type

# Error types
./qi "Err%" -i var
./qi "%Error" -i type

# Test functions
./qi "Test%" -i func -f "%_test.go"
./qi "Benchmark%" -i func -f "%_test.go"
```

### Combining with Shell Tools

```bash
# Count goroutines per file
./qi "%" -i call -c go --columns line,symbol | cut -d: -f1 | sort | uniq -c

# Find most-called functions
./qi "%" -i call | cut -d'|' -f2 | sort | uniq -c | sort -rn | head -10

# Find files with most channel operations
./qi "%" -c send --columns line | cut -d: -f1 | sort | uniq -c | sort -rn

# List all unique function names
./qi "%" -i func --columns symbol | tail -n +2 | sort -u

# Find functions never called as goroutines
comm -23 <(./qi "%" -i func --columns symbol | tail -n +2 | sort -u) \
         <(./qi "%" -i call -c go --columns symbol | tail -n +2 | sort -u)
```

### Performance Tips

```bash
# Use file filters to narrow scope
./qi "handler" -f "%/pkg/%" -i func   # Only pkg directory

# Use context filters to reduce results
./qi "test" -i func                    # Only functions, not all symbols

# Exclude noise for faster results
./qi "error" -x noise                  # Skip comments/strings

# Limit results for quick checks
./qi "%" -i func --limit 50           # First 50 matches
```

### Debugging Queries

```bash
# Use verbose mode to see all columns
./qi "symbol" -v

# Check what's being indexed
./qi "%" --limit 20 -v

# Verify context types
./qi "%" -i func --limit 5
./qi "%" -i var --limit 5
./qi "%" -i type --limit 5

# Check available columns
./qi "test" --columns invalid_column   # Shows available columns in error
```

### Quick Reference Card

```bash
# Most useful Go patterns (copy these!)

# Functions
./qi "functionName" -i func -C 20

# Goroutines
./qi "%" -i call -c go

# Defer
./qi "%" -i call -c defer

# Channel sends
./qi "channelName" -i var -c send

# Channel receives
./qi "channelName" -i var -c receive

# Select cases
./qi "%" -i var -c select

# Definitions
./qi "symbol" --columns line,symbol,type,definition

# Exclude noise
./qi "error" -x noise

# File filter
./qi "handler" -f "%server.go"
```

---

## Real-World Examples

### Example 1: Finding Potential Goroutine Leaks

```bash
# Find all goroutines
./qi "%" -i call -c go --columns line,symbol > goroutines.txt

# Find WaitGroup usage
./qi "WaitGroup" -i var --columns line,symbol

# Find context cancellation
./qi "cancel" -i call --columns line,symbol

# Check if each goroutine file has cleanup
for file in $(./qi "%" -i call -c go --columns line | cut -d: -f1 | sort -u); do
    echo "=== $file ==="
    ./qi "%" -c go -f "$file" --columns line,symbol
    ./qi "Done" -f "$file" --columns line,symbol
done
```

### Example 2: Auditing Channel Operations

```bash
# Find all channels by looking for sends/receives
./qi "%" -c send --columns line,symbol | cut -d'|' -f2 | sort -u > channels_send.txt
./qi "%" -c receive --columns line,symbol | cut -d'|' -f2 | sort -u > channels_recv.txt

# Channels that are only sent to (potential leak)
comm -23 channels_send.txt channels_recv.txt

# Channels that are only received from (unusual)
comm -13 channels_send.txt channels_recv.txt
```

### Example 3: Finding Defer/Unlock Mismatches

```bash
# Find all mutex locks
./qi "Lock" -i call --columns line,symbol,parent -f "%.go"

# Find all mutex unlocks
./qi "Unlock" -i call --columns line,symbol,parent -f "%.go"

# Find deferred unlocks (correct pattern)
./qi "Unlock" -i call -c defer --columns line,symbol,parent

# Find non-deferred unlocks (potential bug)
comm -23 <(./qi "Unlock" -i call --columns line | sort) \
         <(./qi "Unlock" -i call -c defer --columns line | sort)
```

### Example 4: Understanding Error Flow

```bash
# Find error variable declarations
./qi "err" -i var --columns line,symbol,type,definition -C 3

# Find error returns
./qi "%" -i func --columns line,symbol,type | grep error

# Find error wrapping patterns
./qi "fmt.Errorf" -i call -C 2
./qi "errors.Wrap" -i call -C 2

# Find error ignoring (underscore assignment)
grep -n "_ =" *.go | head -20
```

### Example 5: Reviewing Concurrency Safety

```bash
# Find shared state (global/package-level variables)
./qi "%" -i var -f "%globals.go"

# Find mutex definitions
./qi "Mutex" -i var --columns line,symbol,type,definition

# Find atomic operations
./qi "atomic" --columns line,symbol

# Find channels used for synchronization
./qi "%" -c send --columns line,symbol
./qi "%" -c receive --columns line,symbol
./qi "%" -c select --columns line,symbol
```

---

## Troubleshooting

### Pattern Not Matching

```bash
# Problem: Symbol not found
./qi "MyFunction" -i func
# No results

# Solution 1: Try case-insensitive pattern
./qi "%function%" -i func

# Solution 2: Check if it's a method
./qi "MyFunction" -i func -p "%"

# Solution 3: Search without context filter
./qi "MyFunction"

# Solution 4: Try wildcard
./qi "%Function" -i func
```

### Too Many Results

```bash
# Problem: Thousands of matches
./qi "test"
# Found 5000 matches

# Solution 1: Add context filter
./qi "test" -i func

# Solution 2: Add file filter
./qi "test" -i func -f "%handler.go"

# Solution 3: Exclude noise
./qi "test" -x noise

# Solution 4: Use limit
./qi "test" -i func --limit 20
```

### Symbol in Stopwords

```bash
# Problem: Common word filtered
./qi "out"
# Note: 'out' is in shared/data/stopwords.txt

# Solution: Use wildcard
./qi "%out%"

# Or be more specific
./qi "stdout"
./qi "output"
```

### Need to Search for Flags

```bash
# Problem: Can't search for "--columns"
./qi "--columns"
# Error: parsed as flag

# Workaround: Use wildcard
./qi "%columns%"

# Or search in strings only
./qi "columns" -i string
```

---

## Configuration

### Query Config File

Create `~/.query-indexrc` for default settings:

```
# Default flags
--limit 100
-x noise

# Default columns (commented out by default)
# --columns line,symbol,type,definition
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
qi '*' -f 'file.go' --toc      # Always start with TOC
qi "func" -i func -e           # See implementations with -e
qi "Type" -i type -e           # Expand type definitions
```

**Level 3: Go-Specific Patterns**
```bash
qi '%' -c go                   # Find goroutines
qi '%' -c defer                # Find defer statements
qi '%' -c send                 # Find channel sends
qi '%' -c receive              # Find channel receives
```

**Level 4: Advanced Filtering**
```bash
qi 'Lock' -i call -p 'mu'      # Filter by parent
qi '%' -c go --within 'handler' # Scoped search
qi 'err' 'nil' --and           # Related patterns
```

**Level 5: Power Combinations**
```bash
qi '%' -c go --within 'processRequest'         # Goroutines in function
qi 'Unlock' -c defer --within 'handleRequest'  # Deferred cleanup
qi '%' -c send -p 'taskQueue' -C 3             # Channel operations
qi '%' -i call -c go --limit-per-file 2        # Sample goroutines
```

**Level 6: Master Workflows**
- Combine TOC + -e + --within for complete understanding
- Use -c for Go-specific patterns (go, defer, send, receive)
- Stack filters for precision concurrency analysis
- Use --limit-per-file for distributed sampling

### Common Pitfalls to Avoid

**❌ Don't do this:**
```bash
# Reading files instead of using TOC
cat handler.go | less

# Using grep for concurrency patterns
grep -r "go func" .

# Forgetting to exclude noise
qi "error"  # Includes comments/strings

# Not using -e to see code
qi "func" -i func  # Just shows line numbers
```

**✅ Do this instead:**
```bash
# Use TOC for structure
qi '*' -f 'handler.go' --toc

# Use qi for concurrency
qi '%' -c go

# Always exclude noise for code symbols
qi "error" -x noise

# See code inline
qi "func" -i func -e
```

### Efficiency Tips

1. **Start with TOC**: `qi '*' -f 'file.go' --toc` shows structure instantly
2. **Use -e liberally**: Seeing code inline is faster than jumping to files
3. **Use clues for Go patterns**: `-c go`, `-c defer`, `-c send`, `-c receive`
4. **Use --within**: Understanding what a function does without reading entire file
5. **Sample with --limit-per-file**: Get distributed examples of goroutines/channels
6. **Exclude noise by default**: Add `-x noise` to most queries

### When to Use What

| Task | Best Tool |
|------|-----------|
| File structure | `qi '*' -f 'file.go' --toc` |
| See function code | `qi "func" -i func -e` |
| Find goroutines | `qi '%' -c go` |
| Find defer statements | `qi '%' -c defer` |
| Channel sends | `qi '%' -c send` |
| Channel receives | `qi '%' -c receive` |
| Select statements | `qi '%' -c select` |
| Range loops | `qi '%' -c range` |
| What function does | `qi '%' --within 'function'` |
| Related patterns | `qi 'a' 'b' --and 5` |
| Sample across files | `qi '%' --limit-per-file 3` |
| Literal text search | `grep` (not qi) |

### Go-Specific Best Practices

**For Concurrency Analysis:**
```bash
# Always start with overview
qi '*' -f 'worker.go' --toc

# Find goroutines
qi '%' -c go

# Check for proper cleanup
qi '%' -c defer --within 'worker'
qi 'Done' -i call -c defer

# Trace channel usage
qi 'taskQueue' -c send
qi 'taskQueue' -c receive
qi 'close' -i call
```

**For Error Handling:**
```bash
# Find error patterns
qi 'err' 'nil' --and -x noise

# Within specific functions
qi 'err' --within 'processRequest' -x noise
```

**For Resource Management:**
```bash
# Find lock/unlock patterns
qi 'Lock' 'Unlock' --and 10
qi 'Unlock' -c defer  # Should be deferred!
```

---

## Comparison with Other Tools

### qi vs grep

```bash
# grep: Text-based, many false positives
grep -r "processRequest" .

# qi: Semantic, finds only real function
./qi "processRequest" -i func

# grep: Can't distinguish goroutines from regular calls
grep -r "go processRequest" .

# qi: Finds all goroutine launches
./qi "processRequest" -i call -c go
```

### qi vs go list / go doc

```bash
# go list: Shows package structure
go list ./...

# qi: Shows symbols within packages
./qi "%" -i func -f "pkg/%"

# go doc: Shows exported documentation
go doc http.Client

# qi: Shows all members (exported and unexported)
./qi "%" -i prop -p "Client"
./qi "%" -i func -p "Client"
```

### qi vs IDE "Find Usages"

```bash
# IDE: Great for active development, one file at a time
# qi: Great for codebase-wide analysis, scriptable

# IDE: Find usages of function
# (click, wait, see results in window)

# qi: Find all usages, all goroutines, all defers
./qi "processRequest" -i call
./qi "processRequest" -i call -c go
./qi "processRequest" -i call -c defer

# qi advantage: Scriptable and composable
./qi "%" -i call -c go | wc -l  # Count goroutines
```

---

## Glossary

**Context**: What a symbol *is* (function, variable, type, etc.)

**Clue**: How a symbol is being *used* (go, defer, send, receive, etc.)

**Definition**: The location where a symbol is declared (def=1)

**Usage**: A reference to a symbol (def=0)

**Modifier**: Declaration modifiers (static, const, etc.) - not used much in Go

**Parent**: The containing type/struct for methods and fields

**Scope**: Visibility (public/private based on capitalization in Go)

**Namespace**: Package name

**Type**: Variable type, function return type, etc.

---

## Getting Help

```bash
# Show full help
./qi --help

# List available columns
./qi "test" --columns invalid   # Shows available columns in error

# Check version/build info
./qi --version  # If implemented

# Report issues
# https://github.com/your-repo/indexer-c/issues
```

---

## Additional Resources

- **Gap Analysis**: See `go-indexer-gaps.md` for unimplemented features
- **Work Summary**: See `work_summary_*.md` files for implementation details
- **Source Code**: `go/go_language.c` - Handler implementations

---

## Quick Command Reference

| Task | Command |
|------|---------|
| File structure | `qi '*' -f 'file.go' --toc` |
| Find function | `qi "name" -i func` |
| Expand definition | `qi "name" -i func -e` |
| Find goroutines | `qi "%" -i call -c go` |
| Find defers | `qi "%" -i call -c defer` |
| Find channel sends | `qi "ch" -i var -c send` |
| Find channel receives | `qi "ch" -i var -c receive` |
| Find select cases | `qi "%" -i var -c select` |
| Filter by parent | `qi "Lock" -i call -p "mu"` |
| Search within function | `qi "err" --within "processRequest"` |
| Related patterns (same line) | `qi "err" "nil" --and` |
| Related patterns (N lines) | `qi "lock" "unlock" --and 5` |
| Limit per file | `qi "%" --limit-per-file 3` |
| Show context | `qi "name" -C 20` |
| Exclude noise | `qi "name" -x noise` |
| Limit results | `qi "%" --limit 50` |
| Custom columns | `qi "%" --columns line,symbol,type` |

---

*This guide covers qi version with Go-specific clue support (go, defer, send, receive, select). Last updated: December 2025.*
