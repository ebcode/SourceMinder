# Adding a New Language to the Indexer

This document describes the step-by-step process for adding support for a new language to the multi-language code indexer. This guide was created while implementing Python support and serves as a reference for future language additions.

## Overview

Each language implementation consists of:
1. A language directory with source files and configuration
2. Tree-sitter grammar integration (as a git submodule)
3. AST exploration tool for understanding tree-sitter output
4. Test files exercising language-specific features
5. Language-specific parser implementation
6. Main indexer entry point
7. Makefile integration

## Architecture Pattern

All language implementations follow this structure:

```
<language>/
├── config/
│   ├── file_extensions.txt   # File extensions to index (e.g., .py, .pyx)
│   ├── ignore_files.txt       # Directories to skip (e.g., __pycache__)
│   └── keywords.txt           # Language keywords to filter out
├── src/                       # Tree-sitter grammar (git submodule)
├── <language>_language.h      # Parser interface declaration
├── <language>_language.c      # Language-specific AST parsing logic
└── index-<language>.c         # Main entry point (thin wrapper)

scratch/
├── tools/
│   ├── ast-explorer-<language>.c    # AST visualization tool
│   ├── ast-explorer-<language>      # Compiled binary
│   └── Makefile                     # Build rules for tools
└── sources/<language>/
    ├── feature1.py              # Test file for specific feature
    ├── feature2.py              # Another targeted test
    └── ...                      # More feature-specific tests
```

## Step-by-Step Implementation Guide

### Step 1: Explore Existing Implementations

Before starting, familiarize yourself with the existing language implementations to understand the patterns:

**Using qi to explore the codebase:**
```bash
# Find all parser initialization functions
./qi parser_init -i func -x noise

# Find all parser_parse_file functions
./qi parser_parse_file -i func -x noise

# Find extract functions to understand parsing patterns
./qi extract% -i func -x noise --limit 20

# Look at a simple implementation (index-*.c files are ~40 lines)
./qi % -f typescript/index-ts.c
```

**Key functions every language must implement:**
- `parser_init()` - Initialize the parser with symbol filters
- `parser_parse_file()` - Parse a file and extract symbols
- `parser_free()` - Clean up parser resources

**Examine existing implementations:**
- C language: `c/c_language.c` and `c/index-c.c`
- TypeScript: `typescript/ts_language.c` and `typescript/index-ts.c`
- PHP: `php/php_language.c` and `php/index-php.c`
- Go: `go/go_language.c` and `go/index-go.c`

### Step 2: Set Up Tree-Sitter Grammar

Each language uses the official tree-sitter grammar as a git submodule.

**Find the appropriate grammar:**
1. Visit https://github.com/tree-sitter
2. Find the grammar repo (e.g., `tree-sitter-python`)

**Add as submodule:**
```bash
git submodule add https://github.com/tree-sitter/tree-sitter-python.git
git submodule update --init --recursive
```

**Verify the grammar structure:**
```bash
ls tree-sitter-python/src/
# Should contain: parser.c, scanner.c (or scanner.cc), tree_sitter/parser.h
```

**Note:** Some grammars have subdirectories (e.g., TypeScript has `tree-sitter-typescript/typescript/src/`). Check the structure and adjust paths accordingly.

### Step 3: Create AST Explorer Tool

**CRITICAL STEP:** Before implementing the parser, create an AST explorer tool to understand how tree-sitter parses your target language. This tool is essential for development.

**Create the explorer source file** `scratch/tools/ast-explorer-python.c`:

Copy the pattern from existing explorers (e.g., `ast-explorer-ts.c`). The key elements:
- Include tree-sitter headers
- Declare: `extern const TSLanguage *tree_sitter_python(void);`
- Implement `print_tree()` function to recursively display AST nodes
- Main function: read file, parse with tree-sitter, print AST

The file is typically ~90 lines. See existing explorers for the exact pattern.

**Update scratch/tools/Makefile** to build the Python explorer:

Add to the parser sources section:
```makefile
PYTHON_PARSER_SRC = ../../tree-sitter-python/src/parser.c ../../tree-sitter-python/src/scanner.c
```

Add to AST_EXPLORERS:
```makefile
AST_EXPLORERS = ast-explorer-go ast-explorer-c ast-explorer-ts ast-explorer-php ast-explorer-python
```

Add build rule:
```makefile
ast-explorer-python: ast-explorer-python.c $(PYTHON_PARSER_SRC)
	$(CC) $(CFLAGS) $< $(PYTHON_PARSER_SRC) -o $@ $(LDFLAGS)
```

**Build the explorer:**
```bash
cd scratch/tools
make ast-explorer-python
```

**Test the explorer:**
```bash
# Create a simple test file
echo 'def hello(): pass' > /tmp/test.py
./ast-explorer-python /tmp/test.py
```

You should see tree-sitter's AST structure printed. This is your Rosetta Stone for implementing the parser.

### Step 4: Create Test Files for Language Features

**Create the sources directory:**
```bash
mkdir -p scratch/sources/python
```

**Create targeted test files** for each language feature you want to support. These files should be small and focused on exercising specific constructs.

