# qi Guide

qi is a fast, SQLite-backed code symbol indexer. It searches identifiers and their metadata, not arbitrary text. Every query hits a pre-built index — no file scanning.

## Powerful features discovered in practice

| Feature | Flag | What we saw |
|---|---|---|
| Symbol precision | `-i func/class/enum/prop/call/imp/...` | 18 context types. `execute_plane -i call` returns exactly 9 call sites. `TokenGraph -i imp` shows exactly 17 imports. |
| Def vs usage | `-d 0/1`, `--def`, `--usage` | `has_tracing -d 0` shows only usages (PROP access, ARG passing, CALL) — definitions hidden. Knowing which is which is the whole point. |
| Within-def search | `-w ParentSymbol` | `evaluate -w PredicateScope` finds exactly 1 result — the method defined inside that impl. Pinpoint navigation. |
| Namespace filter | `-ns path::to::module` | `execute_plane -ns pdfx_core::runtime` shows only the 2 IMP entries from that module — call sites and other imports vanish. Exact match required. |
| Proximity same-line | `--and` | Two+ indexed symbols must appear on the same line. `qi plan fitness --and` found a comment with both. |
| Proximity N-line | `--and N` | Same but within N lines. |
| Parent symbol | `-p parent_name` | Symbols accessed as members. Narrowed down correctly for property access patterns. |
| Parent type | `--parent-type TypeName` | Resolves parent to its definition type. More precise than `-p` alone. |
| LLM-friendly output | `--raw` | Pure source code, no line numbers, no chrome. `PredicateAtom --def -e --raw` gives clean enum body. |
| Expanded defs | `-e` | Shows the full definition body inline. `CompileError --def -i enum -e` shows variants with attributes. |
| Verbose metadata | `-v` | All columns: PAR, SCOPE, NS, MOD, CLUE, TYPE, D. Reveals `execute_plane` lives in namespace `pdfx_core::runtime`. |
| Custom columns | `--columns sym,ctx,d` | Pick your own table. Good for piping into scripts or diffing. |
| File TOC | `--toc -f './path/*'` | Full symbol inventory per file. `qi '*' --toc -f './crates/pdfx-core/src/genome/*'` dumps all 125 functions, 13 structs, 10 enums grouped by file. |
| File-only listing | `--files` | `qi 'main' -f '*.rs' --files` returns just 19 filenames. |
| Exclude noise | `-x noise` | Strips comments and strings. `qi version -x noise` drops all COM/STR matches, keeps only CASE/PROP/IMP/VAR. |
| Debug SQL | `--debug` | Reveals the SQLite schema and exact query. Table: `code_index`. Columns: `directory, filename, line, symbol, context, parent_symbol, scope, namespace, modifier, clue, type, is_definition`. Uses `symbol LIKE ? ESCAPE '\'`. |
| Line range | `--lines 50-100` | Constrain to specific lines in a file. |
| Limit per file | `-lpf N` | `qi process -lpf 2` caps at 2 results per file. |
| Quiet | `-q` | Drops banner/footer chrome, keeps header + rows. |
| Type annotation | `-t TYPE` | Filter by declared type annotation. |
| Modifier filter | `-m static/const/...` | Filter by modifier (works in C; empty for this Rust codebase). |

## Context types

`qi --list-types` shows 22 types with abbreviations. Key ones for Rust:

| Short | Full | What it catches |
|---|---|---|
| `func` | function | `fn foo() { ... }` |
| `class` | class | `struct Foo { ... }` (and usage as type) |
| `enum` | enum | `enum Foo { ... }` |
| `trait` | trait | `trait Foo { ... }` |
| `prop` | property | struct fields |
| `call` | call | function/method calls |
| `imp` | import | `use` statements |
| `var` | variable | `let`, `const` bindings |
| `arg` | argument | function parameters |
| `case` | case | enum variants (as values) |
| `mac` | macro | `macro_rules!` definitions |
| `com` | comment | words in comments |
| `str` | string | words in string literals |
| `file` | filename | file basename |
| `ns` | namespace | module declarations |
| `type` | type | type definitions |
| `lam` | lambda | closures / arrow functions |

## Go-to recipes

```bash
# Full definition, raw — perfect for LLM context
qi SymbolName --def -e --raw

# All call sites
qi func_name -i call

# All imports
qi TypeName -i imp

# Method defined inside a specific impl
qi method_name -w ParentStruct -i func --def

# All definitions in a namespace
qi pattern -ns crate::module --def

# File structure overview
qi '*' --toc -f './crates/name/src/*'

# Prefix wildcard
qi 'eval_*' -i func --def

# Exclude comments and strings
qi pattern -x noise

# Two symbols on same line
qi symbol1 symbol2 --and

# Verbose to inspect metadata
qi symbol -v

# File listing only
qi Symbol --files

# Custom columns for scripting
qi Symbol --columns sym,ctx,d,ns
```

