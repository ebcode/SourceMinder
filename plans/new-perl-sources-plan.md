# New Perl Source Files — Indexing Plan

## Files Added

- `tools/sources/perl/closures.pl`
- `tools/sources/perl/file-io.pl`
- `tools/sources/perl/context.pl`
- `tools/sources/perl/map-grep-sort.pl`

---

## 1. closures.pl

### Features covered
- Shared mutable state across sibling closures (factory returning multiple coderefs)
- Generator-style closure (counter with step)
- Memoization via closure over a hash
- Capturing loop variable by value
- Named inner sub closing over lexical scope
- Closure-as-object (hash of coderefs)
- `\&SubName` — coderef to a named subroutine

### Indexing analysis

| Feature | Current status | Action needed |
|---|---|---|
| Anonymous sub assigned to `$var` | `$var` → VAR, anon sub → LAMBDA | OK — no change |
| Named sub inside another sub | Visited recursively via `process_children` | OK — no change |
| Hash of coderefs (dispatch table) | Keys: `autoquoted_bareword`; values: anon sub → LAMBDA | OK — no change |
| `\&SubName` refgen expression | Not handled | **New: handle `refgen_expression`** |

### Implementation

**New handler: `handle_refgen_expression`**

The `\&Foo` coderef syntax produces a `refgen_expression` node. The sub name inside is a `bareword` child (via the `&` sigil). Index it as `CONTEXT_CALL` (it is a usage, not a definition).

Add to `visit_node`:
```c
if (node_sym == perl_symbols.refgen_expression) {
    handle_refgen_expression(node, source_code, ...);
    return;
}
```

The handler should walk children for a `bareword` (or `function`) child and emit a CALL entry for it, then recurse.

---

## 2. file-io.pl

### Features covered
- Lexical filehandle: `open(my $fh, '<', 'file')`, `close($fh)`
- All open modes: `<`, `>`, `>>`, `+<`, `<:raw`, `<:encoding(...)`
- `while (<$fh>)` readline loop
- Bareword filehandles: `STDOUT`, `STDERR`, `STDIN`, `LOGFILE`
- `print STDOUT`, `print STDERR` indirect-object form
- Slurp idiom: `local $/`
- `read`, `seek`, `chomp` built-ins
- File test operators: `-e`, `-f`, `-d`, `-s`
- In-memory (string) filehandle: `open(my $fh, '>', \$buf)`

### Indexing analysis

| Feature | Current status | Action needed |
|---|---|---|
| `open(my $fh, ...)` | `$fh` → VAR via `variable_declaration`, `open` → CALL | OK |
| `close`, `seek`, `read`, `chomp` | CALL via `function_call_expression` | OK |
| `local $/` | VAR with `local` modifier via `variable_declaration` | OK |
| `<$fh>` readline | Not handled (`readline_expression` node) | **New: handle `readline_expression`** |
| Bareword filehandles (`STDOUT`, `LOGFILE`) | Not handled | **New: handle bare `bareword` nodes** |
| `-e`, `-f`, `-d`, `-s` file tests | Not handled (`file_test_expression` node) | **New: handle `file_test_expression`** |

### Implementation

**1. `handle_readline_expression`**

`<$fh>` produces a `readline_expression`. Walk its children for a `scalar` node and index the variable inside as VAR (usage). `<>` (bare diamond) has no variable child — skip.

**2. Bareword filehandles**

Bareword filehandles (`STDOUT`, `LOGFILE`) appear as `bareword` nodes in argument position. They are currently unhandled — `visit_node` falls through to `process_children` which silently skips them.

Options:
- Add a general `bareword` handler that indexes any bareword as VAR (risk: over-indexing keywords)
- Only index barewords that look like filehandles (all-uppercase) — fragile
- Index any `bareword` child of a `function_call_expression` or `print_expression` as CALL/VAR

**Recommended:** Add a targeted `bareword` case inside `handle_function_call` and similar handlers that emit a CALL entry when a bareword is the first argument to `open`, `close`, `print`, `say`, `warn`. This is scoped and avoids indexing unrelated barewords.

**3. `handle_file_test_expression`**

`-e $path` / `-f 'file.txt'` produces a `file_test_expression`. The operator itself (`-e`, `-f`, etc.) could be indexed as CALL. The operand (scalar or string) is already handled by the normal variable/string path if recursed into.

Add to `visit_node` — call `process_children` (no special handler needed if we just want the operand indexed). If we want the operator itself (e.g. `-e`) as a CALL, we need to extract the operator token and emit it.

---

## 3. context.pl

