# Go Language Indexer Implementation Plan

## Project Overview

This is a multi-language code indexer written in C11 that uses SQLite and tree-sitter for semantic code search. Currently supports TypeScript, C, and PHP. 
The goal is to add Go language support.

**Database:** Creates `code-index.db` in current working directory
**Query tool:** `./qi` works with all languages

## What We've Accomplished

Downloaded tree-sitter-go and added files to `./go/src/`
Created `./tools/ast-explorer-go` tool (compiled and working)
Created test files:
  - `./tools/sources/go/minimal.go` - Simple "Hello World" program
  - `./tools/sources/go/test.go` - Complex example (structs, methods, etc.)

## Goal

Create a Go language indexer (`./go/index-go.c` + `./go/go_language.c`) that can:
1. Index Go source files
2. Extract symbols (packages, imports, functions, types, variables, etc.)
3. Store them in the shared SQLite database
4. Work with the existing `./query-index` tool

**Initial scope:** Handle the symbols in `minimal.go` first, then expand.

## Architecture Summary

The indexer uses a clean separation of concerns:

```
┌─────────────────────────────────────────┐
│  Main Binary (index-go.c)               │
│  - CLI parsing                          │
│  - File discovery                       │
│  - Database orchestration               │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│  Language Parser (go_language.c)        │
│  - AST traversal (tree-sitter)          │
│  - Symbol extraction                    │
│  - Context type determination           │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│  Shared Components (reused as-is)       │
│  - Database (database.c)                │
│  - Filter (filter.c)                    │
│  - File Walker (file_walker.c)          │
│  - Validation (validation.c)            │
└─────────────────────────────────────────┘
```

### Key Concepts

1. **ParseResult**: Array of extracted symbols (up to 100k per file)
2. **IndexEntry**: Single symbol with metadata (line, context, type, parent, etc.)
3. **ContextType**: Enum defining symbol types (FUNCTION_NAME, VARIABLE_NAME, etc.)
4. **Node Handlers**: Functions that extract symbols from specific AST node types
5. **Dispatch Table**: Maps node type strings to handler functions
6. **Filter**: Excludes noise (stopwords, keywords, short symbols, numbers)

## Critical Files to Reference

### Essential Reading (Language-Specific Logic):

1. **`c/c_language.h`** (60 lines)
   - Parser interface definition
   - Copy this structure exactly for `go_language.h`

2. **`c/c_language.c`** (1200 lines)
   - Read these functions in order:
     - `parser_init()` - Initialize parser with filter
     - `add_entry()` - Add symbol to results
     - `process_children()` - Generic child visitor
     - `visit_node()` - Main dispatcher
     - `handle_function_definition()` - Example handler (shows pattern)
     - `handle_declaration()` - Variable extraction pattern
     - `handle_comment()` - Text extraction pattern
     - `handle_string_literal()` - String processing pattern
   - The dispatch table near the top shows all node types handled

3. **`c/index-c.c`** (300 lines)
   - Main binary structure
   - Copy this for `go/index-go.c` with minimal changes:
     - Change `#include "c_language.h"` → `"go_language.h"`
     - Change `data_dir = "c/data"` → `"go/data"`
     - Change `CParser` → `GoParser`
     - Update usage examples (`.go` instead of `.c`)

### Important Shared Components (Read for Understanding):

4. **`shared/database.h`** (150 lines)
   - `IndexEntry` struct - What data we store per symbol
   - `ExtColumns` struct - Extensible columns (type, parent, scope, etc.)
   - `ContextType` enum - All context types available
   - Database functions: `db_init()`, `db_insert()`, `db_delete_by_file()`

5. **`shared/filter.h`** (100 lines)
   - `SymbolFilter` struct
   - `filter_should_index()` - Call this before adding symbols
   - `filter_clean_string_symbol()` - For comments/strings

6. **`shared/constants.h`** (50 lines)
   - `MAX_SYMBOL_LENGTH` (255)
   - `MAX_ENTRIES_PER_FILE` (100000)
   - `MAX_PATH_LENGTH` (512)

### Configuration Reference:

7. **`c/data/keywords.txt`** - Pattern for Go keywords
8. **`c/data/file-extensions.txt`** - Pattern for Go extensions
9. **`typescript/data/ignore_files.txt`** - Example ignore patterns

### Build Reference:

10. **`Makefile`** (lines 1-95)
    - See TypeScript/C/PHP patterns (lines 9-56)
    - Will add similar section for Go