Example test files (you may already have these):
- `classes.py` - Class definitions, methods, inheritance
- `decorators.py` - Decorator patterns
- `async-await.py` - Async functions and await expressions
- `type-hints.py` - Type annotations (if applicable)
- `imports.py` - Import statements and variations
- `generators.py` - Generator functions and yield
- `list-comprehensions.py` - Comprehension expressions
- `lambdas.py` - Lambda functions
- `properties.py` - Property decorators and getters/setters
- `magic-methods.py` - `__init__`, `__str__`, etc.

**Example test file** `scratch/sources/python/classes.py`:
```python
class SimpleClass:
    """A simple example class"""

    def method(self, param):
        return param

    def another_method(self):
        pass

class InheritedClass(SimpleClass):
    class_var = 42

    def __init__(self, value):
        self.value = value
```

**Workflow for understanding AST structure:**
```bash
# Create test file with specific feature
vim scratch/sources/python/classes.py

# Run AST explorer to see tree-sitter output
./scratch/tools/ast-explorer-python scratch/sources/python/classes.py

# Compare AST to source code
# Note the node types for: class, function_definition, identifier, etc.
# This informs your parser implementation
```

**This is crucial:** You'll repeatedly edit test files and run the AST explorer to understand how tree-sitter represents different language constructs.

### Step 5: Create Language Directory Structure

**Create the directory and config folder:**
```bash
mkdir -p python/config
```

**Create configuration files:**

`python/config/file_extensions.txt` - One extension per line:
```
.py
.pyx
.pyi
```

`python/config/ignore_files.txt` - Directories to skip:
```
__pycache__
.venv
venv
.pytest_cache
.mypy_cache
dist
build
*.egg-info
```

`python/config/keywords.txt` - Language keywords (one per line):
```
and
as
assert
async
await
break
class
continue
def
del
elif
else
except
False
finally
for
from
global
if
import
in
is
lambda
None
nonlocal
not
or
pass
raise
return
True
try
while
with
yield
```

**Tips for creating config files:**
- Look at similar languages for inspiration
- Keywords prevent indexing common language tokens
- File extensions should include all variants (.pyi for type stubs, .pyx for Cython)
- Ignore directories should include virtual environments and build artifacts

### Step 6: Create Language Header File

Create `python/python_language.h`:

```c
#ifndef PYTHON_LANGUAGE_H
#define PYTHON_LANGUAGE_H

#include "../shared/parse_result.h"
#include "../shared/filter.h"
#include <tree_sitter/api.h>

typedef struct {
    TSParser *parser;
    SymbolFilter *filter;
} PythonParser;

int parser_init(PythonParser *parser, SymbolFilter *filter);
int parser_parse_file(PythonParser *parser, const char *filepath, const char *project_root, ParseResult *result);
void parser_free(PythonParser *parser);

#endif // PYTHON_LANGUAGE_H
```

**Pattern notes:**
- Struct name follows pattern: `<Language>Parser` (e.g., `PythonParser`, `TypeScriptParser`)
- Must contain `TSParser *parser` and `SymbolFilter *filter`
- Three required functions with exact signatures shown above

### Step 7: Implement Main Entry Point

Create `python/index-python.c` (this is mostly boilerplate):

```c
#include <stdio.h>
#include <stdlib.h>
#include "../shared/indexer_main.h"
#include "../shared/constants.h"
#include "python_language.h"

/* Wrapper for parser_init to match IndexerConfig signature */
static void* parser_init_wrapper(SymbolFilter *filter) {
    PythonParser *parser = malloc(sizeof(PythonParser));
    if (!parser) {
        return NULL;
    }
    if (parser_init(parser, filter) != 0) {
        free(parser);
        return NULL;
    }
    return parser;
}

/* Wrapper for parser_parse_file to match IndexerConfig signature */
static int parser_parse_wrapper(void *parser, const char *filepath, const char *project_root, ParseResult *result) {
    return parser_parse_file((PythonParser*)parser, filepath, project_root, result);
}

/* Wrapper for parser_free to match IndexerConfig signature */
static void parser_free_wrapper(void *parser) {
    if (parser) {
        parser_free((PythonParser*)parser);
        free(parser);
    }
}

int main(int argc, char *argv[]) {
    IndexerConfig config = {
        .name = "index-python",
        .data_dir = "python/" CONFIG_DIR,
        .parser_init = parser_init_wrapper,
        .parser_parse = parser_parse_wrapper,
        .parser_free = parser_free_wrapper
    };

    return indexer_main(argc, argv, &config);
}
```

**Key points:**
- Replace `Python` with your language name (capitalized)
- Replace `python` with your language name (lowercase) in paths
- The `CONFIG_DIR` macro is defined in `shared/constants.h`
- This file is usually ~45 lines and rarely needs changes beyond names

### Step 8: Implement Language Parser (python_language.c)

This is the most complex part. The parser must:
1. Walk the AST tree
2. Extract symbols (functions, classes, variables, etc.)
3. Determine context types (FUNCTION, CLASS, VARIABLE, etc.)
4. Track parent symbols for member expressions
5. Extract type information where available
6. Handle language-specific constructs

**Development workflow:**

1. **Use the AST explorer constantly:**
   ```bash
   # Create a test case
   echo 'def foo(x: int) -> str: return str(x)' > /tmp/test.py

   # See how tree-sitter parses it
   ./scratch/tools/ast-explorer-python /tmp/test.py

   # Note the node types: function_definition, identifier, parameters, type, etc.
   ```

