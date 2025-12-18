# Implementation Plan: Strip $ from PHP Variable Symbols

## Goal
For PHP variables and properties, store the symbol **without** the `$` prefix in the `symbol` column (used for searching), 
while keeping the full `$variable` in the `full_symbol` column (used for display).

## Current Behavior
```
symbol:      $name     (lowercase with $)
full_symbol: $name     (original case with $)
```

## Desired Behavior
```
symbol:      name      (lowercase, NO $)
full_symbol: $name     (original case with $)
```

## Benefits
- Users can search for PHP variables without escaping: `qi name` instead of `qi \$name`
- Maintains backward compatibility in full_symbol for display purposes
- Consistent with other languages where special prefixes aren't required for search

## Investigation Summary

### Current Flow
1. PHP parser extracts variable names including `$` via tree-sitter
2. Extracted text is passed to `add_entry()` as the `symbol` parameter
3. `add_entry()` in `shared/parse_result.c:76` calls `to_lowercase_copy()` to create the search symbol
4. Both `symbol` and `full_symbol` end up with the `$` prefix

### Key Files
- **php/php_language.c**: Contains 8 `add_entry()` calls with `CONTEXT_VARIABLE` or `CONTEXT_PROPERTY`
- **shared/parse_result.c**: Contains `add_entry()` function that processes symbols
- **shared/string_utils.c**: Contains string manipulation utilities

## Implementation Strategy

### Option A: Strip in add_entry() (Recommended)
**Location**: `shared/parse_result.c:add_entry()`

**Approach**: Add PHP-specific logic to strip leading `$` when creating the lowercase symbol, but preserve it in full_symbol.

**Pseudocode**:
```c
void add_entry(...) {
    // Store full_symbol first (with $)
    snprintf(entry->full_symbol, sizeof(entry->full_symbol), "%s", symbol);

    // For PHP variables/properties, strip leading $ from symbol
    const char *symbol_for_lowercase = symbol;
    if ((context == CONTEXT_VARIABLE || context == CONTEXT_PROPERTY) &&
        symbol[0] == '$' && symbol[1] != '\0') {
        symbol_for_lowercase = symbol + 1;  // Skip the $
    }

    // Create lowercase symbol (without $ for PHP vars)
    to_lowercase_copy(symbol_for_lowercase, entry->symbol, sizeof(entry->symbol));

    // ... rest of function
}
```

**Pros**:
- Centralized: Single point of change
- Clean: No modifications needed to 8+ call sites in PHP parser
- Language-aware: Logic lives in the shared layer where symbol/full_symbol distinction is made

**Cons**:
- Adds PHP-specific logic to shared code (acceptable since we already have context-specific logic there)

### Option B: Strip in PHP Parser
**Location**: All 8 `add_entry()` call sites in `php/php_language.c`

**Approach**: Create a helper function to strip `$` before calling `add_entry()`.

**Pseudocode**:
```c
// New helper function
static void strip_dollar_prefix(const char *var_with_dollar, char *var_without_dollar, size_t size) {
    if (var_with_dollar[0] == '$' && var_with_dollar[1] != '\0') {
        snprintf(var_without_dollar, size, "%s", var_with_dollar + 1);
    } else {
        snprintf(var_without_dollar, size, "%s", var_with_dollar);
    }
}

// At each call site:
char var_no_dollar[SYMBOL_MAX_LENGTH];
strip_dollar_prefix(var_name, var_no_dollar, sizeof(var_no_dollar));
add_entry(result, var_no_dollar, line, CONTEXT_VARIABLE, ...);
```

**Pros**:
- Language-specific logic stays in language parser
- No changes to shared code

**Cons**:
- Requires modifications at 8+ call sites
- More code duplication
- Still need to pass original symbol somewhere for full_symbol (requires API change)

### Option C: Modify add_entry API
**Location**: `shared/parse_result.c` and all call sites

**Approach**: Split `add_entry()` to take both `symbol` and `full_symbol` as separate parameters.

**Pros**:
- Most explicit and flexible

**Cons**:
- Requires changes to ALL language parsers (C, TypeScript, Go, Python, PHP)
- Much larger change surface
- Overkill for this single use case

## Recommended Approach: Option A

Modify `add_entry()` in `shared/parse_result.c` to:
1. Store `full_symbol` first (preserving `$`)
2. Strip leading `$` for CONTEXT_VARIABLE and CONTEXT_PROPERTY before creating lowercase symbol
3. No changes needed to PHP parser or other language parsers

## Implementation Steps

1. **Add helper function** to `shared/string_utils.c`:
   ```c
   const char *skip_leading_char(const char *str, char ch);
   ```
   Returns pointer past the first character if it matches `ch`, otherwise returns original pointer.

2. **Modify `add_entry()`** in `shared/parse_result.c:76`:
   - Store full_symbol BEFORE processing symbol
   - Strip $ for PHP variables/properties before to_lowercase_copy
   - Reorder operations to preserve full_symbol correctly

3. **Test**:
   - Re-compile and re-index PHP test files
   - Verify: `qi name` finds `$name` variables
   - Verify: Full symbol display still shows `$name`
   - Verify: Other languages unaffected

## Edge Cases to Handle

1. **Empty after strip**: `$` alone (shouldn't happen, but check)
2. **Multiple $**: `$$var` (keep both or strip one?)
3. **Other contexts**: Ensure we only strip for VARIABLE and PROPERTY, not CALL or STRING
4. **Non-PHP languages**: Ensure no impact ($ can appear in other languages)

## Testing Strategy

1. Query without escape: `qi name` should find `$name`
2. Query with escape: `qi \$name` should still work (backward compat)
3. Display check: `qi name -v` should show `full_symbol=$name`
4. Edge case: Variable named `$_` or single char after $
5. Other languages: Verify C, TypeScript, etc. unaffected

## Files to Modify

1. `shared/string_utils.h` - Add function declaration
2. `shared/string_utils.c` - Implement helper function
3. `shared/parse_result.c` - Modify add_entry() logic

Total: 3 files, ~15 lines of code