## AST Structure from minimal.go

When we ran `./scratch/tools/ast-explorer-go scratch/sources/go/minimal.go`, we got:

```
source_file [1:0 - 8:0]
  package_clause [1:0 - 1:12]
    package [1:0 - 1:7] "package"
    package_identifier [1:8 - 1:12] "main"

  import_declaration [3:0 - 3:12]
    import [3:0 - 3:6] "import"
    import_spec [3:7 - 3:12]
      interpreted_string_literal [3:7 - 3:12]

  function_declaration [5:0 - 7:1]
    func [5:0 - 5:4] "func"
    identifier [5:5 - 5:9] "main"
    parameter_list [5:9 - 5:11]
      ( [5:9 - 5:10] "("
      ) [5:10 - 5:11] ")"
    block [5:12 - 7:1]
      expression_statement [6:1 - 6:29]
        call_expression [6:1 - 6:29]
          selector_expression [6:1 - 6:12]
            identifier [6:1 - 6:4] "fmt"
            field_identifier [6:5 - 6:12] "Println"
          argument_list [6:12 - 6:29]
            interpreted_string_literal [6:13 - 6:28]
```

### Node Types We Need to Handle (Minimal Set):

| Node Type | What to Extract | Context Type | Priority |
|-----------|----------------|--------------|----------|
| `package_clause` → `package_identifier` | Package name ("main") | CONTEXT_NAMESPACE | P0 |
| `import_declaration` → `import_spec` | Import path ("fmt") | CONTEXT_IMPORT | P0 |
| `function_declaration` → `identifier` | Function name ("main") | CONTEXT_FUNCTION_NAME | P0 |
| `parameter_list` | Parameter names | CONTEXT_FUNCTION_ARGUMENT | P0 |
| `call_expression` | Function being called | CONTEXT_CALL_EXPRESSION | P0 |
| `selector_expression` | Method call (Println, parent=fmt) | CONTEXT_PROPERTY_NAME | P0 |
| `identifier` | Variable references | CONTEXT_VARIABLE_NAME | P1 |
| `comment` | Words in comments | CONTEXT_COMMENT | P1 |
| `interpreted_string_literal` | Words in strings | CONTEXT_STRING | P1 |

**P0 = Required for minimal.go**
**P1 = Good to have**

## Step-by-Step Implementation Plan

### Phase 1: Configuration Files

Create these files in `go/data/`:

#### `go/data/file-extensions.txt`
```
.go
```

#### `go/data/keywords.txt`
```
break
case
chan
const
continue
default
defer
else
fallthrough
for
func
go
goto
if
import
interface
map
package
range
return
select
struct
switch
type
var
```

#### `go/data/ignore_files.txt`
```
vendor
node_modules
```

### Phase 2: Go Language Header

Create `go/go_language.h`:

```c
#ifndef GO_LANGUAGE_H
#define GO_LANGUAGE_H

#include "../shared/filter.h"
#include "../shared/database.h"

typedef struct {
    SymbolFilter *filter;
} GoParser;

int parser_init(GoParser *parser, SymbolFilter *filter);
int parser_parse_file(GoParser *parser, const char *filepath,
                      const char *project_root, ParseResult *result);
void parser_free(GoParser *parser);

#endif // GO_LANGUAGE_H
```

### Phase 3: Go Language Parser

Create `go/go_language.c` following this structure:

#### Includes and External Function
```c
#include "go_language.h"
#include "../shared/string_utils.h"
#include "../shared/comment_utils.h"
#include "../shared/file_opener.h"
#include "../shared/constants.h"
#include <tree_sitter/api.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

extern const TSLanguage *tree_sitter_go(void);
```

#### Forward Declarations
```c
static void visit_node(TSNode node, const char *source_code,
                      const char *directory, const char *filename,
                      ParseResult *result, SymbolFilter *filter,
                      int *entry_count, int line_number);

static void process_children(TSNode node, const char *source_code,
                            const char *directory, const *filename,
                            ParseResult *result, SymbolFilter *filter,
                            int *entry_count);
```