### Features covered
- `wantarray` — detects list / scalar / void call context
- Array in scalar context (`my $count = @arr`)
- Hash in scalar context (`my $info = %h`)
- `scalar` keyword as explicit coercion
- `localtime` context-sensitivity
- String vs numeric context (`$val + 0` vs `$val . ''`)
- Boolean context (`if (@arr)`, `if (%h)`)
- `//` (defined-or) and `//=`
- `x` operator in string vs list context
- `grep`/`map` imposing list context
- Context-propagating wrapper sub

### Indexing analysis

| Feature | Current status | Action needed |
|---|---|---|
| `wantarray` | Likely a `bareword` or zero-arg `function` — falls through to `process_children` | **Verify node type; index as CALL** |
| `scalar @arr` coercion | `scalar` keyword may parse as a unary op node rather than a sigil | **Verify no collision with `perl_symbols.scalar` sigil** |
| `//` defined-or, `//=` | Operators — not indexed | OK (operators not indexed by design) |
| `grep`/`map` in context | `function_call_expression` or `func1op_call_expression` | OK — already handled |
| `$a || 'default'` | `$a` → VAR via scalar handler | OK |

### Implementation

**`wantarray`**

Check the tree-sitter node type for `wantarray()`. It is likely a `function_call_expression` with function name `wantarray` — already handled by `handle_function_call`. If it parses as a bare `bareword`, it will currently fall through unindexed. Add to the `visit_node` bareword path or verify via debug output.

**`scalar` coercion**

`scalar @arr` — the `scalar` keyword here is a unary operator, not the `$` sigil. Verify that tree-sitter-perl does not produce a `perl_symbols.scalar` node for this (which would cause `index_sigil_node` to try to extract a variable name from it). If it does, add a guard in `index_sigil_node` or `visit_node` to distinguish the sigil node from the unary-op node (they should have different node types, but worth confirming with debug output).

---

## 4. map-grep-sort.pl

### Features covered
- `map { expr } @list` — block and expression forms
- `grep { expr } @list` — filtering
- `sort { $a <=> $b } @list` — numeric and string comparators
- Multi-key sort (`<=>` then `cmp` fallback)
- Schwartzian transform (chained `map`/`sort`/`map`)
- Sort by struct field (`$a->{field}`)
- `List::Util`: `reduce`, `sum`, `min`, `max`, `first`, `any`, `all`, `none`
- `$a` and `$b` magic sort variables

### Indexing analysis

| Feature | Current status | Action needed |
|---|---|---|
| `map { } @list` | `func1op_call_expression` or `function_call_expression` | Verify — see below |
| `grep { } @list` | Same | Verify |
| `sort { } @list` | Same | Verify |
| `$a`, `$b` in sort/reduce blocks | `scalar` node → VAR | OK |
| `$a->{field}` in sort | `hash_element_expression` → handled | OK |
| `use List::Util qw(...)` | `use_statement` + `quoted_word_list` → IMPORT | OK |
| `reduce { $a * $b }` | Same as map/grep — verify | Verify |

### Implementation

**map / grep / sort dispatch**

These are Perl builtins that take a block as their first argument. In tree-sitter-perl they likely parse as `func1op_call_expression` (the same node type used for `shift`/`chomp`/etc.), not `function_call_expression`. Check `visit_node` — `func1op_call_expression` is **not currently dispatched** (it falls through to `process_children`). The function name (`map`, `grep`, `sort`) would not be indexed as a CALL.

**Recommended fix:** Add a case in `visit_node` for `func1op_call_expression` that emits a CALL entry for the function name, then recurses via `process_children`. This also benefits `chomp`, `chop`, `defined`, `ref`, `scalar`, `wantarray`, etc.

The block argument `{ ... }` is a regular `block` node and will be recursed into, so variables and calls inside the block will be indexed correctly once the parent is visited.

---

## Testing Sequence

After implementation, index and verify with these `qi` queries:

```bash
# closures.pl
qi make_stack make_counter memoize make_account -f closures.pl -v
qi Animal -f closures.pl -i call   # should find \&Animal::speak as CALL

# file-io.pl
qi STDOUT STDERR LOGFILE -f file-io.pl   # bareword filehandles
qi fh -f file-io.pl -i var              # lexical filehandle vars
qi open close read seek -f file-io.pl -i call

# context.pl
qi wantarray -f context.pl -i call
qi context_aware fetch_config -f context.pl -i func

# map-grep-sort.pl
qi map grep sort reduce -f map-grep-sort.pl -i call
qi sum min max first any all none -f map-grep-sort.pl -i imp,call
qi doubled odds by_len -f map-grep-sort.pl -i var
```
