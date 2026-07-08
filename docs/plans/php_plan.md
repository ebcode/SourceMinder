# PHP Indexer Implementation Plan

## Current Status

### ✅ Implemented Features

**Core Declarations:**
- ✅ Namespaces (`namespace App\Models`)
- ✅ Classes (`class User`)
- ✅ Interfaces (`interface Storable`)
- ✅ Traits (`trait Loggable`)
- ✅ Enums (`enum Color`)
- ✅ Enum cases (`case Red`)
- ✅ Functions (`function transform()`)
- ✅ Methods (`public function show()`)

**Class Members:**
- ✅ Properties (`public string $name`)
- ✅ Static properties (`static int $count`)
- ✅ Constants (`const VERSION = '1.0'`)
- ✅ Method declarations with visibility (public/private/protected)
- ✅ Static methods

**Expressions & Access Patterns:**
- ✅ Variable assignments (`$user = ...`)
- ✅ Member access (`$obj->property`)
- ✅ Method calls (`$obj->method()`)
- ✅ Static property access (`self::$property`, `ClassName::$property`)
- ✅ Static method calls (`self::method()`, `ClassName::method()`)
- ✅ Class constant access (`ClassName::CONSTANT`, `parent::CONSTANT`)
- ✅ **NEW:** Expression-based constant access (`getClass()::CONSTANT`)
- ✅ Scoped expressions with qualified names (`\App\Models\User::method()`)
- ✅ Chained static calls (`$obj::method1()::method2()`)

**Other:**
- ✅ Function parameters (`function foo($userId)`)
- ✅ Comments (word extraction)
- ✅ Strings (single, double-quoted, heredoc, nowdoc)
- ✅ Filenames

**Modifiers Tracked:**
- ✅ Visibility: public, private, protected (in `scope` field)
- ✅ Static methods/properties (in `modifier` field)
- ✅ Constants (modifier="const")

---

## ❌ Not Yet Implemented

### 1. Type Annotations ⭐ HIGH PRIORITY

**Test file:** `scratch/sources/php/type_declarations.php`

**What's missing:**
- Property type hints: `private ?string $name`
- Parameter types: `function foo(string|int $value)`
- Return types: `function bar(): string|array`
- Union types: `int|float|string`
- Nullable types: `?User`
- Intersection types: `Countable&Traversable` (PHP 8.1+)
- Mixed type: `mixed`

**Implementation notes:**
- Context type: `CONTEXT_TYPE_NAME` (already exists in database schema)
- AST nodes to handle:
  - `union_type` - Contains multiple types
  - `intersection_type` - Contains multiple types with & separator
  - `primitive_type` - Built-in types (string, int, bool, etc.)
  - `named_type` - Class/interface names
  - `optional_type` - Nullable types (?)
- Should index each type in a union/intersection separately
- Track the symbol being typed in `parent` field (e.g., parameter name, property name)
- Consider tracking type position: "property_type", "parameter_type", "return_type" in `context_clue`

**Example queries after implementation:**
```bash
# Find all uses of User type
./query-index "User" -i type_name

# Find nullable types
# (Would need to store ? in modifier or context_clue)

# Find all string parameters
./query-index "string" -i type_name
```

---

### 2. Import Statements (use) ⭐ HIGH PRIORITY

**Test file:** `scratch/sources/php/namespace_imports.php`

**What's missing:**
- Class imports: `use App\Models\User;`
- Aliased imports: `use App\Models\Product as Item;`
- Grouped imports: `use App\Interfaces\{Storable, Cacheable};`
- Function imports: `use function App\Helpers\formatDate;`
- Constant imports: `use const App\Config\DEFAULT_TIMEZONE;`

**Implementation notes:**
- Context type: `CONTEXT_IMPORT` (already exists in database schema)
- AST nodes to handle:
  - `namespace_use_declaration` - Main use statement
  - `namespace_use_clause` - Individual import within use statement
  - `namespace_use_group` - Grouped imports
  - `namespace_aliasing_clause` - Aliased imports (as)
- Store imported symbol in `full_symbol`
- Store alias (if any) in `parent` field or `context_clue`
- Store import type (class/function/const) in `modifier` field
- Store full namespace path in `namespace` field

**Example queries after implementation:**
```bash
# Find all imports of User class
./query-index "User" -i import

# Find all function imports
./query-index "%" -i import --modifier function

# Files importing StoreDelta
./query-index "StoreDelta" -i import
```

---

### 3. Attribute Usage 🔶 MEDIUM PRIORITY

**Test file:** `scratch/sources/php/attributes.php`

**What's missing:**
- Attribute usage on classes: `#[Route('/api/users')]`
- Attribute usage on methods: `#[Deprecated(since: '2.0')]`
- Attribute usage on properties/parameters
- Multiple attributes on same target

