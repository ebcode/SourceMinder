# Lambda/Anonymous Function Support Plan

## Overview

This document outlines the plan for implementing `CONTEXT_LAMBDA` as a distinct context type across all supported languages in the indexer. Anonymous functions (lambdas) are a universal concept in modern programming languages and deserve first-class indexing support.

## Motivation

Currently, lambda expressions are not indexed as entities themselves. We index:
- Variables that lambdas are assigned to (CONTEXT_VARIABLE)
- Parameters of lambdas (CONTEXT_ARG with parent="lambda")
- Calls within lambda bodies (CONTEXT_CALL)

But we don't index the **lambda expression itself** as a searchable entity.

### Why CONTEXT_LAMBDA is valuable:

1. **Cross-language consistency:** Anonymous functions exist in Python, JavaScript, TypeScript, PHP, Go, Java, and others
2. **Useful queries:** Find all anonymous function definitions, identify lambda-heavy code patterns
3. **Pattern detection:** Locate callback patterns, functional programming style, closure usage
4. **Semantic clarity:** Explicit distinction between named functions (FUNC) and anonymous functions (LAMBDA)
5. **Code smell detection:** Find overly complex lambdas that should be named functions

## Current State (as of implementation)

### Python Example:
```python
sum_all = lambda *args: sum(args)  # Line 97
```

**Currently indexed:**
- `sum_all` → CONTEXT_VARIABLE (the variable)
- `args` → CONTEXT_ARG with parent="lambda" (the parameter)
- `sum` → CONTEXT_CALL (call in lambda body)

**Missing:**
- The lambda expression itself is not indexed

## Proposed Implementation

### Symbol Naming Convention

Use `"<lambda>"` as the symbol name for all anonymous functions:
- Matches Python's convention: `<function <lambda> at 0x...>`
- Language-agnostic identifier
- Clearly distinguishes from named functions
- Prevents confusion with variable names

### Database Schema

