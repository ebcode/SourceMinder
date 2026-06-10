# Why This Schema

*Draft — 2026-06-10*

SourceMinder's entire index is one flat table:

```sql
CREATE TABLE code_index (
  symbol TEXT NOT NULL,
  directory TEXT NOT NULL,
  filename TEXT NOT NULL,
  line INTEGER NOT NULL,
  context TEXT NOT NULL,
  full_symbol TEXT NOT NULL,
  source_location TEXT,
  parent_symbol TEXT,
  scope TEXT,
  namespace TEXT,
  modifier TEXT,
  clue TEXT,
  type TEXT,
  is_definition INTEGER
);
```

One row per symbol **occurrence** — not per symbol. Every row is a lexical
fact: *this word appeared here, in this role, with these attributes as
written in the source.* That sentence is the whole design. Everything below
is a consequence of it.

## The columns, and what each one buys

### `context` — the role, not just the match

grep tells you a string appeared. `context` tells you whether it appeared as
a function definition, a call, a variable, a parameter, a property access, a
type, an import — or just a word inside a comment (`COM`) or string literal
(`STR`). That last distinction sounds small, but it is the difference between
a usable fan-in metric and one drowned in comment vocabulary:

```sql
-- top referenced symbols, comments and strings excluded
SELECT symbol, COUNT(*) FROM code_index
WHERE is_definition = 0 AND context NOT IN ('COM', 'STR')
GROUP BY symbol ORDER BY 2 DESC;
```

(`qi <pattern> -x noise` is the same move at the CLI.)

### `is_definition` — defs and usages in one table

The same table answers "where is this defined" and "where is it used"
(`--def` / `--usage`), and their combination is where it gets interesting —
anti-joins like *defined but never called*:

```sql
SELECT DISTINCT f.directory, f.filename, f.symbol
FROM code_index f
WHERE f.context = 'FUNC' AND f.is_definition = 1
  AND NOT EXISTS (SELECT 1 FROM code_index c
                  WHERE c.context = 'CALL'
                    AND c.symbol = f.symbol COLLATE NOCASE);
```

### `parent_symbol` — structure, one hop at a time

The **syntactic parent as written in source**: `config->parser_init()` is a
CALL with parent `config` — the variable, never the resolved type
(`IndexerConfig`). Chains use the immediate parent (`a.b.c` → `c`'s parent is
`b`). Initializer fields get the variable being initialized.

Keeping the parent syntactic keeps it honest — and it loses nothing, because
the type is still *deducible* by joining through the parent's own definition
row. That join is exactly what `qi --parent-type` does:

```sql
-- members of any variable declared as IndexerConfig (same-file resolution)
... AND EXISTS (SELECT 1 FROM code_index def
                WHERE def.symbol = code_index.parent_symbol
                  AND def.directory = code_index.directory
                  AND def.filename = code_index.filename
                  AND def.is_definition = 1
                  AND def.context IN ('VAR', 'ARG', 'PROP')
                  AND def.type LIKE 'IndexerConfig')
```

One column stores a fact; query-time joins synthesize the relationships.

### `type` — annotations down to the parameter

Types are captured per occurrence, including individual function parameters.
That enables checks no string-matching tool can express, like the
argument-swap hazard (a signature with N parameters of one type compiles
fine with the arguments in any order):

```sql
SELECT parent_symbol, type, COUNT(*) AS n FROM code_index
WHERE context = 'ARG' AND is_definition = 1 AND COALESCE(type,'') <> ''
GROUP BY directory, filename, parent_symbol, type HAVING n >= 4;
```

First run of this query found a 10-`int` signature in qi's own source.

### `modifier`, `scope`, `namespace` — the declaration attributes

`static`, `const`, `inline`, `async`, `public`/`private`, package/namespace —
as written. `modifier` alone turns "where is our hidden state?" into a
one-liner:

```sql
-- file-scope mutable state (static, non-const variable definitions)
SELECT directory, filename, symbol, type FROM code_index
WHERE context = 'VAR' AND is_definition = 1
  AND modifier LIKE '%static%' AND modifier NOT LIKE '%const%';
```

`scope` and `namespace` are populated by the OOP language modules and exist
as columns only on builds configured with those languages — the schema is
generated from the same X-macro definition (`shared/column_schema.def`) that
drives qi's flags, columns, help text, and SQL, so schema and tooling cannot
drift apart.

### Comment and string words as first-class rows

Indexing comment *words* (stopword-filtered) sounds like noise until you
realize it makes documentation queryable against the code it describes:

```sql
-- comments naming identifiers that are defined nowhere (stale docs)
SELECT c.filename, c.line, c.symbol FROM code_index c
WHERE c.context = 'COM' AND c.symbol LIKE '%\_%' ESCAPE '\'
  AND NOT EXISTS (SELECT 1 FROM code_index d
                  WHERE d.is_definition = 1
                    AND d.symbol = c.symbol COLLATE NOCASE);
```

First run found a comment referencing `visit_nodes` where the function is
`visit_node`. No AST tool catches that; no grep distinguishes it from a
legitimate reference.

### `full_symbol`, `source_location`, `directory`/`filename`/`line`

`source_location` stores the span of a full definition, which is how
`qi -e` expands a complete function without re-parsing. Paths are stored
relative and `./`-canonical, so output is directly usable by editors and
other tools, and one writer normalizes rather than N readers tolerating.

## What the schema deliberately does not store

- **No resolved types.** Resolution is a query-time join (`--parent-type`),
  scoped to the same file, documented as such. The index never guesses.
- **No call graph.** A span-containment "callers" feature was considered and
  shelved: lexical containment only equals call semantics in C, and would
  mislead in proportion to language dynamism. qi reports lexical facts;
  features that masquerade as semantic analysis are out.
- **No cross-file symbol resolution.** Same-named variables in different
  files are different rows. Honest, cheap, and correct more often than a
  heuristic linker would be.

The boundary rule: **store what the source says; synthesize relationships
with SQL at query time.** When a query can't be expressed, that pressure
points at a missing *fact* (a column or a row kind), not a missing analysis
engine — which is how `--parent-type`, designated-initializer parents, and
the per-language parent audit all happened.

## Engineering choices

- **One flat table, no joins on the hot path.** Single-pattern lookups hit
  covering indexes (`symbol`, `parent_symbol`, `type`, ... all indexed
  `COLLATE NOCASE`); structural queries pay for joins only when they ask for
  them, via correlated `EXISTS`.
- **NOCASE collation instead of a `lowercase_symbol` column.** Earlier
  schema versions stored a lowercased copy of every symbol to make
  case-insensitive grouping cheap; collated indexes provide the same plans
  without doubling symbol storage.
- **SQLite, WAL mode.** Multiple language indexers write concurrently
  without locks; the same file is queryable by qi, by raw `sqlite3`, by the
  WASM build in a browser (`VACUUM INTO` produces the self-contained
  snapshot), and by anything else that speaks SQLite. The database *is* the
  API.

## Worked evidence

`code-quality-queries.txt` and `quality-queries-2.txt` are the running
proof: fan-in, god files, dead-code candidates, shadowing, comment drift,
argument-swap hazards, mutable-state inventories — each one a few lines of
SQL over this table, several of which found real issues in SourceMinder's
own source the first time they ran. Queries that come back structurally
empty have repeatedly turned out to be indexer gaps, not query bugs — the
schema doubles as its own conformance oracle.

## See also

- `docs/QI_VS_GREP.md` — when the index beats text search (and when not)
- `docs/ADVANCED_USAGE.md` — the query-time joins exposed as qi flags
- `shared/column_schema.def` — the X-macro single source of truth
- `code-quality-queries.txt`, `quality-queries-2.txt` — the query cookbook
