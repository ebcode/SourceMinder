# SQL Recipes

Because the indexer separates structure extraction (indexing) from analysis
(querying), you can run many analyses without re-parsing the code. The
database becomes a queryable "code knowledge graph."

These recipes use direct SQL against `code-index.db`. For the qi CLI
equivalents of metadata filtering, see [ADVANCED_USAGE.md](ADVANCED_USAGE.md).

## Schema Notes

The current schema uses compact column names. For context when reading older
recipes or the source code:

| Old name | Current name |
|----------|-------------|
| `context_type` | `context` (compact values: FUNC, ARG, VAR, CALL, CLASS, IMP, EXP, COM, etc.) |
| `line_number` | `line` |
| `full_symbol` | `symbol` (qualified by `directory \|\| filename`) |
| `lowercase_symbol` | dropped; use `symbol COLLATE NOCASE` (indexed) |

`filename` alone is ambiguous — always group or join on `directory, filename` together.

---

## Symbol Rename Impact Analysis

Before renaming a function, class, or variable, query the database to see
everywhere it appears across all contexts:

```bash
qi UserService
```

This shows:
- Where it's defined (class, function, variable)
- Where it's called (call)
- Where it's imported (import)
- Where it's exported (export)
- Where it appears in comments (comment)
- Where it appears in strings (string)

This gives you the complete **blast radius** of a rename.

**Example workflow:**
```bash
# Before renaming UserService → UserRepository
qi UserService

# Review all occurrences, then perform the rename
# Re-index the affected files
index-ts ./src --quiet

# Verify the old name is gone
qi UserService
```

---

## Definition Density per File

Files with the most definitions — a rough maintainability signal:

```bash
sqlite3 code-index.db "
SELECT directory, filename, COUNT(*) AS definition_count
FROM code_index
WHERE is_definition = 1
GROUP BY directory, filename
ORDER BY definition_count DESC
LIMIT 10;
"
```

---

## Files with the Most Function Definitions

Potential "god files" with too many responsibilities:

```bash
sqlite3 code-index.db "
SELECT directory, filename, COUNT(*) AS function_count
FROM code_index
WHERE context = 'FUNC' AND is_definition = 1
GROUP BY directory, filename
ORDER BY function_count DESC
LIMIT 10;
"
```

---

## Hotspot Files

Files that both define many functions AND have many calls — central hubs:

```bash
sqlite3 code-index.db "
SELECT
    directory, filename,
    SUM(CASE WHEN context = 'FUNC' AND is_definition = 1 THEN 1 ELSE 0 END) AS definitions,
    SUM(CASE WHEN context = 'CALL' THEN 1 ELSE 0 END) AS calls
FROM code_index
GROUP BY directory, filename
HAVING definitions > 5 AND calls > 20
ORDER BY (definitions + calls) DESC
LIMIT 10;
"
```

---

## Largest Classes (Method Count)

Classes sorted by method count. A high count may indicate poor cohesion:

```bash
sqlite3 code-index.db "
SELECT parent_symbol, COUNT(*) AS method_count
FROM code_index
WHERE context = 'FUNC' AND is_definition = 1
  AND parent_symbol IS NOT NULL AND parent_symbol != ''
GROUP BY parent_symbol
ORDER BY method_count DESC
LIMIT 10;
"
```

---

## Functions with the Most Arguments

Signatures with many parameters are harder to use and test. ARG rows carry
`parent_symbol` = the function they belong to. Caveat: prototypes and
definitions each contribute ARG rows, so header+impl pairs can double-count:

```bash
sqlite3 code-index.db "
SELECT directory, filename, parent_symbol AS function_name, COUNT(*) AS arg_count
FROM code_index
WHERE context = 'ARG' AND COALESCE(parent_symbol, '') <> ''
GROUP BY directory, filename, parent_symbol
ORDER BY arg_count DESC
LIMIT 15;
"
```

---

## Same-Type Parameter Runs (Argument-Swap Hazard)

