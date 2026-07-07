# qi Hints & Import Resolution Plan

## Problem

When an agent queries a symbol that was defined under a private/internal name
(e.g. `_AnsibleUnicode`) but used under a public alias (`AnsibleUnicode`), qi
shows only the definition row. The 4 IMP/CALL usage sites in `test_objects.py`
exist in the DB under the public name but are invisible — the agent has to guess
to drop the underscore.

This is not Python-specific. The same class of disconnect exists across
languages: `$variable` in PHP/Perl, `@decorator` in Python, `pub fn` vs bare
call in Rust, receiver-method disambiguation in Go.

There is currently no mechanism in qi that says _"here is where this symbol is
used"_ except `--usage` (which filters `is_definition=0` on the same symbol
name — useless when the usage is stored under a different name).

---

## Option B: Query-Time Hints

**What:** qi detects when a query returns few results and a variant of the
pattern (stripping a common leading character) would match more. It appends a
one-line hint to stderr.

**Where:** `query-index.c`, inside the results-display path, after the result
loop but before the `Found N matches` line.

**When:** The result count is ≤ 5 AND ≥ 1 (hint is noise if 0 or hundreds).

**How:**

1. After the main query loop, before the summary line.
2. For each common stripping operation, run a second query:
   - Strip leading `_` from the pattern → `LTRIM(pattern, '_')`
   - Strip leading `$` from the pattern → `LTRIM(pattern, '$')`
3. If the stripped variant matches ≥ 1 row (quick `SELECT COUNT(*)`), emit:

   ```
   Tip: also try qi PublicName  (private → public alias)
   ```

   The message uses the **stripped pattern**, not the original.

**Scope of changes:**

| File | Change |
|------|--------|
| `query-index.c` | Add `emit_query_hints()` function, called after the main result loop when `result_count > 0 && result_count <= 5`. Runs `LTRIM` variant queries and emits hints to stderr. |

**Design decisions:**

- **Hints go to stderr** so they don't pollute `--raw` piped output.
- **Only fires on low result counts** (≤ 5). On broad queries (`qi %`), the
  hint would be noise.
- **One hint per query, not per stripping operation.** If both `_` and `$`
  stripping produce matches, show only the one with more results.
- **Language-agnostic.** `_` and `$` are conventions across Python, PHP, Perl,
  and shell. Adding new conventions later is a one-line edit.
- **Uses existing SQLite functions** (`LTRIM` available since SQLite 3.41.0;
  the container and host both run ≥ 3.45).

**Effort:** Small. ~30 lines of C, one file touched. No re-index needed.

**Limitation:** Heuristic. `_Object` → `object` would fire the hint even though
`object` is a built-in, not an import alias. Mitigated by the result-count gate
(if `_Object` is defined in a codebase, it would match). Further mitigation
possible if needed (e.g., only emit when LTRIM variant matches IMP context),
but YAGNI for now.

---

## Option C: Index-Time Import Resolution

**What:** The indexer records the **resolved target** of every IMP entry, so qi
can reverse-lookup "who imports this symbol" with a generic JOIN.

**Where:** Indexer-side (one new column per language) + query-side (one new
flag or `--def` augmentation).

### C1. Schema Change

Add a `full_target` column to `code_index`:

```sql
ALTER TABLE code_index ADD COLUMN full_target TEXT;
CREATE INDEX idx_full_target ON code_index(full_target COLLATE NOCASE);
```

`full_target` stores the fully-qualified name that an IMP/CALL entry resolves
to. It is NULL for non-import/call rows and for rows where resolution fails.

Example values:

| Symbol (IMP) | File | full_target |
|---|---|---|
| `AnsibleUnicode` | test_objects.py:42 | `_AnsibleUnicode` |
| `AnsibleTagHelper` | test_objects.py:8 | `_datatag.AnsibleTagHelper` |
| `fmt` | some_file.go:12 | `fmt` (stdlib, self-resolving) |
| `$config` | some_file.php:15 | `$config` (same name) |

### C2. Indexer Changes

**Python** (`python_language.c`):

