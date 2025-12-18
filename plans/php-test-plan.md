# PHP Indexer Test Plan

## Overview
Test each PHP feature file individually to verify the indexer correctly extracts symbols, contexts, namespaces, and relationships.

## Testing Procedure

For each file:
1. Remove existing database: `rm code-index.db`
2. Index the file: `./index-php scratch/sources/php/<filename>.php`
3. Run queries to verify extraction
4. Generate AST if needed: `./scratch/tools/ast-explorer-php ./scratch/sources/php/<filename>.php > ast-output.txt`

---

## 1. basic_class.php

**Focus:** Simple class with properties and methods (baseline test)

**Expected Symbols:**
- Class: `Product`
- Properties: `id`, `name`, `price`
- Methods: `__construct`, `getTotal`, `updatePrice`
- Method arguments: `id`, `name`, `price`, `quantity`, `newPrice`

**Test Queries:**
```bash
./query-index "Product" -i class_name
# Expected: Product at line ~7

./query-index "price" -i property_name
# Expected: price property at line ~10

./query-index "getTotal" -i function_name
# Expected: getTotal method at line ~18

./query-index "quantity" -i function_argument
# Expected: quantity parameter at line ~18
```

**Expected Namespace:** All symbols should have namespace `App\Models`

**Expected Contexts:**
- `CLASS_NAME`: Product
- `PROPERTY_NAME`: id, name, price
- `FUNCTION_NAME`: __construct, getTotal, updatePrice
- `FUNCTION_ARGUMENT`: id, name, price, quantity, newPrice

---

## 2. visibility_modifiers.php

**Focus:** Public/private/protected modifiers on properties and methods

**Expected Symbols:**
- Class: `Account`
- Properties: `username` (public), `email` (protected), `passwordHash` (private)
- Methods: `getUsername` (public), `validateEmail` (protected), `hashPassword` (private)

**Test Queries:**
```bash
./query-index "Account" -i class_name
# Expected: Account at line ~7

./query-index "email" -i property_name
# Expected: email (protected) at line ~9

./query-index "passwordHash" -i property_name
# Expected: passwordHash (private) at line ~10

./query-index "hashPassword" -i function_name
# Expected: hashPassword (private) at line ~20
```

**Scope Verification:**
- `username` / `getUsername` should have scope: `public`
- `email` / `validateEmail` should have scope: `protected`
- `passwordHash` / `hashPassword` should have scope: `private`

---

## 3. interfaces.php

**Focus:** Interface definitions and class implementations

**Expected Symbols:**
- Interface: `Storable`
- Interface methods: `save`, `delete`, `find`
- Class: `Document` (implements Storable)
- Properties: `id`, `content`
- Methods: `save`, `delete`, `find`

**Test Queries:**
```bash
./query-index "Storable" -i class_name
# Expected: Storable interface at line ~7
# Note: Tree-sitter may use INTERFACE_NAME or CLASS_NAME context

./query-index "save" -i function_name
# Expected: save method in both interface (~8) and class (~20)

./query-index "Document" -i class_name
# Expected: Document class at line ~16
```

**Special Considerations:**
- Interface methods may appear as abstract/signature-only
- Class methods are concrete implementations
- Check if tree-sitter distinguishes interface vs class declarations

---

## 4. traits.php

**Focus:** Trait definitions and usage in classes

**Expected Symbols:**
- Traits: `Timestampable`, `SoftDeletes`
- Trait properties: `createdAt`, `updatedAt`, `deletedAt`
- Trait methods: `setTimestamps`, `softDelete`
- Class: `Post`
- Class properties: `title`, `content`

**Test Queries:**
```bash
./query-index "Timestampable" -i class_name
# Expected: Timestampable trait at line ~7
# Note: May need TRAIT_NAME context type

./query-index "setTimestamps" -i function_name
# Expected: setTimestamps in Timestampable trait at line ~11

./query-index "Post" -i class_name
# Expected: Post class at line ~29

./query-index "title" -i property_name
# Expected: title in Post at line ~32
```

**Special Considerations:**
- `use Timestampable, SoftDeletes;` at line 31 - should this be indexed?
- Traits are like classes but cannot be instantiated
- Check if tree-sitter uses `trait_declaration` node type

---

## 5. enums.php

**Focus:** Backed enums, pure enums, enum methods