#### Helper: Add Entry
```c
static void add_entry(const char *symbol, int line, ContextType context,
                     const char *directory, const char *filename,
                     ParseResult *result, SymbolFilter *filter,
                     int *entry_count, ExtColumns *ext) {
    // Check if we should index this symbol
    if (!filter_should_index(filter, symbol, context)) {
        return;
    }

    // Don't exceed max entries
    if (*entry_count >= MAX_ENTRIES_PER_FILE) {
        return;
    }

    // Convert to lowercase for indexing
    char lower_symbol[MAX_SYMBOL_LENGTH];
    to_lowercase(symbol, lower_symbol, sizeof(lower_symbol));

    // Create entry
    IndexEntry *entry = &result->entries[*entry_count];
    strncpy(entry->symbol, lower_symbol, MAX_SYMBOL_LENGTH - 1);
    entry->symbol[MAX_SYMBOL_LENGTH - 1] = '\0';
    entry->line_number = line;
    entry->context = context;
    strncpy(entry->directory, directory, MAX_PATH_LENGTH - 1);
    entry->directory[MAX_PATH_LENGTH - 1] = '\0';
    strncpy(entry->filename, filename, MAX_FILENAME_LENGTH - 1);
    entry->filename[MAX_FILENAME_LENGTH - 1] = '\0';

    // Copy extensible columns
    if (ext) {
        entry->ext = *ext;
    } else {
        memset(&entry->ext, 0, sizeof(ExtColumns));
    }

    (*entry_count)++;
}
```

#### Helper: Lowercase Conversion
```c
static void to_lowercase(const char *src, char *dst, size_t dst_size) {
    size_t i;
    for (i = 0; i < dst_size - 1 && src[i] != '\0'; i++) {
        dst[i] = tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}
```

#### Node Handlers (Start with These)

