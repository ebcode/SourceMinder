# Plan: Adding C Language Support to Multi-Language Code Indexer

**Date**: 2025-10-13
**Goal**: Implement C language indexing capability to index the project's own C codebase

## Project Overview

The indexer currently supports TypeScript. We need to add C language support following the same architecture:
- **Shared Core** (database, filter, file_walker, validation) - already implemented, language-agnostic
- **Language Implementation** (C-specific AST visitor and main program)
- **Configuration** (C keywords, file extensions, ignored directories)

## Architecture Analysis

### Current TypeScript Structure
```
typescript/
├── src/                      # Tree-sitter generated files (8.7MB parser.c + scanner.c)
│   ├── parser.c
│   ├── scanner.c
│   └── tree_sitter/*.h
├── ts_language.{c,h}         # 787 LOC - AST visitor with handlers for TS constructs
├── index-ts.c                # 293 LOC - Main program (arg parsing, file walking, DB ops)
└── data/                     # TypeScript-specific configuration
    ├── file-extensions.txt   # .ts, .tsx
    ├── ts-keywords.txt       # 73 TypeScript keywords
    └── ignore_files.txt      # node_modules, dist
```

### Target C Structure (Mirror Pattern)
```
c/
├── src/                      # Tree-sitter C grammar files (to be downloaded)
│   ├── parser.c
│   └── tree_sitter/*.h
├── c_language.{c,h}          # C AST visitor (functions, structs, typedefs, etc.)
├── index-c.c                 # Main program (similar to index-ts.c)
└── data/                     # C-specific configuration
    ├── file-extensions.txt   # .c, .h
    ├── c-keywords.txt        # C11 keywords (auto, break, case, etc.)
    └── ignore_files.txt      # build, obj, bin, .git (optional)
```

## Context Types Mapping

### TypeScript Context Types (Current)
- CLASS_NAME, INTERFACE_DECLARATION, TYPE_ALIAS
- FUNCTION_NAME, METHOD_DEFINITION
- FUNCTION_ARGUMENT, VARIABLE_NAME, PROPERTY_NAME
- IMPORT, EXPORT
- COMMENT, STRING, FILENAME, CALL_EXPRESSION