The indexer already walks import statements. When processing `handle_import_from`
or `handle_import`, the symbols being imported are known, as is the module path.
The indexer currently records IMP entries with the local alias name. Add the
resolved full name:

```
from ansible.parsing.yaml.objects import AnsibleUnicode
```
→ IMP entry: `symbol=AnsibleUnicode`, `full_target=_AnsibleUnicode`

Resolution: when the import source module (`ansible.parsing.yaml.objects`) is
resolvable to a file already indexed, look up the CLASS/FUNC/VAR definition
with the matching symbol name in that file. Record its `full_symbol` as
`full_target`.

If the import source module is external or unresolvable, `full_target` stays
NULL (no reverse-lookup possible).

**Go, TypeScript, PHP, Perl, Rust, C:**

Same pattern: at the IMP recording site, resolve the imported symbol to its
definition's `full_symbol` and store it. Each language's import handler differs,
but the pattern is uniform: walk the import, look up the target in the current
file's scope or the imported module, store `full_target`.

### C3. Query-Side Changes

Two possible UI surfaces:

**a) `--def` augmentation (automatic, no new flag):**

When `--def` displays a CLASS/FUNC, also run:

```sql
SELECT u.full_symbol, u.context, u.filename, u.line
FROM code_index u
WHERE u.full_target = '<current definition full_symbol>'
  AND u.context IN ('IMP', 'CALL', 'VAR')
  AND u.is_definition = 0
  AND u.filename != '<current filename>'
ORDER BY u.filename, u.line;
```

Display as a compact indented block under the definition row:

```
19 | _AnsibleUnicode | CLASS
    Used in:
    test_objects.py:42 (IMP as AnsibleUnicode)
    test_objects.py:45 (CALL as AnsibleUnicode)
    test_objects.py:52 (IMP as AnsibleUnicode)
    test_objects.py:55 (CALL as AnsibleUnicode)
```

**b) New `-r` / `--refs` flag (explicit):**

`qi NAME -r` shows only the reverse-lookup rows (no definition). Useful when the
agent wants usage sites without the definition body.

Both options use the same SQL; they differ only in when the query fires.

### C4. Migration

1. **Schema migration:** `db_init` adds the `full_target` column and index if
   not present. For existing DBs, `full_target` is NULL for all rows (backward
   compatible).
2. **Re-index:** Existing DBs need re-indexing to populate `full_target`. This
   is a one-time cost per instance DB.
3. **Seed DBs:** New seed DBs built after this change will have `full_target`
   populated automatically.

### C5. Scope of Changes

| File | Change |
|------|--------|
| `shared/database.c` / `database.h` | Schema: `full_target` column + index |
| `python/python_language.c` | Resolve import targets, populate `full_target` |
| `go/go_language.c` | Same |
| `typescript/ts_language.c` | Same |
| `php/php_language.c` | Same |
| `perl/perl_language.c` | Same |
| `rust/rust_language.c` | Same |
| `c/c_language.c` | Same |
| `query-index.c` | `--def` augmentation and/or `-r` flag |
| `shared/parse_result.h` / `parse_result.c` | Add `full_target` to `ExtColumns` or `add_entry` signature |

**Effort:** Large. 10+ files touched, 8 indexers modified, schema migration,
re-index of all existing DBs.

**Benefit:** Clean, language-agnostic, exact (no heuristics). Works for any
symbol that the indexer can resolve. The `full_target` column is useful beyond
imports — it could also store call-target resolution in the future.

---

## Recommendation

**Ship B now, do C later.**

- B is ~30 lines of C, one file, no re-index, ships today. It handles the
  common case (leading `_` / `$`) and is a direct prompt-to-agent signal.
- C is the correct long-term solution but requires touching every indexer,
  schema migration, and re-indexing all DBs. The `full_target` column enables
  more than import resolution — it's a general-purpose "what does this reference
  resolve to" column that could eventually support call-graph navigation.

B and C are not mutually exclusive. B's hint covers the heuristic case; C's
exact resolution covers everything else. When C lands, the hint in B becomes
redundant for symbols that have `full_target` populated, but still useful as a
fallback for external/unresolvable imports.