2. **Start with basic structure:**
   ```c
   // 1. Include required headers
   #include "python_language.h"
   #include "../shared/constants.h"
   #include "../shared/string_utils.h"

   // 2. Declare the tree-sitter language
   TSLanguage *tree_sitter_python(void);

   // 3. Implement helper functions
   // 4. Implement main traversal function
   // 5. Implement the three required interface functions
   ```

3. **Implement incrementally:**
   - Start with function definitions
   - Test with: `./index-python scratch/sources/python/`
   - Query with: `./qi % -i func`
   - Add class support
   - Test again
   - Continue iteratively

**Structure overview:**

The parser implementation consists of:
- Helper functions: `extract_node_text()`, `extract_parameters()`, `extract_decorators()`, etc.
- Node handlers: `handle_function_definition()`, `handle_class_definition()`, etc.
- Dispatch table: Maps node types to handlers
- Traversal functions: `visit_node()` and `process_children()`
- Three required interface functions: `parser_init()`, `parser_parse_file()`, `parser_free()`

See `python/python_language.c` (~500 lines) for a complete, well-documented reference implementation.

**Tips for implementation:**
- Keep AST explorer running in a terminal
- Test each new feature with targeted test files in `scratch/sources/python/`
- Use existing implementations as reference
- Start simple, add complexity incrementally
- **Fix all compiler warnings immediately**

**Common patterns to implement:**
- Function definitions
- Class definitions
- Method definitions (functions within classes)
- Variable assignments
- Parameters and type hints
- Imports
- Decorators
- Comments
- String literals

### Step 9: Update Makefile

Add the new language to the main Makefile following the existing pattern.

**Add grammar directory variable** (near line 11):
```makefile
PYTHON_GRAMMAR_DIR = tree-sitter-python
```

**Add to INCLUDE_PATHS** (around line 18 and 22):
```makefile
# macOS:
INCLUDE_PATHS = ... -I$(PYTHON_GRAMMAR_DIR)/src

# Linux:
INCLUDE_PATHS = ... -I$(PYTHON_GRAMMAR_DIR)/src
```

**Add language-specific variables** (after other language sections, ~line 95):
```makefile
# Python language files
PYTHON_TREE_SITTER_SRC = $(PYTHON_GRAMMAR_DIR)/src/parser.c $(PYTHON_GRAMMAR_DIR)/src/scanner.c
PYTHON_TREE_SITTER_OBJ = $(PYTHON_TREE_SITTER_SRC:.c=.o)

PYTHON_LANGUAGE_SRC = python/python_language.c
PYTHON_LANGUAGE_OBJ = $(PYTHON_LANGUAGE_SRC:.c=.o)

PYTHON_MAIN_SRC = python/index-python.c
PYTHON_MAIN_OBJ = $(PYTHON_MAIN_SRC:.c=.o)
```

**Note:** If scanner is C++ (.cc), you'll need special handling (see PHP in Makefile).

**Add to all target** (line ~105):
```makefile
all: $(BUILD_DIR) ... $(BUILD_DIR)/index-python ... ./index-python
```

**Add build rule** (after other language rules, ~line 125):
```makefile
# Python indexer
$(BUILD_DIR)/index-python: $(SHARED_OBJ) $(PYTHON_TREE_SITTER_OBJ) $(PYTHON_LANGUAGE_OBJ) $(PYTHON_MAIN_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
```

**Add symlink target** (after other symlinks, ~line 142):
```makefile
./index-python: $(BUILD_DIR)/index-python
	ln -sf $(BUILD_DIR)/index-python index-python
```

**Update clean target** (line ~155):
```makefile
clean:
	rm -f ... $(PYTHON_TREE_SITTER_OBJ) $(PYTHON_LANGUAGE_OBJ) $(PYTHON_MAIN_OBJ)
	...
	rm -f ... index-python
```

### Step 10: Build and Test

**Build the indexer:**
```bash
make
```

**Fix any compiler warnings immediately** - Don't defer them.

**Test with existing files:**
```bash
# Remove old database for clean test
rm -f code-index.db

# Index the test files
./index-python scratch/sources/python/

# Test basic queries
./qi % -x noise --limit 20                    # See all symbols
./qi % -i class -x noise                      # All classes
./qi % -i func -x noise                       # All functions
./qi % -i var -f classes.py                   # Variables in specific file
```

**Test with the AST explorer side-by-side:**
```bash
# Compare what you're indexing vs what tree-sitter sees
./scratch/tools/ast-explorer-python scratch/sources/python/classes.py
./qi % -f classes.py
```

**Iterative development cycle:**
1. Notice missing symbols or incorrect parsing
2. Use AST explorer on problematic code
3. Update `python_language.c` to handle that node type
4. Rebuild: `make`
5. Re-index: `rm code-index.db && ./index-python scratch/sources/python/`
6. Test: `./qi % -i <context> -x noise`
7. Repeat

**Test different symbol types:**
```bash
./qi % -i func -x noise         # Functions
./qi % -i class -x noise        # Classes
./qi % -i var -x noise          # Variables
./qi % -i arg -x noise          # Parameters
./qi % -i imp -x noise          # Imports
```

