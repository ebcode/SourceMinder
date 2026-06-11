# Rust Developer's Guide to qi

A practical guide for using qi to search and analyze Rust codebases. It focuses on
Rust-specific patterns: visibility, modifiers, traits and impl blocks, pattern
matching, async/unsafe, macros, and FFI.

Every query in this guide works against the example sources in
`scratch/sources/rust/` — index them with `./index-rust scratch/sources/rust --once`
to follow along.

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Core Concepts](#core-concepts)
3. [Finding Definitions](#finding-definitions)
4. [Visibility & Modifiers](#visibility--modifiers)
5. [Traits & Impl Blocks](#traits--impl-blocks)
6. [Enums & Pattern Matching](#enums--pattern-matching)
7. [Structs & Construction Sites](#structs--construction-sites)
8. [Async & Unsafe](#async--unsafe)
9. [Macros & FFI](#macros--ffi)
10. [Modules & Imports](#modules--imports)
11. [Common Workflows](#common-workflows)
12. [Advanced Queries](#advanced-queries)
13. [Troubleshooting & Pitfalls](#troubleshooting--pitfalls)
14. [Quick Reference](#quick-reference)

---

## Quick Start

### Index Your Rust Project

```bash
# Index current directory (runs as a daemon, watching for changes)
index-rust .

# Index once and exit
index-rust ./src --once

# Index multiple crates in a workspace
index-rust ./crates/core ./crates/cli ./crates/server
```

The indexer skips `target/` and other build directories listed in
`rust/config/ignore_files.txt`.

### First Five Queries on a New Codebase

```bash
# 1. Map the entry points' structure
qi % -f src/lib.rs --toc
qi % -f src/main.rs --toc

# 2. The public API surface
qi % -s pub --def -i func class enum trait --limit 40

# 3. The trait landscape (the type system's backbone)
qi % -i trait --def

# 4. Expand the key types you saw
qi Config AppError -i class enum -e

# 5. Walk the main entry point
qi % --within main -x noise --limit 30
```

This reveals: what the crate exposes → what traits govern behavior → what the
core types look like → what the program does at startup.

### The Core Loop

```bash
qi % -f file.rs --toc            # 1. structure first
qi handle_request -i func -e     # 2. read a definition inline
qi handle_request -i call -C 3   # 3. find and inspect callers
qi token --within handle_request # 4. explore inside a function
```

`--toc` gives you the map, `-e` shows the code without opening the file,
`--within` respects function boundaries (no false matches from neighbors).

---

## Core Concepts

### Context Types (CTX column)

| Context | Short | Rust constructs |
|---------|-------|-----------------|
| `function` | `func` | `fn` definitions: free functions, methods, trait signatures, FFI declarations |
| `argument` | `arg` | Function parameters; identifier arguments at call sites (clue = callee) |
| `variable` | `var` | `let` / `const` / `static` bindings; pattern bindings (match arms, if-let, for) |
| `property` | `prop` | Struct and enum-variant fields; field accesses (`self.name`) and initializers |
| `class` | `class` | `struct`, `union`, `impl` blocks; struct-literal and pattern-constructor usages |
| `enum` | `enum` | `enum` definitions |
| `case` | `case` | Enum variants; scoped variant references in patterns (`Shape::Circle =>`) |
| `trait` | `trait` | `trait` definitions; trait bounds, supertraits, where clauses, derive contents |
| `type` | `type` | `type` aliases and associated types (incl. GATs) |
| `namespace` | `ns` | `mod` declarations |
| `import` | `imp` | `use` items (incl. `pub use` re-exports) and `extern crate` |
| `call` | `call` | Function, method, and macro calls |
| `lambda` | `lambda` | Closures (indexed as `<closure>` alongside their binding) |
| `macro` | `macro` | `macro_rules!` definitions |
| `comment` | `com` | Words from comments |
| `string` | `str` | Words from string literals |
| `filename` | `file` | Filename without extension |

### Scope (visibility)

Captured exactly as written:

| Scope | Meaning |
|-------|---------|
| `pub` | Fully public |
| `pub(crate)` | Crate-private |
| `pub(super)` | Visible to parent module |
| *(empty)* | Module-private (Rust default) |

### Modifiers

| Modifier | Applies to | Meaning |
|----------|------------|---------|
| `async` | functions | Returns a `Future` |
| `unsafe` | functions | Requires `unsafe` to call |
| `const` | functions | Callable at compile time |
| `extern "C"`, `extern "system"`, ... | functions, statics | FFI ABI — set on `extern fn` and on every declaration inside an `extern { }` block |
| `await` | calls | The call was `.await`-ed |
| `mut` | variables, arguments | Mutable binding: `let mut x`, `fn f(mut x: T)`, `static mut X`, `Some(mut x)` patterns |

Modifiers combine with everything else: `pub const fn` → scope `pub`, modifier `const`;
`static mut` inside `extern "C" { }` → modifier `extern "C" mut`.

### Clues

| Clue | Where | Meaning |
|------|-------|---------|
| `#[test]`, `#[inline]`, `#[derive]`, ... | definitions | Outer attributes, comma-joined when stacked: `#[should_panic],#[test]`. Inner attributes (`#![...]`) are *not* attributed to the following item. |
| `impl Display` | impl blocks | The impl implements the named trait |
| `impl` | impl blocks | Inherent impl (no trait) |
| `derive` | trait usages | The trait reference came from a `#[derive(...)]` list |
| `as` | imports | Aliased import: `use foo as bar`, `extern crate proc_macro as pm` |
| *(callee name)* | call arguments | `format`, `println`, etc. — which call this argument belongs to |

### Parent

The PAR column holds the nearest enclosing or qualifying symbol:

- **Methods** → the impl target (`impl User` → parent `User`) or trait (for trait methods)
- **Enum variants** → the enum; **variant fields** → the variant (`width` → `Rect`)
- **Struct fields** → the struct
- **Associated consts and types** → the trait/impl
- **Method calls** → the receiver: `stack.pop()` → `stack`, `self.read(...)` → `self`.
  In a chain, each link's parent is the previous link:
  `text.trim().parse()` → `trim`|`text`, `parse`|`trim`
- **Field accesses** → the immediate owner: `person.address.city` → `city`|`address`
- **Struct-literal fields** → the let-bound variable when assigned
  (`let addr = Address { street: ... }` → `street`|`addr`), the enclosing field for
  nested literals, otherwise the literal's type name (`Person { name }` in return
  position → `name`|`Person`)

### Namespace (NS column)

Module-path qualifiers, captured for:

- imports: `use std::io::{Read, Write}` → `Read`|`std::io` (brace groups and nested
  groups resolve fully: `use a::{b::{C}}` → `C`|`a::b`)
- scoped calls: `AppError::Io(e)` → `Io`|`AppError`, `std::fs::read_to_string(p)` → NS `std::fs`
- scoped patterns: `Shape::Circle(r) =>` → `Circle`|`Shape`
- scoped trait bounds: `K: std::hash::Hash` → `Hash`|`std::hash`
- aliases: `use auth::tokens::generate as gen_token` → `gen_token` with the full source path

### Type

The TYPE column holds the annotation text verbatim:

- function return types: `Result<String, std::io::Error>`
- `let x: T`, parameters, fields, consts: `T`
- type aliases: the aliased type (`type Cache<T> = HashMap<UserId, T>` → `HashMap<UserId, T>`)
- `macro_rules!` for macro definitions; `macro` for macro invocations

---

## Finding Definitions

### Functions and Methods

```bash
qi parse_config -i func              # find it
qi parse_config -i func -e           # full body inline
qi % -i func -f src/parser.rs        # all functions in a file
qi % -i func -p User                 # methods on a type (impl target)
qi % -i func -p Storage              # trait method signatures
qi new with_% from_% -i func --def   # constructors by convention
qi % -i func -p User -t Self         # methods returning Self (builders)
```

### Structs, Enums, Traits

```bash
qi User -i class -e                  # struct definition, expanded
qi % -i class --def -f src/models/   # all structs in a module
qi % -i prop -p Config               # fields of a struct
qi Status -i enum -e                 # enum definition
qi % -i case -p Status --def         # its variants
qi % -i prop -p Rect                 # fields of a struct-like variant
qi Storage -i trait -e               # trait definition
```

### Variables, Constants, Type Aliases

```bash
qi MAX_RETRIES -i var                   # a const/static/let binding
qi % -i var -f src/constants.rs --def  # all consts in a file
qi % -i var -t 'Vec<%' --def            # bindings with a Vec type annotation
qi % -i var -m mut --def                # mutable bindings
qi % -i var -p Storage                  # associated consts of a trait
qi AppResult -i type                    # type alias (TYPE column shows the target)
qi % -i type -p Container               # associated types: type Item; / GATs
```

---

## Visibility & Modifiers

### Public API Surface

```bash
qi % -s pub --def --limit 50            # everything public
qi % -i func -s pub --def               # public functions
qi % -i class enum trait -s pub --def   # public types
qi % -s 'pub(crate)' --def              # crate-private items
qi %helper% %internal% -s pub --def     # inadvertently-public helpers?
```

### Mutability Audit

```bash
qi % -m mut --def                       # every mutable binding
qi % -i var -m '%mut%' -f src/ffi/      # mutable statics (incl. extern blocks)
qi % -i arg -m mut                      # mut-by-value parameters
```

### Const fn

```bash
qi % -i func -m const --def
qi % -i func -s pub -m const --def
```

---

## Traits & Impl Blocks

### Trait Definitions and Members

```bash
qi Serializable -i trait -e         # define-site
qi % -i trait --def                 # all traits in the crate
qi % -i func -p Storage             # method signatures (bodyless ones included)
qi % -i var -p Container            # associated consts (const CAPACITY: usize)
qi % -i type -p Stream              # associated types (type Item<'a>)
```

### Impl Blocks

Impl blocks are `class` entries whose clue records the trait: `impl Display`
(or just `impl` for inherent impls). The symbol is the impl target.

```bash
qi % -i class -c impl --def         # inherent impls
qi % -i class -c 'impl %' --def     # all trait impls
qi % -i class -c 'impl Display'     # who implements Display?
qi % -i class -c 'impl From'        # all From conversions
qi User -i class --def              # struct def + every impl block for User
```

### Trait Bounds, Supertraits, Where Clauses

Every trait named in a bound is indexed as a `trait` **usage** — inline bounds
(`<T: Clone>`), where clauses, supertraits, and associated-type bounds alike.
Scoped paths carry their namespace.

```bash
qi Display -i trait --usage         # everywhere Display is required as a bound
qi % -i trait --usage -f src/       # the crate's full constraint vocabulary
qi Hash -i trait -ns std::hash      # scoped bound: where K: std::hash::Hash
qi Send Sync -i trait --usage       # supertraits: trait Serializable: Send + Sync
```

### Derives

Names inside `#[derive(...)]` are indexed as `trait` usages with clue `derive`:

```bash
qi % -i trait -c derive             # every derived trait, everywhere
qi Serialize -i trait -c derive     # which types derive Serialize?
qi Clone -i trait --usage           # Clone as a bound *and* as a derive
```

### Method Call Sites

```bash
qi serialize -i call                # all calls to a method
qi % -i call -p self                # all self.method() calls
qi lock -i call -p mutex            # specific method on specific receiver
qi parse -i call                    # includes turbofish calls: .parse::<i32>()
```

Chained calls keep their link-to-link parents, so `-p` works mid-chain:

```bash
# nums.iter().map(...).collect() indexes: iter|nums, map|iter, collect|map
qi collect -i call -p map
```

---

## Enums & Pattern Matching

### Variants: Definitions and References

```bash
qi % -i case -p Status --def        # variants of an enum
qi % -i case --usage                # variant references in match/if-let patterns
qi % -i case -ns Shape              # arms matching on Shape::*
qi NotFound -i case -ns AppError    # one variant, pattern position
qi Io -i call -ns AppError          # same variant, construction position
```

Scoped paths in patterns (`AppError::NotFound =>`) are indexed as `case` usages
with the path in NS. Bare constructors in patterns (`Ok(v)`, `Point { .. }`) are
indexed as `class` usages.

### Pattern Bindings

Bindings are extracted from `match` arms, `if let`, `while let`, `for`, and `let`
destructuring — including struct-pattern shorthand:

```bash
# Shape::Rect { width, height } => ...   binds width and height as VARs
qi width height -i var --def

# renamed bindings: let Point { x: px, y: py } = ...
qi px py -i var

# mut bindings in patterns: Some(mut conn) => ...
qi % -i var -m mut --def
```

Uppercase-start identifiers in pattern position (`None`, `Pending`) are treated
as constructor/constant references, not bindings, so they never pollute variable
searches. One-character bindings (`x`, `i`) are dropped by the noise filter.

---

## Structs & Construction Sites

Struct literals index both the type name (a `class` usage) and each field:

```bash
qi User -i class --usage            # every place a User { ... } is built
qi Person --usage -C 2              # construction sites with context

# Fields at initialization sites:
qi street -i prop -p addr           # let addr = Address { street: ... }
qi name -i prop -p Person           # Person { name, .. } in return position
qi age -i prop --usage              # all initializations + accesses of `age`
```

Field-access chains keep per-link parents:

```bash
# person.address.city indexes: address|person, city|address
qi city -i prop -p address
```

---

## Async & Unsafe

### Async Functions and Await Points

```bash
qi % -i func -m async --def              # all async functions
qi % -i func -s pub -m async --def       # public async API
qi % -i call -m await                    # every await point
qi % -i call -m await --within fetch_data   # awaits inside one function
qi % -i call -m await -p client          # awaited calls on one receiver
```

### Unsafe Audit

```bash
qi % -i func -m unsafe --def             # the audit surface
qi % -i func -m unsafe -s pub --def      # unsafe public API (highest priority)
qi % -i call --within do_unsafe          # what unsafe functions call
qi transmute -i call -C 3                # always worth a look
```

---

## Macros & FFI

### Macro Definitions

`macro_rules!` definitions have their own context:

```bash
qi % -i macro                       # all macro definitions
qi debug_print -i macro -e          # expand one
```

Note: macro *bodies* are token trees and are not indexed — calls inside a
`macro_rules!` definition won't appear in the index.

### Macro Invocations

Macro calls are `call` entries with `macro` in the TYPE column:

```bash
qi % -i call -t macro               # all macro invocations
qi println panic assert_eq -i call  # specific macros
qi % -i call -t macro --within main # macros used in one function

# Arguments passed to macros carry the macro name as their clue:
qi % -i arg -c format               # everything passed to format!
```

### FFI

```bash
# Declarations inside extern blocks get the block's ABI as modifier
qi % -i func -m 'extern "C"' --def       # foreign fns + #[no_mangle] exports
qi % -i func -m 'extern "system"' --def  # other ABIs
qi % -i var -m 'extern%' --def           # foreign statics (errno etc.)
qi % -i func -c '%no_mangle%' --def      # exported with C ABI

# extern crate declarations are imports
qi alloc -i imp                          # extern crate alloc;
qi pm -i imp -c as                       # extern crate proc_macro as pm;
```

---

## Modules & Imports

### Module Structure

```bash
qi % -i ns --def                    # all module declarations
qi % -i ns -c '#[cfg]'              # conditionally-compiled modules (mod tests)
qi tests -i ns --def                # test modules by convention
```

### Use Statements

The NS column holds the source path, including brace groups and nested groups:

```bash
qi % -i imp --limit 30                   # all imported names
qi % -i imp -ns std::collections         # from a specific path
qi % -i imp -ns 'serde%'                 # external dependency usage
qi % -i imp -ns 'crate%'                 # internal imports
qi % -i imp -c as                        # aliases: use foo as bar
qi % -i imp -f src/lib.rs                # what lib.rs pulls in (incl. pub use)
```

### Scoped Calls

```bash
qi new -i call -ns Vec                   # Vec::new()
qi spawn -i call -ns 'tokio%'            # tokio::spawn, tokio::task::spawn...
qi % -i call -ns 'std::fs'               # all std::fs calls (sync IO audit)
```

---

## Common Workflows

### Understanding a Function

```bash
qi handle_request -i func           # 1. find it
qi handle_request -i func -e        # 2. read it
qi % -i call --within handle_request --limit 30   # 3. what it calls
qi % -i call -m await --within handle_request     # 4. where it awaits
qi handle_request -i call -C 3      # 5. who calls it, in context
```

### Tracing a Type

```bash
qi User -i class -e                 # 1. definition
qi User -i class --def              # 2. struct + every impl block
qi % -i func -p User                # 3. methods
qi % -i prop -p User --def          # 4. fields
qi User -i class --usage            # 5. construction sites
qi % -i arg -t '%User%'             # 6. parameters of this type
qi % -i func -t '%User%' --def      # 7. functions returning it
```

### Error Handling Analysis

```bash
qi AppError -i enum -e                       # 1. the error enum
qi % -i case -p AppError --def               # 2. variants
qi AppError -i class -c 'impl From'          # 3. conversions feeding into it
qi AppError -i class -c 'impl Display'       # 4. formatting impl
qi % -i func -t '%AppError%' --def           # 5. functions producing it
qi % -i case -ns AppError                    # 6. where each variant is matched
qi % -i call -ns AppError                    # 7. where each variant is built
```

### Trait Refactoring

```bash
qi Storage -i trait -e                       # 1. the trait
qi % -i class -c 'impl Storage' --def        # 2. all implementations
qi % -i func -p Storage --def                # 3. the method set
qi read write delete -i call -C 2            # 4. call sites of those methods
qi Storage -i trait --usage                  # 5. where it's used as a bound
qi % -i arg -t '%dyn Storage%' -t '%impl Storage%'   # 6. trait-object/impl params
```

### Test Discovery

```bash
qi % -i func -c '#[test]' --def          # all tests
qi % -i func -c '%should_panic%' --def   # panic tests
qi % -i ns -c '#[cfg]' --def             # #[cfg(test)] modules
qi parse_config -i call -f tests/        # tests exercising a function
```

---

## Advanced Queries

### Stacking Filters

All filters compose; each one narrows the result set:

```bash
qi % -i func -p Client -s pub -m async --def    # public async methods on Client
qi % -i func -s pub -c '#[deprecated]' --def    # deprecated public API
qi % -i call -m await --limit-per-file 2        # sample await points per file
qi % -i trait -c derive -f src/models/          # derives in one module
```

### Scoped Search (`--within`)

```bash
qi headers --within parse_request -x noise   # one symbol inside one function
qi % -i var --within process_data --def      # locals of a function
qi token --within authenticate authorize     # multiple functions (OR)
```

### Type Filter (`-t`)

The TYPE column is matched as text, so wildcards work on the annotation:

```bash
qi % -i func -t 'Result<%' --def    # functions returning Result
qi % -i arg -t '&str'               # &str parameters
qi % -i arg -t '&mut %'             # mutable-reference parameters
qi % -i arg -t 'impl %'             # impl Trait parameters
qi % -i func -t 'impl Future%'      # functions returning futures
```

### Related Patterns (`--and`)

```bash
qi fprintf stderr --and             # both on the same line
qi lock unlock --and 5              # within 5 lines of each other
qi unwrap expect --and 10           # clustered panicking calls
```

### Display Control

```bash
qi % -i func -v                                 # all columns
qi % -i func --columns line sym par mod type    # chosen columns (space-separated)
qi getUserById -i func -e --raw                 # bare source (good Edit anchors)
qi handle_request -C 5                          # context lines
qi % -i call --limit-per-file 3 --limit 30      # distributed sampling
```

---

## Troubleshooting & Pitfalls

### Pattern Not Matching

```bash
qi my_function -i func        # no results?
qi '%function%'               # try partial match, no context filter
qi my_function -v             # check what context it actually has
```

### Generics: Search the Base Name

The indexed symbol is the **base** name; the full generic expression lives in
the TYPE column.

```bash
qi 'Vec<u8>' -i class         # ✗ won't match — symbol is just "Vec"
qi Wrapper -i class --def     # ✓ finds Wrapper<'a, T>
qi % -i var -t 'Vec<u8>'      # ✓ full expressions go in -t
```

### Filtered Symbols

Rust keywords (`self`, `Self`, `crate`, ...) are never indexed, and symbols
shorter than two characters are dropped (`x`, `i`, type params `T`, `K`).

**Current limitation:** the shared stopword list is applied case-insensitively
to code symbols, so a few common English words are unfindable even when they are
real Rust symbols — notably `from` (`String::from`, `impl From`'s `fn from`),
`Some`, and a `Move` variant. Prefer searching the surrounding context
(`qi String -ns %`, `qi % -i case -ns Option`) until this is fixed.

### Common Pitfalls

```bash
# ✗ grep for impl blocks            # ✓ ask the index
grep -rn "impl Display" .           qi % -i class -c 'impl Display'

# ✗ forget noise exclusion          # ✓ skip comments/strings
qi error                            qi error -x noise

# ✗ comma-separated columns         # ✓ space-separated
qi % --columns line,sym             qi % --columns line sym

# ✗ macro filter that doesn't exist # ✓ macro calls live in the TYPE column
qi % -i call -c 'macro!'            qi % -i call -t macro
```

---

## Quick Reference

| Task | Command |
|------|---------|
| File structure | `qi % -f file.rs --toc` |
| Expand a definition | `qi name -i func -e` |
| Public API | `qi % -s pub --def` |
| Async functions | `qi % -i func -m async --def` |
| Unsafe functions | `qi % -i func -m unsafe --def` |
| Mutable bindings | `qi % -m mut --def` |
| Methods on a type | `qi % -i func -p Type` |
| Fields of a type | `qi % -i prop -p Type` |
| Enum variants | `qi % -i case -p Enum --def` |
| Variant match sites | `qi % -i case -ns Enum` |
| Construction sites | `qi Type -i class --usage` |
| Trait impls | `qi % -i class -c 'impl Trait'` |
| Trait bounds | `qi Trait -i trait --usage` |
| Derived traits | `qi % -i trait -c derive` |
| Associated types | `qi % -i type -p Trait` |
| Associated consts | `qi % -i var -p Trait` |
| Await points | `qi % -i call -m await` |
| Macro definitions | `qi % -i macro` |
| Macro invocations | `qi % -i call -t macro` |
| FFI declarations | `qi % -m 'extern%' --def` |
| Imports from a path | `qi % -i imp -ns std::io` |
| Scoped calls | `qi new -i call -ns Vec` |
| Inside a function | `qi token --within authenticate` |
| Same-line patterns | `qi lock unlock --and 5` |
| By type annotation | `qi % -i arg -t 'Result<%'` |
| Tests | `qi % -i func -c '#[test]' --def` |

---

## Additional Resources

- **Handler source**: `rust/rust_language.c`
- **Configuration**: `rust/config/{file_extensions,ignore_files,keywords}.txt`
- **AST debugging**: `tools/ast-explorer-rust file.rs` — view tree-sitter output
- **Known-good examples**: `scratch/sources/rust/*.rs` — one file per language feature
