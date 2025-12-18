# When to Use qi vs grep

## Quick Decision

```
Looking for CODE SYMBOLS (function, variable, class, type)?     → qi
Looking for TEXT/ERROR MESSAGES (strings, comments)?            → grep
Need REGEX patterns or complex text matching?                   → grep
Searching NON-CODE FILES (markdown, config, logs)?              → grep
Exploring CODE STRUCTURE (imports, relationships)?              → qi
```

---

## Configuration Tip

**Add to `~/.smconfig` for cleaner results:**
```
[qi]
-x noise
```
This excludes comments and strings by default, reducing false positives significantly.

---

## When qi Excels

### 1. Symbol Search
Find functions, variables, types, and calls with context awareness.

```bash
./qi database              # Find all references
./qi init% -i func         # Only function definitions
./qi malloc -i call        # Only function calls
./qi RowData -i type       # Struct/type definitions
```

### 2. Full Definitions with -e
View entire function/struct bodies without opening files.

```bash
./qi db_init --def -e                    # Show full function
./qi % -i func -m static -e --limit 5    # All static functions
./qi User -i type -e                     # Complete struct definition
```

**Key feature:** `-e` expands definitions—one of qi's most powerful capabilities.

### 3. Precision Filters
Target exact symbol types with metadata filters impossible in grep.

```bash
./qi % -i func -m static --limit 10      # Find static functions
./qi % -i arg -t "int" --limit 10        # Find int arguments
./qi getUserById --def                   # Definition only
./qi getUserById --usage                 # Usages only
./qi % -i func -p UserManager            # Functions in specific class
./qi count -p patterns                   # Find patterns->count member access
./qi init -p 'User%'                     # All init methods on User objects
```

**Filters:** `-t` (type), `-m` (modifier), `-s` (scope), `-p` (parent), `--def`, `--usage`, `--within`

**Parent filter (`-p`):** Finds member access like `obj->field`, `obj.method()`, even `array[i].field`

### 4. Code Context
Show surrounding lines like grep, but with symbol highlighting and all qi filters.

```bash
./qi fprintf -i call -C 3     # 3 lines context
./qi malloc -A 5 --limit 5    # 5 lines after
./qi init -i func -B 2        # 2 lines before
```

### 5. File Filtering
Narrow searches to specific files or directories with SQL LIKE patterns.

```bash
./qi user -f %.c            # Only .c files
./qi user -f src/%          # Only src/ directory
./qi user -f database.c     # Specific file
./qi user -f shared/%.c     # .c files in shared/
```

### 6. Files-Only Output
Faster and cleaner than `grep -l`.

```bash
./qi database --files                     # List files with symbol
./qi fprintf -i call --files              # Files with calls
./qi % -i func -m static -f %.c --files   # Combined filters
```

### 7. Multi-Symbol Search
Find lines with multiple symbols (order-independent).

```bash
./qi fprintf stderr --and          # Both symbols on same line
./qi malloc NULL --and             # Memory checks
./qi SELECT transaction --and      # SQL patterns
```

**Note:** Shows each symbol separately, not combined.

### 8. Structure Exploration
Navigate codebases by symbol types and relationships.

```bash
./qi % -i imp --limit 50       # All imports
./qi % -i func --limit 20      # All functions
./qi % -f database.c           # All symbols in file
./qi % -i class --limit 20     # All classes
```

### 9. Wildcard Searches
SQL LIKE patterns (`%`, `_`) for flexible symbol matching.

```bash
./qi get% -i func       # Functions starting with "get"
./qi %Error             # Symbols ending with "Error"
./qi %user% --limit 20  # Symbols containing "user"
```

---

## When grep Excels

### 1. Literal Text
Error messages, comments, multi-word phrases, string constants.

```bash
grep "Error: Database connection failed" *.c
grep -r "TODO" --include="*.c"
grep "Initializing database" *.c
```

### 2. Regex Patterns
Complex patterns, syntax matching, character classes.

```bash
grep "^static void" *.c                   # Function signatures
grep -E "[0-9]{3,}" *.c                   # Numeric constants
grep -E "\*[a-z_]+\s*=" *.c               # Pointer declarations
grep -Pzo "SELECT[\s\S]*?FROM" *.c        # Multi-line patterns
```