```c
// Handler: package_clause
static void handle_package_clause(TSNode node, const char *source_code,
                                  const char *directory, const char *filename,
                                  ParseResult *result, SymbolFilter *filter,
                                  int *entry_count, int line_number) {
    // Find package_identifier child
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "package_identifier") == 0) {
            char pkg_name[MAX_SYMBOL_LENGTH];
            if (safe_extract_node_text(source_code, child, pkg_name, sizeof(pkg_name))) {
                add_entry(pkg_name, line_number, CONTEXT_NAMESPACE,
                         directory, filename, result, filter, entry_count, NULL);
            }
            break;
        }
    }
}

// Handler: import_declaration
static void handle_import_declaration(TSNode node, const char *source_code,
                                      const char *directory, const char *filename,
                                      ParseResult *result, SymbolFilter *filter,
                                      int *entry_count, int line_number) {
    // Visit all import_spec children
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "import_spec") == 0) {
            // Find interpreted_string_literal
            uint32_t spec_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < spec_count; j++) {
                TSNode spec_child = ts_node_child(child, j);
                const char *spec_type = ts_node_type(spec_child);

                if (strcmp(spec_type, "interpreted_string_literal") == 0) {
                    char import_path[MAX_SYMBOL_LENGTH];
                    if (safe_extract_node_text(source_code, spec_child, import_path, sizeof(import_path))) {
                        // Remove quotes
                        if (import_path[0] == '"' && strlen(import_path) > 2) {
                            import_path[strlen(import_path) - 1] = '\0';
                            add_entry(import_path + 1, line_number, CONTEXT_IMPORT,
                                    directory, filename, result, filter, entry_count, NULL);
                        }
                    }
                }
            }
        }
    }
}

// Handler: function_declaration
static void handle_function_declaration(TSNode node, const char *source_code,
                                        const char *directory, const char *filename,
                                        ParseResult *result, SymbolFilter *filter,
                                        int *entry_count, int line_number) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (!ts_node_is_null(name_node)) {
        char func_name[MAX_SYMBOL_LENGTH];
        if (safe_extract_node_text(source_code, name_node, func_name, sizeof(func_name))) {
            add_entry(func_name, line_number, CONTEXT_FUNCTION_NAME,
                     directory, filename, result, filter, entry_count, NULL);
        }
    }

    // Process parameters
    TSNode params_node = ts_node_child_by_field_name(node, "parameters", 10);
    if (!ts_node_is_null(params_node)) {
        process_children(params_node, source_code, directory, filename,
                        result, filter, entry_count);
    }

    // Process body
    TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body_node)) {
        process_children(body_node, source_code, directory, filename,
                        result, filter, entry_count);
    }
}

// Handler: call_expression
static void handle_call_expression(TSNode node, const char *source_code,
                                   const char *directory, const char *filename,
                                   ParseResult *result, SymbolFilter *filter,
                                   int *entry_count, int line_number) {
    TSNode function_node = ts_node_child_by_field_name(node, "function", 8);
    if (ts_node_is_null(function_node)) return;

    const char *func_type = ts_node_type(function_node);

    if (strcmp(func_type, "selector_expression") == 0) {
        // Handle method call: obj.method()
        TSNode field_node = ts_node_child_by_field_name(function_node, "field", 5);
        TSNode operand_node = ts_node_child_by_field_name(function_node, "operand", 7);

        if (!ts_node_is_null(field_node) && !ts_node_is_null(operand_node)) {
            char method_name[MAX_SYMBOL_LENGTH];
            char parent_name[MAX_SYMBOL_LENGTH];

            if (safe_extract_node_text(source_code, field_node, method_name, sizeof(method_name)) &&
                safe_extract_node_text(source_code, operand_node, parent_name, sizeof(parent_name))) {

                ExtColumns ext = {0};
                strncpy(ext.parent_symbol, parent_name, MAX_SYMBOL_LENGTH - 1);

                add_entry(method_name, line_number, CONTEXT_CALL_EXPRESSION,
                         directory, filename, result, filter, entry_count, &ext);
            }
        }
    } else if (strcmp(func_type, "identifier") == 0) {
        // Handle simple function call: foo()
        char func_name[MAX_SYMBOL_LENGTH];
        if (safe_extract_node_text(source_code, function_node, func_name, sizeof(func_name))) {
            add_entry(func_name, line_number, CONTEXT_CALL_EXPRESSION,
                     directory, filename, result, filter, entry_count, NULL);
        }
    }

    // Process arguments
    TSNode args_node = ts_node_child_by_field_name(node, "arguments", 9);
    if (!ts_node_is_null(args_node)) {
        process_children(args_node, source_code, directory, filename,
                        result, filter, entry_count);
    }
}

// Handler: comment
static void handle_comment(TSNode node, const char *source_code,
                          const char *directory, const char *filename,
                          ParseResult *result, SymbolFilter *filter,
                          int *entry_count, int line_number) {
    char comment_text[4096];
    if (!safe_extract_node_text(source_code, node, comment_text, sizeof(comment_text))) {
        return;
    }

    // Extract words from comment
    char *words[256];
    int word_count = extract_comment_words(comment_text, words, 256);

    for (int i = 0; i < word_count; i++) {
        char cleaned[MAX_SYMBOL_LENGTH];
        filter_clean_string_symbol(words[i], cleaned, sizeof(cleaned));

        if (strlen(cleaned) >= 2) {
            add_entry(cleaned, line_number, CONTEXT_COMMENT,
                     directory, filename, result, filter, entry_count, NULL);
        }
        free(words[i]);
    }
}

// Handler: interpreted_string_literal
static void handle_string_literal(TSNode node, const char *source_code,
                                  const char *directory, const char *filename,
                                  ParseResult *result, SymbolFilter *filter,
                                  int *entry_count, int line_number) {
    char string_text[4096];
    if (!safe_extract_node_text(source_code, node, string_text, sizeof(string_text))) {
        return;
    }

    // Remove quotes
    size_t len = strlen(string_text);
    if (len < 2 || string_text[0] != '"') return;

    char content[4096];
    strncpy(content, string_text + 1, sizeof(content) - 1);
    content[sizeof(content) - 1] = '\0';
    if (strlen(content) > 0) {
        content[strlen(content) - 1] = '\0'; // Remove closing quote
    }

    // Extract words
    char *words[256];
    int word_count = extract_comment_words(content, words, 256);

    for (int i = 0; i < word_count; i++) {
        char cleaned[MAX_SYMBOL_LENGTH];
        filter_clean_string_symbol(words[i], cleaned, sizeof(cleaned));

        if (strlen(cleaned) >= 2) {
            add_entry(cleaned, line_number, CONTEXT_STRING,
                     directory, filename, result, filter, entry_count, NULL);
        }
        free(words[i]);
    }
}
```

#### Dispatch Table
```c
typedef void (*NodeHandler)(TSNode, const char *, const char *, const char *,
                           ParseResult *, SymbolFilter *, int *, int);

typedef struct {
    const char *node_type;
    NodeHandler handler;
} NodeHandlerEntry;

static const NodeHandlerEntry node_handlers[] = {
    {"package_clause", handle_package_clause},
    {"import_declaration", handle_import_declaration},
    {"function_declaration", handle_function_declaration},
    {"call_expression", handle_call_expression},
    {"comment", handle_comment},
    {"interpreted_string_literal", handle_string_literal},
    {NULL, NULL} // Sentinel
};
```

