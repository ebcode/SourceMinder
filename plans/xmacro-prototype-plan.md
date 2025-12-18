# Focused X-Macro Prototype: Extensible Filterable Columns Only

## Core Philosophy
**X-Macros ONLY for columns that follow the consistent pattern:**
- User-facing filterable columns with `--flag value1 value2 ...` syntax
- Examples: parent_symbol, modifier (and future: scope, namespace, context_clue)

**Traditional C code for everything else:**
- Infrastructure columns: lowercase_symbol, directory, filename (indexing internals)
- Core display: line_number, full_symbol (always shown, no filter)
- Special case: context_type (uses -i/-x mutually exclusive flags, not --context)

---

## Step-by-Step Implementation Plan

### Step 1: Identify Column Categories

**A. Infrastructure (stays traditional C - not in X-Macro):**
```c
char lowercase_symbol[SYMBOL_MAX_LENGTH];  // For LIKE queries
char directory[DIRECTORY_MAX_LENGTH];      // File location
char filename[FILENAME_MAX_LENGTH];        // File location
```

**B. Core Display (stays traditional C - not in X-Macro):**
```c
int line_number;              // Always displayed, no filter flag
char full_symbol[...];        // Always displayed, no filter flag
ContextType context_type;     // Special: uses -i/-x flags, not --context
```

**C. Extensible Filterable (X-MACRO TERRITORY):**
```c
char parent_symbol[...];      // --parent/-p filter
char modifier[...];           // --modifier/-m filter
// Future additions just add to X-Macro:
// char scope[...];           // --scope/-s filter
// char namespace[...];       // --namespace/-ns filter
// char context_clue[...];    // --clue/-c filter
```

---

### Step 2: Create column_schema.def (X-Macro Definition)

**File:** `proto/shared/column_schema.def`

```c
/* X-Macro Schema for Extensible Filterable Columns
 *
 * ⭐ SINGLE SOURCE OF TRUTH for user-facing filterable columns
 *
 * To add a new filterable column (e.g., 'scope'):
 * 1. Add ONE line here: COLUMN(scope, TEXT, "SCOPE", "SCP", "scope", "s")
 * 2. Add field to IndexEntry struct in database.h
 * 3. Implement extraction in language indexers
 * 
 * Everything else auto-generates: CLI parsing, SQL filters, display
 *
 * Format: COLUMN(db_name, sql_type, display_full, display_compact, cli_long, cli_short)
 */

COLUMN(parent_symbol, TEXT, "PARENT",   "PAR", "parent",   "p")
COLUMN(modifier,      TEXT, "MODIFIER", "MOD", "modifier", "m")
```

---

### Step 3: Modify proto/shared/database.h

**Keep IndexEntry struct traditional** (no X-Macro here - struct is still hand-written):
```c
typedef struct {
    // Infrastructure (traditional)
    char lowercase_symbol[SYMBOL_MAX_LENGTH];
    char directory[DIRECTORY_MAX_LENGTH];
    char filename[FILENAME_MAX_LENGTH];
    
    // Core display (traditional)
    int line_number;
    ContextType context_type;
    char full_symbol[SYMBOL_MAX_LENGTH];
    
    // Extensible filterable (also traditional struct fields)
    char parent_symbol[SYMBOL_MAX_LENGTH];
    char modifier[SYMBOL_MAX_LENGTH];
} IndexEntry;
```

**Why not X-Macro the struct?** The struct definition is simple and stable. The pain point is maintaining 11 places of *logic*, not the struct itself.

---

### Step 4: Modify proto/shared/database.c

**Schema creation uses X-Macro for filterable columns:**

