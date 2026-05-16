# General Guide to qi

A newcomer-friendly introduction to using qi for semantic code search, across any language qi supports.

## Core Idea

qi understands code structure, not just text. It knows the difference between a function definition, a variable, a type, a call site, and more. This lets you ask precise questions instead of guessing with grep.

## Universal Workflow

Always start with structure, then drill down:

```bash
# 1. Get a file's overview (always first!)
qi '*' -f 'file.py' --toc

# 2. See a specific definition
qi "parse_config" -i func -e

# 3. Find where it's used
qi "parse_config" -i call

# 4. Explore what a function does internally
qi 'error' --within 'parse_config' -x noise
```

The `--toc` flag shows all symbols, types, and imports with line ranges. The `-e` flag shows the full definition inline. Together they replace reading files.

## Key Concepts

| Concept | Meaning | Example |
|---------|---------|---------|
| **Context** (`-i`) | What a symbol *is* | `-i func`, `-i class`, `-i var`, `-i type` |
| **Clue** (`-c`) | How a symbol is *used* | `-c go` (goroutine), `-c '@property'`, `-c 'macro!'` |
| **Modifier** (`-m`) | Behavioral annotation | `-m async`, `-m unsafe`, `-m static`, `-m const` |
| **Scope** (`-s`) | Visibility | `-s pub` (Rust), defaults work per language |
| **Parent** (`-p`) | Containing type or receiver | `-p 'User'` finds User's methods/fields |
| **Type** (`-t`) | Type annotation | `-t 'Result<%'` finds Result-returning functions |

## Essential Patterns

These work in any language:

```bash
# Search by context
qi "symbol"                        # all uses (broad)
qi "symbol" -i func                # definitions only
qi "symbol" -i call                # call sites only
qi "symbol" -x noise               # skip comments & strings

# Navigate code
qi '*' -f 'file.go' --toc          # file overview
qi "function" -i func -e           # read implementation inline
qi '%' --within 'function'         # everything inside a function

# See results better
qi "symbol" -C 5                   # show surrounding context
qi "symbol" --columns line,symbol,type  # custom columns
qi '%' --limit-per-file 3          # sample across files, not just one

# Find related code
qi 'open' 'close' --and 5          # patterns within 5 lines
qi 'save' -i call -p 'form'        # calls on a specific object
```

## Language-Specific Quick Reference

| Language | Key contexts | Key clues/modifiers |
|----------|-------------|---------------------|
| **Go** | `func`, `var`, `type`, `call`, `prop` | `-c go`, `-c defer`, `-c send`, `-c receive` |
| **C** | `func`, `var`, `type`, `goto`, `label` | `-m static`, `-i goto`, `-i label` |
| **Python** | `func`, `class`, `var`, `call` | `-c '@property'`, `-m async`, `-c 'yield'` |
| **Rust** | `func`, `class`, `enum`, `trait` | `-s pub`, `-m async`, `-m unsafe`, `-c 'impl %'` |

## Common Pitfalls

```bash
# ❌ Don't read files when you can use qi
cat handler.go | less
# ✅ Use TOC + -e instead
qi '*' -f 'handler.go' --toc
qi "function" -i func -e

# ❌ Don't use grep for semantic searches
grep -r "parse_config" .
# ✅ qi understands code boundaries
qi "parse_config" -i func

# ❌ Forget to exclude noise
qi "error"
# ✅ Much cleaner results
qi "error" -x noise
```

## Next Steps

Each language has a detailed guide with advanced patterns and real-world examples:
- [Go Guide](GO_GUIDE.md) — goroutines, channels, defer
- [C Guide](C_GUIDE.md) — memory management, goto cleanup, preprocessor
- [Python Guide](PYTHON_GUIDE.md) — decorators, async, generators, classes
- [Rust Guide](RUST_GUIDE.md) — traits, unsafe, visibility, macros

## Quick Reference

```bash
qi "symbol"                    # find a symbol
qi "symbol" -i func -e         # see its definition
qi "symbol" -i call            # find all callers
qi '*' -f 'file' --toc         # get a file's structure
qi '%' --within 'fn' -x noise  # explore a function
qi 'a' 'b' --and 5             # find related patterns
qi '%' --limit-per-file 3      # sample across files
qi "symbol" -x noise           # exclude comments/strings
qi "symbol" -C 5               # show context
qi --help                      # full documentation
```