#### Visit Node Function
```c
static void visit_node(TSNode node, const char *source_code,
                      const char *directory, const char *filename,
                      ParseResult *result, SymbolFilter *filter,
                      int *entry_count, int line_number) {
    const char *node_type = ts_node_type(node);

    // Update line number
    TSPoint start = ts_node_start_point(node);
    line_number = start.row + 1;

    // Look up handler
    for (int i = 0; node_handlers[i].node_type != NULL; i++) {
        if (strcmp(node_type, node_handlers[i].node_type) == 0) {
            node_handlers[i].handler(node, source_code, directory, filename,
                                   result, filter, entry_count, line_number);
            return; // Handler processes children if needed
        }
    }

    // No handler found, recursively visit children
    process_children(node, source_code, directory, filename,
                    result, filter, entry_count);
}

static void process_children(TSNode node, const char *source_code,
                            const char *directory, const char *filename,
                            ParseResult *result, SymbolFilter *filter,
                            int *entry_count) {
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSPoint start = ts_node_start_point(child);
        visit_node(child, source_code, directory, filename,
                  result, filter, entry_count, start.row + 1);
    }
}
```

#### Public Interface Implementation
```c
int parser_init(GoParser *parser, SymbolFilter *filter) {
    parser->filter = filter;
    return 0;
}

void parser_free(GoParser *parser) {
    // Nothing to free currently
    (void)parser;
}

int parser_parse_file(GoParser *parser, const char *filepath,
                      const char *project_root, ParseResult *result) {
    // Open file
    FileContent *file = open_file(filepath);
    if (!file) {
        fprintf(stderr, "Failed to open file: %s\n", filepath);
        return -1;
    }

    // Create parser
    TSParser *ts_parser = ts_parser_new();
    if (!ts_parser_set_language(ts_parser, tree_sitter_go())) {
        fprintf(stderr, "Failed to set Go language\n");
        close_file(file);
        ts_parser_delete(ts_parser);
        return -1;
    }

    // Parse
    TSTree *tree = ts_parser_parse_string(ts_parser, NULL, file->content, file->size);
    if (!tree) {
        fprintf(stderr, "Failed to parse file: %s\n", filepath);
        close_file(file);
        ts_parser_delete(ts_parser);
        return -1;
    }

    // Get relative path
    char directory[MAX_PATH_LENGTH] = "";
    char filename[MAX_FILENAME_LENGTH] = "";
    get_relative_path(filepath, project_root, directory, filename,
                     sizeof(directory), sizeof(filename));

    // Index filename itself (without .go extension)
    char name_only[MAX_FILENAME_LENGTH];
    strncpy(name_only, filename, sizeof(name_only) - 1);
    name_only[sizeof(name_only) - 1] = '\0';
    char *dot = strrchr(name_only, '.');
    if (dot) *dot = '\0';

    int entry_count = 0;
    add_entry(name_only, 1, CONTEXT_FILENAME, directory, filename,
             result, parser->filter, &entry_count, NULL);

    // Traverse AST
    TSNode root = ts_tree_root_node(tree);
    visit_node(root, file->content, directory, filename,
              result, parser->filter, &entry_count, 1);

    result->count = entry_count;

    // Cleanup
    ts_tree_delete(tree);
    ts_parser_delete(ts_parser);
    close_file(file);

    return 0;
}
```

### Phase 4: Main Binary

Create `go/index-go.c` - Copy from `c/index-c.c` and make these changes:

1. Line 1-10: Change include from `"c_language.h"` to `"go_language.h"`
2. Line ~50: Change `const char *data_dir = "c/data";` to `"go/data"`
3. Line ~100: Change `CParser parser;` to `GoParser parser;`
4. Line ~250: Update usage examples to show `.go` files

### Phase 5: Build Integration

Edit `Makefile` and add after the PHP section (around line 37):

```makefile
# Go language files
GO_TREE_SITTER_SRC = go/src/parser.c
GO_TREE_SITTER_OBJ = $(GO_TREE_SITTER_SRC:.c=.o)

GO_LANGUAGE_SRC = go/go_language.c
GO_LANGUAGE_OBJ = $(GO_LANGUAGE_SRC:.c=.o)

GO_MAIN_SRC = go/index-go.c
GO_MAIN_OBJ = $(GO_MAIN_SRC:.c=.o)
```