```c
const char *schema =
    "CREATE TABLE IF NOT EXISTS code_index ("
    /* Infrastructure columns (traditional) */
    "  lowercase_symbol TEXT NOT NULL,"
    "  directory TEXT NOT NULL,"
    "  filename TEXT NOT NULL,"
    /* Core display columns (traditional) */
    "  line_number INTEGER NOT NULL,"
    "  context_type TEXT NOT NULL,"
    "  full_symbol TEXT NOT NULL,"
    /* Extensible filterable columns (X-Macro generated) */
#define COLUMN(name, type, ...) "  " #name " " #type ","
#include "column_schema.def"
#undef COLUMN
    "  _dummy TEXT"  /* Handle trailing comma */
    ");"
    /* Indexes */
    "CREATE INDEX IF NOT EXISTS idx_lowercase_symbol ON code_index(lowercase_symbol);"
#define COLUMN(name, ...) "CREATE INDEX IF NOT EXISTS idx_" #name " ON code_index(" #name ");"
#include "column_schema.def"
#undef COLUMN
    ;
```

**db_insert() parameter binding uses X-Macro:**

```c
int db_insert(CodeIndexDatabase *db, const IndexEntry *entry) {
    const char *sql =
        "INSERT INTO code_index ("
        "lowercase_symbol, directory, filename, line_number, context_type, full_symbol, "
#define COLUMN(name, ...) #name ", "
#include "column_schema.def"
#undef COLUMN
        "_dummy) VALUES (?, ?, ?, ?, ?, ?, "
#define COLUMN(...) "?, "
#include "column_schema.def"
#undef COLUMN
        "NULL)";
    
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    
    // Bind infrastructure + core (traditional)
    sqlite3_bind_text(stmt, 1, entry->lowercase_symbol, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, entry->directory, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, entry->filename, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, entry->line_number);
    sqlite3_bind_text(stmt, 5, context_type_to_string(entry->context_type, 0), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, entry->full_symbol, -1, SQLITE_STATIC);
    
    // Bind extensible filterable columns (X-Macro)
    int param_idx = 7;
#define COLUMN(name, ...) \
    sqlite3_bind_text(stmt, param_idx++, entry->name, -1, SQLITE_STATIC);
#include "column_schema.def"
#undef COLUMN
    
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}
```

---

### Step 5: Create proto/query-proto.c

**Filter structure uses X-Macro to declare filter fields:**

```c
typedef struct {
    char *values[MAX_FILTER_VALUES];
    int count;
} StringList;

typedef struct {
    // Core filters (traditional)
    ContextTypeList *include_context;  // -i flag
    ContextTypeList *exclude_context;  // -x flag
    
    // Extensible filterable columns (X-Macro generated)
#define COLUMN(name, ...) StringList name;
#include "shared/column_schema.def"
#undef COLUMN
} QueryFilters;

// Result: QueryFilters has .parent_symbol and .modifier fields auto-generated
```

**CLI flag parsing uses X-Macro:**

```c
// Traditional flags first
if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--include-context") == 0) {
    // Parse context include
}
if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--exclude-context") == 0) {
    // Parse context exclude
}

// Extensible filterable columns (X-Macro)
#define COLUMN(name, type, full, cmp, long_flag, short_flag) \
    if (strcmp(argv[i], "--" #long_flag) == 0 || \
        (strcmp(#short_flag, "") != 0 && strcmp(argv[i], "-" #short_flag) == 0)) { \
        while (i + 1 < argc && argv[i + 1][0] != '-') { \
            if (filters.name.count < MAX_FILTER_VALUES) { \
                filters.name.values[filters.name.count++] = argv[++i]; \
            } \
        } \
    }
#include "shared/column_schema.def"
#undef COLUMN

// Result: Auto-generates parsing for --parent/-p and --modifier/-m
```

**SQL filter generation uses X-Macro:**

```c
int build_query_filters(char *sql, size_t size, int offset, QueryFilters *filters) {
    // Add context include/exclude (traditional)
    if (filters->include_context) { /* ... */ }
    if (filters->exclude_context) { /* ... */ }
    
    // Add extensible filterable columns (X-Macro)
#define COLUMN(name, ...) \
    if (filters->name.count > 0) { \
        offset += snprintf(sql + offset, size - offset, " AND " #name " IN ("); \
        for (int i = 0; i < filters->name.count; i++) { \
            offset += snprintf(sql + offset, size - offset, "%s'%s'", \
                i > 0 ? ", " : "", filters->name.values[i]); \
        } \
        offset += snprintf(sql + offset, size - offset, ")"); \
    }
#include "shared/column_schema.def"
#undef COLUMN
    
    return offset;
}

// Result: Auto-generates WHERE clauses for parent_symbol and modifier
```

