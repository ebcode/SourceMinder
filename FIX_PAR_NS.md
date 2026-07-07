# Fix: method/member definitions missing parent (and namespace) columns

**Date found:** 2026-06-09 (wasm2 branch, surfaced by code-quality-queries.txt
Q3/Q4; fix targets **main**)

## Symptom

Class-membership queries are structurally unanswerable in TypeScript and
Python code:

```
qi getOperation execute -f Command.ts --def -v
LINE | SYM          | PAR | SCOPE  | NS | ... | D | CTX
173  | getOperation |     | public |    |     | 1 | FUNC
181  | execute      |     | public |    |     | 1 | FUNC
```

Method definitions carry `scope` but neither `parent_symbol` nor `namespace`
— the enclosing class is not captured anywhere. TS class *field* declarations
(PROP, D=1) have the same gap. Consequence: "methods per class" /
"public members per class" aggregations (quality queries Q3/Q4) return empty
on TS/Python corpora.

## Current state per language (definition rows → enclosing type)

| Language | Methods get parent? | Mechanism | NS column |
|----------|--------------------|-----------|-----------|
| PHP | YES | `extract_parent_any()` ancestor walk at add time (`php_language.c:909` `handle_method_declaration`) | PHP namespace via `get_namespace()` |
| Go | YES | receiver type extracted directly from `method_declaration` (`go_language.c` `handle_method_declaration`: `.parent = receiver_type`) | package via `get_package()` |
| Rust | YES | `g_current_impl` file-scope tracker (`rust/rust_language.c:40`), set on impl descent | — |
| C | N/A (no methods) | — | — |
| **TypeScript** | **NO** | `handle_class_declaration` (`ts_language.c:1438`) and `handle_interface_declaration` (`:1470`) just `process_children()` the body; member handlers receive no context | TS `namespace {}` blocks not propagated to members |
| **Python** | **NO** | `handle_class_definition` (`python_language.c:355`) `process_children()`s the body; `handle_function_definition` emits no `.parent` | N/A (modules = files) |
| **Perl** | **NO** (lesser) | subs indexed flat (`perl_language.c:584`); `package` statements indexed as NAMESPACE defs (`:562`) but never attached to subs | empty |

Two established in-house patterns for conveying the enclosing context:
1. **Ancestor walk at add time** (PHP `extract_parent_any`) — stateless,
   nesting-safe, no save/restore bugs.
2. **File-scope tracker with save/restore on descent** (Rust
   `g_current_impl`, C `g_initializer_parent`).

The ancestor walk is preferred for the new code: tree-sitter gives
`ts_node_parent()`, members are rare relative to total nodes, and it cannot
leak state across siblings.

## Column convention (matches Go/PHP/Rust precedent)

- `parent_symbol` = the **enclosing type as written in source** (class /
  interface / impl target / receiver type). This is the syntactic-parent
  convention; no type resolution.
- `namespace` = the language's package/namespace concept (PHP namespace, Go
  package, TS `namespace {}` block, Perl `package`).
- Caveat to preserve: Python CLASS rows already use `parent_symbol` for the
  **superclass** (inheritance). That stays; method rows are distinct rows, no
  conflict.

## Proposed fix

### 1. TypeScript (`typescript/ts_language.c`)

New static helper, PHP-style ancestor walk:

```c
/* Innermost enclosing class/interface name, or "" if file-scope.
 * Walks ts_node_parent() until class_declaration,
 * abstract_class_declaration, or interface_declaration; extracts its
 * "name" field. */
static void extract_enclosing_type(TSNode node, const char *source_code,
                                   char *out, size_t out_size,
                                   const char *filename);
```

Apply in:
- `handle_method_definition` (~line 1614): `.parent = enclosing` on the FUNC
  add_entry.
- `handle_property_signature` (covers both `property_signature` and
  `public_field_definition` dispatch, ~line 2170): `.parent = enclosing` on
  the PROP add_entry — fixes interface members and class fields in one place.

Optional, same pass (the NS half): a second walk target for
`internal_module` / namespace blocks → `.namespace` on the same add_entry
calls. Verify the grammar node name with `tools/ast-explorer-ts` first.