### 3. Non-Code Files
Documentation, configs, build scripts—anything qi doesn't index.

```bash
grep -r "installation" docs/
grep "database_url" config/*.conf
grep "CFLAGS" Makefile
```

### 4. Fuzzy Text Matching
Partial matches, exploratory searches, text anywhere.

```bash
grep -r "database" src/                # Including comments
grep -E "initializ(e|ing|ed)" *.c      # Variations
```

---

## Hybrid Workflows

**qi for Discovery → grep for Details**
```bash
./qi initialize -i func                      # Find function
grep -C 10 "initialize()" database.c         # See full context
```

**grep for Text → qi for Symbols**
```bash
grep -n "Database connection failed" *.c     # Find error
./qi % -f database.c | grep "245:"           # Symbols near line
```

**qi for Structure → grep for Comments**
```bash
./qi database_connect --def -e               # Function definition
grep -B 5 "database_connect" database.c | grep "^/\*"  # Documentation
```

---

## Common Patterns

**Member Access (Parent Filter):**
```bash
./qi count -p patterns                   # Find patterns->count or patterns.count
./qi 'field%' -p 'config%'               # All fields on config objects
./qi directory -p 'file_filter%'         # file_filter->patterns[i].directory
./qi '*' -p UserManager --limit 20       # All members of UserManager
```

**Type-Based Refactoring:**
```bash
./qi % -i arg -t 'OldType%' -f %.c       # Find old type usage (refactor target)
./qi % -i var -t 'int *'                 # All int pointer variables
./qi % -t 'uint32_t' --limit 20          # All uint32_t usage
```

**Scoped Searching:**
```bash
./qi malloc --within handle_request      # malloc only in handle_request
./qi fprintf --within 'debug%'           # fprintf in debug functions
./qi % -i var --within main              # Variables in main
```

**Combining Filters:**
```bash
./qi % -i func -m static -f shared/%.c --limit 10    # Static funcs in shared/
./qi % -i arg -t 'int *' -m static                   # Static int pointer args
./qi count -p patterns -C 3                          # Show patterns.count with context
```

**Two-Step Workflow:**
```bash
./qi auth --limit 10                    # Quick discovery
./qi auth -i func -f auth.c -C 5        # Detailed exploration
```

**Understanding Schema:**
```bash
./qi fprintf -v --limit 3               # Show all metadata columns
# Output: LINE | SYM | PAR | SCOPE | MOD | CLUE | NS | TYPE | D | CTX
```

---

## Quick Reference

| Task | Command |
|------|---------|
| Find function definition | `./qi funcName --def` |
| View full function body | `./qi funcName --def -e` |
| Find variable references | `./qi varName -i var` |
| Find with code context | `./qi sym -C 5` |
| Find error message | `grep "Error:" *.c` |
| Find regex pattern | `grep -E "pattern" *.c` |
| Find two symbols together | `./qi sym1 sym2 --and` |
| Find all imports | `./qi % -i imp` |
| List files with symbol | `./qi sym --files` |
| Search documentation | `grep -r "topic" docs/` |
| Find static functions | `./qi % -i func -m static` |
| Find only usages | `./qi sym --usage` |

---

## Mental Models

**qi = "Symbol Navigator"**
- Input: Symbol names (code identifiers)
- Output: Symbol locations with rich metadata
- Strength: Code structure and relationships
- Limitation: No regex, no arbitrary text search

**grep = "Text Finder"**
- Input: Any text pattern or regex
- Output: Matching lines in files
- Strength: Maximum flexibility, finds anything
- Limitation: No code structure awareness, noisy results

---

## Key Points

1. **Config file recommended**: Add `-x noise` to `~/.indexerconfig` to exclude comments/strings by default
2. **Context support**: qi's `-C`, `-A`, `-B` flags work excellently with symbol highlighting
3. **Files-only**: qi's `--files` is faster and cleaner than `grep -l`
4. **Expand definitions**: Use `-e` to see complete function/struct bodies
5. **Refinement filters**: Use `-t`, `-m`, `-s`, `-p`, `--def`, `--usage` for precision
6. **Complementary tools**: qi for symbols, grep for text—use both strategically

---

## Golden Rule

**Symbol? Use qi. Text? Use grep.**

When in doubt, try qi first—easier to fall back to grep than wade through grep noise for code symbols.