**Display column registry uses X-Macro:**

```c
// Getter functions (X-Macro generated)
#define COLUMN(name, ...) \
    static void* get_##name##_col(RowData *d) { return (void*)d->name; }
#include "shared/column_schema.def"
#undef COLUMN

// Column registry
static ColumnSpec columns[] = {
    // Core columns (traditional)
    {"line",    "LINE",   "LINE",   get_line_col},
    {"symbol",  "SYMBOL", "SYMBOL", get_symbol_col},
    {"context", "CTX",    "CTX",    get_context_col},
    
    // Extensible filterable (X-Macro)
#define COLUMN(name, type, full, compact, ...) \
    {#name, full, compact, get_##name##_col},
#include "shared/column_schema.def"
#undef COLUMN
    
    {NULL}
};
```

---

### Step 6: Create README-PROTO.md Tutorial

**Example walkthrough:** "Adding a 'scope' column"

```markdown
## How to Add a New Filterable Column (Example: 'scope')

### 1. Update X-Macro Schema (1 line change)
File: `proto/shared/column_schema.def`
```c
COLUMN(parent_symbol, TEXT, "PARENT",   "PAR", "parent",   "p")
COLUMN(modifier,      TEXT, "MODIFIER", "MOD", "modifier", "m")
COLUMN(scope,         TEXT, "SCOPE",    "SCP", "scope",    "s")  // ← ADD THIS
```

### 2. Add to IndexEntry struct (1 line change)
File: `proto/shared/database.h`
```c
typedef struct {
    // ... existing fields ...
    char modifier[SYMBOL_MAX_LENGTH];
    char scope[SYMBOL_MAX_LENGTH];  // ← ADD THIS
} IndexEntry;
```

### 3. Extract in language indexers (language-specific)
File: `typescript/index-ts.c`, `c/index-c.c`, `php/index-php.c`
```c
// Add extraction logic for scope based on AST
strncpy(entry.scope, extract_scope(node), SYMBOL_MAX_LENGTH);
```

### 4. Done! Automatic features:
✓ CLI flags: `--scope public` or `-s public private`
✓ Multi-value: `--scope public protected private`
✓ SQL filtering: `WHERE scope IN ('public', 'private')`
✓ Display column with full/compact modes
✓ Help text auto-updated

**Total changes: 3 files, ~5 lines of code**
```

---

## File Structure Summary

```
./proto/
├── shared/
│   ├── column_schema.def       ← ⭐ X-MACRO: Filterable columns only
│   ├── database.h              ← Traditional struct + X-Macro for schema
│   ├── database.c              ← Traditional + X-Macro for insert binding
│   ├── constants.h
│   ├── string_utils.h/c
│   └── file_opener.h/c
├── query-proto.c               ← X-Macro for filters, CLI, SQL, display
└── README-PROTO.md             ← Tutorial: adding 'scope' column example
```

---

## Success Criteria

1. **Compiles and runs:** `./query-proto "test"`
2. **Multi-value filters work:** `./query-proto "test" --parent obj this self`
3. **Short flags work:** `./query-proto "test" -p obj -m static`
4. **Long flags work:** `./query-proto "test" --parent obj --modifier static`
5. **Developer follows README:** Can add hypothetical 'scope' column by understanding the pattern
6. **Code is cleaner:** Adding new filterable column = 3 file changes (down from 11)

---

## What This Achieves

✓ **Pragmatic:** X-Macros only where they add value (extensible columns)
✓ **Clear separation:** Infrastructure vs core vs extensible
✓ **Easy to extend:** Adding scope/namespace/return_type is now ~5 lines
✓ **Maintainable:** Single source of truth for filterable column metadata
✓ **Educational:** Prototype shows pattern, not full rewrite