A signature with 4+ parameters of the same type compiles fine with the
arguments in any order. ARG rows carry per-parameter types; parameters of
one signature share the definition line:

```bash
sqlite3 code-index.db "
SELECT a.directory, a.filename, a.line, a.type, COUNT(*) AS same_type_args,
       (SELECT f.symbol FROM code_index f
        WHERE f.context = 'FUNC' AND f.is_definition = 1
          AND f.directory = a.directory AND f.filename = a.filename
          AND f.line <= a.line
        ORDER BY f.line DESC LIMIT 1) AS function_name
FROM code_index a
WHERE a.context = 'ARG' AND a.is_definition = 1 AND COALESCE(a.type, '') <> ''
GROUP BY a.directory, a.filename, a.line, a.type
HAVING same_type_args >= 4
ORDER BY same_type_args DESC;
"
```

---

## Orphaned Symbol Detection

Functions defined but never called anywhere in the codebase. Caveats:
address-taken functions (dispatch tables, designated initializers), exported
entry points, and `main()` are false positives — they are reached without a
CALL row on their name:

```bash
sqlite3 code-index.db "
SELECT DISTINCT f.directory, f.filename, f.symbol
FROM code_index f
WHERE f.context = 'FUNC' AND f.is_definition = 1
  AND f.symbol <> 'main'
  AND NOT EXISTS (
      SELECT 1 FROM code_index c
      WHERE c.context = 'CALL'
        AND c.symbol = f.symbol COLLATE NOCASE
  )
ORDER BY f.directory, f.filename, f.symbol;
"
```

---

## Most-Used Symbols (Fan-in)

Which symbols are referenced most often (excluding comments and strings):

```bash
sqlite3 code-index.db "
SELECT symbol, COUNT(*) AS usage_count
FROM code_index
WHERE is_definition = 0
  AND context NOT IN ('COM', 'STR')
GROUP BY symbol COLLATE NOCASE
ORDER BY usage_count DESC
LIMIT 15;
"
```

---

## Symbols in the Most Files (High Coupling)

Symbols that appear across many files — high coupling candidates:

```bash
sqlite3 code-index.db "
SELECT symbol, COUNT(DISTINCT directory || filename) AS file_count
FROM code_index
WHERE context IN ('CALL', 'VAR', 'CLASS')
GROUP BY symbol COLLATE NOCASE
HAVING file_count > 5
ORDER BY file_count DESC
LIMIT 20;
"
```

---

## Mutable Module State Inventory (Hidden Coupling)

File-scope mutable state: static, non-const variable definitions, ranked by
how often the same file references them:

```bash
sqlite3 code-index.db "
SELECT v.directory, v.filename, v.symbol, v.type,
       (SELECT COUNT(*) FROM code_index u
        WHERE u.symbol = v.symbol COLLATE NOCASE AND u.is_definition = 0
          AND u.directory = v.directory AND u.filename = v.filename) AS local_refs
FROM code_index v
WHERE v.context = 'VAR' AND v.is_definition = 1
  AND v.modifier LIKE '%static%' AND v.modifier NOT LIKE '%const%'
ORDER BY local_refs DESC
LIMIT 15;
"
```

---

## Most Imported Modules (Core Dependencies)

Which modules the codebase depends on most heavily:

```bash
sqlite3 code-index.db "
SELECT symbol, COUNT(*) AS import_count
FROM code_index
WHERE context = 'IMP'
GROUP BY symbol COLLATE NOCASE
ORDER BY import_count DESC
LIMIT 15;
"
```

---

## Files with the Most Exports (API Surface Area)

Files that export too many symbols may indicate poor module boundaries or
"barrel file" antipatterns:

```bash
sqlite3 code-index.db "
SELECT directory, filename, COUNT(*) AS export_count
FROM code_index
WHERE context = 'EXP'
GROUP BY directory, filename
ORDER BY export_count DESC
LIMIT 10;
"
```

---

## Variables that Shadow Function Names

Variables that share a name with a function in the same file — potential
confusion:

```bash
sqlite3 code-index.db "
SELECT DISTINCT v.directory, v.filename, v.symbol, v.line
FROM code_index v
WHERE v.context = 'VAR'
  AND EXISTS (
      SELECT 1 FROM code_index f
      WHERE f.context = 'FUNC' AND f.is_definition = 1
        AND f.symbol = v.symbol COLLATE NOCASE
        AND f.directory = v.directory AND f.filename = v.filename
  )
ORDER BY v.directory, v.filename, v.line;
"
```

---

## Most Reused Variable Names

Common variable names across the codebase. Very common names may indicate
overly generic abstractions:

```bash
sqlite3 code-index.db "
SELECT symbol, COUNT(DISTINCT directory || filename) AS file_count
FROM code_index
WHERE context = 'VAR'
GROUP BY symbol COLLATE NOCASE
ORDER BY file_count DESC
LIMIT 15;
"
```

---

## Overly Generic Names

Names that are too vague to convey meaning — potential clarity issues:

```bash
sqlite3 code-index.db "
SELECT symbol, COUNT(*) AS usage_count
FROM code_index
WHERE symbol COLLATE NOCASE IN ('data', 'info', 'temp', 'tmp', 'result', 'value', 'obj', 'item')
  AND context IN ('VAR', 'ARG')
GROUP BY symbol COLLATE NOCASE
ORDER BY usage_count DESC;
"
```

---

## Naming Convention Auditor

Check naming patterns against your project's style guide:

**qi approach:**
```bash
qi % -i class          # All class names (should be PascalCase)
qi % -i func           # All function names (should be camelCase)
qi % -i var            # All variable names
```

**SQL queries for violations:**

```bash
# Classes that don't start with uppercase letter
sqlite3 code-index.db "
SELECT symbol, directory || '/' || filename AS file
FROM code_index
WHERE context = 'CLASS'
AND SUBSTR(symbol, 1, 1) NOT BETWEEN 'A' AND 'Z';
"

# Functions that start with uppercase (likely should be camelCase)
sqlite3 code-index.db "
SELECT symbol, directory || '/' || filename AS file
FROM code_index
WHERE context = 'FUNC'
AND SUBSTR(symbol, 1, 1) BETWEEN 'A' AND 'Z';
"
```

Naming conventions vary by project. Adjust these queries to match your
team's style guide.

---

## Comment-Word Density (Documentation Quality)

Measures the share of indexed tokens that came from comments (not a true
comment/code ratio, but a rough proxy):

```bash
sqlite3 code-index.db "
SELECT
    directory, filename,
    SUM(CASE WHEN context = 'COM' THEN 1 ELSE 0 END) AS comment_words,
    COUNT(*) AS total_entries,
    ROUND(CAST(SUM(CASE WHEN context = 'COM' THEN 1 ELSE 0 END) AS FLOAT) / COUNT(*), 3) AS comment_ratio
FROM code_index
GROUP BY directory, filename
HAVING total_entries > 100
ORDER BY comment_ratio DESC
LIMIT 10;
"
```

---

## Stale Comment References (Documentation Drift)

COM rows are words from comments. Snake_case words in comments are almost
always identifier references. An anti-join against definitions finds comments
naming identifiers that no longer exist — usually code that was renamed or
deleted after the comment was written:

```bash
sqlite3 code-index.db "
SELECT c.directory, c.filename, c.line, c.symbol
FROM code_index c
WHERE c.context = 'COM'
  AND c.symbol LIKE '%\_%' ESCAPE '\'
  AND NOT EXISTS (
      SELECT 1 FROM code_index d
      WHERE d.is_definition = 1
        AND d.symbol = c.symbol COLLATE NOCASE)
ORDER BY c.directory, c.filename, c.line
LIMIT 25;
"
```

---

**Performance Note:** All these queries run in milliseconds on the indexed
database, compared to potentially minutes of parsing and analyzing the raw
source files.

**Threshold Tuning:** The numeric thresholds are examples. Adjust them based
on your project's complexity and team standards.