**Current behavior:**
- Attribute **definitions** are already indexed (they're just classes)
- Attribute **usage** is not indexed

**Implementation notes:**
- Need new context type or reuse existing one
- Could use `CONTEXT_CALL_EXPRESSION` with special modifier="attribute"
- Or create `CONTEXT_ATTRIBUTE` (would require database schema change)
- AST nodes to handle:
  - `attribute_group` - `#[...]`
  - `attribute` - Individual attribute inside group
- Store attribute name in `full_symbol`
- Store target type (class/method/property) in `context_clue`
- Store on which symbol in `parent` field

**Example queries after implementation:**
```bash
# Find all uses of Route attribute
./query-index "Route" --modifier attribute

# Find what's marked as Deprecated
./query-index "Deprecated" --modifier attribute
```

**Alternative approach:**
- Don't index attribute usage separately
- Focus on type annotations and imports first
- Attributes can be found via grep/search if needed

---

### 4. Additional Modifiers 🔶 MEDIUM PRIORITY

**Test files:** `scratch/sources/php/abstract_final.php`, `scratch/sources/php/readonly.php`

**What's missing:**
- `abstract` modifier on classes and methods
- `final` modifier on classes and methods
- `readonly` modifier on classes and properties (PHP 8.1+)

**Current behavior:**
- Only `static` and `const` stored in `modifier` field
- Visibility stored in `scope` field

**Implementation notes:**
- Extend `extract_visibility()` or create similar function for modifiers
- AST nodes already encountered, just not extracted:
  - `abstract_modifier`
  - `final_modifier`
  - `readonly_modifier`
- Store in `modifier` field (may need to combine multiple modifiers)
- Format: "abstract", "final", "readonly", "static,readonly", etc.

**Example queries after implementation:**
```bash
# Find abstract classes
./query-index "%" -i class_name --modifier abstract

# Find final methods
./query-index "%" -i function_name --modifier final

# Find readonly properties
./query-index "%" -i property_name --modifier readonly
```

---

### 5. Anonymous Class Markers 🔹 LOW PRIORITY

**Test file:** `scratch/sources/php/anonymous_class.php`

**Current behavior:**
- Methods inside anonymous classes ARE indexed
- Properties inside anonymous classes ARE indexed
- BUT: No indication that they're from an anonymous class
- Parent symbol is empty

**What's missing:**
- Marker/flag to indicate anonymous class context
- Parent symbol for anonymous class members
- Anonymous class location tracking

**Implementation notes:**
- Could use special parent name like `<anonymous>` or `<anonymous@line>`
- Or add flag in `context_clue` field: "anonymous_class"
- AST node: `anonymous_function_creation_expression` with class keyword
- Low priority because anonymous classes are rare and methods/properties are still indexed

**Example queries after implementation:**
```bash
# Find all anonymous class members
./query-index "%" --parent "<anonymous>"
```

---

## Implementation Priority

### Phase 1: High Priority (Most Useful) ⭐
1. **Type Annotations** - Critical for understanding code contracts
   - Estimated effort: Medium (need to handle multiple AST nodes)
   - High value: Essential for modern PHP codebases

2. **Import Statements** - Critical for understanding dependencies
   - Estimated effort: Medium (need to handle aliases and groups)
   - High value: Shows what each file depends on

### Phase 2: Medium Priority 🔶
3. **Additional Modifiers** (abstract, final, readonly)
   - Estimated effort: Low (infrastructure exists, just extract more modifiers)
   - Medium value: Useful for filtering, but less critical

4. **Attribute Usage** (optional)
   - Estimated effort: Medium-High (need to decide on schema)
   - Medium value: Modern PHP feature, but less commonly used

### Phase 3: Low Priority 🔹
5. **Anonymous Class Markers**
   - Estimated effort: Low
   - Low value: Edge case, existing indexing works for most use cases

---

## Database Schema Status

**Current schema supports:**
- ✅ `CONTEXT_IMPORT` - Ready for use imports
- ✅ `CONTEXT_TYPE_NAME` - Ready for type annotations
- ✅ All other context types being used

**No schema changes needed** for the high-priority features!

---

## Testing Strategy

For each new feature:
1. Test file already exists in `scratch/sources/php/`
2. Index the test file: `./index-php ./scratch/sources/php/<file>.php`
3. Query for expected symbols: `./query-index "<symbol>" -i <context>`
4. Verify parent relationships and modifiers
5. Test with full directory: `./index-php ./scratch/sources/php/`

---

## Notes

- **Backwards compatibility is not a concern** (per CLAUDE.md)
- Infrastructure is solid - most features just need new AST node handlers
- Test files already exist for all planned features
- Database schema already supports the high-priority features
- Focus on type annotations and imports first for maximum value