**Expected Symbols:**
- Enums: `UserStatus`, `Priority`, `Color`
- Enum cases: `Active`, `Inactive`, `Suspended`, `Pending`, `Low`, `Medium`, `High`, `Critical`, `Red`, `Green`, `Blue`
- Enum method: `getHex` (in Color enum)

**Test Queries:**
```bash
./query-index "UserStatus" -i class_name
# Expected: UserStatus enum at line ~7
# Note: May need ENUM_NAME context type

./query-index "Active" -i property_name
# Expected: Active case at line ~8
# Note: May need ENUM_CASE context type

./query-index "getHex" -i function_name
# Expected: getHex method in Color enum at line ~27
```

**Special Considerations:**
- Backed enums have values (`case Active = 'active'`)
- Pure enums have no values (`case Low;`)
- Enum methods are like class methods
- Check tree-sitter node types: `enum_declaration`, `enum_case`

---

## 6. static_members.php

**Focus:** Static properties, methods, and class constants

**Expected Symbols:**
- Class: `Database`
- Static properties: `instance`, `queryCount`
- Constants: `HOST`, `PORT`, `TIMEOUT`
- Methods: `__construct`, `getInstance`, `resetQueryCount`

**Test Queries:**
```bash
./query-index "Database" -i class_name
# Expected: Database at line ~7

./query-index "queryCount" -i property_name
# Expected: queryCount (static) at line ~9

./query-index "PORT" -i property_name
# Expected: PORT constant at line ~12
# Note: Constants may need CONSTANT_NAME context type

./query-index "getInstance" -i function_name
# Expected: getInstance (static) at line ~17
```

**Special Considerations:**
- Static members accessed via `self::` or `Database::`
- Constants use `const` keyword
- Check if static modifier is captured in scope field

---

## 7. functions.php

**Focus:** Functions, arrow functions, closures

**Expected Symbols:**
- Functions: `calculateDiscount`, `formatName`
- Arrow function: `$square`
- Anonymous functions: `$greet`, `$multiply`
- Function arguments: `price`, `percentage`, `first`, `last`, `middle`, `name`, `value`, `n`

**Test Queries:**
```bash
./query-index "calculateDiscount" -i function_name
# Expected: calculateDiscount at line ~7

./query-index "formatName" -i function_name
# Expected: formatName at line ~14

./query-index "square" -i variable_name
# Expected: square variable at line ~19

./query-index "greet" -i variable_name
# Expected: greet variable at line ~24
```

**Special Considerations:**
- Arrow functions: `fn($n) => $n * $n`
- Anonymous functions: `function($name) { ... }`
- Closures with `use` clause: `function($value) use ($multiplier)`
- Check if arrow/anonymous functions are indexed differently

---

## 8. namespace_imports.php

**Focus:** Namespace declarations, use statements, aliases

**Expected Symbols:**
- Namespace: `App\Services` (different from other files!)
- Imports: `User`, `Product` (aliased as `Item`), `UserRepository`, `Storable`, `Cacheable`, `Loggable`
- Functions/constants imports: `formatDate`, `DEFAULT_TIMEZONE`
- Class: `OrderService`
- Method: `createOrder`

**Test Queries:**
```bash
./query-index "OrderService" -i class_name
# Expected: OrderService with namespace App\Services at line ~13

./query-index "User" -i import
# Expected: User import at line ~5

./query-index "Item" -i import
# Expected: Item (alias for Product) at line ~6
# Note: Should we index both 'Product' and 'Item'?

./query-index "createOrder" -i function_name
# Expected: createOrder at line ~17
```

**Special Considerations:**
- Namespace is `App\Services` (not `App\Models`)
- Grouped imports: `use App\Interfaces\{Storable, Cacheable, Loggable};`
- Aliased imports: `as Item`, `as Auth`
- Function/constant imports with `use function` and `use const`

---

## 9. match_expressions.php

**Focus:** Match expressions (PHP 8.0+)

**Expected Symbols:**
- Class: `StatusHandler`
- Methods: `getStatusMessage`, `getStatusCode`
- Match arms contain strings, but those might be indexed as STRING context

**Test Queries:**
```bash
./query-index "StatusHandler" -i class_name
# Expected: StatusHandler at line ~7

./query-index "getStatusMessage" -i function_name
# Expected: getStatusMessage at line ~8

./query-index "active" -i string
# Expected: 'active' string literal (appears multiple times)
```

