# C Indexer Improvements

This document outlines missing features, limitations, and proposed improvements for the C language indexer. The goal is to bring C support to the same level of sophistication as Python and Go, with additional C-specific semantic understanding.

---

## Executive Summary

The C indexer currently provides basic symbol tracking (functions, variables, types) but lacks deep semantic understanding of C-specific constructs that are critical for effective code navigation:

- **No macro support** (major gap)
- **No pointer/dereference tracking** (C's defining feature)
- **No type cast tracking**
- **Limited type information utilization**
- **No call graph support**
- **Overly aggressive keyword filtering**

Python has decorators, async/await, yield, walrus operator. Go has goroutines, channels, defer. C needs pointers, macros, casts, sizeof, and memory semantics.

---

## Priority 1: Critical Missing Features

### 1. Preprocessor Macro Support

**Current State**: Macros are completely ignored despite being fundamental to C.

**Why It Matters**:
- Macros define constants, inline functions, conditional compilation
- Understanding macro usage is critical for understanding C code
- Header files are full of macros that control behavior

**Proposed Implementation**:

```c
// New context types
CONTEXT_MACRO_DEFINE    // #define MAX_SIZE 1024
CONTEXT_MACRO_FUNCTION  // #define MIN(a,b) ((a)<(b)?(a):(b))
CONTEXT_MACRO_USAGE     // Usage of macro in code

// New clue types
"define"     // Macro definition site
"undef"      // #undef directive
"ifdef"      // Conditional compilation
"ifndef"
"if"         // #if directives
"else"
"elif"
"endif"
```

**Query Examples**:
```bash
# Find macro definitions
qi 'MAX_%' -i macro --def

# Find where macro is used
qi 'MAX_BUFFER_SIZE' -i macro --usage

# Find function-like macros
qi '%' -i macro --with-params

# Find all #ifdef guards
qi '%' -c ifdef --limit 30

# Find conditional compilation
qi 'DEBUG' -c ifdef -c ifndef

# See macro expansion sites
qi 'MIN' -i macro --usage -C 3
```

**Implementation Notes**:
- Tree-sitter C grammar already parses preprocessor directives
- Node types: `preproc_def`, `preproc_function_def`, `preproc_ifdef`, etc.
- Track both definition site and usage sites
- Consider tracking macro arguments separately
- Handle multi-line macros (backslash continuation)

**Test Cases Needed**:
```c
#define MAX_SIZE 1024
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#ifdef DEBUG
#define LOG(msg) printf("[DEBUG] %s\n", msg)
#else
#define LOG(msg)
#endif

int buffer[MAX_SIZE];  // Should find MAX_SIZE as usage
int x = MIN(10, 20);   // Should find MIN as usage
LOG("test");           // Should find LOG as usage
```

---

### 2. Pointer Operations Tracking

**Current State**: No tracking of pointer-specific operations.

**Why It Matters**:
- Pointers are C's defining feature
- Understanding pointer usage is critical for debugging
- Memory safety depends on pointer analysis
- Pointer arithmetic is a common source of bugs

**Proposed Implementation**:

```c
// New clue types
"deref"        // *ptr (dereference)
"addr_of"      // &var (address-of)
"arrow"        // ptr->field (arrow operator)
"dot"          // struct.field (dot operator, for comparison)
"ptr_arith"    // ptr++, ptr+n (pointer arithmetic)
"sizeof"       // sizeof(type) or sizeof(var)
"cast"         // (Type *)ptr
```

**Query Examples**:
```bash
# Find all pointer dereferences
qi '%' -c deref --limit 30

# Find address-of operations
qi '%' -c addr_of

# Find arrow operator usage (pointer field access)
qi '%' -c arrow --limit 20

# Find pointer arithmetic
qi '%' -c ptr_arith

# Find where specific pointer is dereferenced
qi 'conn' -c deref --within 'handle_request'

# Find sizeof usage
qi '%' -c sizeof --limit 30

# Find type casts
qi '%' -c cast --limit 20
qi 'void' -c cast  # Find void* casts specifically
```

**Implementation Notes**:
- Track `pointer_expression` nodes (dereference)
- Track `unary_expression` with `&` operator (address-of)
- Track `field_expression` to distinguish `->` from `.`
- Track `sizeof_expression` nodes
- Track `cast_expression` nodes
- Consider tracking pointer arithmetic operators: `++`, `--`, `+`, `-` on pointer types

**Test Cases Needed**:
```c
int *ptr = &value;        // addr_of on value
int x = *ptr;              // deref on ptr
struct Node *node = ...;
node->next = NULL;         // arrow on node
int size = sizeof(int);    // sizeof
void *p = (void *)ptr;     // cast
ptr++;                     // ptr_arith
```

---

### 3. Call Graph Support

**Current State**: Can find function calls but no relationship tracking.

**Why It Matters**:
- Understanding call chains is critical for debugging
- Finding "who calls this" is a daily task
- Dead code detection requires call graph
- Performance analysis needs call chains

**Proposed Implementation**:

**New Query Flags**:
```bash
--calls           # Show functions this function calls
--called-by       # Show functions that call this function
--call-depth N    # Limit depth of call graph traversal
--recursive       # Show recursive calls
--call-chain      # Show full call chain from main
```

**Query Examples**:
```bash
# Find what functions parse_config calls
qi 'parse_config' -i func --calls

# Find what calls parse_config
qi 'parse_config' -i func --called-by

# Find call chains to malloc
qi 'malloc' --called-by --call-depth 3

# Find recursive functions
qi '%' -i func --recursive

# Find dead code (never called)
qi '%' -i func --called-by --count 0

# Find call chain from main to specific function
qi 'connect_database' --call-chain-from main

# Find functions that call both malloc and free
qi 'malloc' 'free' --calls-both
```

**Implementation Notes**:
- Requires second indexing pass to build call graph
- Store caller -> callee relationships in new table:
  ```sql
  CREATE TABLE call_graph (
      caller_symbol TEXT,
      caller_file TEXT,
      caller_line INTEGER,
      callee_symbol TEXT,
      call_site_line INTEGER
  );
  ```
- Handle function pointers (best effort)
- Consider memory impact of call graph table
- Make it optional via indexer flag: `--index-calls`

**Complex Cases**:
```c
// Function pointers - challenging but valuable
typedef int (*handler_t)(void);
handler_t handler = &process_request;
handler();  // Should track as call to process_request if possible

// Indirect calls through struct
struct ops {
    int (*read)(void);
    int (*write)(void);
};
ops->read();  // Best effort tracking
```

---

### 4. Type Information Enhancement

**Current State**: Type column exists but is underutilized.

**Why It Matters**:
- Type-based refactoring is common
- Finding all uses of deprecated types
- Understanding API contracts

**Proposed Implementation**:

**Better Type Tracking**:
```c
// Track full type information including:
- Base type (int, char, struct Foo)
- Pointer level (*, **, ***)
- Qualifiers (const, volatile, restrict)
- Array dimensions ([10], [][20])
- Function pointer signatures
```

**Query Examples**:
```bash
# Find all int pointers
qi '%' -i var -t 'int *'

# Find all void pointers
qi '%' -i var -t 'void *'

# Find all const pointers
qi '%' -i var -t 'const %*'

# Find all function pointers
qi '%' -i var -t '%(*%'

# Find all arrays
qi '%' -i var -t '%[%'

# Find functions returning pointers
qi '%' -i func -t '%*'

# Find double/triple pointers
qi '%' -i var -t '%**' '%***'

# Find all uses of deprecated type
qi '%' -t 'OldConnectionType%'
```

**Implementation Notes**:
- Extract full type from declarator
- Normalize type representation for consistent querying
- Handle complex declarators: `int (*(*fp)(int))[10]`
- Store both exact type and normalized form
- Consider separate table for complex type queries

**Test Cases Needed**:
```c
int *ptr;                           // int *
const char *str;                    // const char *
int **matrix;                       // int **
int (*func_ptr)(int, int);          // int (*)(int, int)
struct Node *nodes[10];             // struct Node *[10]
const volatile int * const ptr;     // const volatile int * const
```

---

## Priority 2: Important Enhancements

### 5. Memory Management Analysis

**Current State**: Can find malloc/free but no semantic understanding.

**Why It Matters**:
- Memory leaks are common in C
- Use-after-free bugs are critical
- Double-free detection

**Proposed Implementation**:

**New Query Flags**:
```bash
--alloc-pair          # Find matching malloc/free pairs
--unpaired-alloc      # Find malloc without corresponding free
--unpaired-free       # Find free without malloc
--alloc-sites         # Show all allocation sites for symbol
--free-sites          # Show all free sites for symbol
```

**Query Examples**:
```bash
# Find unpaired allocations (potential leaks)
qi 'buffer' --alloc-pair --unpaired

# Find all allocations without free
qi '%' -c malloc --unpaired-alloc

# Check specific function's memory management
qi 'parse_config' --memory-check

# Find potential double-free
qi 'conn' -c free --count '>1'

# Find use-after-free candidates
qi 'buffer' --used-after-free
```

**Implementation Notes**:
- Track malloc/calloc/realloc/free relationships
- Correlate with variable lifetimes
- Consider control flow (if/else branches)
- Challenging but high value

---

### 6. Control Flow Enhancement

**Current State**: Basic goto/label tracking only.

**Why It Matters**:
- Understanding control flow is critical
- Finding complex/nested logic
- Identifying error handling patterns

**Proposed Implementation**:

**New Clue Types**:
```c
"break"        // break statements
"continue"     // continue statements
"return"       // return statements (currently filtered!)
"ternary"      // ?: operator
"loop"         // for/while/do-while
"nested_loop"  // Loops inside loops
```

**Query Examples**:
```bash
# Find early returns (error handling)
qi 'return' --within 'parse_config'

# Find break statements
qi '%' -c break

# Find nested loops
qi '%' -c nested_loop

# Find ternary operators
qi '%' -c ternary

# Find complex control flow (multiple gotos)
qi '%' -i goto --count '>3' --group-by parent
```

---

### 7. Struct/Union Field Access Analysis

**Current State**: Parent tracking exists but limited.

**Why It Matters**:
- Understanding how structs are used
- Refactoring struct fields
- Finding field access patterns

**Proposed Implementation**:

**Enhanced Parent Tracking**:
```bash
# Find all accesses to specific field
qi 'next' -i prop --accessed

# Find structs with most field accesses
qi '%' -p '%' --group-by parent --order-by count

# Find read vs write access
qi 'count' -p 'list' --access-type write
qi 'count' -p 'list' --access-type read

# Find fields never accessed
qi '%' -i prop --access-count 0
```

**Implementation Notes**:
- Distinguish between field access (read) and assignment (write)
- Track nested field access: `obj->nested->field`
- Handle array of structs: `arr[i].field`

---

### 8. Better Include/Import Analysis

**Current State**: Tracks #include but no dependency analysis.

**Why It Matters**:
- Understanding dependencies
- Finding circular includes
- Unused include detection

**Proposed Implementation**:

**Query Examples**:
```bash
# Find include dependency tree
qi 'database.h' -i imp --dep-tree

# Find circular includes
qi '%' -i imp --circular

# Find unused includes
qi 'string.h' -i imp --unused

# Find missing includes (used but not included)
qi '%' --missing-includes

# Find include hierarchy depth
qi '%' -i imp --depth '>5'
```

---

## Priority 3: Usability Improvements

### 9. Keyword Filtering Policy

**Current Problem**: Too aggressive - filters `sizeof`, `NULL`, `return`, `typedef`, etc.

**Proposed Solutions**:

**Option 1: Whitelist Critical Keywords**
```c
// Allow these keywords to be indexed:
NULL, sizeof, return, typedef, const, static, inline, extern, volatile
```

**Option 2: Context-Specific Filtering**
```bash
# Let context override filtering
qi 'return' -i statement  # New context type: statement
qi 'NULL' -i constant     # New context type: constant
```

**Option 3: Wildcard Bypass**
```bash
# Wildcards override keyword filter (current inconsistency)
qi 'NULL'    # Filtered
qi '%NULL%'  # Works - should be consistent
qi '*NULL*'  # Works - should be consistent
```

**Recommended Approach**: Combination
- Whitelist critical keywords (NULL, sizeof, return)
- Add new context types for language constructs
- Document clearly what's filtered and why
- Make filtering configurable via indexer flag

---

### 10. Type Cast Tracking

**Current State**: No tracking of type conversions.

**Why It Matters**:
- Finding unsafe casts
- Understanding type conversions
- Refactoring type hierarchies

**Query Examples**:
```bash
# Find all casts
qi '%' -c cast --limit 30

# Find void* casts
qi 'void' -c cast

# Find casts to specific type
qi '%' -c cast --to-type 'Connection *'

# Find casts from specific type
qi '%' -c cast --from-type 'void *'

# Find C++ style casts in C code (misuse)
qi 'reinterpret_cast' 'static_cast'
```

---

### 11. Array and Subscript Tracking

**Current State**: No tracking of array access patterns.

**Query Examples**:
```bash
# Find array subscript operations
qi '%' -c subscript

# Find multidimensional array access
qi '%' -c subscript --dimensions '>1'

# Find array declarations
qi '%' -i var -t '%[%'

# Find potential buffer overflows (heuristic)
qi '%' -c subscript --no-bounds-check
```

---

### 12. Compound Literal Support

**Current State**: C99 compound literals not tracked.

**Query Examples**:
```bash
# Find compound literals
qi '%' -c compound

# Example: (struct Point){.x=1, .y=2}
qi 'Point' -c compound
```

---

### 13. Designated Initializer Tracking

**Current State**: C99 designated initializers not tracked specially.

**Query Examples**:
```bash
# Find designated initializers
qi '%' -c designated_init

# Example: struct Point p = {.x = 10, .y = 20};
qi 'Point' -c designated_init
```

---

## Implementation Strategy

### Phase 1: Critical Foundations (3-4 weeks)
1. ✅ Macro definitions and usage tracking
2. ✅ Pointer operations (deref, addr_of, arrow)
3. ✅ sizeof and cast tracking
4. ✅ Better type information extraction

### Phase 2: Advanced Features (4-6 weeks)
5. ✅ Call graph support (major effort)
6. ✅ Memory management pairing
7. ✅ Enhanced control flow tracking

### Phase 3: Polish & Usability (2-3 weeks)
8. ✅ Keyword filtering review
9. ✅ Include dependency analysis
10. ✅ Documentation updates

---

## Test Coverage Required

### Macro Tests
```c
// test-macros.c
#define MAX_SIZE 1024
#define MIN(a,b) ((a)<(b)?(a):(b))
#define SQUARE(x) ((x)*(x))
#ifdef DEBUG
#define LOG(msg) printf("%s\n", msg)
#else
#define LOG(msg)
#endif

int buffer[MAX_SIZE];
int x = MIN(10, 20);
int y = SQUARE(5);
LOG("test");
```

Expected queries:
```bash
qi 'MAX_SIZE' -i macro --def         # Find definition
qi 'MAX_SIZE' -i macro --usage       # Find usage in buffer[MAX_SIZE]
qi 'MIN' -i macro --with-params      # Find function-like macro
qi 'LOG' -i macro --usage            # Find both usage sites
```

### Pointer Tests
```c
// test-pointers.c
int value = 10;
int *ptr = &value;          // addr_of
int x = *ptr;               // deref
ptr++;                      // ptr_arith
struct Node *node = ...;
node->next = NULL;          // arrow
int size = sizeof(int);     // sizeof
void *p = (void *)ptr;      // cast
```

Expected queries:
```bash
qi 'value' -c addr_of
qi 'ptr' -c deref
qi 'ptr' -c ptr_arith
qi 'node' -c arrow
qi '%' -c sizeof
qi '%' -c cast
```

### Type Tests
```c
// test-types.c
int *ptr1;
const char *str;
int **matrix;
int (*func_ptr)(int, int);
struct Node *nodes[10];
```

Expected queries:
```bash
qi '%' -i var -t 'int *'
qi '%' -i var -t 'const char *'
qi '%' -i var -t 'int **'
qi '%' -i var -t '%(*%'        # Function pointers
qi '%' -i var -t '%[%'         # Arrays
```

### Call Graph Tests
```c
// test-calls.c
void leaf() { }
void middle() { leaf(); }
void top() { middle(); }
void recursive() { recursive(); }

int main() {
    top();
    return 0;
}
```

Expected queries:
```bash
qi 'top' --calls              # Shows: middle
qi 'leaf' --called-by         # Shows: middle
qi 'leaf' --call-chain        # Shows: main -> top -> middle -> leaf
qi 'recursive' --recursive    # Detects recursion
```

---

## Documentation Updates Needed

### 1. Update c-guide.md
- Add Preprocessor Directives section
- Add Pointer Operations section
- Add Call Graph Queries section
- Add Memory Management section
- Update Examples with new features

### 2. Update README.md
- Add C-specific clue types
- Update feature comparison table
- Add C examples prominently

### 3. Create Advanced C Patterns Guide
- Memory leak detection patterns
- Call graph analysis workflows
- Macro debugging techniques
- Pointer analysis patterns

---

## Performance Considerations

### Indexing Speed
- Call graph adds second pass: +20-30% indexing time
- Macro tracking adds complexity: +10-15%
- Consider flags to disable expensive features:
  ```bash
  ./index-c --no-call-graph --no-macro-expansion
  ```

### Database Size
- Call graph table can be large (2-3x code size)
- Consider separate database: `code-index-callgraph.db`
- Prune unnecessary relationships

### Query Performance
- Call graph queries may be slow with deep recursion
- Add indexes on caller/callee columns
- Limit default depth to 5 levels
- Cache common query patterns

---

## Similar Tools Comparison

### cscope
- Has call graph support (inspiration)
- Has macro expansion
- Limited query language
- **qi advantage**: Better filtering, modern query syntax

### GNU GLOBAL (gtags)
- Has hypertext references
- Call graph via `global -r`
- **qi advantage**: Semantic understanding, better filters

### ctags
- Very basic symbol listing
- No call graph, no macros
- **qi advantage**: Everything else

### clang AST
- Complete semantic info
- Very complex API
- **qi advantage**: Simple queries, fast, no compilation

---

## Open Questions

1. **Function pointer analysis**: How deep should we go?
   - Best effort? Track declarations but not runtime assignments?
   - Document limitations clearly

2. **Macro expansion**: Should we track expanded form?
   - Just definition and usage sites?
   - Or also track what macros expand to?

3. **Call graph storage**: Separate DB or same DB?
   - Performance vs. convenience trade-off
   - Make it configurable?

4. **Type normalization**: How to represent complex types?
   - `int * const volatile` vs `const volatile int *`
   - Canonical form needed for queries

5. **Backward compatibility**: Breaking changes acceptable?
   - New schema version?
   - Migration tool needed?

---

## Success Metrics

After these improvements, we should be able to answer:

✅ "Where is this macro defined?"
✅ "Who calls this function?" (full call chain)
✅ "Where is this pointer dereferenced?"
✅ "Find all malloc without matching free"
✅ "What type is this variable?" (with full precision)
✅ "Find all casts to void pointer"
✅ "Show me sizeof usage"
✅ "Find recursive functions"
✅ "What functions does main call?" (transitively)
✅ "Find all arrow operators on this struct"

These are fundamental C programming queries that should "just work."

---

## Conclusion

The C indexer has a solid foundation but needs significant C-specific enhancements to match the sophistication of Python and Go support. The priorities are:

1. **Macros** - Can't analyze C without understanding macros
2. **Pointers** - C's defining feature, must track thoroughly
3. **Call graphs** - Essential for understanding any codebase
4. **Types** - Underutilized currently, needs enhancement

With these improvements, qi would become the definitive tool for C code exploration, far surpassing existing tools like cscope, ctags, and GNU GLOBAL in usability while maintaining competitive performance.

The investment is worthwhile: C remains the foundation of systems programming, and developers need better tools to navigate legacy and new C codebases safely and efficiently.
