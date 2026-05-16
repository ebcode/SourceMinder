# Rust Developer's Guide to qi

A practical guide for using qi to search and analyze Rust codebases. This guide focuses on Rust-specific patterns and workflows that leverage qi's semantic understanding of Rust code.

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Core Concepts](#core-concepts)
3. [Finding Definitions](#finding-definitions)
4. [Visibility & Modifiers](#visibility--modifiers)
5. [Traits & Impl Blocks](#traits--impl-blocks)
6. [Pattern Matching](#pattern-matching)
7. [Async & Unsafe](#async--unsafe)
8. [Macros & FFI](#macros--ffi)
9. [Modules & Imports](#modules--imports)
10. [Common Workflows](#common-workflows)
11. [Power User Combinations](#power-user-combinations)
12. [Advanced Queries](#advanced-queries)
13. [Tips & Tricks](#tips--tricks)
14. [Real-World Examples](#real-world-examples)

---

## Quick Start

### Index Your Rust Project

```bash
# Index current directory
./index-rust .

# Index a Cargo project's source tree
./index-rust ./src

# Index multiple crates in a workspace
./index-rust ./crates/core ./crates/cli ./crates/server
```

The indexer automatically skips `target/`, `.cargo/`, `vendor/`, and similar build directories (configured in `rust/config/ignore_files.txt`).

### First Five Queries for a New Project

If you know nothing about a Rust codebase, run these in order:

```bash
# 1. Discover the source layout
qi '*' -i file -f 'src/'

# 2. Map the entry points' structure
qi '*' -f 'src/lib.rs' --toc
qi '*' -f 'src/main.rs' --toc

# 3. Expand the key types you saw in the TOC
qi 'Config' 'AppError' 'Service' -i class -e

# 4. Understand the trait landscape (the type system's backbone)
qi '*' -i trait --def

# 5. Walk the main entry point
qi '%' --within 'main' -x noise --limit 30
```

This reveals: what files exist → what the crate exposes → what the core types look like → what traits govern behavior → what the program actually does at startup.

### Basic Search

```bash
# Find all uses of a symbol
qi "User"

# Find function definition
qi "parse_config" -i func

# Find with full definition
qi "parse_config" -i func -e

# Exclude comments and string-literal words
qi "error" -x noise
```

### Your First Steps with qi (Recommended Workflow)

**Start with structure, then drill down:**

```bash
# 1. Get file overview (always start here!)
qi '*' -f 'lib.rs' --toc

# 2. See a specific function's implementation
qi "handle_request" -i func -e

# 3. Find where it's called from
qi "handle_request" -i call --limit 20

# 4. Understand what happens inside
qi 'token' --within 'handle_request' -x noise

# 5. Explore async/unsafe usage
qi '%' -i func -m async --within 'handle_request'
```

**Why this order?**
- `--toc` gives you the map (types, functions, imports)
- `-e` shows you the code (full definitions inline)
- `--within` reveals what a function does
- `-s pub` finds the public API surface
- `-m async`, `-m unsafe` find Rust-specific code

This is **way more efficient** than reading files or using grep!

---

## Core Concepts

### Context Types

qi understands these Rust-specific contexts:

| Context | Abbreviation | Description |
|---------|--------------|-------------|
| `function` | `func` | `fn` definitions (free, method, signature, macro_rules) |
| `argument` | `arg` | Function parameters |
| `variable` | `var` | `let`, `const`, `static` bindings; pattern bindings |
| `property` | `prop` | Struct/union fields |
| `class` | `class` | `struct`, `union`, and `impl` blocks |
| `enum` | `enum` | `enum` type definitions |
| `case` | `case` | Enum variants |
| `trait` | `trait` | `trait` definitions |
| `namespace` | `ns` | `mod` declarations |
| `import` | `imp` | `use` items (including re-exports) |
| `call` | `call` | Function calls, method calls, macro invocations |
| `lambda` | `lambda` | Closure expressions |
| `type` | `type` | `type` aliases |
| `comment` | `com` | Words from comments |
| `string` | `str` | Words from string literals |
| `filename` | `file` | Filename without extension |

### Scope (Visibility)

The `scope` column captures Rust's visibility modifier exactly as written:

| Scope value | Meaning |
|-------------|---------|
| `pub` | Fully public |
| `pub(crate)` | Crate-private |
| `pub(super)` | Visible to parent module |
| `pub(in path::to::module)` | Visible to specified module |
| *(empty)* | Module-private (Rust default) |

### Modifiers

The `modifier` column captures behavior keywords that change how an item executes:

| Modifier | Applies to | Meaning |
|----------|-----------|---------|
| `async` | functions | Returns a `Future` |
| `unsafe` | functions, blocks | Requires `unsafe` to call |
| `const` | functions | Callable at compile time |
| `extern` | functions | FFI / ABI specification |
| `await` | calls | Call was `.await`-ed |

Combinations stack (e.g., `pub const fn` → scope=`pub`, modifier=`const`).

### Clues (Rust-Specific Annotations)

The `clue` column captures attributes and impl-block traits:

| Clue pattern | Meaning |
|--------------|---------|
| `#[derive],#[allow]` | Outer attributes applied to the item |
| `#[test]` | Test function marker |
| `#[inline]` | Inline hint |
| `#[deprecated]` | Deprecation warning |
| `#[cfg]`, `#[cfg_attr]` | Conditional compilation |
| `#[no_mangle]` | Disable name mangling |
| `impl Display` | Item is an impl block implementing the named trait |
| `impl` | Inherent impl block (no trait) |
| `macro_rules!` | Item is a `macro_rules!` definition |
| `macro!` | Call is a macro invocation |
| `as` | Use-statement alias (`use foo as bar`) |

### Parent

For methods, the `parent` column holds the **impl target type** (or trait, for trait methods). For enum variants, it's the enum. For method calls, it's the receiver name (when statically resolvable).

### Type

Captures the type annotation:
- Function `return_type` → return type
- `let x: T = ...` → `T`
- `parameter: T` → `T`
- `type Alias<T> = U` → `U`
- `const NAME: T = ...` → `T`

---

## Finding Definitions

### Functions and Methods

```bash
# Find function definition
qi "parse_config" -i func

# Show full function body
qi "parse_config" -i func -e

# All functions in a file
qi '*' -i func -f 'src/parser.rs'

# Methods on a specific type (via parent)
qi '*' -i func -p 'User'
qi '*' -i func -p 'HashMap'

# Methods on a trait
qi '*' -i func -p 'Iterator'
qi '*' -i func -p 'Display'

# Constructors (Rust convention: `new`, `with_`, `from_`)
qi 'new' 'with_*' 'from_*' -i func --def
```

### Structs, Enums, Traits

```bash
# Find struct definition
qi "User" -i class -e

# All structs in a module
qi '*' -i class --def -f 'src/models/'

# Enum and its variants
qi "Status" -i enum -e
qi '*' -i case -p 'Status'

# Trait definition + its required methods
qi "Storage" -i trait -e
qi '*' -i func -p 'Storage'

# Find struct fields
qi '*' -i prop -p 'Config'
```

### Variables and Constants

```bash
# Find a let/const/static binding
qi "MAX_RETRIES" -i var

# All consts in a file
qi '*' -i var -f 'src/constants.rs' --def

# Find variables with a specific type
qi '*' -i var -t 'String' --def
qi '*' -i var -t 'Vec<_>' --def

# Statics (mutable global state — usually unsafe)
qi '*' -i var -m static --def
```

### Type Aliases

```bash
# Find a type alias
qi "AppResult" -i type

# All type aliases in a crate
qi '*' -i type --def
```

---

## Visibility & Modifiers

Rust's visibility rules are central; qi makes them queryable.

### Finding the Public API

```bash
# All public items in a crate
qi '*' -s pub --def --limit 50

# Public functions only
qi '*' -i func -s pub --def

# Public types only
qi '*' -i class enum trait -s pub --def

# Crate-private items (potential refactoring candidates)
qi '*' -s 'pub(crate)' --def

# Items visible to parent module only
qi '*' -s 'pub(super)' --def
```

### Auditing Visibility

```bash
# Find items in a module's public API
qi '*' -s pub --def -f 'src/api/'

# Find inadvertently-public helpers (might want pub(crate))
qi '*helper*' '*internal*' -s pub --def

# Find module-private functions (the default — empty scope)
qi '*' -i func --def | grep -v ' pub '
```

### Async Functions

```bash
# All async functions
qi '*' -i func -m async --def

# Public async API
qi '*' -i func -s pub -m async --def

# Async functions returning a specific type
qi '*' -i func -m async -t 'Result<%' --def
```

### Unsafe Code

```bash
# All unsafe functions
qi '*' -i func -m unsafe --def

# Unsafe public API (audit surface)
qi '*' -i func -s pub -m unsafe --def

# Unsafe function with context to see why
qi '*' -i func -m unsafe --def -e
```

### Const fn (compile-time evaluable)

```bash
# All const fn
qi '*' -i func -m const --def

# Public const fn
qi '*' -i func -s pub -m const --def
```

---

## Traits & Impl Blocks

Traits and impl blocks are the heart of Rust's type system. qi tracks both ends of the relationship.

### Finding Trait Definitions

```bash
# Define-site of a trait
qi "Serializable" -i trait -e

# All traits in a crate (Rust convention: often ends in -er, -able, -or)
qi '*er' '*able' '*or' -i trait --def

# Trait method signatures (with parent = trait name)
qi '*' -i func -p 'Iterator'
qi '*' -i func -p 'Display'
```

### Finding Impl Blocks

Impl blocks are recorded as `CLASS` entries with `clue=impl <Trait>` (or just `impl` for inherent impls), and the symbol is the impl target.

```bash
# All inherent impls (impl Foo { ... })
qi '*' -i class -c impl --def

# All trait impls (impl Trait for Foo { ... })
qi '*' -i class -c 'impl %' --def

# Implementations of a specific trait
qi '*' -i class -c 'impl Display'
qi '*' -i class -c 'impl From'
qi '*' -i class -c 'impl Iterator'

# Impls of any trait on a specific type
qi 'User' -i class --def
# Returns: the struct def + all impl blocks for User
```

### Finding Methods

Method definitions carry their impl target in the `parent` column.

```bash
# All methods on a type (inherent + trait impls)
qi '*' -i func -p 'User'

# Methods on a generic type (use base name)
qi '*' -i func -p 'Vec'
qi '*' -i func -p 'Option'

# Constructor-like methods
qi 'new' 'with_*' -i func -p 'Config'

# Method definitions that return Self (constructors/builders)
qi '*' -i func -p 'User' -t 'Self'
```

### Method Call Sites

```bash
# All calls to a method
qi 'serialize' -i call

# Method calls on a specific receiver name
qi '*' -i call -p 'self'      # all self.something() calls
qi '*' -i call -p 'request'   # all request.foo() calls

# Specific method on specific receiver
qi 'lock' -i call -p 'mutex'
qi 'unwrap' -i call -p 'result'
```

---

## Pattern Matching

qi extracts variable bindings from `match`, `if let`, `while let`, `for`, and `let` patterns — including destructuring.

### Match Arms

```bash
# Find variable bindings inside match arms
qi '*' -i var --def -f 'src/parser.rs'

# Find match expressions with a specific binding
qi 'value' -i var --def -C 5

# Find match arms over a specific enum variant
qi 'Some' -i call         # Variant constructors used as patterns
qi 'Ok' 'Err' -i call
```

**Note:** qi treats uppercase-start identifiers in pattern position as constructor references (skipped as bindings) — so `None`, `Ok`, `Some` won't pollute your variable searches.

### If-let / While-let

```bash
# Find if-let / while-let bindings (look for var defs near `if let`)
qi '*' -i var --def -C 2 | grep -B1 'if let'

# Specific binding inside if-let
qi 'session' -i var --def -C 1
```

### For Loops

```bash
# All loop variables
qi '*' -i var --def -f 'src/main.rs'

# Destructured for-loops bind each element separately:
# `for (key, value) in map` produces two VAR entries on the same line.
```

### Let Destructuring

```bash
# `let (a, b, c) = tuple` — each name is indexed as VAR
qi 'a' 'b' 'c' -i var --def
```

---

## Async & Unsafe

### Async Function Analysis

```bash
# All async functions
qi '*' -i func -m async --def -v

# Async functions in a module
qi '*' -i func -m async --def -f 'src/handlers/'

# Public async API
qi '*' -i func -s pub -m async --def
```

### Await Points

```bash
# All awaited calls
qi '*' -i call -m await

# Awaited calls inside a specific function
qi '*' -i call -m await --within 'handle_request'

# Awaited method calls on a specific receiver
qi '*' -i call -m await -p 'client'
```

### Finding Blocking in Async Code (Heuristic)

```bash
# Async functions that might block (look for std::thread::sleep, std::sync::Mutex)
qi 'sleep' 'lock' -i call --within 'handle_request'

# Async functions calling std::fs (likely should be tokio::fs)
qi '*' -i call --within 'fetch_data' -p 'fs'
```

### Unsafe Audit

```bash
# All unsafe functions (the audit surface)
qi '*' -i func -m unsafe --def

# Unsafe public functions (broadest audit target)
qi '*' -i func -m unsafe -s pub --def

# Calls inside unsafe functions
qi '*' -i call --within 'do_unsafe'

# FFI calls (extern functions usually called from unsafe blocks)
qi '*' -i call -ns 'libc'
```

---

## Macros & FFI

### Macro Definitions

```bash
# All macro_rules! definitions
qi '*' -i func -c 'macro_rules!'

# Specific macro
qi 'debug_print' -i func -c 'macro_rules!' -e
```

### Macro Invocations

```bash
# Common macro call sites
qi 'println' -i call
qi 'vec' -i call
qi 'format' -i call
qi 'assert_eq' -i call
qi 'panic' -i call

# All macro invocations (clue='macro!')
qi '*' -i call -c 'macro!'

# Macro invocations within a function
qi '*' -i call -c 'macro!' --within 'main'
```

### FFI Declarations

```bash
# Functions declared in extern blocks (return types preserved)
qi 'malloc' 'free' 'strlen' -i func --def -v

# extern crate declarations
qi '*' -i imp -c 'extern_crate'   # If you add this clue

# Functions exported with C ABI (#[no_mangle])
qi '*' -i func -c '%no_mangle%' --def
qi '*' -i func -m 'extern "C"' --def
```

### Foreign Statics

```bash
# extern static items
qi 'errno' -i var --def -v
qi '*' -i var -f 'src/ffi/' --def
```

---

## Modules & Imports

### Module Structure

```bash
# All module declarations
qi '*' -i ns --def

# Public modules (forms the module path tree)
qi '*' -i ns -s pub --def

# Inline modules (mod foo { ... })
qi '*' -i ns --def -e
```

### Use Statements

```bash
# All imported names
qi '*' -i imp --limit 30

# Imports from a specific path (uses namespace column)
qi '*' -i imp -ns 'std::collections'
qi '*' -i imp -ns 'tokio%'
qi '*' -i imp -ns 'crate%'

# Find use-aliases (use foo as bar)
qi '*' -i imp -c 'as'

# Find re-exports (pub use)
qi '*' -i imp -f 'src/lib.rs'
```

### Import Audit

```bash
# What does this module pull in?
qi '*' -i imp -f 'src/main.rs'

# What standard library bits does the crate use?
qi '*' -i imp -ns 'std%'

# External crate dependencies (via use)
qi '*' -i imp -ns 'serde%'
qi '*' -i imp -ns 'tokio%'
```

### Scoped Calls

```bash
# Calls to a specific path: e.g., asyncio::sleep
qi 'sleep' -i call -ns 'tokio::time'
qi 'spawn' -i call -ns 'tokio'

# All calls to functions in std::collections
qi '*' -i call -ns 'std::collections%'

# Constructor patterns: Type::new()
qi 'new' -i call -ns 'User'
qi 'new' -i call -ns '%::Builder'
```

---

## Common Workflows

### Understanding a New Codebase (START HERE!)

**Always start with Table of Contents:**

```bash
# 1. Get the entry-point structure
qi '*' -f 'src/main.rs' --toc
qi '*' -f 'src/lib.rs' --toc

# Shows:
# - What's imported (dependencies)
# - All types, traits, functions defined
# - Line ranges for jumping directly to definitions

# 2. Explore module structure
qi '*' -i ns --def -s pub

# 3. Public API surface
qi '*' -s pub --def -i func class enum trait

# 4. Entry points
qi 'main' -i func -e
```

**Why TOC first?**
- Instant overview without opening files
- Line ranges let you jump straight to code
- Reveals module structure and dependencies
- Faster than reading entire files

### Understanding a Function

```bash
# 1. Find it
qi "handle_request" -i func

# 2. See the full body
qi "handle_request" -i func -e

# 3. What does it operate on?
qi 'request' --within 'handle_request' -x noise

# 4. What does it call?
qi '*' -i call --within 'handle_request' --limit 30

# 5. Awaited calls inside it
qi '*' -i call -m await --within 'handle_request'

# 6. Who calls it?
qi 'handle_request' -i call

# 7. Is it called as a macro? In a closure? With what arg types?
qi 'handle_request' -i call -C 3
```

### Tracing a Type Through a Codebase

```bash
# 1. Find the definition
qi "User" -i class -e

# 2. Find all impl blocks
qi 'User' -i class --def       # struct + all impls

# 3. List methods
qi '*' -i func -p 'User'

# 4. List fields
qi '*' -i prop -p 'User'

# 5. Find variables of this type
qi '*' -i var -t 'User%' --def

# 6. Find function parameters of this type
qi '*' -i arg -t 'User%'
qi '*' -i arg -t '&User%'
qi '*' -i arg -t '&mut User%'

# 7. Find functions that return it
qi '*' -i func -t 'User' --def
qi '*' -i func -t 'Result<User,%' --def
```

### Trait Implementation Survey

```bash
# 1. Find the trait
qi "Display" -i trait -e

# 2. Required methods
qi '*' -i func -p 'Display'

# 3. All implementations
qi '*' -i class -c 'impl Display'

# 4. Implementations in a specific module
qi '*' -i class -c 'impl Display' -f 'src/types/'

# 5. Concrete impl bodies
qi '*' -i class -c 'impl Display' -e
```

### Error Handling Analysis

```bash
# 1. Error enum definition
qi "AppError" -i enum -e

# 2. Variants
qi '*' -i case -p 'AppError'

# 3. From impls (conversions)
qi 'AppError' -i class -c 'impl From'

# 4. Display/Error trait impls
qi 'AppError' -i class -c 'impl Display'
qi 'AppError' -i class -c 'impl Error'

# 5. Functions returning the error
qi '*' -i func -t 'Result<%AppError%' --def
qi '*' -i func -t '%AppError>' --def

# 6. ? operator usage (find call sites that propagate errors)
qi '*' -i call --within 'load_config' -C 2
```

### Refactoring a Function Signature

```bash
# 1. Find the function
qi 'parse_config' -i func -e

# 2. Find every caller
qi 'parse_config' -i call --limit 50

# 3. See callers in context
qi 'parse_config' -i call -C 3

# 4. Find argument names that callers use (to update naming consistently)
qi '*' -i arg --columns line,symbol,parent,type --within 'parse_config'

# 5. Tests that exercise it
qi 'parse_config' -i call -f '%_test%' -f 'tests/'
qi 'parse_config' -i call --within 'test_*'
```

### Test Discovery

```bash
# All test functions
qi '*' -i func -c '%test%' --def

# Tests in a module
qi '*' -i func -c '#[test]' --def

# Benchmark functions
qi '*' -i func -c '#[bench]' --def

# should_panic tests
qi '*' -i func -c '%should_panic%' --def

# Test modules (Rust convention)
qi 'tests' -i ns --def
```

---

## Power User Combinations

These combinations unlock qi's full potential by stacking filters creatively.

### Combining Scope + Modifier + Within

```bash
# Public async functions called from a specific function
qi '*' -i call --within 'orchestrator' -m await

# Public unsafe surface within a module
qi '*' -i func -s pub -m unsafe -f 'src/ffi/'

# Const fn in the public API
qi '*' -i func -s pub -m const --def
```

### Combining Parent + Modifier + Type

```bash
# Methods on User that return Result
qi '*' -i func -p 'User' -t 'Result%' --def

# Async methods on a specific type
qi '*' -i func -p 'Client' -m async --def

# Static methods (no &self) — heuristic: returns Self or no parent in call
qi 'new' '*_new' -i func -p 'User' --def
```

### Combining Clue + Within + Definition

```bash
# Test functions within a specific module
qi '*' -i func -c '#[test]' --def -f 'src/parser/'

# Derived traits (audit derives across the codebase)
qi '*' -i class -c '%Serialize%' --def
qi '*' -i class -c '%Debug%' --def

# Deprecated public API
qi '*' -s pub -c '#[deprecated]' --def
```

### Combining Multiple Filters for Precision

```bash
# All public async methods on a client
qi '*' -i func -p 'Client' -s pub -m async --def

# Inherent impl blocks for a specific type
qi '*' -i class -c 'impl' -f 'src/models/user.rs'

# Methods returning Self (likely builders/constructors)
qi '*' -i func -t 'Self' --def
qi '*' -i func -t '&mut Self' --def
```

### Creative Sampling

```bash
# Sample async usage per file
qi '*' -i call -m await --limit-per-file 2

# Sample test patterns
qi '*' -i func -c '%test%' --def --limit-per-file 3

# Compare distribution
qi 'unwrap' -i call --limit 20            # Might cluster in one file
qi 'unwrap' -i call --limit-per-file 3    # Distributed sample
```

---

## Advanced Queries

### Parent Symbol Filtering (`-p`)

`-p` filters by the parent symbol — for Rust, this is:
- The **impl target** for methods (e.g., `User` for `impl User { ... }`)
- The **trait** for trait methods
- The **enum** for variants
- The **receiver name** for method calls

```bash
# All methods on a type
qi '*' -i func -p 'Client'

# All variants of an enum
qi '*' -i case -p 'Result'

# All calls on a specific local variable
qi '*' -i call -p 'response'
qi '*' -i call -p 'config'

# Specific method call on specific receiver
qi 'lock' -i call -p 'mutex'
qi 'await' -i call -p 'future'
```

**Note:** Parent is populated for method **definitions** (impl target) and method **calls** (receiver name).

### Scoped Search (`--within`)

```bash
# What does parse_request do with `headers`?
qi 'headers' --within 'parse_request' -x noise

# Find all .await points within a function
qi '*' -i call -m await --within 'fetch_user'

# Find local variables in a function
qi '*' -i var --within 'process_data' --def

# Multiple functions (OR logic)
qi 'token' --within 'authenticate' 'authorize' 'refresh_token'
```

**Why better than grep:** Understands function boundaries — no false matches from neighbors.

### Type Filter (`-t`)

```bash
# Find all `Result<...>` returning functions
qi '*' -i func -t 'Result<%' --def

# Find `&str` parameters
qi '*' -i arg -t '&str'

# Find `Vec<u8>` returns
qi '*' -i func -t 'Vec<u8>' --def

# Generic constraints
qi '*' -i arg -t 'impl %'    # impl Trait params
qi '*' -i arg -t 'Box<dyn %' # trait object params
```

### Related Patterns (`--and`)

```bash
# Find unwrap on a Result on the same line
qi 'Result' 'unwrap' --and

# Find lock/unlock within 5 lines (manual locking — usually a bug)
qi 'lock' 'unlock' --and 5

# Find unsafe + raw pointer use within 10 lines
qi 'unsafe' '*mut' --and 10

# Find ?-operator usage with specific functions
qi 'parse' '?' --and -C 2
```

### Sampling (`--limit-per-file`)

```bash
# Sample async usage per file
qi '*' -i func -m async --limit-per-file 2

# Sample trait impls per file
qi '*' -i class -c 'impl %' --limit-per-file 2

# Sample macro invocations per file
qi '*' -i call -c 'macro!' --limit-per-file 5
```

### Multi-Column Display

```bash
# Default columns
qi '*' -i func --def --limit 20

# Custom columns
qi '*' -i func --columns line,symbol,scope,modifier,type

# All columns
qi '*' -i func -v

# Just symbols (good for piping)
qi '*' -i func --columns symbol
```

### File Filtering

```bash
# Single file
qi 'handler' -f 'src/server.rs'

# Extension
qi '*' -i func -f '.rs'

# Directory subtree
qi '*' -i func -f 'src/parser/'

# Wildcard pattern
qi '*' -i func -f '%/handlers/%'

# Tests vs production
qi '*' -i func -f 'tests/'
qi '*' -i func -f '%_test.rs'
```

### Excluding Noise

```bash
# Exclude comments and strings (recommended default)
qi 'error' -x noise

# Exclude only comments
qi 'error' -x comment

# Exclude only strings
qi 'error' -x string
```

### Context Display

```bash
# Show N lines after match
qi 'fn main' -A 10

# Show N lines before match
qi 'return' -B 5

# Show N lines before and after
qi 'handle_request' -C 20

# Best for definitions — show full body inline:
qi 'handle_request' -i func -e
```

---

## Tips & Tricks

### Search Patterns

```bash
# Wildcard at start
qi '*Error' -i enum         # Error enum types

# Wildcard at end
qi 'new_*' -i func          # Constructor variants

# Wildcard in middle
qi 'Http*Client' -i class   # Hybrid client types

# Match anything
qi '*' -i func              # All functions
```

### Common Rust Naming Patterns

```bash
# Constructors
qi 'new' 'with_*' 'from_*' -i func --def

# Builders
qi '*Builder' -i class --def
qi 'build' -i func -p '*Builder'

# Conversions
qi 'to_*' 'into_*' 'as_*' -i func --def

# Predicates (return bool)
qi 'is_*' 'has_*' -i func --def

# Trait-impl conventional methods
qi 'fmt' -i func -p 'Display'
qi 'next' -i func -p 'Iterator'
qi 'drop' -i func -p 'Drop'
qi 'eq' -i func -p 'PartialEq'

# Error types
qi '*Error' -i enum --def

# Tests
qi 'test_*' -i func -c '#[test]'
qi '*' -i func -f 'tests/'
```

### Combining with Shell Tools

```bash
# Count public functions per file
qi '*' -i func -s pub --def --columns line,symbol \
    | cut -d: -f1 | sort | uniq -c | sort -rn

# Most-called functions
qi '*' -i call --columns symbol \
    | sort | uniq -c | sort -rn | head -20

# Files with most unsafe code
qi '*' -i func -m unsafe --def --columns line \
    | cut -d: -f1 | sort | uniq -c | sort -rn

# All unique trait names
qi '*' -i trait --def --columns symbol | sort -u

# Functions defined but never called (potential dead code)
comm -23 \
    <(qi '*' -i func --def --columns symbol | sort -u) \
    <(qi '*' -i call --columns symbol | sort -u)
```

### Performance Tips

```bash
# Narrow with file filter first
qi 'handler' -f 'src/server/' -i func

# Use --limit during exploration
qi '*' -i func --limit 20

# Exclude noise for code-only searches
qi 'error' -x noise -i call

# Use --def or --usage to halve the result set
qi 'parse' -i func --def
```

### Quick Reference Card

```bash
# Most useful Rust patterns (copy these!)

# File structure
qi '*' -f 'src/lib.rs' --toc

# See a function
qi 'parse_config' -i func -e

# Public API
qi '*' -s pub --def

# Async functions
qi '*' -i func -m async --def

# Unsafe functions
qi '*' -i func -m unsafe --def

# All methods on a type
qi '*' -i func -p 'User'

# All implementations of a trait
qi '*' -i class -c 'impl Display'

# All await points
qi '*' -i call -m await

# Macro invocations
qi '*' -i call -c 'macro!'

# Tests
qi '*' -i func -c '#[test]'

# Imports from a path
qi '*' -i imp -ns 'std::collections'

# What does function X do?
qi '*' --within 'X' -x noise
```

---

## Real-World Examples

### Example 1: Auditing the Public API

```bash
# Get the full public API surface
qi '*' -s pub --def --columns line,symbol,context_type,type > public_api.txt

# Categorize:
qi '*' -i func -s pub --def       # Functions
qi '*' -i class -s pub --def      # Structs
qi '*' -i enum -s pub --def       # Enums
qi '*' -i trait -s pub --def      # Traits

# Find pub items that look internal (audit candidates)
qi '*helper*' '*internal*' '*impl*' -s pub --def

# Items potentially exposed too broadly (should be pub(crate)?)
qi '*' -s pub --def -f 'src/internal/'
```

### Example 2: Async Performance Audit

```bash
# 1. All async functions (the candidate set)
qi '*' -i func -m async --def > async_fns.txt

# 2. For each, find any sync-blocking calls
for fn in $(cat async_fns.txt | awk '{print $1}'); do
    echo "=== $fn ==="
    qi 'sleep' 'lock' 'recv' -i call --within "$fn"
done

# 3. Find std::fs usage in async code (should be tokio::fs)
qi '*' -i call --within '*async*' -ns 'std::fs'

# 4. Find blocking println in async code
qi 'println' -i call --within 'handle_*'
```

### Example 3: Unsafe Code Audit

```bash
# Where is unsafe used?
qi '*' -i func -m unsafe --def       # unsafe functions
qi 'unsafe' -f '.rs' -i func -e      # see implementations

# What calls do unsafe functions make?
for fn in $(qi '*' -i func -m unsafe --def --columns symbol); do
    echo "=== $fn ==="
    qi '*' -i call --within "$fn"
done

# Find raw pointer dereferences in context
qi '*mut' '*const' --and -C 2

# Find transmute calls (always a smell)
qi 'transmute' -i call -C 3
```

### Example 4: Tracing an Error Type

```bash
# 1. Find the error type
qi 'AppError' -i enum -e

# 2. All variants
qi '*' -i case -p 'AppError'

# 3. All From<X> conversions feeding into AppError
qi 'AppError' -i class -c 'impl From'

# 4. All functions that produce this error
qi '*' -i func -t '%AppError%' --def

# 5. Where are these errors created?
qi '*' -i call -ns 'AppError'

# 6. How are they handled? (look for ? and match arms)
qi 'AppError' --within '*' -C 5
```

### Example 5: Reviewing a New Module

```bash
# 1. Module overview
qi '*' -f 'src/auth/' --toc

# 2. Public API of this module
qi '*' -s pub --def -f 'src/auth/'

# 3. External dependencies (use statements)
qi '*' -i imp -f 'src/auth/'

# 4. Tests for this module
qi '*' -i func -c '#[test]' --def -f 'src/auth/'

# 5. Type-by-type drill-down
qi '*' -i class -f 'src/auth/' --def -e

# 6. Methods grouped by type
for ty in $(qi '*' -i class --def --columns symbol -f 'src/auth/' | sort -u); do
    echo "=== $ty ==="
    qi '*' -i func -p "$ty"
done
```

### Example 6: Finding Implementations of a Trait Across Crates

```bash
# Definition
qi 'Iterator' -i trait -e

# All types implementing Iterator in your crate
qi '*' -i class -c 'impl Iterator'

# With full impl bodies
qi '*' -i class -c 'impl Iterator' --def -e

# Their `next` implementations
qi 'next' -i func -p 'Iterator' --def
```

### Example 7: Refactoring a Trait

```bash
# 1. Trait definition
qi 'Storage' -i trait -e

# 2. Find all implementations
qi '*' -i class -c 'impl Storage' --def --columns line,symbol

# 3. Find all required methods
qi '*' -i func -p 'Storage' --def

# 4. Find all call sites of those methods (heavy lift!)
qi 'read' 'write' 'delete' -i call -C 2

# 5. Find places that use the trait as a bound
qi '*' -i arg -t 'impl Storage' --def
qi '*' -i arg -t 'dyn Storage' --def
qi '*' -i arg -t '&dyn Storage' --def
```

---

## Troubleshooting

### Pattern Not Matching

```bash
# Symbol not found
qi 'my_function' -i func
# (no results)

# Try case-insensitive partial match
qi '%function%' -i func

# Check whether it's a method (look for parent)
qi 'my_function' -i func -v

# Check unfiltered
qi 'my_function' -v

# Generic types — try base name
qi 'Vec' -i class       # works
qi 'Vec<u8>' -i class   # won't match — symbol is just "Vec"
```

### Too Many Results

```bash
# Add context filter
qi 'test' -i func

# Add file filter
qi 'test' -i func -f 'src/server/'

# Filter by visibility
qi 'test' -i func -s pub --def

# Exclude noise
qi 'test' -x noise

# Limit
qi 'test' -i func --limit 20
```

### Symbol in Stopwords

```bash
# Common words may be filtered (`new`, `next`, etc. are NOT — common Rust words are kept)
qi 'new'    # Works fine
qi 'self'   # Filtered (Rust keyword)

# Use wildcards for common words
qi '%self%'
```

### Generics & Lifetimes

qi indexes the **base type name**, not the full generic type. The full type expression is in the `type` column.

```bash
# Find users of Wrapper<'a, T>
qi 'Wrapper' -i class --def       # finds it
qi 'Wrapper<...>' -i class         # won't work

# Filter by type expression instead
qi '*' -i arg -t 'Wrapper<%'       # any Wrapper<...> arg
qi '*' -i var -t '&%' --def        # any & reference type
```

---

## Best Practices & Learning Path

### Progressive Learning Path

**Level 1: Basics**
```bash
qi 'symbol'                       # Find a symbol
qi 'symbol' -i func               # Filter by context
qi 'symbol' -x noise              # Exclude comments/strings
```

**Level 2: Structure First**
```bash
qi '*' -f 'src/lib.rs' --toc      # Always start with TOC
qi 'fn_name' -i func -e           # See implementations
qi 'Type' -i class -e             # Expand type definitions
```

**Level 3: Rust-Specific Patterns**
```bash
qi '*' -s pub --def               # Public API
qi '*' -i func -m async           # Async functions
qi '*' -i func -m unsafe          # Unsafe code
qi '*' -i class -c 'impl %'       # Trait implementations
```

**Level 4: Advanced Filtering**
```bash
qi '*' -i func -p 'User'          # Methods on a type
qi '*' -i call --within 'main'    # Calls within a function
qi 'lock' 'unlock' --and 5        # Related patterns
```

**Level 5: Power Combinations**
```bash
qi '*' -i func -s pub -m async --def              # Public async API
qi '*' -i class -c 'impl Display' --def -e        # Display impls with bodies
qi '*' -i call -m await --within 'handle_request' # Async flow
qi '*' -i func -p 'Iterator' --def                # Iterator methods
```

**Level 6: Master Workflows**
- Start with `--toc` for any unfamiliar file
- Use `-e` to read code inline rather than opening files
- Use `--within` for function-scoped exploration
- Stack `-s`, `-m`, `-c`, `-p`, `-t` for surgical precision

### Common Pitfalls to Avoid

**❌ Don't do this:**
```bash
# Reading files instead of TOC
cat src/handler.rs | less

# Using grep for impl blocks
grep -rn "impl Display" .

# Forgetting -x noise
qi 'error'                           # buried in comments

# Searching for full generic types
qi 'Result<String, AppError>' -i func   # won't match
```

**✅ Do this instead:**
```bash
qi '*' -f 'src/handler.rs' --toc

qi '*' -i class -c 'impl Display'

qi 'error' -x noise -i call

qi '*' -i func -t 'Result<%AppError%' --def
```

### Efficiency Tips

1. **TOC first**: `qi '*' -f 'file.rs' --toc` shows structure instantly
2. **Use -e liberally**: Inline code beats opening files
3. **Filter by scope**: `-s pub` for API audit, no-scope for internals
4. **Filter by modifier**: `-m async`, `-m unsafe`, `-m const` are Rust gold
5. **Use clue for attrs/impls**: `-c '#[test]'`, `-c 'impl Display'`
6. **Sample with --limit-per-file**: Get distributed examples
7. **Always `-x noise`**: Unless searching docs/strings specifically

### When to Use What

| Task | Best Tool |
|------|-----------|
| File structure | `qi '*' -f 'file.rs' --toc` |
| See function code | `qi 'name' -i func -e` |
| Public API surface | `qi '*' -s pub --def` |
| Async functions | `qi '*' -i func -m async` |
| Unsafe audit | `qi '*' -i func -m unsafe` |
| Methods on a type | `qi '*' -i func -p 'Type'` |
| Trait implementations | `qi '*' -i class -c 'impl Trait'` |
| Macro invocations | `qi '*' -i call -c 'macro!'` |
| Imports | `qi '*' -i imp -ns 'crate%'` |
| Await points | `qi '*' -i call -m await` |
| What function does | `qi '*' --within 'fn_name'` |
| Related patterns | `qi 'a' 'b' --and 5` |
| Literal text search | `grep` (not qi) |

---

## Comparison with Other Tools

### qi vs grep

```bash
# grep: text-based, many false positives
grep -rn 'parse_config' .

# qi: semantic, finds only real symbol
qi 'parse_config' -i func

# grep: can't distinguish trait impls from other usages
grep -rn 'impl Display' .

# qi: precise — finds only impl blocks for Display
qi '*' -i class -c 'impl Display'
```

### qi vs cargo doc

```bash
# cargo doc: rendered HTML for public items
cargo doc --open

# qi: queryable index of all items (pub + private), CLI-driven
qi '*' -s pub --def                       # Public API
qi '*' --def -f 'src/internal/'           # Private internals
qi '*' -i func -p 'Client' --def          # All Client methods
```

### qi vs cargo expand

```bash
# cargo expand: shows macro-expanded source
cargo expand my_macro

# qi: shows macro definitions and invocations
qi 'my_macro' -i func -c 'macro_rules!' -e
qi 'my_macro' -i call -c 'macro!'
```

### qi vs IDE "Find Usages"

```bash
# IDE: great for active development, single symbol
# qi: great for codebase-wide audits, scriptable

# Find all callers of a function
qi 'process' -i call --limit 100

# Find all async callers of it
qi 'process' -i call -m await

# qi advantage: composable + pipeable
qi '*' -i func -s pub --def | wc -l       # count public functions
```

---

## Glossary

**Context**: What a symbol *is* (function, struct, trait, etc.)

**Clue**: Auxiliary annotation — attributes for definitions (`#[derive]`, `#[test]`), `impl <Trait>` for impl blocks, `macro_rules!` / `macro!` for macros.

**Definition**: Where a symbol is declared (def=1)

**Usage**: A reference to a symbol (def=0)

**Scope**: Visibility — `pub`, `pub(crate)`, `pub(super)`, or empty for private.

**Modifier**: Behavior keyword — `async`, `unsafe`, `const`, `extern` for functions; `await` for calls.

**Parent**: For methods, the impl target type (or trait); for variants, the enum; for method calls, the receiver name.

**Namespace**: Module path qualifier — `std::collections`, `crate::auth`, `tokio::time`, etc.

**Type**: The full type annotation text — `Result<String, io::Error>`, `&'a mut T`, `Vec<u8>`, etc.

---

## Getting Help

```bash
# Show full help
qi --help

# List available columns
qi 'test' --columns invalid   # error message shows valid column names

# Check version
qi --version

# Report issues
# https://github.com/ebcode/SourceMinder/issues
```

---

## Additional Resources

- **Source Code**: `rust/rust_language.c` — handler implementations
- **Configuration**: `rust/config/{file_extensions,ignore_files,keywords}.txt`
- **AST Explorer**: `tools/ast-explorer-rust` — debug tree-sitter output for a `.rs` file
- **Test Files**: `scratch/sources/rust/*.rs` — known-good examples covering each feature

---

## Quick Command Reference

| Task | Command |
|------|---------|
| File structure | `qi '*' -f 'file.rs' --toc` |
| Find function | `qi 'name' -i func` |
| Expand definition | `qi 'name' -i func -e` |
| Public API | `qi '*' -s pub --def` |
| Async functions | `qi '*' -i func -m async` |
| Unsafe functions | `qi '*' -i func -m unsafe` |
| Methods on type | `qi '*' -i func -p 'Type'` |
| Trait impls | `qi '*' -i class -c 'impl Trait'` |
| Enum variants | `qi '*' -i case -p 'Enum'` |
| Struct fields | `qi '*' -i prop -p 'Struct'` |
| Macro definitions | `qi '*' -i func -c 'macro_rules!'` |
| Macro invocations | `qi '*' -i call -c 'macro!'` |
| Await points | `qi '*' -i call -m await` |
| Scoped calls | `qi 'fn' -i call -ns 'std::%'` |
| Search within function | `qi 'token' --within 'authenticate'` |
| Related patterns | `qi 'lock' 'unlock' --and 5` |
| Filter by type | `qi '*' -i arg -t 'Result<%'` |
| Sample per file | `qi '*' --limit-per-file 3` |
| Show context | `qi 'name' -C 20` |
| Exclude noise | `qi 'name' -x noise` |
| Custom columns | `qi '*' --columns line,symbol,scope,modifier` |

---

*This guide covers qi version with Rust-specific scope (pub/pub(crate)), modifiers (async/unsafe/const/extern/await), and clues (#[attrs], impl blocks, macro_rules!).*