**Special Considerations:**
- Match expressions are like switch statements
- Match arms: `'active' => 'User is active'`
- Multiple conditions: `'active', 'verified' => 200`
- Check if strings in match arms are indexed

---

## 10. property_promotion.php

**Focus:** Constructor property promotion (PHP 8.0+)

**Expected Symbols:**
- Class: `Customer`
- Promoted properties: `id`, `name`, `email`, `passwordHash`, `phone`
- Method: `__construct`, `updateEmail`
- Class: `Order`
- Traditional property: `items`
- Promoted properties: `orderId`, `customer`, `total`

**Test Queries:**
```bash
./query-index "Customer" -i class_name
# Expected: Customer at line ~7

./query-index "email" -i property_name
# Expected: email property (promoted) at line ~11
# Also: email parameter in constructor

./query-index "phone" -i property_name
# Expected: phone (promoted, protected, nullable) at line ~13

./query-index "items" -i property_name
# Expected: items (traditional property) in Order at line ~23
```

**Special Considerations:**
- Promoted properties declared in constructor parameters
- Mix of promoted and traditional properties in `Order` class
- Visibility modifiers on promoted properties
- Check if tree-sitter distinguishes promoted vs traditional properties

---

## 11. readonly.php

**Focus:** Readonly properties and classes (PHP 8.1+/8.2+)

**Expected Symbols:**
- Class: `Configuration`
- Readonly properties: `appName`, `environment`, `maxConnections`
- Readonly class: `Point`
- Properties: `x`, `y`
- Method: `distanceFrom`

**Test Queries:**
```bash
./query-index "Configuration" -i class_name
# Expected: Configuration at line ~7

./query-index "appName" -i property_name
# Expected: appName (readonly) at line ~9

./query-index "Point" -i class_name
# Expected: Point (readonly class) at line ~17

./query-index "distanceFrom" -i function_name
# Expected: distanceFrom at line ~23
```

**Special Considerations:**
- `readonly` keyword on properties
- `readonly` keyword on entire class (PHP 8.2+)
- Readonly properties combined with promotion
- Check if readonly modifier is captured

---

## 12. attributes.php

**Focus:** Attributes/annotations (PHP 8.0+)

**Expected Symbols:**
- Attribute classes: `Route`, `Deprecated`
- Class: `UserController`
- Methods: `show`, `oldMethod`
- Attributes on class: `#[Route('/api/users')]`
- Attributes on methods: `#[Route(...)]`, `#[Deprecated(...)]`

**Test Queries:**
```bash
./query-index "Route" -i class_name
# Expected: Route attribute class at line ~7

./query-index "UserController" -i class_name
# Expected: UserController at line ~19

./query-index "show" -i function_name
# Expected: show method at line ~21

./query-index "Deprecated" -i class_name
# Expected: Deprecated attribute at line ~14
```

**Special Considerations:**
- Attributes use `#[AttributeName]` syntax
- Attribute classes have `#[\Attribute]` on them
- Multiple attributes on same method
- Attributes can have parameters
- Check if attributes themselves are indexed

---

## 13. named_arguments.php

**Focus:** Named parameter calling (PHP 8.0+)

**Expected Symbols:**
- Function: `createUser`
- Parameters: `name`, `email`, `age`, `active`, `role`
- Class: `UserFactory`
- Methods: `create`, `createMinimal`

**Test Queries:**
```bash
./query-index "createUser" -i function_name
# Expected: createUser function at line ~7

./query-index "email" -i function_argument
# Expected: email parameter at line ~9

./query-index "UserFactory" -i class_name
# Expected: UserFactory at line ~19

./query-index "create" -i function_name
# Expected: create method at line ~20
```

**Special Considerations:**
- Function calls with named arguments: `email: 'test@example.com'`
- Named arguments can be in any order
- Optional parameters can be skipped
- Check if named argument syntax affects indexing

---

## 14. magic_methods.php

**Focus:** All 14 magic methods

**Expected Symbols:**
- Class: `MagicContainer`
- Properties: `data`, `methods`
- Magic methods: `__construct`, `__get`, `__set`, `__isset`, `__unset`, `__call`, `__callStatic`, `__toString`, `__invoke`, `__clone`, `__destruct`, `__sleep`, `__wakeup`, `__debugInfo`

