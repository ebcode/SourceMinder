# Real-World Codebase Testing Ideas

Testing approaches for validating the PHP indexer against large, real-world codebases like Symfony, Laravel, WordPress, and Drupal.

---

## 🎯 Approach 1: **Automated Cross-Validation Test Harness**

Build a verification tool that uses tree-sitter independently to validate indexing:

```bash
# Conceptual tool: ./validate-index
# 1. Parse a random PHP file with tree-sitter
# 2. Query code-index.db for symbols from that file
# 3. Cross-check what tree-sitter found vs what's indexed
# 4. Report discrepancies

./validate-index scratch/sources/php/symfony/src/Component/HttpFoundation/Request.php
```

**What it checks:**
- Every class definition in the file is indexed
- Every public method is indexed with correct visibility
- Line numbers match between tree-sitter AST and database
- Namespace declarations are captured
- Property counts match

**Implementation:** A separate C program or Python script that:
1. Parses PHP with tree-sitter
2. Counts declarations by type
3. Queries SQLite for the same file
4. Compares counts and reports delta

---

## 🔍 Approach 2: **Known Symbol Spot Checks**

Create a curated list of well-known Symfony symbols and verify them:

```bash
# Test file: symfony_known_symbols.txt
Symfony\Component\HttpFoundation\Request::class
Symfony\Component\HttpKernel\Kernel::boot
Symfony\Component\DependencyInjection\ContainerBuilder
```

**Test script:**
```bash
#!/bin/bash
# For each known symbol, verify it exists with correct context
while IFS= read -r symbol; do
    result=$(./query-index "$symbol" --columns line context)
    if [[ -z "$result" ]]; then
        echo "MISSING: $symbol"
    fi
done < symfony_known_symbols.txt
```

**Variations:**
- Test for specific namespaces (all `Symfony\Component\Cache\*`)
- Test for trait usage (Symfony uses lots of traits)
- Test for interface implementations

---

## 🎲 Approach 3: **Random File Deep Dive**

Interactive testing workflow:

```bash
#!/bin/bash
# random-file-test.sh

# Pick random PHP file from Symfony
RANDOM_FILE=$(find ./symfony/src -name "*.php" | shuf -n 1)

echo "Testing file: $RANDOM_FILE"
echo "----------------------------------------"

# Show file stats
echo "Lines in file: $(wc -l < "$RANDOM_FILE")"

# Show what we indexed
echo -e "\nIndexed symbols:"
sqlite3 code-index.db "SELECT line_number, full_symbol, context_type, scope
                        FROM code_index
                        WHERE filename='${RANDOM_FILE#./}'
                        ORDER BY line_number"

# Show the actual file for manual comparison
echo -e "\n\nOpening file for manual verification..."
echo "CHECK: Do all classes appear? All public methods? Correct line numbers?"
cat "$RANDOM_FILE"
```

**What to manually verify:**
- Class definitions indexed
- All methods present (especially public ones)
- Namespace captured
- Properties indexed
- Comments NOT mistaken for code
- String contents NOT indexed as symbols (they should be indexed as STRING context)

---

## 🧪 Approach 4: **Missing Feature Detection**

From `php_plan.md`, test for NOT YET IMPLEMENTED features:

```bash
# Test for type annotations (HIGH PRIORITY missing feature)
./query-index "string" -i type_name
./query-index "User" -i type_name

# If these return nothing → confirms type annotations aren't indexed yet

# Test for imports (HIGH PRIORITY missing feature)
./query-index "Request" -i import

# If this returns nothing → confirms imports aren't indexed yet
```

**Create a comprehensive missing features report:**
```bash
#!/bin/bash
# missing-features-report.sh

echo "=== Type Annotations Test ==="
if ./query-index "string" -i type_name | grep -q "Found 0"; then
    echo "❌ Type annotations NOT implemented"
else
    echo "✅ Type annotations working"
fi

echo -e "\n=== Import Statements Test ==="
if ./query-index "%" -i import --limit 1 | grep -q "Found 0"; then
    echo "❌ Import statements NOT implemented"
else
    echo "✅ Import statements working"
fi

echo -e "\n=== Abstract Modifier Test ==="
# Query for abstract classes
count=$(sqlite3 code-index.db "SELECT COUNT(*) FROM code_index WHERE modifier LIKE '%abstract%'")
if [[ $count -eq 0 ]]; then
    echo "❌ Abstract modifier not captured"
else
    echo "✅ Abstract modifier working ($count instances)"
fi
```