**Entry for lambda:**
- `symbol`: `"<lambda>"`
- `context_type`: `CONTEXT_LAMBDA`
- `line_number`: Line where lambda is defined
- `definition`: `"1"` (it's a function definition)
- `source_location`: Full lambda expression (for `-e` expand flag)
- `parent`: Variable name if assigned, or empty for inline lambdas
- `modifier`: Language-specific modifiers (e.g., "async" for async lambdas)
- `clue`: Context hint (e.g., "arrow" for JS, "closure" for PHP with `use`)

### Query Examples

After implementation, these queries would work:

```bash
# Find all lambda definitions
./qi % -i lambda

# Find lambdas in specific file
./qi % -i lambda -f routes.js

# Find async lambdas (Python, JavaScript)
./qi % -i lambda -m async

# Expand lambda to see full definition
./qi % -i lambda -e

# Find lambda parameters
./qi % -i arg -p "<lambda>"

# Smart filter: find both named and anonymous functions
./qi getUserById -i func lambda
```

### Duplication Consideration

For assigned lambdas like `sum_all = lambda x: x`, we'd index BOTH:
- `sum_all` → CONTEXT_VARIABLE (line 97)
- `<lambda>` → CONTEXT_LAMBDA (line 97)

**This is acceptable because:**
- They represent different semantic entities (the variable vs the function object)
- You might search for "what lambdas exist" (LAMBDA) vs "what variables exist" (VARIABLE)
- Mirrors how we handle function definitions: the function name AND its components

**Alternative approach (NOT recommended):**
- Only index lambda if it's NOT assigned to a variable
- Problem: Inconsistent behavior, harder to query all lambdas

## Language-Specific Implementation

### Python

**Syntax patterns:**
```python
lambda x: x * 2                          # Simple
lambda x, y: x + y                       # Multiple params
lambda *args: sum(args)                  # Variadic positional
lambda **kwargs: kwargs.get('key')       # Variadic keyword
lambda x, *args, **kwargs: ...           # Mixed
lambda: 42                               # No parameters
```

**AST node type:** `lambda`

**Implementation notes:**
- Already have `handle_lambda` function
- Need to add CONTEXT_LAMBDA entry before processing parameters
- Symbol: `"<lambda>"`
- Extract source location for full lambda expression
- No modifiers in Python (async lambdas don't exist - they're `async def`)

**Parent field:**
- If assigned to variable: parent = variable name
- If passed as argument: parent = empty or function being called into
- If returned from function: parent = function name

### JavaScript/TypeScript

**Syntax patterns:**
```javascript
// Arrow functions
(x) => x * 2
x => x * 2
(x, y) => x + y
() => 42
async (x) => await fetch(x)

// Anonymous function expressions
function(x) { return x * 2; }
async function(x) { return await fetch(x); }
```

**AST node types:**
- `arrow_function` (ES6)
- `function_expression` (when anonymous)

**Implementation notes:**
- Symbol: `"<lambda>"`
- Modifier: `"async"` for async arrow functions
- Clue: `"arrow"` for arrow syntax, `"function"` for function expressions
- TypeScript adds return type annotations to extract

### PHP

**Syntax patterns:**
```php
// Anonymous functions (closures)
function($x) { return $x * 2; }

// With use clause (closure over variables)
function($x) use ($multiplier) { return $x * $multiplier; }

// Arrow functions (PHP 7.4+)
fn($x) => $x * 2
```

**AST node types:**
- `anonymous_function_creation_expression`
- `arrow_function` (PHP 7.4+)

**Implementation notes:**
- Symbol: `"<lambda>"`
- Clue: `"closure"` if has `use` clause, `"arrow"` for arrow syntax
- `use` variables could be extracted to parent or separate entries

### Go

**Syntax patterns:**
```go
// Anonymous function
func(x int) int { return x * 2 }

// Immediately invoked
func(x int) { fmt.Println(x) }(42)

// As goroutine
go func() { /* ... */ }()
```

**AST node type:** `func_literal`

**Implementation notes:**
- Symbol: `"<lambda>"`
- Modifier: Could detect if launched as goroutine (parent is `go_statement`)
- Extract parameter types and return type

### Java (Future)

**Syntax patterns:**
```java
// Lambda expressions
x -> x * 2
(x, y) -> x + y
() -> 42
(String s) -> s.length()

// Method references
String::length
System.out::println
```

**Implementation notes:**
- Symbol: `"<lambda>"` for lambda expressions
- Method references might be separate (CONTEXT_METHOD_REFERENCE?)
- Extract parameter types and return type from context

### C (Not applicable)

C11 does not have lambda expressions. Function pointers are references to named functions, not anonymous function definitions.

C++ (if added later) has lambdas: `[capture](params) { body }`

## Implementation Steps

### Phase 1: Extend Schema
1. Add `CONTEXT_LAMBDA` constant to `shared/constants.h`
2. Update context type mapping in query tool
3. Add abbreviated form: `LAMBDA` → `LAM` (like `FUNCTION` → `FUNC`)

### Phase 2: Python Implementation
1. Modify `handle_lambda` in `python/python_language.c`:
   - Add entry for lambda expression itself
   - Use symbol `"<lambda>"`
   - Extract source location
   - Set definition="1"
2. Test with args-kwargs.py and lambdas.py
3. Verify queries work

### Phase 3: JavaScript/TypeScript Implementation
1. Add `handle_arrow_function` to `typescript/typescript_language.c`
2. Add `handle_function_expression` (check if anonymous)
3. Extract async modifier
4. Test with JavaScript test files

### Phase 4: PHP Implementation
1. Add `handle_anonymous_function` to `php/php_language.c`
2. Handle `use` clause for closures
3. Add arrow function support
4. Test with PHP test files

### Phase 5: Go Implementation
1. Add `handle_func_literal` to `go/go_language.c`
2. Detect goroutine context
3. Test with Go test files

### Phase 6: Smart Filtering
1. Add "smart filter" that treats `func` and `lambda` as synonyms
2. Update README with examples
3. Document in query tool help

## Edge Cases to Handle

### 1. Nested Lambdas
```python
outer = lambda x: lambda y: x + y
```
- Each lambda gets its own CONTEXT_LAMBDA entry
- Inner lambda parent: outer lambda's line or `"<lambda>"`

### 2. Lambda as Argument
```python
map(lambda x: x * 2, [1, 2, 3])
```
- Lambda still indexed with CONTEXT_LAMBDA
- Parent: Could be `"map"` or empty

### 3. Lambda in Return Statement
```python
def make_multiplier(n):
    return lambda x: x * n
```
- Lambda indexed with parent=`"make_multiplier"` or empty

### 4. Immediately Invoked Lambda
```python
result = (lambda x: x * 2)(5)
```
- Lambda still indexed
- Parent: Could track variable being assigned

### 5. Lambda in List/Dict Comprehension
```javascript
[1, 2, 3].map(x => x * 2)
```
- Lambda indexed normally
- Parent: empty or method being called

## Testing Strategy

### Test Files Needed

**Python:**
- `scratch/sources/python/lambdas.py` (already exists)
- `scratch/sources/python/args-kwargs.py` (already exists)
- Add tests for nested lambdas, lambda in arguments

**JavaScript/TypeScript:**
- `scratch/sources/typescript/lambdas.ts`
- Test arrow functions, async arrows, function expressions

**PHP:**
- `scratch/sources/php/lambdas.php`
- Test anonymous functions, closures with `use`, arrow functions

**Go:**
- `scratch/sources/go/lambdas.go`
- Test func literals, goroutines, closures

### Query Tests

For each language, verify:
```bash
# Find all lambdas
./qi % -i lambda -f <test-file>

# Expand lambda to see full definition
./qi % -i lambda -f <test-file> -e

# Find lambda parameters
./qi % -i arg -p "<lambda>"

# Find calls within lambdas
./qi % -i call -f <test-file>

# Combined: named and anonymous functions
./qi % -i func lambda
```

## Documentation Updates

### README.md
Add section on lambda queries:
```markdown
### Finding Anonymous Functions

qi % -i lambda              # All lambda expressions
qi % -i lambda -f routes.js # Lambdas in specific file
qi % -i func lambda         # Both named and anonymous functions
```

### Query Tool Help
Update context type list to include:
- `lambda` | `lam` - Lambda/anonymous function definitions

### NEW_LANGUAGE.md
Add section on implementing lambda support:
- How to identify anonymous function nodes in AST
- Using `"<lambda>"` as symbol
- Setting definition="1" and extracting source location
- Handling parameters with parent="<lambda>"

## Open Questions

1. **Parent field for lambdas:**
   - Should we populate parent with the variable name if assigned?
   - Should we leave it empty for all lambdas?
   - Should we use the enclosing function name?

   **Recommendation:** Leave parent empty for lambdas. Use clue or other field if needed.

2. **Symbol naming:**
   - `"<lambda>"` for all languages?
   - Or language-specific: `"lambda"` (Python), `"arrow"` (JS), `"fn"` (PHP)?

   **Recommendation:** Stick with `"<lambda>"` for consistency.

3. **Smart filter implementation:**
   - Hardcode `func` → `func,lambda` substitution?
   - Add configuration file for smart filters?
   - User flag like `--smart-filter`?

   **Recommendation:** Start with hardcoded substitution, add configuration later.

4. **Method references (Java):**
   - Are `String::length` method references lambdas?
   - Separate context type CONTEXT_METHOD_REFERENCE?

   **Recommendation:** Separate context type, implement when adding Java.

5. **Line number for multi-line lambdas:**
   - Use line where lambda keyword/arrow appears?
   - Or line where lambda ends?

   **Recommendation:** Line where lambda starts (keyword/arrow).

## Benefits Summary

After implementation, developers can:

1. **Find all lambdas:** `./qi % -i lambda`
2. **Identify lambda-heavy code:** `./qi % -i lambda -f module.py --limit 50`
3. **See lambda definitions:** `./qi % -i lambda -e`
4. **Find lambda parameters:** `./qi % -i arg -p "<lambda>"`
5. **Pattern matching:** Find all async lambdas, closures, etc.
6. **Cross-language queries:** Same query syntax across Python, JS, PHP, Go
7. **Code review:** Quickly assess use of anonymous vs named functions

## Future Enhancements

1. **Lambda complexity metrics:** Count lines/statements in lambda body
2. **Closure analysis:** Track captured variables from outer scope
3. **Lambda refactoring hints:** Suggest converting complex lambdas to named functions
4. **Call graph integration:** Include lambdas in function call graphs
5. **Performance tracking:** Flag lambdas in hot paths (requires profiling data integration)

## Related Work

This complements existing context types:
- `CONTEXT_FUNCTION` - Named function definitions
- `CONTEXT_CALL` - Function calls (including lambda calls)
- `CONTEXT_ARGUMENT` - Function parameters (including lambda params)

And follows the pattern established by:
- Functions having both definition (FUNC) and calls (CALL)
- Lambdas will have both definition (LAMBDA) and their parameters (ARG)

## Estimated Implementation Time

- **Phase 1 (Schema):** 30 minutes
- **Phase 2 (Python):** 2 hours (including testing)
- **Phase 3 (JavaScript/TypeScript):** 3 hours
- **Phase 4 (PHP):** 2 hours
- **Phase 5 (Go):** 2 hours
- **Phase 6 (Smart filtering):** 1 hour
- **Documentation:** 1 hour

**Total:** ~12 hours of focused development

## Approval Status

**Status:** PENDING APPROVAL

**Decision needed on:**
1. Proceed with CONTEXT_LAMBDA implementation?
2. Implement across all languages at once, or incrementally?
3. Symbol naming convention approved?
4. Parent field usage for lambdas?

---

**Last updated:** 2025-11-14
**Author:** Claude (with human guidance)
**Related:** `docs/NEW_LANGUAGE.md`, work summaries