**Test Queries:**
```bash
./query-index "MagicContainer" -i class_name
# Expected: MagicContainer at line ~7

./query-index "__get" -i function_name
# Expected: __get method at line ~14

./query-index "__callStatic" -i function_name
# Expected: __callStatic (static) at line ~30

./query-index "__invoke" -i function_name
# Expected: __invoke at line ~38
```

**Special Considerations:**
- All magic methods start with `__`
- `__callStatic` is static
- `__construct` and `__destruct` are lifecycle methods
- Check if magic methods are treated differently

---

## 15. abstract_final.php

**Focus:** Abstract and final classes/methods

**Expected Symbols:**
- Abstract class: `BaseRepository`
- Abstract methods: `find`, `validate`
- Regular method: `getTable`
- Concrete class: `ProductRepository` (extends BaseRepository)
- Final class: `ImmutableConfig`
- Final method: `get`

**Test Queries:**
```bash
./query-index "BaseRepository" -i class_name
# Expected: BaseRepository (abstract) at line ~7

./query-index "find" -i function_name
# Expected: find (abstract) in BaseRepository at line ~11
# Also: find (concrete) in ProductRepository at line ~23

./query-index "ImmutableConfig" -i class_name
# Expected: ImmutableConfig (final) at line ~34

./query-index "get" -i function_name
# Expected: get (final) at line ~39
```

**Special Considerations:**
- `abstract` keyword on classes and methods
- `final` keyword on classes and methods
- Abstract methods have no body
- Check if abstract/final modifiers are captured

---

## 16. type_declarations.php

**Focus:** Union types, nullable types, mixed type

**Expected Symbols:**
- Class: `TypeExample`
- Properties: `optionalName` (?string), `optionalId` (?int), `number` (int|float), `data` (string|array), `anything` (mixed)
- Methods: `processValue`, `getValue`, `handleData`
- Function: `transformData`

**Test Queries:**
```bash
./query-index "TypeExample" -i class_name
# Expected: TypeExample at line ~7

./query-index "number" -i property_name
# Expected: number (int|float) at line ~13

./query-index "optionalName" -i property_name
# Expected: optionalName (?string) at line ~9

./query-index "transformData" -i function_name
# Expected: transformData at line ~33
```

**Special Considerations:**
- Union types: `int|float`, `string|array`, `string|int|null`
- Nullable types: `?string`, `?int`
- Mixed type: `mixed`
- Intersection types commented out (requires interfaces)
- Check if type information is captured in clue or separate field

---

## 17. anonymous_class.php

**Focus:** Anonymous class instantiation

**Expected Symbols:**
- Class: `AnonymousExample`
- Methods: `getLogger`, `getCounter`
- Anonymous class methods: `log`, `error`, `increment`, `get`, `__construct`, `handle`
- Variable: `handler`

**Test Queries:**
```bash
./query-index "AnonymousExample" -i class_name
# Expected: AnonymousExample at line ~7

./query-index "getLogger" -i function_name
# Expected: getLogger at line ~9

./query-index "log" -i function_name
# Expected: log method in anonymous class at line ~12
# Note: May or may not be indexed - check tree-sitter behavior

./query-index "handler" -i variable_name
# Expected: handler variable at line ~40
```

**Special Considerations:**
- Anonymous classes: `new class { ... }`
- Anonymous classes can have constructors
- Anonymous classes can have properties and methods
- Check if symbols inside anonymous classes are indexed
- May need special handling for unnamed classes

---

## Summary

**Total Test Files:** 17
**Estimated Testing Time:** 2-3 hours for complete coverage

**Priority Order:**
1. Start with **basic_class.php** - establishes baseline
2. **visibility_modifiers.php** - tests scope extraction
3. **interfaces.php** - tests interface vs class
4. **traits.php** - tests trait declaration
5. Continue with remaining files in order

**Common Checks for All Files:**
- Namespace extraction (`App\Models` for most, `App\Services` for imports)
- Context types (CLASS_NAME, FUNCTION_NAME, PROPERTY_NAME, etc.)
- Scope values (public, private, protected, static)
- Parent symbols for methods/properties
- Line numbers accuracy
- PHPDoc comments indexed as COMMENT context

**Next Steps:**
1. Build the PHP indexer if not already done
2. Start testing with basic_class.php
3. Document any AST parsing challenges
4. Update php_language.c handlers as needed
5. Add new context types if required (TRAIT_NAME, ENUM_NAME, ENUM_CASE, etc.)
