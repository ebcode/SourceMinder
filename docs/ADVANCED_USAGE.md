# Advanced Usage

This document covers advanced querying techniques and code analysis patterns enabled by the indexed database.
Because the indexer separates **structure extraction** (indexing) from **analysis** (querying), you can run many 
different analyses without re-parsing the code. The database becomes a queryable "code knowledge graph."

## Advanced SQL Queries

### Parent Symbol Tracking

The indexer tracks parent symbols for member expressions and method calls, enabling relationship-based queries:

```bash
# Find all methods called on a specific object
sqlite3 code-index.db "SELECT DISTINCT full_symbol FROM code_index
WHERE parent_symbol='target' AND context_type='call';"

# Find all field accesses on 'entry'
sqlite3 code-index.db "SELECT line_number, full_symbol, filename FROM code_index
WHERE parent_symbol='entry' AND context_type='property';"

# Find methods called on 'this'
sqlite3 code-index.db "SELECT DISTINCT full_symbol FROM code_index
WHERE parent_symbol='this';"
```

**How it works:**
- For `obj.method()` → `method` has parent `obj`
- For `this.target.getBounds()` → `getBounds` has parent `target` (immediate parent)
- Direct calls like `foo()` have no parent (empty string)

### Scope/Access Modifier Filtering

The indexer captures TypeScript access modifiers (public, private, protected) for class methods and properties:

```bash
# Find all public methods
sqlite3 code-index.db "SELECT full_symbol, filename, line_number FROM code_index
WHERE scope='public' AND context_type='function';"

# Find all private properties
sqlite3 code-index.db "SELECT full_symbol, filename, line_number FROM code_index
WHERE scope='private' AND context_type='property';"

# Find public API surface (public methods and properties)
sqlite3 code-index.db "SELECT context_type, full_symbol, filename FROM code_index
WHERE scope='public' ORDER BY filename, line_number;"
```

**Notes:**
- Only TypeScript/JavaScript class members have scope tracking
- Functions declared outside classes have empty scope
- Scope values: `public`, `private`, `protected`, or empty string

### Modifier Filtering

The `modifier` column tracks behavioral modifiers that are semantically distinct from visibility (scope):

**Key distinction:**
- `scope` = WHO can access (public, private, protected)
- `modifier` = HOW it behaves (static, async, readonly, final, abstract)

```bash
# Find all static methods
sqlite3 code-index.db "SELECT full_symbol, filename, line_number FROM code_index
WHERE modifier='static' AND context='function';"

# Find all static properties
sqlite3 code-index.db "SELECT full_symbol, filename, line_number FROM code_index
WHERE modifier='static' AND context='property';"

# Combine scope and modifier filters (e.g., public static methods)
sqlite3 code-index.db "SELECT full_symbol, filename FROM code_index
WHERE scope='public' AND modifier='static' AND context='function';"
```

**Cross-language support:**
- PHP: `static`, `final`, `abstract`, `readonly`
- TypeScript/JavaScript: `static`, `async`, `readonly` (planned)
- C: `static`, `inline`, `const` (planned)

## Symbol Rename Impact Analysis

Before renaming a function, class, or variable, query the database to see everywhere it appears across all contexts:

```bash
./query-index "UserService"
```

This shows:
- Where it's defined (`class`, `function`, `variable`)
- Where it's called (`call`)
- Where it's imported (`import`)
- Where it's exported (`export`)
- Where it appears in comments (`comment`)
- Where it appears in strings (`string`)

This gives you the complete **blast radius** of a rename, helping you understand the full impact before making changes.

**Example workflow:**
```bash
# Before renaming UserService → UserRepository
./query-index "UserService"

# Review all occurrences, then perform the rename
# Re-index the affected files
./index-ts ./src --quiet

# Verify the old name is gone
./query-index "UserService"
```

## Orphaned Symbol Detection

Find functions that are defined but never called, which may indicate dead code:

```bash
# Query the database directly with SQLite
sqlite3 code-index.db "
SELECT DISTINCT f.full_symbol, f.directory || f.filename AS file, f.line_number
FROM code_index f
WHERE f.context_type = 'function'
AND f.symbol NOT IN (
    SELECT DISTINCT symbol
    FROM code_index
    WHERE context_type = 'call'
)
ORDER BY f.full_symbol;
"
```