---

## 🏗️ Approach 5: **Namespace Coverage Map**

Ensure all Symfony namespaces are discovered:

```bash
# Get all namespaces from index
sqlite3 code-index.db "SELECT DISTINCT namespace FROM code_index ORDER BY namespace" > indexed_namespaces.txt

# Get all namespaces from source (grep approach)
grep -rh "^namespace " ./symfony/src | sed 's/namespace \(.*\);/\1/' | sort -u > actual_namespaces.txt

# Compare
diff indexed_namespaces.txt actual_namespaces.txt
```

**What this reveals:**
- Files that weren't indexed at all
- Namespace extraction issues
- Potential file discovery problems

---

## 🎯 Approach 6: **Query-Driven Testing**

Test real use cases:

```bash
# Use case: Find all Cache-related classes
./query-index "%Cache%" -i class_name --columns line symbol

# Use case: Find Request handlers
./query-index "%Request%" -i class_name filename

# Use case: Find all methods called "execute"
./query-index "execute" -i function_name

# Use case: Find all static methods
sqlite3 code-index.db "SELECT filename, full_symbol FROM code_index
                        WHERE modifier='static' AND context_type='FUNCTION_NAME'
                        LIMIT 20"
```

**Success criteria:**
- Results seem reasonable and comprehensive
- No obvious missing classes
- Line numbers look correct
- File paths are valid

---

## 🚀 Approach 7: **Performance & Scale Testing**

```bash
# Measure indexing performance
time ./index-php ./symfony/src --quiet

# Check database size
ls -lh code-index.db

# Count indexed symbols
sqlite3 code-index.db "SELECT COUNT(*) FROM code_index"

# Check for memory issues (run in background, monitor)
/usr/bin/time -v ./index-php ./symfony/src 2>&1 | grep -E "(Maximum|elapsed)"

# Test query performance
time ./query-index "%Controller%" --limit 100
```

**Benchmarks to establish:**
- Files/second indexing rate
- Symbols indexed per MB of source code
- Query response time for common patterns
- Memory usage for large codebases

---

## 🎨 Approach 8: **Mutation Testing**

Modify a known good file and verify changes are detected:

```bash
# 1. Index Symfony
./index-php ./symfony/src

# 2. Add a new method to a random class
echo "public function testMethod() { return true; }" >> ./symfony/src/Some/Class.php

# 3. Re-index that file
./index-php ./symfony/src/Some/Class.php

# 4. Verify new method appears
./query-index "testMethod" -i function_name

# 5. Restore original file
git checkout ./symfony/src/Some/Class.php
```

---

## 📋 **Recommended Test Suite Structure**

```
tests/
├── integration/
│   ├── symfony-test.sh          # Full Symfony indexing
│   ├── laravel-test.sh          # Laravel framework
│   ├── wordpress-test.sh        # WordPress core
│   └── drupal-test.sh           # Drupal core
├── validation/
│   ├── cross-validate.c         # Tree-sitter comparison tool (Approach 1)
│   ├── known-symbols.txt        # Curated symbol list (Approach 2)
│   ├── check-coverage.sh        # Feature coverage checks (Approach 4)
│   └── random-file-test.sh      # Random file deep dive (Approach 3)
├── coverage/
│   └── namespace-coverage.sh    # Namespace coverage map (Approach 5)
└── performance/
    ├── benchmark.sh             # Speed/memory tests (Approach 7)
    └── stress-test.sh           # Large codebase handling
```

---

## 🏆 **Best Immediate Actions**

1. **Start simple:** Download Symfony, index it, run spot checks (Approach 2, 6)
2. **Build validation:** Create the cross-validation tool (Approach 1) - **HIGHEST VALUE**
3. **Check missing features:** Run tests for type annotations and imports (Approach 4)
4. **Verify namespace coverage:** Ensure all files/namespaces discovered (Approach 5)
5. **Test other frameworks:** Laravel, WordPress, Drupal

---

## 🎯 **Target Codebases for Testing**

1. **Symfony** - Modern PHP framework, heavy use of namespaces, interfaces, traits
2. **Laravel** - Popular framework, different coding style than Symfony
3. **WordPress** - Legacy codebase, mix of old and new PHP
4. **Drupal** - Enterprise CMS, complex architecture
5. **PHPUnit** - Testing framework, good for finding edge cases
6. **Composer** - Package manager, dependency resolution code