### C Context Types (Needed)
- **FUNCTION_NAME** - function declarations and definitions
- **FUNCTION_ARGUMENT** - function parameters
- **VARIABLE_NAME** - variable declarations (global and local)
- **TYPE_NAME** - struct, union, enum, typedef
- **PROPERTY_NAME** - struct/union member fields
- **COMMENT** - comments (single-line // and block /* */)
- **STRING** - string literals
- **FILENAME** - filename without extension

### Not Applicable for C
- CLASS_NAME (C has no classes)
- IMPORT/EXPORT (C has #include, but these are preprocessor directives, not language constructs)
- CALL_EXPRESSION (may add later for completeness, but lower priority)

## C Language Features to Index

### Priority 1 (Essential)
1. **Function declarations/definitions**
   - `int foo(int x, char *y)` → index "foo" as FUNCTION_NAME
   - Parameters "x", "y" as FUNCTION_ARGUMENT

2. **Struct/Union definitions**
   - `struct CodeIndexDatabase { ... }` → index "CodeIndexDatabase" as TYPE_NAME
   - Member fields as PROPERTY_NAME

3. **Typedef declarations**
   - `typedef struct { ... } ContextType;` → index "ContextType" as TYPE_NAME
   - `typedef enum { ... } ContextType;` → similar

4. **Enum definitions**
   - `enum { CONTEXT_CLASS_NAME, ... }` → index "CONTEXT_CLASS_NAME" as VARIABLE_NAME (or TYPE_NAME)

5. **Variable declarations**
   - `static SymbolFilter *filter;` → index "filter" as VARIABLE_NAME
   - `int count = 0;` → index "count" as VARIABLE_NAME

6. **Comments and strings**
   - Extract words from comments and string literals (like TypeScript)

7. **Filename indexing**
   - Index filename without extension

### Priority 2 (Nice to Have - Future Sessions)
- #define macros (preprocessor)
- Static vs extern modifiers
- Function pointers in typedefs
- Call expressions (function calls)

## Implementation Phases

### Phase 1: Foundation & Tree-sitter Setup (Session 1)
**Goal**: Get tree-sitter-c integrated and basic structure in place

**Tasks**:
1. Create `c/` directory structure
2. Download tree-sitter-c grammar from https://github.com/tree-sitter/tree-sitter-c
   - Extract parser.c and tree_sitter/*.h files
   - Verify compilation
3. Create C configuration files:
   - `c/data/file-extensions.txt` (.c, .h)
   - `c/data/c-keywords.txt` (C11 keywords: auto, break, case, char, const, continue, default, do, double, else, enum, extern, float, for, goto, if, inline, int, long, register, restrict, return, short, signed, sizeof, static, struct, switch, typedef, union, unsigned, void, volatile, while, _Alignas, _Alignof, _Atomic, _Bool, _Complex, _Generic, _Imaginary, _Noreturn, _Static_assert, _Thread_local)
   - `c/data/ignore_files.txt` (build, obj, bin, .git, node_modules)
4. Create skeleton `c/c_language.h` with function signatures matching TypeScript pattern:
   ```c
   int parser_init(CParser *parser, SymbolFilter *filter);
   int parser_parse_file(CParser *parser, const char *filepath, const char *project_root, ParseResult *result);
   void parser_free(CParser *parser);
   ```
5. Update Makefile with C targets (mirror TypeScript build rules)
6. Test compilation

**Expected Output**: Compiles successfully, but doesn't parse yet

---

### Phase 2: Basic AST Visitor (Session 2)
**Goal**: Implement core C language visitor to extract functions, structs, and variables

**Tasks**:
1. Study tree-sitter-c node types:
   - Use `tmp/ast-explorer.c` pattern to understand C grammar nodes
   - Document key node types: function_definition, declaration, struct_specifier, etc.
2. Implement `c/c_language.c` with handlers for:
   - **function_definition** → FUNCTION_NAME + parameters (FUNCTION_ARGUMENT)
   - **declaration** → VARIABLE_NAME (global/local variables)
   - **struct_specifier** → TYPE_NAME + field_declaration (PROPERTY_NAME)
   - **enum_specifier** → TYPE_NAME + enumerator (VARIABLE_NAME or TYPE_NAME)
   - **comment** → COMMENT (extract words)
   - **string_literal** → STRING (extract words)
3. Implement main program `c/index-c.c` (mostly copy from index-ts.c with path changes)
4. Create convenience symlink `index-c -> c/index-c`
5. Test on `shared/database.c` and verify output

**Expected Output**: Can index functions and variables from simple C files

---

### Phase 3: Typedef and Type Annotations (Session 3)
**Goal**: Handle typedef declarations and complex type references

**Tasks**:
1. Add handlers for:
   - **typedef** declarations → TYPE_NAME
   - **type_identifier** in parameter types → TYPE_NAME
   - **type_qualifier** (const, volatile, etc.) - filter these out
   - **pointer_declarator** - extract underlying type
2. Handle anonymous structs in typedefs:
   ```c
   typedef struct {
       int x;
   } Point;  // Index "Point"
   ```
3. Handle named structs:
   ```c
   struct Point {
       int x;
   };  // Index "Point"
   ```
4. Test on `shared/filter.c` and `shared/validation.c`

**Expected Output**: Can extract typedef names and struct member fields

---

### Phase 4: Refinement and Edge Cases (Session 4)
**Goal**: Handle C-specific edge cases and improve accuracy

**Tasks**:
1. Handle function declarations vs definitions:
   - Both should index the function name
   - Header files (.h) typically have declarations
   - Source files (.c) have definitions
2. Handle function pointers in typedefs:
   ```c
   typedef void (*NodeHandler)(TSNode node, ...);
   ```
3. Handle multi-declarators:
   ```c
   int x, y, z;  // Should index x, y, z separately
   ```
4. Handle macros in #define (if time permits):
   ```c
   #define MAX_BUFFER 1024  // Index "MAX_BUFFER"?
   ```
5. Test edge cases and refine filtering
6. Consider: Should we distinguish between .h and .c in context types?

**Expected Output**: Robust indexing of the project's own C files

---

### Phase 5: Full Project Indexing & Validation (Session 5)
**Goal**: Index the entire indexer-c project and validate results

**Tasks**:
1. Run full index on project:
   ```bash
   ./index-c ./shared ./c --quiet
   ```
2. Query validation:
   ```bash
   ./query-index "parser" -i function_name     # Find parser functions
   ./query-index "%Result" -i type_name        # Find Result types
   ./query-index "filter" -i variable_name     # Find filter variables
   ./query-index "database" -x comment         # Exclude comments
   ```
3. Compare with grep results to verify accuracy
4. Performance testing on larger C codebases
5. Update README.md with C language documentation
6. Update Makefile to build both languages by default:
   ```make
   all: typescript/index-ts c/index-c query-index
   ```

**Expected Output**: Fully functional C indexer ready for use

## C Language Challenges & Solutions

### Challenge 1: Header Files vs Source Files
**Issue**: Function declarations in .h and definitions in .c
**Solution**: Index both. Users can filter by filename if needed.

### Challenge 2: Preprocessor Directives
**Issue**: tree-sitter parses after preprocessing, so #define macros are tricky
**Solution**: Start without macro support. Add in later if needed (may require separate handling).

### Challenge 3: Complex Declarators
**Issue**: C has complex syntax like `int (*foo[10])(void)` (array of function pointers)
**Solution**: Focus on simple cases first. Use tree-sitter queries to extract identifier nodes.

### Challenge 4: Type vs Variable Context
**Issue**: `typedef struct Foo Foo;` - "Foo" appears twice with different meanings
**Solution**: Use context_type to distinguish (TYPE_NAME vs VARIABLE_NAME).

### Challenge 5: Static vs Global
**Issue**: Should we distinguish between static/extern/global variables?
**Solution**: No - index all as VARIABLE_NAME. Users can grep for `static` if needed.

## Tree-sitter C Grammar Download

**Source**: https://github.com/tree-sitter/tree-sitter-c
Already have these files, located in ./tree-sitter-c. Necessary files have been added to ./c/src

## Testing Strategy

### Unit Testing (per session)
- Test each handler on minimal C snippets
- Use `tmp/test.c` for quick tests
- Verify correct context types assigned

### Integration Testing (Session 5)
- Index entire project
- Query for known symbols
- Compare with grep results
- Performance benchmark

### Query Examples for Validation
```bash
# Find all parser functions
./query-index "parser%" -i function_name

# Find ParseResult type
./query-index "ParseResult" -i type_name

# Find database-related symbols (exclude comments)
./query-index "%database%" -x comment

# Find all struct definitions
./query-index "%" -i type_name | grep -i struct
```

## Makefile Updates

Add C language targets (mirror TypeScript):
```make
# C language files
C_TREE_SITTER_SRC = c/src/parser.c
C_TREE_SITTER_OBJ = $(C_TREE_SITTER_SRC:.c=.o)

C_LANGUAGE_SRC = c/c_language.c
C_LANGUAGE_OBJ = $(C_LANGUAGE_SRC:.c=.o)

C_MAIN_SRC = c/index-c.c
C_MAIN_OBJ = $(C_MAIN_SRC:.c=.o)

# C indexer
c/index-c: $(SHARED_OBJ) $(C_TREE_SITTER_OBJ) $(C_LANGUAGE_OBJ) $(C_MAIN_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Convenience symlink
index-c: c/index-c
	ln -sf c/index-c index-c
```

## Success Criteria

After all phases complete:
1. ✓ Can index all .c and .h files in the project
2. ✓ Extracts functions, structs, typedefs, variables, enums
3. ✓ Filters out C keywords and stopwords
4. ✓ Query results are accurate and useful
5. ✓ Performance is acceptable (<1s for 26 files)
6. ✓ README.md documents C language support
7. ✓ Can find symbols in the project's own C code

## Session Breakdown

| Session | Focus | LOC Estimate | Complexity |
|---------|-------|--------------|------------|
| 1 | Foundation & tree-sitter setup | ~100 | Low |
| 2 | Basic AST visitor | ~300 | Medium |
| 3 | Typedef & type annotations | ~200 | Medium |
| 4 | Refinement & edge cases | ~150 | Medium |
| 5 | Full indexing & validation | ~50 | Low |
| **Total** | | ~800 LOC | (vs 787 for TypeScript) |

## Implementation Notes

- **Follow TypeScript pattern closely** - proven architecture
- **Start simple** - get basic functionality working first
- **Test incrementally** - verify each handler before moving on
- **Use tree-sitter queries** - cleaner than nested loops (TypeScript learned this)
- **Document as you go** - comment complex handlers
- **No backwards compatibility concerns** - user confirmed we can break things

## Session 1 Detailed Tasks

For this first session, we'll focus on:

1. **Create directory structure**
   ```bash
   mkdir -p c/src/tree_sitter
   mkdir -p c/data
   ```

2. **Download tree-sitter-c**
   - Clone repo in /tmp
   - Copy src/parser.c to c/src/
   - Copy src/tree_sitter/parser.h to c/src/tree_sitter/
   - Verify files compile

3. **Create configuration files**
   - `c/data/file-extensions.txt`: .c and .h
   - `c/data/c-keywords.txt`: All C11 keywords
   - `c/data/ignore_files.txt`: build, obj, bin

4. **Create c_language.h header**
   - Parser struct definition
   - Function signatures matching TypeScript pattern
   - Include guards

5. **Create minimal c_language.c**
   - Stub implementations that compile but don't parse yet
   - Include tree-sitter headers
   - External function declaration: `const TSLanguage *tree_sitter_c(void);`

6. **Create index-c.c**
   - Copy from index-ts.c
   - Update paths: "typescript/data" → "c/data"
   - Update function calls: use CParser instead of TypeScriptParser

7. **Update Makefile**
   - Add C targets
   - Update `all` target to include both languages
   - Add to clean target

8. **Test compilation**
   ```bash
   make clean
   make
   ```

9. **Document progress**
   - Update this plan.md with completed tasks
   - Note any issues encountered

**Estimated time**: 2-3 hours

## Notes for Future Sessions

- Consider adding a CONTEXT_MACRO type for #define macros
- May want to index function calls (CALL_EXPRESSION)
- Consider distinguishing header vs source context
- Performance optimization if needed (tree-sitter queries vs loops)
- Documentation in README.md after Session 5

## End State

After all sessions, we'll have:
- Dual-language indexer (TypeScript + C)
- Ability to index our own C codebase
- Query tool works for both languages
- Clear pattern for adding more languages
- Foundation for indexing C projects anywhere

---

## Questions for Consideration

1. Should #define macros be indexed? (Preprocessor directives)
2. Should we distinguish static vs global variables?
3. Should function declarations and definitions both be indexed?
4. Should we index function calls (like TypeScript's CALL_EXPRESSION)?
5. Should header files (.h) have special handling?

**Initial Answers**:
1. No - start without macros
2. No - index all as VARIABLE_NAME
3. Yes - both are useful
4. Not in Phase 1, maybe later
5. No - treat .h and .c the same way