## Critical gotchas verified by experiment

1. **No keywords are indexed.** `pub`, `fn`, `async`, `impl`, `await`, `mod`, `use`, `let`, `self`, `Self`, `&self`, `where`, `for`, `if`, `else`, `match`, `return`, `struct`, `enum`, `trait`, `type` — none of them exist in the index. Searching any of these always returns 0 partial matches or falls through to substring retry.

2. **SCOPE column is always empty for Rust.** `-s public` and `-s private` silently return zero results. `-v` confirms it — the SCOPE column is blank for every single symbol we checked.

3. **MODIFIER column is always empty for Rust.** Same story. `-m static`, `-m const`, `-m inline` — nothing. The column just isn't populated.

4. **No punctuation.** `.`, `::`, `()`, `<>`, `=`, `#`, `!`, `[`, `]`, `{`, `}`, `;`, `,` — invisible. `.clone()` → 0 matches. Must search `clone -i call`. `::` path separator not searchable. `#[derive(...)]` attributes fully invisible.

5. **No regex.** Only glob characters: `*` (zero or more), `?` (exactly one). `%` and `_` also work. `impl.*Component` won't match anything.

6. **Prefix patterns need explicit `*`.** `qi test_` does NOT match `test_analyze_instrumented_function`. It matches the symbol `tests` (exact match) and then retries as `*test_*`. Use `'test_*'` explicitly.

7. **Constants (`const`) are not flagged as definitions.** `STYLE_BOLD`, `SCHEMA_VERSION`, `STR_CLASS_LIB_VER` — all appear as `VAR` context with `D=0`. `--def` won't find them. You have to search for them without `--def` and inspect context manually.

8. **`-ns` requires exact match.** `execute_plane -ns pdfx_core` excludes everything. `execute_plane -ns pdfx_core::runtime` works. No partial matching on namespace.

9. **`--and` requires indexed symbols.** `qi pub fn --and` fails with "individual symbols only, breaks on word boundaries." Both patterns must exist in the index.

10. **`--and` defaults to same-line.** `--and` without a number = same line. `--and 5` = within 5 lines.

11. **`--parent-type` resolves to definition.** Only works when qi can trace the parent. Failed for standalone functions.

12. **Order matters.** `qi --def -e --raw Symbol` consumes flags as patterns. Pattern first: `qi Symbol --def -e --raw`. Same for `-v`.

13. **No Lisp files.** `.lisp` files are completely absent from the index. `qi '*' -f './lisp/*' --files` returns 0. Indexer doesn't support Lisp.

14. **Pure numbers are not indexed.** `qi 42` → "is a pure number and is not indexed."

15. **CLUE column is always empty.** Tested across multiple symbol types. Column exists but never populated.

16. **Arguments (ARG) for imported/foreign symbols aren't distinguished.** `STYLE_BOLD` appears as ARG at call sites but qi can't tell you which function it was passed to.

17. **No call graph.** Can't ask "who calls execute_plane?" — you get all call sites but no reverse mapping.

18. **No trait hierarchy.** Can't find "all types implementing Component" or "all methods of Component."

19. **Quoted strings invisible.** `"schema_version"` → 0 matches. Qi indexes the content inside strings but strips the quotes.

20. **Single-char symbols not indexed.** `#`, `&`, etc — "Symbols less than 2 characters are not indexed."

## How the index works (from --debug)

Backend is a SQLite database with a single table:

```
code_index(
  directory TEXT,
  filename TEXT,
  line INTEGER,
  symbol TEXT,
  context TEXT,
  parent_symbol TEXT,
  scope TEXT,
  namespace TEXT,
  modifier TEXT,
  clue TEXT,
  type TEXT,
  is_definition INTEGER
)
```

Queries use `symbol LIKE ? ESCAPE '\'` with the glob characters translated to SQL LIKE. qi first tries exact match, then retries with `*...*` for substring. The `--debug` flag prints every SQL query it executes.

## When to fall back to rg

- Rust keywords (`pub fn`, `impl Trait for`, `async fn`)
- Attributes (`#[derive`, `#[serde`, `#[cfg`)
- Regex patterns
- Punctuation-containing patterns (`.clone()`, `::`)
- Multi-line context
- Lisp files
- Arbitrary text inside comments or string literals