**Test with expand and context:**
```bash
./qi UserManager -e             # Show full class definition
./qi get_user -e                # Show full function definition
./qi % -i func -C 5             # Show functions with 5 lines of context
```

**Test with real code:**
```bash
# Index a real Python project
./index-python ~/projects/some-python-project/

# Query it
./qi authenticate -i func -e
./qi % -i class --limit 20
```

### Step 11: Iterate and Improve

As you test, you'll discover edge cases and missing functionality:

1. **Test with real-world code** - Index actual projects in your language
2. **Check for missing symbols** - Use `./qi % -i <context>` to see what's captured
3. **Refine parsing logic** - Add handlers for constructs you missed
4. **Test special cases** - Use your `scratch/sources/python/` test files
5. **Verify type extraction** - Check if type annotations are captured properly

**Use scratch test files systematically:**
```bash
# Test decorators
./scratch/tools/ast-explorer-python scratch/sources/python/decorators.py
# Update parser to handle decorators
# Re-test
./qi % -f decorators.py
```

**Common issues to address:**
- Nested functions/classes
- Decorators/attributes
- Special methods (e.g., `__init__`, `__str__`)
- Lambda functions
- List comprehensions
- Property decorators
- Async functions

### Step 12: Documentation

Once implementation is complete:

1. Update README.md to list the new language
2. Consider adding language-specific notes if there are unique features
3. Update this document with any lessons learned

## Summary Checklist

When adding a new language, you need to:

- [ ] Add tree-sitter grammar as git submodule
- [ ] Create AST explorer tool (`scratch/tools/ast-explorer-<language>.c`)
- [ ] Update `scratch/tools/Makefile` to build AST explorer
- [ ] Create test files in `scratch/sources/<language>/` for each language feature
- [ ] Create `<language>/config/` directory
- [ ] Create `file_extensions.txt`
- [ ] Create `ignore_files.txt`
- [ ] Create `keywords.txt`
- [ ] Create `<language>_language.h` header
- [ ] Create `index-<language>.c` main entry point
- [ ] Implement `<language>_language.c` parser (use AST explorer constantly!)
- [ ] Add grammar directory variable to main Makefile
- [ ] Add include paths to main Makefile
- [ ] Add source/object variables to main Makefile
- [ ] Add to `all` target in Makefile
- [ ] Add build rule in Makefile
- [ ] Add symlink target in Makefile
- [ ] Update `clean` target in Makefile
- [ ] Build with `make`
- [ ] Test with `scratch/sources/<language>/` files
- [ ] Iterate using AST explorer for missing features
- [ ] Test with real-world projects
- [ ] Update README.md

## Development Workflow

The key to successful implementation is this iterative workflow:

```
1. Create/update test file (scratch/sources/python/feature.py)
   ↓
2. Run AST explorer to see tree-sitter output
   ./scratch/tools/ast-explorer-python scratch/sources/python/feature.py
   ↓
3. Identify node types and structure
   ↓
4. Update python_language.c to handle those nodes
   ↓
5. Rebuild: make
   ↓
6. Re-index: rm code-index.db && ./index-python scratch/sources/python/
   ↓
7. Query and verify: ./qi % -f feature.py
   ↓
8. If not correct, goto step 2
   ↓
9. If correct, move to next feature (goto step 1)
```

**The AST explorer is your most important tool** - use it constantly to understand how tree-sitter represents language constructs.

## Files to Reference

When implementing a new language, these files are helpful references:

**For AST exploration:**
- `scratch/tools/ast-explorer-ts.c` - Template for AST explorer
- `scratch/tools/Makefile` - Build rules for tools
- `scratch/sources/typescript/` - Example test files

**For simple structure:**
- `typescript/index-ts.c` - Clean, simple main entry point
- `typescript/ts_language.h` - Clear header structure

**For comprehensive parsing:**
- `typescript/ts_language.c` - Well-documented TypeScript implementation
- `c/c_language.c` - C implementation with good examples
- `php/php_language.c` - Shows namespace handling

**For build configuration:**
- `Makefile` - Main build file (see how each language is integrated)
- `scratch/tools/Makefile` - Tool-specific builds

**For shared utilities:**
- `shared/string_utils.h` - String manipulation helpers
- `shared/parse_result.h` - Result data structures
- `shared/filter.h` - Symbol filtering
- `shared/constants.h` - Shared constants including context types

## Implementation Time Estimate

Based on Python implementation:

- Directory/config setup: 15 minutes
- Tree-sitter integration: 15 minutes
- AST explorer creation: 30 minutes
- Test file creation/organization: 30 minutes
- Header and main entry: 15 minutes
- Basic parser implementation: 3-5 hours (with AST explorer)
- Makefile integration: 15 minutes
- Testing and iteration: 2-4 hours

**Total: 6-10 hours** for a working implementation with common symbol types.

Additional time needed for:
- Advanced language features
- Edge case handling
- Performance optimization
- Real-world testing

## Tips for Success

