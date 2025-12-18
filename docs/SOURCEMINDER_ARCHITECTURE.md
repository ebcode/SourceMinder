# SourceMinder Architecture Guide

**Last Updated:** 2025-12-17
**Status:** Living Document

---

## Table of Contents

1. [Overview](#overview)
2. [Core Design Principles](#core-design-principles)
3. [Handler-Based Dispatch Pattern](#handler-based-dispatch-pattern)
4. [Established Pattern: Handler Ownership](#established-pattern-handler-ownership)
5. [TypeScript Skip Mechanism (Deprecated)](#typescript-skip-mechanism-deprecated)
6. [Migration Plan: TypeScript to C-Style](#migration-plan-typescript-to-c-style)
7. [Common Pitfalls](#common-pitfalls)
8. [Adding New Handlers](#adding-new-handlers)
9. [Adding New Language Indexers](#adding-new-language-indexers)
10. [Testing Strategies](#testing-strategies)

---

## Overview

SourceMinder provides language-aware code indexing through tree-sitter AST parsing.
Each language has its own indexer (`<language>/<language>_language.c`) that follows a common architectural pattern.

**Current Language Support:**
- C
- Go
- PHP
- Python
- TypeScript

---

## Core Design Principles

### 1. **Handler Ownership**

Each handler is responsible for:
1. Extracting and indexing symbols it owns (function names, variables, types, etc.)
2. Visiting children that need custom processing (expressions, nested statements)
3. **Nothing more** - handlers should be focused and explicit

**Anti-pattern:** Generic handlers that index everything (e.g., `handle_identifier` that indexes all identifiers)

### 2. **Explicit Child Traversal**

Handlers must explicitly visit their children using:
- `visit_expression()` - for expression nodes (operators, calls, assignments)
- `visit_node()` - for statement nodes (if, while, declarations)
- Manual iteration through children when selective processing is needed

**Anti-pattern:** Relying on implicit traversal or assuming children will be visited automatically

### 3. **Minimal `process_children` Usage**

`process_children()` should only be called:
1. In `visit_node()` fallback (when no handler exists)
2. In specific handlers that genuinely need to visit ALL children after extracting specific symbols

**Current C indexer usage:** Only 2 calls to `process_children`
- `handle_function_definition` - processes function body after extracting function name/params
- `visit_node` - fallback when no handler exists

**Anti-pattern (TypeScript):** 14+ calls to `process_children`, leading to duplicate symbol indexing

### 4. **No Duplicate Indexing**

Each symbol should be indexed exactly once. Duplicates indicate architectural problems:
- Generic handlers re-indexing what specific handlers already indexed
- Handlers calling `process_children` which triggers other handlers on already-processed nodes
- The same symbol appearing multiple times on the same line should be indexed for each occurrence
---

## Handler-Based Dispatch Pattern

All language indexers use an if-else mapping node types to handler functions:

```c
/* Fast symbol-based dispatch - if-else chain with early returns */
    if (node_sym == c_symbols.function_definition) {
        handle_function_definition(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.declaration) {
        handle_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.struct_specifier || node_sym == c_symbols.union_specifier) {
        handle_struct_specifier(node, source_code, directory, filename, result, filter, line);
        return;
    }
    ...
```

**Traversal Flow:**

```
visit_node(node)
  ├─> If handler found:
  │   ├─> Call handler(node, ...)
  │   └─> RETURN (handler owns all child processing)
  └─> If no handler:
      └─> Call process_children(node, ...) [fallback]
```

**Critical Insight:** When a handler is called, `visit_node` returns immediately. The handler MUST explicitly visit its children or they will never be visited.

---

## Established Pattern: Handler Ownership

Based on the C indexer (reference implementation), here is the established pattern:

### Pattern 1: Extract-Then-Visit (Most Common)

**Use Case:** Handler extracts specific symbols, then visits children for nested content

```c
static void handle_function_definition(TSNode node, const char *source_code,
                                       const char *directory, const char *filename,
                                       ParseResult *result, SymbolFilter *filter,
                                       int line_number) {
    // 1. Extract specific symbols this handler owns
    extract_function_name_and_index(...);
    extract_parameters_and_index(...);

    // 2. Find and visit the function body for nested symbols
    TSNode body = find_compound_statement(node);
    if (!ts_node_is_null(body)) {
        process_children(body, source_code, directory, filename, result, filter);
    }
}
```

**Key Points:**
- Handler extracts what it owns (function name, parameters)
- Handler visits children to find nested symbols (local variables, calls in body)
- No duplicates because handler doesn't index generic identifiers

### Pattern 2: Manual Iteration for Selective Processing

**Use Case:** Handler needs different logic for different child types

```c
static void handle_if_statement(TSNode node, const char *source_code,
                                const char *directory, const char *filename,
                                ParseResult *result, SymbolFilter *filter,
                                int line_number) {
    if (g_debug) {
        debug("[handle_if_statement] Line %d: Processing if_statement", line_number);
    }

    // Iterate through all children with type-specific handling
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "parenthesized_expression") == 0) {
            // Condition is an expression - use visit_expression
            visit_expression(child, source_code, directory, filename, result, filter);
        } else if (strcmp(child_type, "compound_statement") == 0 ||
                   strcmp(child_type, "expression_statement") == 0 ||
                   strcmp(child_type, "return_statement") == 0 ||
                   strcmp(child_type, "if_statement") == 0 ||
                   strcmp(child_type, "while_statement") == 0 ||
                   strcmp(child_type, "for_statement") == 0) {
            // Body statements - use visit_node
            visit_node(child, source_code, directory, filename, result, filter);
        }
        // Keywords like "if", "else" are automatically skipped
    }
}
```

**Key Points:**
- Manual iteration with type-based routing
- Expressions go to `visit_expression()`
- Statements go to `visit_node()`
- Keywords/punctuation automatically ignored
- **CRITICAL:** Handler visits ALL children (condition + consequence + alternative)

**Why This Works:**
- Control flow statements don't extract symbols themselves
- They route children to appropriate visit functions
- No risk of missing nested symbols in statement bodies

### Pattern 3: Fallback via Dispatch Table

**Use Case:** Node type has no special processing needs

```c
// NO handler needed! Let visit_node fallback handle it
// The dispatch table simply omits this node type
```

**When to use:**
- Node types with no symbols to extract
- Node types where `process_children` is sufficient
- Generic container nodes

**Example:** `compound_statement` doesn't need a handler - it's just a container for other statements.

---

## TypeScript Skip Mechanism (Deprecated)

### What Was Implemented

The TypeScript indexer added a skip mechanism to prevent duplicate symbol indexing:

```c
// Modified signature
static void process_children(TSNode node, const char *source_code,
                            const char *directory, const char *filename,
                            ParseResult *result, SymbolFilter *filter,
                            const char *skip_handler);  // ← Added parameter

// Skip logic function
static bool should_skip_child(const char *child_type,
                              const char *skip_handler,
                              TSNode parent_node) {
    if (strcmp(skip_handler, "assignment_expression") == 0) {
        // Assignment handler already indexed LHS/RHS
        if (strcmp(child_type, "identifier") == 0) return true;
        if (strcmp(child_type, "member_expression") == 0) return true;
    }
    // ... more skip rules
}
```

**Usage Pattern:**
```c
static void handle_assignment_expression(...) {
    // Index LHS and RHS explicitly
    index_lhs(...);
    index_rhs(...);

    // Visit children but skip identifiers we already indexed
    process_children(node, ..., "assignment_expression");
}
```

### Why This Was Implemented

**Root Cause:** TypeScript indexer used generic handlers:
- `handle_identifier` - indexes ALL identifiers
- `handle_property_identifier` - indexes ALL property accesses

**Problem:** Specific handlers (like `handle_assignment_expression`) index symbols explicitly, then call `process_children`, which triggers generic handlers, creating duplicates.

**Example Duplication:**
```typescript
this.name = value;
```

Without skip mechanism:
1. ✅ `handle_assignment_expression` indexes `name` as PROP (LHS)
2. ✅ `handle_assignment_expression` indexes `value` as VAR (RHS)
3. ❌ `process_children` visits children
4. ❌ `handle_property_identifier` indexes `name` as VAR (DUPLICATE!)
5. ❌ `handle_identifier` indexes `value` as VAR (DUPLICATE!)

Result: **4 entries for 1 line** (should be 2)

### Why This Is Wrong

**Fundamental Design Flaw:** Using generic handlers that index everything.

**Problems:**
1. **Complexity:** Need to maintain skip rules for every specific handler
2. **Fragility:** Easy to under-skip (duplicates) or over-skip (missing symbols)
3. **Unclear ownership:** Multiple handlers can touch the same symbol
4. **Hard to debug:** Difficult to understand why a symbol was/wasn't indexed
5. **Doesn't scale:** Adding new handlers requires updating skip rules

**Evidence:**
- TypeScript has 14 `process_children` calls (C has 2)
- TypeScript needs skip mechanism (C doesn't)
- TypeScript had duplicate indexing issues (C doesn't)

### The Right Solution

**Remove generic handlers entirely.**

Instead of:
```c
// Generic handler - BAD
static void handle_identifier(...) {
    // Index ALL identifiers everywhere
    index_symbol(identifier);
}
```

Do this:
```c
// Specific handlers own their symbols - GOOD
static void handle_variable_declaration(...) {
    // Only index identifiers in variable declarations
    index_variable_name(...);
    // Visit initializer for nested symbols
    visit_expression(initializer, ...);
}

static void handle_assignment_expression(...) {
    // Only index identifiers in assignments
    index_lhs_and_rhs(...);
    // Don't call process_children - we handled what we need
}
```

**Result:** No duplicates, no skip mechanism needed.

---

## Rationale & Design Decisions

### Decision 1: C Indexer as Reference Implementation

**Date:** 2025-11-20
**Context:** Discovered if_statement bug in C indexer, compared with TypeScript

**Observation:**
- C indexer: 2 `process_children` calls, no duplicates
- TypeScript indexer: 14 `process_children` calls, duplicates present

**Analysis:**
- C indexer follows explicit ownership pattern
- TypeScript indexer uses generic handlers + skip mechanism (band-aid solution)
- Both work, but C approach is simpler and more maintainable

**Decision:** Use C indexer pattern as reference for all languages

**Rationale:**
1. **Simpler:** No skip mechanism complexity
2. **Explicit:** Handler owns what it indexes - clear ownership
3. **No duplicates by design:** Specific handlers don't trigger generic handlers
4. **Easier to debug:** Clear control flow, no skip rule interactions
5. **Proven:** C indexer works correctly without duplicates

### Decision 2: Manual Iteration Over Field-Based Access

**Date:** 2025-11-20
**Context:** Fixed if_statement bug, discovered `ts_node_child_by_field_name` unreliability

**Problem:**
```c
// This approach failed
TSNode condition = ts_node_child_by_field_name(node, "condition", 9);
if (!ts_node_is_null(condition)) {
    visit_expression(condition, ...);
}
// BUG: Never visits body!
```

**Solution:**
```c
// Manual iteration works reliably
for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *child_type = ts_node_type(child);

    if (strcmp(child_type, "parenthesized_expression") == 0) {
        visit_expression(child, ...);
    } else if (is_statement(child_type)) {
        visit_node(child, ...);
    }
}
```

**Decision:** Prefer manual iteration over field-based access

**Rationale:**
1. **Reliable:** Child iteration always works
2. **Complete:** Ensures all children are considered
3. **Explicit:** Clear what's being visited vs skipped
4. **Type-based routing:** Natural way to handle different child types
5. **Field access unreliable:** `ts_node_child_by_field_name` sometimes returns null incorrectly

### Decision 3: Handlers Must Visit ALL Children or NONE

**Date:** 2025-11-20
**Context:** If statement only visited condition, not body - symbols missed

**Previous Assumption (WRONG):**
> "Handlers can visit some children, visit_node will visit the rest"

**Reality:**
When a handler is called, `visit_node` returns immediately. If handler doesn't visit children, they're never visited.

**Decision:** Handlers must either:
1. Visit ALL children (via manual iteration or `process_children`)
2. Visit NONE (don't have a handler, let fallback handle it)

**Rationale:**
1. **Prevents missing symbols:** Ensures all AST nodes are visited
2. **Clear ownership:** Handler is responsible for entire subtree
3. **No implicit behavior:** Explicit is better than implicit

### Decision 4: Debug Infrastructure Standardization

**Date:** 2025-11-20
**Context:** Added centralized debug.h for C indexer

**Implemented:**
```c
// shared/debug.h
#define debug(fmt, ...) f_debug_printf(__FILE__, __LINE__, fmt, ##__VA_ARGS__)

// Usage:
if (g_debug) {
    debug("[handle_if_statement] Line %d: Found condition", line_number);
}

// Output:
// c/c_language.c:1211 [handle_if_statement] Line 1600: Found condition
```

**Decision:** Migrate all indexers to centralized debug infrastructure

**Rationale:**
1. **Consistent output:** File:line format across all indexers
2. **Less boilerplate:** Macro handles file/line automatically
3. **Printf-style formatting:** Variadic macro supports format strings
4. **Easier debugging:** Know exactly where debug call originated

**Status:**
- C indexer: Using `debug()` macro
- TypeScript: Still using raw `fprintf(stderr, ...)`
- PHP, Go, Python: Minimal/no debug infrastructure

---

## Common Pitfalls

### Pitfall 1: Handler Doesn't Visit Body

**Symptom:** Symbols inside control flow statements (if, while, for) not indexed

**Example:**
```c
// WRONG
static void handle_if_statement(...) {
    visit_expression(condition);
    // BUG: Forgot to visit body!
}
```

**Fix:**
```c
// RIGHT
static void handle_if_statement(...) {
    // Visit condition AND body
    for each child:
        if (is_condition) visit_expression(child);
        if (is_statement) visit_node(child);
}
```

**How to Detect:** Debug output stops at condition, never shows body symbols

### Pitfall 2: Relying on Field Names

**Symptom:** Handler can't find child nodes via `ts_node_child_by_field_name`

**Example:**
```c
// FRAGILE
TSNode condition = ts_node_child_by_field_name(node, "condition", 9);
// Sometimes returns null even when condition exists!
```

**Fix:**
```c
// RELIABLE
for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    if (strcmp(ts_node_type(child), "parenthesized_expression") == 0) {
        // Found condition by type instead
    }
}
```

**How to Detect:** Handler finds some symbols but not others inconsistently

### Pitfall 3: Generic Handlers Causing Duplicates

**Symptom:** Same symbol indexed multiple times

**Example:**
```c
// BAD PATTERN
static void handle_identifier(...) {
    // This indexes EVERY identifier in the entire file
    index_symbol(identifier);
}

static void handle_variable_declaration(...) {
    extract_variable_name(...);  // Indexes identifier
    process_children(...);        // Triggers handle_identifier - DUPLICATE!
}
```

**Fix:**
```c
// GOOD PATTERN - Remove generic handler
// Let specific handlers own their identifiers
static void handle_variable_declaration(...) {
    extract_variable_name(...);
    // Don't call process_children unless needed for nested content
}
```

**How to Detect:** `./qi symbol -f file.ext` shows multiple entries on same line

### Pitfall 4: Calling process_children After Manual Iteration

**Symptom:** Symbols visited twice, potential duplicates

**Example:**
```c
// WRONG
static void handle_function_definition(...) {
    // Manually extract function name
    extract_function_name(...);

    // Manually iterate to find parameters
    for each child:
        if (is_parameter) extract_parameter(...);

    // BUG: This visits children again!
    process_children(node, ...);
}
```

**Fix:**
```c
// RIGHT - Choose ONE approach
static void handle_function_definition(...) {
    extract_function_name(...);
    extract_parameters(...);

    // Only process the function BODY, not the entire node
    TSNode body = find_compound_statement(node);
    process_children(body, ...);
}
```

**How to Detect:** Debug shows same node visited multiple times

---

## Adding New Handlers

### Step 1: Determine Handler Need

**Ask:**
1. Does this node type contain symbols to extract? (function names, variables, types)
2. Do children need different handling? (expressions vs statements)
3. Or is generic `process_children` sufficient?

**If YES to #1 or #2:** Create handler
**If NO:** No handler needed, fallback handles it

### Step 2: Choose Pattern

**Extract-Then-Visit Pattern:** Node has symbols + nested content
```c
static void handle_class_declaration(...) {
    extract_class_name(...);
    extract_type_parameters(...);
    process_children(body, ...);  // For class members
}
```

**Manual Iteration Pattern:** Different child types need different routing
```c
static void handle_if_statement(...) {
    for each child:
        if (is_expression) visit_expression(child);
        if (is_statement) visit_node(child);
}
```

### Step 3: Implement Handler

```c
static void handle_<node_type>(TSNode node, const char *source_code,
                               const char *directory, const char *filename,
                               ParseResult *result, SymbolFilter *filter,
                               int line_number) {
    // 1. Debug output
    if (g_debug) {
        debug("[handle_<node_type>] Line %d: Processing <node_type>", line_number);
    }

    // 2. Extract symbols this handler owns
    extract_specific_symbols(...);

    // 3. Visit children (choose appropriate method)
    // Option A: Manual iteration
    for each child:
        route to appropriate visit function

    // Option B: Process remaining children
    process_children(remaining_subtree, ...);
}
```

### Step 4: Register handler in visit_node if-else lookup

```c
/* Fast symbol-based dispatch - if-else chain with early returns */
    if (node_sym == c_symbols.function_definition) {
        handle_function_definition(node, source_code, directory, filename, result, filter, line);
        return;
    }
    ...
    if (node_sym == c_symbols.YOUR_IDENTIFIER) {
        handle_YOUR_IDENTIFIER(node, source_code, directory, filename, result, filter, line);
        return;
    }
};
```

### Step 5: Test

1. Add test file with node type
2. Run indexer with `--debug`
3. Verify symbols extracted correctly
4. Verify no duplicates
5. Verify nested symbols indexed

---

## Adding New Language Indexers

### Prerequisites

1. Tree-sitter grammar for the language
2. Understanding of language syntax (what symbols exist)
3. Sample code files for testing

### Step-by-Step Guide

#### 1. Create Language Files

```bash
mkdir <language>
touch <language>/<language>_language.h
touch <language>/<language>_language.c
```

#### 2. Copy C Indexer as Template

```bash
cp c/c_language.h <language>/<language>_language.h
cp c/c_language.c <language>/<language>_language.c
```

**Why C?** It follows the reference pattern: explicit ownership, minimal `process_children`, no skip mechanism.

#### 3. Update Boilerplate

- Change parser struct name
- Update tree-sitter language function call
- Update includes

#### 4. Identify Symbol Types

**For the language, what needs indexing?**
- Functions/methods
- Classes/interfaces/types
- Variables
- Parameters
- Imports/exports
- Properties/fields
- Calls
- etc.

#### 5. Create Handlers Incrementally

**Start with high-value handlers:**
1. Function declarations
2. Variable declarations
3. Class/type declarations
4. Import statements

**For each handler:**
- Follow Extract-Then-Visit or Manual Iteration pattern
- Add debug output
- Test with sample file

#### 6. Handle Expressions

Implement `visit_expression` for language-specific expression types:
- Calls
- Assignments
- Binary operations
- Member access
- etc.

#### 7. Handle Statements

Implement handlers for control flow:
- if/else
- loops (for, while, do-while)
- switch/case
- try/catch

**Use Manual Iteration Pattern** from C indexer

#### 8. Test Thoroughly

1. Create test fixtures in `tests/integration/fixtures/<language>/`
2. Index with `--debug` to verify traversal
3. Check for duplicates: `./qi symbol -f file.ext | sort`
4. Verify symbol counts match expectations

---

## Testing Strategies

### Unit-Level Testing

**Goal:** Verify individual handlers work correctly

**Approach:**
1. Create minimal test file with one construct
2. Run indexer with `--debug`
3. Verify exact symbols indexed
4. Check debug output shows correct traversal

**Example:**
```c
// test_if_minimal.c
void test() {
    if (x > 0) {
        foo();
    }
}
```

**Expected symbols:**
- `test` (FUNC, definition)
- `x` (VAR, condition)
- `foo` (CALL, body)

**Debug output should show:**
```
[visit_node] Line 1: node_type='function_definition'
[handle_function_definition] Found function 'test'
[handle_if_statement] Line 2: Processing if_statement
[handle_if_statement] Line 2: Found condition, visiting
[visit_expression] Line 2: node_type='binary_expression'
[handle_if_statement] Line 3: Found body (compound_statement), visiting
[visit_expression] Line 3: Processing call_expression
```

### Integration Testing

**Goal:** Verify complete file indexing

**Approach:**
1. Use realistic code files
2. Count expected symbols manually
3. Run indexer and compare
4. Check for duplicates

**Example Test Structure:**
```c
// tests/integration/<language>/test_<feature>.c
void test_<feature>() {
    // Index file
    index_file("fixtures/<language>/<feature>.ext");

    // Check symbol count
    assert_symbol_count(<feature>.ext, EXPECTED_COUNT);

    // Check specific symbols exist
    assert_symbol_exists("function_name", FUNC);
    assert_symbol_exists("variable_name", VAR);

    // Check no duplicates
    assert_no_duplicate_symbols();
}
```

### Duplicate Detection

**Quick Check:**
```bash
./qi % -f file.ext | sort | uniq -d
# Shows any duplicate entries (symbol + line should be unique)
```

**Detailed Analysis:**
```bash
./qi % -f file.ext -v | awk '{print $2, $4, $6}' | sort | uniq -c | sort -rn
# Shows symbol + line + context, with count
# Count > 1 = duplicate
```

### Debug-Driven Testing

**When things go wrong:**

1. **Enable debug output:**
   ```bash
   ./index-c file.c --debug 2>&1 | tee debug.log
   ```

2. **Filter to specific lines:**
   ```bash
   ./index-c file.c --debug 2>&1 | grep "Line 42"
   ```

3. **Trace handler execution:**
   ```bash
   ./index-c file.c --debug 2>&1 | grep "handle_"
   ```

4. **Find where traversal stops:**
   ```bash
   # If symbols on line 50 missing, check debug output
   ./index-c file.c --debug 2>&1 | grep -E "Line (4[5-9]|5[0-5])"
   ```

---

## Appendix: Handler Statistics

### C Indexer (Reference)

**Total Handlers:** 29
**Total Lines:** ~1450
**process_children Calls:** 2

**Handler Categories:**
- Declarations: 8 (function, declaration, struct, type_def, enum, preproc_def, preproc_function_def, preproc_include)
- Statements: 7 (if, while, do, for, switch, case, return)
- Expressions: 1 (visit_expression handles all via sub-routing)
- Comments/Strings: 2 (comment, string_literal)
- Utilities: 11 (helpers, process_children, visit_node, etc.)

**Key Metrics:**
- No duplicate symbols
- All control flow bodies indexed correctly
- Debug infrastructure using centralized `debug()` macro
- Follows explicit ownership pattern

### TypeScript Indexer (Needs Refactoring)

**Total Handlers:** 40
**Total Lines:** ~1850
**process_children Calls:** 14

**Handler Categories:**
- Declarations: 12 (class, interface, function, method, variable, property, type_alias, etc.)
- Statements: 3 (return, assignment, call)
- Expressions: 5 (arrow_function, function_expression, template_substitution, etc.)
- Generic Handlers: 2 (identifier, property_identifier - cause duplicates)
- Comments/Strings: 2 (comment, string_literal)
- Utilities: 16 (helpers, type extraction, modifiers, skip mechanism, etc.)

**Issues:**
- Has skip mechanism (needs removal)
- Generic handlers causing duplicates
- Too many `process_children` calls (14 vs C's 2)
- Uses raw `fprintf` instead of centralized debug

**Migration Priority:** HIGH

---

## Document Maintenance

**Update This Document When:**
- Adding new design patterns
- Making architectural decisions
- Discovering new pitfalls
- Completing language migrations
- Adding new debugging techniques

**Review Frequency:** After major indexer changes or new language additions

---