### 2. Python (`python/python_language.c`)

Same-shape helper walking to `class_definition` (extract `name` field).
Apply in `handle_function_definition` (add_entry at ~line 42 of the function):
`.parent = enclosing_class`.

Decision needed (see Open Questions): whether a def nested inside another def
should get the enclosing *function* as parent instead. Minimal scope: nearest
`class_definition` only; nested defs keep empty parent.

### 3. Perl (`perl/perl_language.c`) — the NS half

`package` is (usually) a statement, not a block, so an ancestor walk cannot
find it. Use the tracker pattern instead: file-scope `g_current_package`,
set in `handle_package_statement` (line 562), cleared at file start
(`parse` entry point); `handle_subroutine_declaration` (line 584) adds
`.namespace = g_current_package`. Note: `package NAME { ... }` block syntax
nests — if the grammar exposes it as a block, save/restore around the block
descent, mirroring Rust's `g_current_impl` discipline.

## Files / functions to edit

| File | Function | Change |
|------|----------|--------|
| `typescript/ts_language.c` | new `extract_enclosing_type()` | ancestor walk helper |
| `typescript/ts_language.c` | `handle_method_definition` (~1614) | `.parent` on FUNC def |
| `typescript/ts_language.c` | `handle_property_signature` | `.parent` on PROP def |
| `python/python_language.c` | new `extract_enclosing_class()` | ancestor walk helper |
| `python/python_language.c` | `handle_function_definition` | `.parent` on FUNC def |
| `perl/perl_language.c` | `handle_package_statement` (562) | set `g_current_package` |
| `perl/perl_language.c` | `handle_subroutine_declaration` (584) | `.namespace` on FUNC def |
| `tests/typescript/*/expected.qi.output` | — | regenerate: goldens print all columns (PAR/NS), so basic-class / generics / private-members outputs change |
| `tmp/test-method-call.ts`, `tmp/test_method_call.py`, `tmp/test-method-call.pl` | — | extend per-language conformance fixtures with a class-with-methods case asserting (symbol, FUNC, parent, D=1) |

No schema change; no qi/query-side change (`-p`, `--parent-type`, `-ns`
filters work as soon as the data exists). No web-side change (data-level fix;
parity diffs both sides against the same db).

## Verification

1. `make` — zero warnings.
2. Re-index a TS corpus (`tools/sources/typescript/` works):
   `qi getOperation --def -v` → PAR = `Command`.
3. `qi % -i func -p Command --def` → all Command methods.
4. Interface check: members of an indexed TS interface carry PAR = interface
   name.
5. Python fixture: method in class → PAR = class name; top-level def → PAR
   empty; CLASS row still shows superclass in PAR.
6. Perl fixture: `package Foo; sub bar {}` → bar has NS = Foo.
7. `make test && tests/run-tests` — regenerate TS goldens, confirm
   non-TS goldens unchanged.
8. Quality queries Q3/Q4 (code-quality-queries.txt) now return rows on a TS
   corpus.
9. `--parent-type` smoke: `qi % --parent-type '<some class>'` — method rows
   resolved via their class's definition should now participate.

## Open questions

1. **Nested function defs (Python/TS):** should a def inside a def get the
   enclosing function as parent? Syntactic-parent purity says yes; this plan
   scopes to class-like ancestors only and leaves nested defs empty. Decide
   before implementing the walk's stop condition.
2. **TS enum members:** CASE rows' parent (enum name) not audited here —
   verify while in the file and fix in the same pass if absent.
3. **TS `namespace {}` member NS** is marked optional above — include if the
   grammar walk is cheap, else split out.
4. **Golden fragility:** goldens are configure-flag-dependent (scope/ns
   columns are OOP-only). Regenerate them on an --enable-all build, same as
   today's convention.
5. **C ARG rows have types but no parent** (found 2026-06-10 via
   quality-queries-2.txt #14): `c_language.c` records per-parameter types but
   not the owning function, while Python/TS set `.parent = parent_function`
   on ARG rows. Same fix shape as the rest of this plan (the C function
   definition handler knows its name when it processes the parameter list);
   decide whether to fold it into this pass.