**Limitations:**
- This works best for finding **uncalled functions**
- Less reliable for checking if functions are imported, due to `import *` wildcards
- Exported functions may be used by external code not in the index

**Use case:** Identify private/internal functions that are defined but never called anywhere in the codebase. These are candidates for removal.

## Naming Convention Auditor

Query symbols by context type and check naming patterns against your project's style guide:

```bash
# Find all class names (should be PascalCase)
./query-index "%" -i class

# Find all function names (should be camelCase)
./query-index "%" -i function

# Find all variable names that might be constants (UPPER_SNAKE_CASE)
./query-index "%" -i variable
```

**SQL query to find violations:**
```bash
# Classes that don't start with uppercase letter
sqlite3 code-index.db "
SELECT full_symbol, directory || filename AS file
FROM code_index
WHERE context_type = 'class'
AND SUBSTR(full_symbol, 1, 1) NOT BETWEEN 'A' AND 'Z';
"

# Functions that start with uppercase (likely should be camelCase)
sqlite3 code-index.db "
SELECT full_symbol, directory || filename AS file
FROM code_index
WHERE context_type = 'function'
AND SUBSTR(full_symbol, 1, 1) BETWEEN 'A' AND 'Z';
"
```

**Note:** Naming conventions vary by project. Adjust these queries to match your team's style guide.

## Code Smell Detection

Query the database to detect potential code quality issues:

### Files with Excessive Exports

Files that export too many symbols may indicate poor module boundaries or "barrel file" antipatterns:

```bash
sqlite3 code-index.db "
SELECT directory || filename AS file, COUNT(*) as export_count
FROM code_index
WHERE context_type = 'export'
GROUP BY directory, filename
HAVING COUNT(*) > 50
ORDER BY export_count DESC;
"
```

### Functions with Too Many Parameters

Functions accepting many parameters may be difficult to use and test:

```bash
sqlite3 code-index.db "
SELECT f.full_symbol,
       f.directory || f.filename AS file,
       f.line_number,
       COUNT(*) as param_count
FROM code_index f
JOIN code_index p ON (
    f.directory = p.directory AND
    f.filename = p.filename AND
    f.line_number = p.line_number AND
    p.context_type = 'argument'
)
WHERE f.context_type = 'function'
GROUP BY f.directory, f.filename, f.line_number, f.full_symbol
HAVING COUNT(*) > 10
ORDER BY param_count DESC;
"
```

### Classes with Too Many Properties

Classes with many properties may violate single responsibility principle:

```bash
sqlite3 code-index.db "
SELECT c.full_symbol,
       c.directory || c.filename AS file,
       c.line_number,
       COUNT(*) as property_count
FROM code_index c
JOIN code_index p ON (
    c.directory = p.directory AND
    c.filename = p.filename AND
    p.line_number BETWEEN c.line_number AND c.line_number + 200 AND
    p.context_type = 'property'
)
WHERE c.context_type = 'class'
GROUP BY c.directory, c.filename, c.line_number, c.full_symbol
HAVING COUNT(*) > 30
ORDER BY property_count DESC;
"
```

**Note:** This query uses a heuristic (properties within 200 lines of class declaration) and may need adjustment based on your code style.

### Files with No Exports

Find files that define functions but don't export anything, which may indicate missing exports or internal-only modules:

```bash
sqlite3 code-index.db "
SELECT DISTINCT directory || filename AS file
FROM code_index
WHERE context_type = 'FUNCTION_NAME'
AND (directory || filename) NOT IN (
    SELECT DISTINCT directory || filename
    FROM code_index
    WHERE context_type = 'export'
)
ORDER BY file;
"
```

---

**Performance Note:** All these queries run in milliseconds on the indexed database, compared to potentially minutes of parsing and analyzing the raw source files.

**Threshold Tuning:** The numeric thresholds (50 exports, 10 parameters, 30 properties) are examples. Adjust them based on your project's complexity and team standards.