Update `all` target (line 44):
```makefile
all: typescript/index-ts c/index-c php/index-php go/index-go query-index index-ts index-c index-php index-go
```

Add Go indexer target (around line 56):
```makefile
# Go indexer
go/index-go: $(SHARED_OBJ) $(GO_TREE_SITTER_OBJ) $(GO_LANGUAGE_OBJ) $(GO_MAIN_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
```

Add convenience symlink (around line 71):
```makefile
index-go: go/index-go
	ln -sf go/index-go index-go
```

Update `clean` target (line 77) to include:
```makefile
rm -f $(GO_TREE_SITTER_OBJ) $(GO_LANGUAGE_OBJ) $(GO_MAIN_OBJ)
rm -f go/index-go index-go
```

## Testing Strategy

### Step 1: Build
```bash
make clean
make
```

Expected output:
- No compiler warnings
- `./index-go` symlink created
- `./go/index-go` binary created

### Step 2: Test Indexing
```bash
# Remove old database
rm -f code-index.db

# Index Go test files
./index-go ./scratch/sources/go --verbose

# Expected output:
# - Preflight checks pass
# - Files indexed: minimal.go
# - No errors
```

### Step 3: Query Symbols
```bash
# Test various queries
./query-index "test" --limit 5
./query-index "main" -i function
./query-index "fmt" -i import
./query-index "println" -i call
./query-index "hello" -i string
./query-index "world" -i string
```

Expected results for minimal.go:
- Package "main" (CONTEXT_NAMESPACE)
- Import "fmt" (CONTEXT_IMPORT)
- Function "main" (CONTEXT_FUNCTION_NAME)
- Call "Println" with parent "fmt" (CONTEXT_CALL_EXPRESSION)
- String words "hello", "world" (CONTEXT_STRING)
- Filename "minimal" (CONTEXT_FILENAME)

### Step 4: Verify Database
```bash
sqlite3 code-index.db "SELECT symbol, context, line_number FROM code_index WHERE filename='minimal.go' ORDER BY line_number;"
```

## Common Issues and Solutions

### Issue: Compiler errors about missing functions
- **Solution:** Make sure all helper functions are declared before use (forward declarations at top)

### Issue: Segfault when parsing
- **Solution:** Always check `ts_node_is_null()` before using nodes from `ts_node_child_by_field_name()`

### Issue: Symbols not being indexed
- **Solution:** Check `filter_should_index()` - symbol might be in keywords.txt or too short

### Issue: Wrong context types
- **Solution:** Use AST explorer to verify node types: `./scratch/tools/ast-explorer-go <file.go>`

### Issue: Parent symbols not showing
- **Solution:** Make sure you're filling `ExtColumns` and passing it to `add_entry()`

## Next Steps After Minimal Implementation

Once minimal.go is working, expand to handle test.go:

1. **Type declarations** (`type_declaration`, `type_spec`)
2. **Struct definitions** (`struct_type`, `field_declaration_list`)
3. **Method declarations** (`method_declaration` - has receiver)
4. **Variable declarations** (`var_declaration`, `const_declaration`)
5. **Interface definitions** (`interface_type`)
6. **More expression types** (binary, unary, index, slice, etc.)

## Reference Commands

```bash
# Explore AST structure
./scratch/tools/ast-explorer-go <file.go>

# Build
make

# Index
rm -f code-index.db
./index-go ./scratch/sources/go

# Query
./query-index "pattern" --limit 10
./query-index "pattern" -i context1 context2
./query-index "pattern" -x comment string
./query-index "pattern" --columns line context symbol

# Direct SQL (debugging)
sqlite3 code-index.db "SELECT * FROM code_index LIMIT 10;"
```

## Success Criteria

✅ Compiles without warnings
✅ Indexes minimal.go without errors
✅ Extracts package name
✅ Extracts import
✅ Extracts function name
✅ Extracts function calls with parent symbol
✅ Extracts string content
✅ Query tool finds all symbols
✅ Line numbers are accurate
✅ Relative paths are stored correctly

---

**Ready to implement!** Start with Phase 1 (config files), then Phase 2 (header), then Phase 3 (parser), then Phase 4 (main), then Phase 5 (build).