1. **Use the AST explorer religiously** - It's your Rosetta Stone
2. **Create comprehensive test files** - Cover each language feature in `scratch/sources/`
3. **Start simple** - Get basic symbols working first
4. **Test frequently** - Build and test after each major addition
5. **Use qi to verify** - Dogfood the tool while building it
6. **Reference existing code** - Don't reinvent patterns
7. **Fix warnings immediately** - Compiler warnings often indicate real issues
8. **Commit incrementally** - Don't wait until everything is perfect
9. **Document as you go** - Note tricky node types and patterns
10. **Ask for help** - The tree-sitter Discord is helpful for grammar questions

## Common Pitfalls

- **Not using the AST explorer** - Trying to guess node types leads to wasted time
- **Not creating targeted test files** - Testing with large files makes debugging harder
- Forgetting to handle both scanner.c and scanner.cc (some grammars use C++)
- Not updating all parts of the Makefile (especially `clean` target)
- Indexing language keywords (that's why keywords.txt is important)
- Not filtering short symbols or pure numbers
- Missing nested symbol contexts (methods inside classes)
- Not extracting parent relationships for member access
- **Deferring compiler warnings** - Always fix immediately

## Next Steps

After successfully implementing a language:

1. Index real-world projects to find edge cases
2. Use the comprehensive test files in `scratch/sources/` to verify all features
3. Share implementation patterns that could benefit other languages
4. Document any language-specific quirks for future maintainers
5. Consider contributing improvements to tree-sitter grammar if needed

---

## Appendix: Python Implementation Example

This appendix documents the actual step-by-step commands and code used to implement Python support, serving as a concrete reference for future language implementations.

### A1. Initial Setup

**Verify tree-sitter-python exists:**
```bash
ls tree-sitter-python/src/
# Output: parser.c, scanner.c, grammar.json, node-types.json, tree_sitter/
```

**Check the parser version:**
```bash
head -1 tree-sitter-python/src/parser.c
# Output: /* Automatically @generated by tree-sitter v0.25.9 */
```

### A2. Create AST Explorer

**Create `scratch/tools/ast-explorer-python.c`:**

The file is ~90 lines and follows the exact pattern of other explorers (see Step 3 in main guide).

**Update `scratch/tools/Makefile`:**

Add three changes:
```makefile
# Add parser source variable
PYTHON_PARSER_SRC = ../../tree-sitter-python/src/parser.c ../../tree-sitter-python/src/scanner.c

# Add to AST_EXPLORERS list
AST_EXPLORERS = ast-explorer-go ast-explorer-c ast-explorer-ts ast-explorer-php ast-explorer-python

# Add build rule
ast-explorer-python: ast-explorer-python.c $(PYTHON_PARSER_SRC)
	$(CC) $(CFLAGS) $< $(PYTHON_PARSER_SRC) -o $@ $(LDFLAGS)
```

**Important:** The scratch/tools/Makefile needs the correct library paths:
```makefile
CFLAGS = -O3 -std=c11 -I/usr/local/include -I/usr/include -I../.. -Wall -Wextra
LDFLAGS = -L/usr/local/lib -Wl,-rpath,/usr/local/lib -ltree-sitter
```

**Build the explorer:**
```bash
cd scratch/tools
make ast-explorer-python
```

**Test it:**
```bash
./scratch/tools/ast-explorer-python scratch/sources/python/classes.py | head -50
```

You should see the AST structure. Use this tool constantly during development!

### A3. Create Directory and Config Files

**Create directory:**
```bash
mkdir -p python/config
```

**Create `python/config/file_extensions.txt`:**
```
.py
.pyw
.pyi
```

**Create `python/config/ignore_files.txt`:**
```
__pycache__
.venv
venv
env
.env
.pytest_cache
.mypy_cache
.tox
.nox
dist
build
*.egg-info
.eggs
.Python
htmlcov
.coverage
.cache
node_modules
```

**Create `python/config/keywords.txt`:**
```
False
None
True
and
as
assert
async
await
break
class
continue
def
del
elif
else
except
finally
for
from
global
if
import
in
is
lambda
nonlocal
not
or
pass
raise
return
try
while
with
yield
self
cls
```

**Note:** Include `self` and `cls` in keywords to avoid indexing them as regular parameters.

### A4. Create Header File

**Create `python/python_language.h`:**

```c
#ifndef PYTHON_LANGUAGE_H
#define PYTHON_LANGUAGE_H

#include "../shared/parse_result.h"
#include "../shared/filter.h"
#include <tree_sitter/api.h>

typedef struct {
    TSParser *parser;
    SymbolFilter *filter;
} PythonParser;

int parser_init(PythonParser *parser, SymbolFilter *filter);
int parser_parse_file(PythonParser *parser, const char *filepath, const char *project_root, ParseResult *result);
void parser_free(PythonParser *parser);

#endif // PYTHON_LANGUAGE_H
```

This is nearly identical across all languages - just change `Python` to your language name.

### A5. Create Main Entry Point

**Create `python/index-python.c`:**

This file is pure boilerplate (~45 lines). The only changes needed:
- Replace `Python` with your language name (capitalized) in type names
- Replace `python` with your language name (lowercase) in paths
- Update `.name` and `.data_dir` in the config struct

See Step 7 in the main guide for the complete template.

### A6. Implement the Parser

**Create `python/python_language.c`:**

This is the complex part (~450 lines). Implementation strategy:

1. **Start with the structure and includes:**
   ```c
   #include "python_language.h"
   #include "../shared/constants.h"
   #include "../shared/string_utils.h"
   #include "../shared/comment_utils.h"
   #include "../shared/file_opener.h"
   #include "../shared/file_utils.h"      // For get_relative_path()
   #include "../shared/filter.h"          // For filter_should_index(), filter_clean_string_symbol()
   #include <stdio.h>
   #include <stdlib.h>
   #include <string.h>
   #include <ctype.h>

   /* Declare the tree-sitter Python language */
   TSLanguage *tree_sitter_python(void);
   ```

2. **Implement handlers one at a time:**
   - Start with `handle_function_definition`
   - Test with: `./qi % -i func`
   - Add `handle_class_definition`
   - Test with: `./qi % -i class`
   - Continue with other handlers

3. **Use AST explorer to discover node types:**
   ```bash
   # See how Python represents a function
   echo 'def foo(x: int) -> str: return str(x)' > /tmp/test.py
   ./scratch/tools/ast-explorer-python /tmp/test.py

   # Output shows:
   # - function_definition node
   # - name field with identifier
   # - parameters field
   # - return_type field
   # - body field
   ```

4. **Key handlers implemented for Python:**
   - `handle_function_definition` - Functions and methods
   - `handle_class_definition` - Class declarations
   - `handle_assignment` - Variable assignments and attributes
   - `handle_import_statement` - Import statements
   - `handle_import_from_statement` - From-import statements
   - `handle_string` - String literals (with inline word extraction)
   - `handle_comment` - Comments (with inline word extraction)

5. **Critical implementation details:**

   **a. Correct constant names (from `shared/constants.h`):**
   - Use `SYMBOL_MAX_LENGTH` (not MAX_SYMBOL_LENGTH)
   - Use `DIRECTORY_MAX_LENGTH` (not MAX_PATH_LENGTH)
   - Use `FILENAME_MAX_LENGTH` (not MAX_FILENAME_LENGTH)
   - Use `COMMENT_TEXT_BUFFER` (not MAX_COMMENT_LENGTH)
   - Use `CLEANED_WORD_BUFFER` (for word processing)

   **b. Correct context type constants:**
   - Use `CONTEXT_FUNCTION_NAME` (not CONTEXT_FUNCTION)
   - Use `CONTEXT_CLASS_NAME` (not CONTEXT_CLASS)
   - Use `CONTEXT_VARIABLE_NAME` (not CONTEXT_VARIABLE)
   - Use `CONTEXT_PROPERTY_NAME` (not CONTEXT_PROPERTY)
   - Use `CONTEXT_FUNCTION_ARGUMENT` (not CONTEXT_ARGUMENT)
   - Use `CONTEXT_IMPORT`, `CONTEXT_STRING`, `CONTEXT_COMMENT` (unchanged)

   **c. Correct function names:**
   - Use `get_relative_path()` (not extract_directory_and_filename)
   - Use `format_source_location()` for expand (-e) support
   - Use `filter_should_index()` to check if symbol should be indexed
   - Use `filter_clean_string_symbol()` to clean words from strings/comments
   - Use `strip_comment_delimiters()` to clean comment text

   **d. parse_result_add_entry signature:**
   ```c
   parse_result_add_entry(result, symbol, line_number, context_type,
                          directory, filename, source_location,
                          &(ExtColumns){...});
   ```

   **e. ExtColumns structure (from `shared/column_schema.def`):**
   - `.parent` - Parent symbol (e.g., for parameters)
   - `.type` - Type annotation (e.g., return type, parameter type)
   - `.definition` - Set to "1" for definitions (functions, classes)
   - `.scope`, `.modifier`, `.clue`, `.namespace` - Other available fields
   - Use `NO_EXTENSIBLE_COLUMNS` when no extra metadata needed

6. **Important Python-specific details:**
   - Skip `self` and `cls` parameters (Python conventions)
   - Handle `typed_parameter`, `default_parameter`, `typed_default_parameter`
   - Extract type annotations from type hints
   - Distinguish between simple assignment and attribute assignment (`self.x = ...`)
   - Add source locations for functions and classes to enable `-e` (expand) flag

7. **String and comment handlers:**
   - Extract text from node
   - Split on whitespace to get individual words
   - Clean each word with `filter_clean_string_symbol()`
   - Check if indexable with `filter_should_index()`
   - For comments, use `strip_comment_delimiters()` first
   - Add each cleaned word to results with `CONTEXT_STRING` or `CONTEXT_COMMENT`

8. **Function/class handlers must include source location:**
   - Call `format_source_location(node, location, sizeof(location))`
   - Pass location to `parse_result_add_entry()` as 6th parameter
   - This enables the `-e` (expand) flag in queries
   - Without it, users can't see full function/class definitions

See `python/python_language.c` for complete implementation examples of all handlers.

### A7. Update Main Makefile

**Add grammar directory variable** (after line 11):
```makefile
PYTHON_GRAMMAR_DIR = tree-sitter-python
```

**Update include paths** (lines 17 and 23):
```makefile
# macOS:
INCLUDE_PATHS = ... -I$(PYTHON_GRAMMAR_DIR)/src

# Linux:
INCLUDE_PATHS = ... -I$(PYTHON_GRAMMAR_DIR)/src
```

**Add Python-specific variables** (after Go section, ~line 97):
```makefile
# Python language files
PYTHON_TREE_SITTER_SRC = $(PYTHON_GRAMMAR_DIR)/src/parser.c $(PYTHON_GRAMMAR_DIR)/src/scanner.c
PYTHON_TREE_SITTER_OBJ = $(PYTHON_TREE_SITTER_SRC:.c=.o)

PYTHON_LANGUAGE_SRC = python/python_language.c
PYTHON_LANGUAGE_OBJ = $(PYTHON_LANGUAGE_SRC:.c=.o)

PYTHON_MAIN_SRC = python/index-python.c
PYTHON_MAIN_OBJ = $(PYTHON_MAIN_SRC:.c=.o)
```

**Update `all` target** (line 116):
```makefile
all: ... $(BUILD_DIR)/index-python ... ./index-python ...
```

**Add build rule** (after Go indexer, ~line 138):
```makefile
# Python indexer
$(BUILD_DIR)/index-python: $(SHARED_OBJ) $(PYTHON_TREE_SITTER_OBJ) $(PYTHON_LANGUAGE_OBJ) $(PYTHON_MAIN_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
```

**Add symlink target** (after index-go, ~line 159):
```makefile
./index-python: $(BUILD_DIR)/index-python
	ln -sf $(BUILD_DIR)/index-python index-python
```

**Update `clean` target** (line 173):
```makefile
clean:
	rm -f ... $(PYTHON_TREE_SITTER_OBJ) $(PYTHON_LANGUAGE_OBJ) $(PYTHON_MAIN_OBJ) ...
	...
	rm -f ... index-python ...
```

### A8. Build and Initial Testing

**Build everything:**
```bash
make
```

**Common compilation errors and fixes:**

1. **Undeclared constants:**
   ```
   error: 'MAX_SYMBOL_LENGTH' undeclared
   ```
   Fix: Use `SYMBOL_MAX_LENGTH` instead (see A6 section 5a for complete list)

2. **Undeclared context types:**
   ```
   error: 'CONTEXT_ARGUMENT' undeclared
   ```
   Fix: Use `CONTEXT_FUNCTION_ARGUMENT` instead (see A6 section 5b for complete list)

3. **Wrong function signature:**
   ```
   error: too many arguments to function 'parse_result_add_entry'
   ```
   Fix: Update to new signature - order is: `symbol, line, context, directory, filename, source_location, ext`

4. **Undefined references (linking errors):**
   ```
   undefined reference to 'extract_words_from_comment'
   undefined reference to 'extract_directory_and_filename'
   ```
   Fix:
   - Implement string/comment handlers inline (see A6 section 7)
   - Use `get_relative_path()` instead of `extract_directory_and_filename`
   - Add missing includes: `file_utils.h`, `filter.h`

**Fix all compiler warnings immediately.** Other common issues:
- Unused variables
- Missing return statements
- Type mismatches (use `size_t` for lengths, not `uint32_t`)

**Test basic functionality:**
```bash
# Remove old database (important after code changes)
rm -f code-index.db code-index.db-shm code-index.db-wal

# Index the Python test files with --once flag
./index-python scratch/sources/python/ --once

# View what was indexed
./qi % -x noise --limit 30
```

**Expected output:**
```
Indexing complete: 17 files processed
```
You should see functions, classes, variables, imports, etc. from the test files.

### A9. Verification Tests

**Test each context type:**
```bash
# Functions
./qi % -i func -x noise
# Should show: __init__, greet, increment, area, perimeter, speak, etc.

# Classes
./qi % -i class -x noise
# Should show: User, Counter, Rectangle, Animal, Dog, etc.

# Variables
./qi % -i var -x noise
# Should show: total_count, active, count, width, height, name, etc.

# Properties (attribute assignments)
./qi % -i prop -x noise
# Should show: name, email, active, count, width, height, etc.

# Parameters
./qi % -i arg -x noise
# Should show: name, email, width, height, etc. (but NOT self or cls)
```

**Test type extraction:**
```bash
./qi name -i arg -v
# Should show type annotations like "str" for typed parameters
```

**Test expand functionality:**
```bash
./qi User -i class -e
# Should show the complete class definition

./qi __init__ -i func -e --limit 3
# Should show complete __init__ methods
```

**Test file filtering:**
```bash
./qi % -f classes.py --limit 20
# Should only show symbols from classes.py
```

### A10. Testing with Real Code

**Index a real Python project:**
```bash
# Example with a Python web framework
./index-python ~/projects/flask-app/

# Query for common patterns
./qi % -i class --limit 20
./qi authenticate -i func -e
./qi User% -i class
```

**Common symbols to verify:**
- Async functions (`async def`)
- Decorators (`@property`, `@staticmethod`)
- Type hints
- Class inheritance
- Import patterns

### A11. Files Created Summary

For Python support, we created:

**New files:**
- `python/python_language.h` (17 lines)
- `python/python_language.c` (463 lines)
- `python/index-python.c` (45 lines)
- `python/config/file_extensions.txt` (3 lines)
- `python/config/ignore_files.txt` (17 lines)
- `python/config/keywords.txt` (38 lines)
- `scratch/tools/ast-explorer-python.c` (93 lines)

**Modified files:**
- `Makefile` (7 locations modified)
- `scratch/tools/Makefile` (3 locations modified)

**Total new code:** ~676 lines (excluding config files)

### A12. Lessons Learned from Python Implementation

1. **AST explorer is essential** - Used it 20+ times while implementing handlers
2. **Test files already existed** - Having comprehensive `scratch/sources/python/` files saved hours
3. **Start with minimal handlers** - Implemented functions and classes first, then expanded
4. **Parameter handling is tricky** - Python has many parameter types (typed, default, typed_default)
5. **Skip language conventions** - Filtering `self` and `cls` improves index quality
6. **Type hints are valuable** - Extracting type annotations provides useful context
7. **Attributes vs variables** - Distinguishing `x = 1` from `self.x = 1` matters

### A13. Python Advanced Features Summary

The following advanced features have been successfully implemented:

**✅ Implemented:**
- **Async functions** - `modifier` column captures "async" for `async def` functions
- **Await expressions** - `modifier` column captures "await" for awaited function calls
- **Function calls** - `CONTEXT_CALL_EXPRESSION` indexes all function/method calls
- **Method call parents** - `parent` column tracks object for method calls (e.g., `asyncio.sleep`)
- **Decorators** - `clue` column captures decorators with `@` prefix (e.g., `@property`, `@staticmethod`)
- **Multiple decorators** - Comma-separated when stacked (e.g., `@decorator1,@decorator2`)

**Query examples:**
```bash
./qi % -i func -m async              # Find all async functions
./qi % -i call -m await              # Find all awaited calls
./qi % -i func -c @property          # Find all @property methods
./qi % -i func -c @staticmethod      # Find all static methods
./qi sleep -i call -p asyncio        # Find asyncio.sleep calls
```

**Future enhancements (not yet implemented):**
- Lambda functions
- List/dict/set comprehensions
- Generator expressions
- Exception type extraction from try/except
- Context manager types from with statements
- Enum member definitions
- Dataclass field extraction
- Type alias definitions

### A14. Critical Implementation Patterns

**1. The visit_node Pattern (MOST IMPORTANT)**

The `visit_node` function must **return immediately** after calling a handler. This prevents duplicate indexing:

```c
/* CORRECT pattern (Go, Python) */
static void visit_node(...) {
    /* Look up handler in dispatch table */
    for (int i = 0; node_handlers[i].node_type != NULL; i++) {
        if (strcmp(node_type, node_handlers[i].node_type) == 0) {
            node_handlers[i].handler(...);
            return;  /* ← CRITICAL: Return immediately! */
        }
    }
    /* No handler found, process children */
    process_children(...);
}
```

**Why this matters:**
- If a node has a handler, that handler decides which children to process
- If no handler exists, `visit_node` processes children automatically
- Without the `return`, children get processed twice → duplicates in database

**2. Handler Responsibilities**

Each handler should:
- Extract the symbol/identifier from the node
- Call `process_children()` on specific child nodes it wants to traverse (like function bodies, class bodies)
- NOT call `process_children()` on nodes that will be auto-visited (like arguments in a call expression)

**3. Advanced Features Implementation**

**Modifiers (language keywords):**
- Check child nodes for keywords: `async`, `static`, `const`, etc.
- Store in `modifier` column
- Can also check parent node (for `await` before a call expression)

**Decorators/Annotations:**
- Check parent node for `decorated_definition` wrapper
- Extract decorator names and store in `clue` column
- Include syntax prefix (`@` for Python, `#[...]` for Rust, etc.)

**Parent tracking:**
- For method calls like `obj.method()`, extract both method name and object
- Store object in `parent` column
- Helps with queries like "find all calls to asyncio.sleep"

**4. Common Pitfalls**

- **Not returning from visit_node** → duplicate entries
- **Calling process_children on wrong nodes** → missing or duplicate symbols
- **Using wrong constant names** → compilation errors (check `shared/constants.h`)
- **Missing source_location** → expand flag (`-e`) won't work
- **Not filtering language keywords** → index bloat

**5. Verification Checklist**

After implementation:
```bash
# Check for duplicates (should return nothing)
sqlite3 code-index.db "SELECT filename, line_number, symbol, COUNT(*) as cnt
  FROM code_index GROUP BY filename, line_number, symbol
  HAVING COUNT(*) > 1 LIMIT 10"

# Verify expected symbols are captured
./qi % -i func --limit 20
./qi % -i class --limit 20
./qi % -i var --limit 20

# Test expand flag works
./qi SomeFunction -i func -e

# Test file filtering
./qi % -f test.py

# Test real-world code
./index-<language> ~/projects/real-project/
```

### A15. Implementing New Features

**Workflow for adding a handler:**
1. Create focused test file in `scratch/sources/<language>/`
2. Use AST explorer to understand tree-sitter structure
3. Write handler function
4. Add to dispatch table: `{"node_type", handle_function}`
5. Rebuild and test
6. Verify with `./qi` queries

**Example: Adding decorator support**
- Test file: `scratch/sources/python/decorators.py`
- AST explorer: Identify `decorated_definition` and `decorator` node types
- Implementation: Create `extract_decorators()` helper function
- Storage: Use `clue` column with `@` prefix
- Testing: `./qi % -i func -c @property`

For complete implementation examples, see `python/python_language.c`.
