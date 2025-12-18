# Python Developer's Guide to qi

A practical guide for using qi to search and analyze Python codebases. This guide focuses on Python-specific patterns and workflows that leverage qi's semantic understanding of Python code.

---

## Table of Contents

1.  [Quick Start](#quick-start)
2.  [Core Concepts](#core-concepts)
3.  [Finding Definitions](#finding-definitions)
4.  [Decorators](#decorators)
5.  [Async/Await Patterns](#asyncawait-patterns)
6.  [Generators and Iterators](#generators-and-iterators)
7.  [Modern Python Features](#modern-python-features)
8.  [Exception Handling](#exception-handling)
9.  [Class Exploration](#class-exploration)
10. [Common Workflows](#common-workflows)
11. [Advanced Queries](#advanced-queries)
12. [Tips & Tricks](#tips--tricks)

---

## Quick Start

### Index Your Python Project

```bash
# Index current directory
./index-python .

# Index specific directory
./index-python ./src

# Index multiple packages
./index-python ./app ./lib ./tests
```

### Basic Search

```bash
# Find all uses of a symbol
qi "UserManager"

# Find function definition
qi "authenticate" -i func

# Find with code context
qi "authenticate" -i func -C 10

# Exclude noise (comments/strings)
qi "error" -x noise
```

### Your First Steps with qi (Recommended Workflow)

**Start with structure, then drill down:**

```bash
# 1. Get file overview (always start here!)
qi '*' -f 'auth.py' --toc

# 2. See a specific function's implementation
qi 'authenticate' -i func -e

# 3. Find where it's used
qi 'authenticate' -i call --limit 20

# 4. Understand what happens inside
qi 'user' --within 'authenticate' -x noise

# 5. Explore related patterns
qi 'user' 'backend' --and 5 -C 3
```

**Why this order?**
- `--toc` gives you the map (classes, functions, imports)
- `-e` shows you the code (full definitions inline)
- `--within` reveals what a function does
- `-p` shows you how objects are used
- `--and` finds related patterns

This is **way more efficient** than reading files or using grep!

---

## Core Concepts

### Context Types

qi understands these Python-specific contexts:

| Context | Abbreviation | Description |
|---------|--------------|-------------|
| `class` | - | Class definitions |
| `function` | `func` | Functions and methods |
| `argument` | `arg` | Function parameters |
| `variable` | `var` | Variables and constants |
| `property` | `prop` | Class attributes |
| `import` | `imp` | Import statements |
| `export` | `exp` | Exported symbols (rare in Python) |
| `call` | - | Function/method calls |
| `lambda` | `lam` | Lambda expressions |
| `exception` | `exc` | Exception classes in except blocks |
| `comment` | `com` | Words from comments |
| `string` | `str` | Words from string literals |
| `filename` | `file` | Filename without extension |

### Clues (Python-Specific Usage Contexts)

qi uses "clues" to indicate how a symbol is being used:

| Clue | Meaning |
|------|---------|
| `@property` | Property decorator |
| `@classmethod` | Class method decorator |
| `@staticmethod` | Static method decorator |
| `@cached_property` | Cached property decorator |
| `@override_settings`, `@wraps` | Custom declorators |
| `async` | Async function (appears in MOD column) |
| `yield` | Generator yield statement |
| `lambda` | Lambda expression |
| `:=` | Walrus operator (assignment expression) |
| `*args` | Variadic positional arguments |
| `**kwargs` | Variadic keyword arguments |

---

## Finding Definitions

### Functions and Methods

```bash
# Find function definition
qi "process_data" -i func

# Find all functions in a file
qi '%' -i func -f '%views.py'

# Find async functions
qi '%' -i func -m async --limit 20

# Find all functions starting with "get_"
qi 'get%' -i func

# Expand function to see full code
qi "authenticate" -i func -e
```

### Classes

```bash
# Find class definition
qi "UserManager" -i class

# Find all classes in a module
qi '%' -i class -f '%models.py'

# Find classes with specific base class (via parent column)
qi '%' -i class -v --limit 10  # Use -v to see parent column

# Expand class to see full definition
qi "AppConfig" -i class -e
```

### Variables and Constants

```bash
# Find variable definition
qi "DEFAULT_TIMEOUT" -i var

# Find all constants (uppercase convention)
qi '%' -i var --columns line,symbol,type

# Find variables with walrus operator
qi '%' -c ':='
```

---

## Decorators

### Finding Decorated Functions

```bash
# All decorated functions
qi '%' -i func -c '@%' --limit 20

# Specific decorator
qi '%' -i func -c '@property'
qi '%' -i func -c '@classmethod'
qi '%' -i func -c '@staticmethod'
qi '%' -i func -c '@cached_property'

# Custom decorators
qi '%' -i func -c '@wraps'
qi '%' -i func -c '@override_settings'
```

### Decorator Usage Analysis

```bash
# Count decorator usage across codebase
qi '%' -i func -c '@%' --columns clue | tail -n +3 | awk '{print $1}' | sort | uniq -c | sort -rn | head -20

# Find all @property methods in a class
qi '%' -i func -c '@property' -f 'models/base.py'

# Find files with most decorated functions
qi '%' -i func -c '@%' --files
```

### Common Decorator Patterns

```bash
# Properties
qi '%' -i func -c '@property'

# Class/static methods
qi '%' -i func -c '@classmethod'
qi '%' -i func -c '@staticmethod'

# Cached properties (Django, functools)
qi '%' -i func -c '@cached_property'
qi '%' -i func -c '@functools.cache'

# Context managers
qi '%' -i func -c '@contextmanager'
```

---

## Async/Await Patterns

### Finding Async Functions

```bash
# All async functions
qi '%' -i func -m async

# Async functions in specific module
qi '%' -i func -m async -f '%views.py'

# Count async functions
qi '%' -i func -m async --columns line | wc -l

# Files with most async code
qi '%' -i func -m async --files
```

### Async Function Analysis

```bash
# Find async function definitions with decorators
qi '%' -i func -m async -v --limit 20

# Specific async function
qi "aget_user" -i func -m async -e

# Find all async functions starting with 'a'
qi 'a%' -i func -m async
```

### Understanding Async Patterns

```bash
# 1. Find all async functions
qi '%' -i func -m async --limit 30

# 2. Check which modules use async
qi '%' -i func -m async --files

# 3. See how async functions are implemented
qi "aget_object_or_404" -i func -m async -e
```

---

## Generators and Iterators

### Finding Generators

```bash
# All generators (functions using yield)
qi '%' -c 'yield'

# Generators in specific module
qi '%' -c 'yield' -f '%paginator.py'

# Show generator context
qi '%' -c 'yield' -C 5 --limit 10

# Files using generators
qi '%' -c 'yield' --files
```

### Generator Patterns

```bash
# Find yielded values
qi '%' -c 'yield' --columns line,symbol,clue

# Find generator functions
# (Look for functions containing yield)
qi '%' -c 'yield' -C 20 | grep 'def '
```

---

## Modern Python Features

### Walrus Operator (:=)

```bash
# All walrus operator usage
qi '%' -c ':='

# Walrus in conditionals
qi '%' -c ':=' -C 3

# Variables assigned with walrus
qi '%' -c ':=' --columns line,symbol
```

### *args and **kwargs

```bash
# Find variadic arguments
qi '%' -c '*args'
qi '%' -c '**kwargs'

# Functions with *args
qi 'args' -c '*args' --columns line,symbol,parent

# Functions with **kwargs
qi 'kwargs' -c '**kwargs' --columns line,symbol,parent
```

### Lambda Expressions

```bash
# All lambda expressions
qi '%' -c 'lambda'

# Lambda usage with context
qi '%' -c 'lambda' -v --limit 15

# Files using lambdas
qi '%' -c 'lambda' --files
```

### F-strings and String Formatting

```bash
# Search for format calls
qi 'format' -i call --limit 20

# Find string interpolation patterns
qi '%' -i str --columns line,symbol --limit 20
```

---

## Exception Handling

### Finding Exception Classes

```bash
# All exception classes
qi '%Error' '%Exception' -i class

# Specific exception
qi "ValidationError" -i class -e

# Custom exceptions in project
qi '%Error' -i class -f 'myapp/%'
```

### Exception Usage

```bash
# Exceptions in try/except
qi '%' -i exc --limit 20

# Specific exception handling
qi "KeyError" -i exc -C 5

# Files with most exception handling
qi '%' -i exc --files
```

### Exception Patterns

```bash
# Find all exception types being caught
qi '%' -i exc --columns line,symbol,type --limit 30

# Find exception raising patterns
qi "raise" -x noise -C 3
```

---

## Class Exploration

### Understanding Class Structure

```bash
# 1. Find class definition
qi "UserManager" -i class

# 2. Expand class to see structure
qi "UserManager" -i class -e

# 3. Use TOC for quick overview
qi '*' -f 'path/to/file.py' --toc
```

### Finding Class Members

```bash
# All properties
qi '%' -i prop --limit 20

# Properties of specific class (via parent)
qi '%' -i prop -v --limit 20  # Look at parent column

# Methods (functions with parent)
qi '%' -i func -v --limit 20  # Look at parent column
```

### Class Inheritance

```bash
# Find classes (parent shows base class)
qi '%' -i class -v --limit 20

# Find all classes inheriting from specific base
qi '%' -i class -v | grep "BaseClass"
```

### Class Decorators and Properties

```bash
# Find all @property methods
qi '%' -i func -c '@property' --limit 20

# Find all @classmethod methods
qi '%' -i func -c '@classmethod' --limit 20

# Find cached properties
qi '%' -i func -c '@cached_property' --limit 15
```

---

## Common Workflows

### Understanding a New Codebase (START HERE!)

**Always start with Table of Contents:**

```bash
# 1. Get the entry point structure
qi '*' -f '__init__.py' --toc
qi '*' -f 'main.py' --toc

# Shows:
# - What's imported (dependencies)
# - What classes are defined
# - What functions exist (with line ranges!)

# 2. Explore key modules by name pattern
qi '*' -f '%auth%' --toc
qi '*' -f '%models.py' --toc
qi '*' -f '%views.py' --toc

# 3. Find main entry points
qi 'main' 'run' 'execute' -i func

# 4. See implementations with -e
qi 'setup' -i func -e
qi 'UserManager' -i class -e
```

**Why TOC first?**
- Instant overview without opening files
- Shows line ranges (jump directly to code)
- Reveals module structure and dependencies
- Faster than reading entire files

### Finding a Function's Implementation

**Complete workflow from discovery to understanding:**

```bash
# 1. Find the function (location)
qi "authenticate" -i func

# 2. See the full implementation (best approach!)
qi "authenticate" -i func -e

# Output shows:
# 106:def authenticate(request=None, **credentials):
# 107:    """
# 108:    If the given credentials are valid, return a User object.
# 109:    """
# 110:    for backend, backend_path in _get_compatible_backends(...):
# ...

# 3. Find what the function does with specific variables
qi 'user' --within 'authenticate' -x noise
qi 'backend' --within 'authenticate' -x noise

# 4. Find all calls to this function
qi "authenticate" -i call --limit 20

# 5. See how it's called in context
qi "authenticate" -i call -C 5

# 6. Compare sync vs async versions
qi "authenticate" -i func -e
qi "aauthenticate" -i func -e
```

**Pro tip:** Use `-e` instead of reading files! It shows code inline with syntax highlighting.

### Exploring Async Code

```bash
# 1. Count async functions
qi '%' -i func -m async --columns line | wc -l

# 2. See which modules use async
qi '%' -i func -m async --files

# 3. Examine specific async functions
qi '%' -i func -m async --limit 10 -e
```

### Refactoring a Class

```bash
# 1. Find class definition
qi "User" -i class -e

# 2. Find all methods with @property
qi '%' -i func -c '@property' -f '%models%'

# 3. Find all usages (imports, calls)
qi "User" -i imp
qi "User" -i call --limit 20

# 4. See full structure
qi '*' -f 'path/to/model.py' --toc
```

### Finding Decorator Usage

```bash
# 1. Find all decorators used
qi '%' -i func -c '@%' --columns clue | tail -n +3 | sort -u

# 2. Find functions with specific decorator
qi '%' -i func -c '@cached_property'

# 3. See decorator implementation
qi "cached_property" -i class -e
```

### Understanding Error Handling

```bash
# 1. Find exception classes
qi '%Error' '%Exception' -i class --limit 20

# 2. Find where exceptions are caught
qi '%' -i exc --limit 30

# 3. Find exception raising patterns
qi "ValidationError" -x noise -C 3
```

### Understanding How Objects Are Used (with `-p`)

```bash
# 1. Find how form objects are used
qi '%' -i call -p 'form' --limit 20

# 2. Focus on specific methods
qi 'save' -i call -p 'form'
qi 'is_valid' -i call -p 'form'

# 3. See the patterns in context
qi 'save' -i call -p 'form' -C 3

# 4. Find what methods are called on user objects
qi '%' -i call -p 'user' --limit 30
```

### Understanding a Function's Implementation (with `--within`)

```bash
# 1. Find the function
qi "authenticate" -i func

# 2. See what it does with specific variables
qi 'user' --within 'authenticate' -x noise

# 3. Find all local variables
qi '%' -i var --within 'authenticate'

# 4. Check error handling within function
qi '%' -i exc --within 'authenticate'

# 5. See everything with context
qi '%' --within 'authenticate' -x noise -C 2 --limit 20
```

### Finding Related Code Patterns (with `--and`)

```bash
# 1. Find error handling patterns
qi 'error' 'raise' --and 3 -x noise

# 2. Find form validation patterns
qi 'form' 'is_valid' --and 5

# 3. Find async/await patterns
qi 'await' 'async' --and 10 --limit 20

# 4. Find request/response patterns
qi 'request' 'HttpResponse' --and 10

# 5. See the patterns in context
qi 'user' 'authenticate' --and 5 -C 3
```

### Sampling Across Large Codebases (with `--limit-per-file`)

```bash
# 1. Get examples of how save() is used across files
qi 'save' -i call --limit-per-file 2

# 2. Find which files use async most
qi '%' -i func -m async --limit-per-file 5

# 3. Sample decorator usage
qi '%' -i func -c '@property' --limit-per-file 1

# 4. Find error handling distribution
qi '%' -i exc --limit-per-file 3

# 5. Compare with regular limit
qi 'save' -i call --limit 20              # Might all be from one file
qi 'save' -i call --limit-per-file 2      # Distributed across files
```

---

## Power User Combinations

These combinations unlock qi's full potential by stacking filters creatively.

### Combining Async + Decorators

```bash
# Find decorated async functions
qi '%' -i func -m async -c '@%' --limit 20

# Result: 96 matches showing patterns like:
# - @sensitive_variables with async functions
# - @wraps decorating async wrappers
# - @classmethod with async methods

# See specific decorator pattern
qi '%' -i func -m async -c '@wraps' -e
qi '%' -i func -m async -c '@classmethod'
```

### Combining Parent + Context + Within

```bash
# What methods does save_base call on self?
qi '%' -i call -p 'self' --within 'save_base' --limit 30

# Result shows internal workflow:
# - self._validate_force_insert()
# - self._save_parents()
# - self._save_table()
# - self._do_update()

# What's called on form objects within save_model?
qi '%' -i call -p 'form' --within 'save_model'
```

### Combining --and + Parent + Context

```bash
# Find user/backend patterns with parent info
qi 'user' 'backend' --and 5 -i var -v --limit 10

# Shows: user.backend = backend_path pattern

# Find form validation patterns
qi 'form' 'is_valid' --and 5 -i call --limit 15
```

### Combining Multiple Filters for Precision

```bash
# Find async functions with specific decorator in test files
qi '%' -i func -m async -c '@override_settings' -f '%test%.py'

# Find @property methods that return specific types
qi '%' -i func -c '@property' -e --limit 10 | grep 'return self'

# Find decorated async functions with context
qi '%' -i func -m async -c '@%' -C 5 --limit 10
```

### Creative Sampling

```bash
# Sample async functions across codebase
qi '%' -i func -m async --limit-per-file 2

# Sample decorated functions per file
qi '%' -i func -c '@property' --limit-per-file 1

# Compare distribution
qi 'save' -i call --limit 20              # Might all be in one file
qi 'save' -i call --limit-per-file 3      # Distributed across 129+ files
```

---

## Advanced Queries

### Parent Symbol Filtering (`-p`)

The `-p` flag filters by the parent symbol - the object/variable that a method is being called on. This is **extremely useful** for understanding how objects are used in Python's object-oriented code.

```bash
# Find all .save() calls on form objects
qi 'save' -i call -p 'form'

# Find all method calls on self
qi '%' -i call -p 'self' --limit 20

# Find specific patterns
qi 'save' -i call -p 'user'
qi 'save' -i call -p 'obj'
qi 'authenticate' -i call -p 'backend'

# Find what methods are called on request objects
qi '%' -i call -p 'request' --limit 30

# With context to see usage
qi 'save' -i call -p 'form' -C 3
```

**Use cases:**
- Understanding how specific objects are used (e.g., "How are form objects saved?")
- Finding method call patterns (e.g., "What methods are called on user objects?")
- Debugging object interactions
- API exploration

**Note:** Parent is populated for method **calls**, not method definitions. When you call `form.save()`, the parent is `form`. When you define `def save(self):` in a class, there's no parent yet.

### Scoped Search (`--within`)

The `--within` flag searches only within specific function or class definitions. This is **fantastic** for understanding what happens inside a particular function.

```bash
# What does authenticate() do with 'user'?
qi 'user' --within 'authenticate' -C 3

# What happens in save_model?
qi 'save' --within 'save_model'

# Find all variables used within a function
qi '%' -i var --within 'process_request'

# Multiple functions (OR logic)
qi 'error' --within 'authenticate' 'login' 'validate'

# With context to understand usage
qi 'password' --within 'authenticate' -C 5

# Exclude noise for cleaner results
qi 'user' --within 'authenticate' -x noise
```

**Use cases:**
- Understanding function behavior ("What does this function do with X?")
- Debugging specific functions
- Learning how a function works without reading entire file
- Finding local variables and their usage
- Code review (checking what a function touches)

**Why better than grep:** Understands function boundaries! grep can't tell where a function starts and ends.

### Related Patterns (`--and`)

The `--and` flag finds lines where **multiple patterns** co-occur, either on the same line or within N lines of each other.

```bash
# Find 'request' and 'user' on the same line
qi 'request' 'user' --and

# Find 'request' and 'user' within 5 lines
qi 'request' 'user' --and 5

# Find error handling patterns
qi 'error' 'raise' --and 3

# Find related async patterns
qi 'await' 'async' --and 10

# Exclude noise for cleaner results
qi 'request' 'user' --and -x noise

# With context to see the pattern
qi 'form' 'save' --and 5 -C 3
```

**Use cases:**
- Finding related variables/patterns (what's often used together?)
- Understanding common patterns
- Refactoring (finding code that uses multiple symbols)
- Finding error handling patterns (try/except/raise)
- Code analysis (what concepts are related?)

**Performance tip:** Smaller ranges are faster. `--and` (same line) is fastest, `--and 5` is slower but more comprehensive.

### Sampling Results (`--limit-per-file`)

The `--limit-per-file` flag limits results to N matches per file, giving you a **sampling across many files** instead of letting one file dominate.

```bash
# Get 3 examples of save() calls from each file
qi 'save' -i call --limit-per-file 3

# Sample async functions across codebase
qi '%' -i func -m async --limit-per-file 2

# Find decorator usage samples
qi '%' -i func -c '@property' --limit-per-file 1

# Sample error handling across modules
qi '%' -i exc --limit-per-file 5

# Combine with other filters
qi 'authenticate' -i call --limit-per-file 2 -x noise
```

**Use cases:**
- Quick overview of usage across codebase
- Finding which files use a pattern most
- Preventing one giant file from dominating results
- Getting diverse examples
- Understanding distribution of a pattern

**Difference from `--limit`:**
- `--limit 10` = first 10 matches total (might all be from one file)
- `--limit-per-file 3` = up to 3 matches from EACH file (distributed sampling)

### Multi-Column Display

```bash
# Show type annotations
qi '%' -i func --columns line,symbol,type --limit 10

# Show decorators
qi '%' -i func --columns line,symbol,clue --limit 20

# Show class hierarchy
qi '%' -i class --columns line,symbol,parent --limit 15

# Show parent for method calls
qi 'save' -i call --columns line,symbol,parent --limit 20

# Verbose mode (all columns)
qi '%' -i func -v --limit 5
```

### File Filtering

```bash
# Specific file
qi "handler" -f "views.py"

# All Python files in directory
qi '%' -i func -f 'app/%'

# Multiple file patterns
qi "Config" -i class -f '%config.py' '%settings.py'

# Test files
qi '%' -i func -f '%_test.py' '%/tests/%.py'
```

### Excluding Noise

```bash
# Exclude comments and strings
qi "error" -x noise

# Exclude specific contexts
qi "test" -x comment -x string

# Focus on code only
qi "TODO" -x noise
qi "FIXME" -x noise
```

### Context Display

```bash
# Show N lines after match
qi "def main" -A 10

# Show N lines before match
qi "return" -B 5

# Show N lines before and after
qi "authenticate" -C 20

# Maximum context
qi "process_request" -C 50
```

### Limiting Results

```bash
# Show first N matches
qi '%' -i func --limit 20

# Quick overview
qi '%' -i class --limit 10

# Sample async functions
qi '%' -i func -m async --limit 15
```

### Table of Contents

```bash
# File overview
qi '*' -f 'models.py' --toc

# Multiple files
qi '*' -f '%views.py' --toc

# Show only functions
qi '*' -f 'utils.py' --toc -i func

# Show only classes
qi '*' -f 'models.py' --toc -i class
```

---

## Tips & Tricks

### Search Patterns

```bash
# Wildcard at start
qi '%Manager' -i class      # Ends with Manager

# Wildcard at end
qi 'get%' -i func            # Starts with get

# Wildcard both sides
qi '%user%' -i var           # Contains user

# Match anything
qi '%' -i func               # All functions
```

### Common Python Naming Patterns

```bash
# Private methods (leading underscore)
qi '_%' -i func

# Dunder methods
qi '__%' -i func

# Class-based views
qi '%View' -i class

# Managers
qi '%Manager' -i class

# Serializers (Django REST)
qi '%Serializer' -i class

# Test functions
qi 'test%' -i func -f '%test%.py'
```

### Combining with Shell Tools

```bash
# Count functions per file
qi '%' -i func --columns line | cut -d: -f1 | sort | uniq -c | sort -rn | head -10

# Find most-called functions
qi '%' -i call --columns symbol | tail -n +3 | sort | uniq -c | sort -rn | head -10

# List all unique class names
qi '%' -i class --columns symbol | tail -n +2 | sort -u

# Find files with most decorators
qi '%' -i func -c '@%' --columns line | cut -d: -f1 | sort | uniq -c | sort -rn | head -10

# Count async functions per module
qi '%' -i func -m async --files | wc -l
```

### Performance Tips

```bash
# Use file filters to narrow scope
qi "handler" -f '%/app/%' -i func

# Use context filters
qi "test" -i func                    # Only functions

# Exclude noise for faster results
qi "error" -x noise

# Limit results for quick checks
qi '%' -i func --limit 50
```

### Debugging Queries

```bash
# Use verbose mode
qi "symbol" -v

# Check what's indexed
qi '%' --limit 20 -v

# Verify context types
qi '%' -i func --limit 5
qi '%' -i class --limit 5

# See all columns
qi "test" -v --limit 3
```

---

## Real-World Examples

### Example 1: Exploring Django Auth Module (TOC-First Workflow)

```bash
# Step 1: Get structure with TOC
qi '*' -f 'django/contrib/auth/__init__.py' --toc

# Result shows:
# IMPORTS: django.apps, django.conf.settings, PermissionDenied, ...
# FUNCTIONS (22):
#   authenticate ................................................ 106
#   aauthenticate .............................................. 130
#   login ...................................................... 151
#   alogin ..................................................... 184
#   ... (and more)

# Step 2: Compare sync vs async authenticate
qi 'authenticate' -i func -f 'django/contrib/auth/__init__.py' -e
qi 'aauthenticate' -i func -f 'django/contrib/auth/__init__.py' -e

# Shows: async version uses await and .asend() instead of .send()

# Step 3: What does authenticate do with 'user'?
qi 'user' --within 'authenticate' -x noise

# Result: 8 matches showing:
# - user = backend.authenticate(request, **credentials)
# - if user is None: continue
# - user.backend = backend_path
# - return user

# Step 4: Find the pattern
qi 'user' 'backend' --and 5 -C 2 --limit 5

# Shows: user.backend = backend_path assignment pattern
```

### Example 2: Decorated Async Functions (Combining Filters)

```bash
# Find all decorated async functions
qi '%' -i func -m async -c '@%' --limit 20 -v

# Result: 96 matches showing:
# - @sensitive_variables with aauthenticate
# - @wraps with async wrapper functions
# - @classmethod with async aclear_expired
# - @override_settings in async tests

# See a specific pattern in detail
qi '%' -i func -m async -c '@wraps' -e --limit 3

# Shows async wrapper implementations with @wraps decorator
```

### Example 3: Understanding Django's Async Support

```bash
# How many async functions?
qi '%' -i func -m async --columns line | wc -l
# Result: 749 async functions

# Where is async code concentrated?
qi '%' -i func -m async --files | head -15
# Shows: auth, sessions, contrib modules

# What naming pattern for async?
qi '%' -i func -m async --columns symbol | head -20
# Pattern: Functions prefixed with 'a' (aget, alogin, etc.)
```

### Example 4: Decorator Usage Analysis

```bash
# What decorators are most common?
qi '%' -i func -c '@%' --columns clue | tail -n +3 | awk '{print $1}' | sort | uniq -c | sort -rn | head -10

# Result shows:
#   958 @setup
#   870 (blank)
#   864 @override_settings
#   604 @skipUnlessDBFeature
#   565 @classmethod
#   515 @property
```

### Example 5: Finding All Exception Classes

```bash
# Find all custom exceptions
qi '%Error' '%Exception' -i class --limit 20

# Result: 82 exception classes
# Including: ValidationError, FieldError, CommandError, etc.

# See specific exception
qi "ValidationError" -i class -e
```

### Example 6: Exploring the Model Class (Complete Workflow)

```bash
# Step 1: Find Model class location
qi "Model" -i class --limit 1
# Result: django/db/models/base.py:498

# Step 2: Get complete file structure
qi '*' -f 'django/db/models/base.py' --toc

# Shows:
# CLASSES (6): Deferred, ModelBase, ModelState, Model, ...
# FUNCTIONS (82): save, delete, refresh_from_db, _save_table, ...
# IMPORTS (86): inspect, warnings, django.apps.apps, ...

# Step 3: See what methods save_base calls internally
qi '%' -i call -p 'self' --within 'save_base' --limit 20

# Result shows internal workflow:
# - self._validate_force_insert()
# - self._save_parents()
# - self._save_table()
# - self._do_update()

# Step 4: Find @property methods
qi '%' -i func -c '@property' -f 'django/db/models/base.py' -e

# Shows implementations:
# - _base_manager (returns manager)
# - _default_manager (returns manager)

# Step 5: Compare with TOC approach
# TOC showed us line ranges immediately (save: 830)
# Direct to implementation with -e
qi 'save' -i func -f 'django/db/models/base.py' -e --limit 1
```

### Example 7: Finding Walrus Operator Usage

```bash
# How widespread is walrus operator?
qi '%' -c ':=' --columns line | wc -l
# Result: 144 usages

# Where is it used?
qi '%' -c ':=' --files | head -10

# Example usage
qi '%' -c ':=' -C 2 --limit 5
```

### Example 8: Understanding How Forms Are Used (Parent Flag)

```bash
# Find all methods called on form objects
qi '%' -i call -p 'form' --limit 30

# Result shows common patterns:
# - form.save()
# - form.is_valid()
# - form.cleaned_data
# - form.errors

# Focus on save() calls on forms
qi 'save' -i call -p 'form'
# Result: 67 matches across the codebase

# See how they're used in context
qi 'save' -i call -p 'form' -C 3
```

### Example 9: What Does authenticate() Do? (--within Flag)

```bash
# Find what authenticate() does with 'user'
qi 'user' --within 'authenticate' -x noise

# Result: 22 matches showing:
# - user = backend.authenticate(request, **credentials)
# - if user is None: continue
# - user.backend = backend_path
# - return user

# Find all local variables in authenticate
qi '%' -i var --within 'authenticate' --limit 20

# Check error handling
qi '%' -i exc --within 'authenticate'
# Result: Shows PermissionDenied handling
```

### Example 10: Finding Form Validation Patterns (--and Flag)

```bash
# Find where form and is_valid are used together
qi 'form' 'is_valid' --and 5 --limit 20 -x noise

# Find error and raise patterns
qi 'error' 'raise' --and 3 -x noise --limit 15

# Find request and user patterns
qi 'request' 'user' --and --limit 20 -x noise
# Result: 123 matches (same line only)

# Within 5 lines
qi 'request' 'user' --and 5 --limit 20 -x noise
# Result: 1,724 matches
```

### Example 11: Sampling save() Usage (--limit-per-file)

```bash
# Get 3 examples from each file
qi 'save' -i call --limit-per-file 3

# Result: 387 matches across 129+ files
# Shows distribution:
# - django/forms/models.py: 3 matches
# - django/contrib/admin/options.py: 3 matches
# - tests/auth_tests/test_forms.py: 3 matches
# ... (many more files)

# Compare with regular limit
qi 'save' -i call --limit 20
# Might show all 20 from just 2-3 files!

# Sample async functions across codebase
qi '%' -i func -m async --limit-per-file 2
# Shows 2 async functions from each of 94 files
```

---

## Best Practices & Learning Path

### Progressive Learning Path

**Level 1: Basics (Start here)**
```bash
qi "symbol"                    # Find a symbol
qi "symbol" -i func            # Filter by context
qi "symbol" -x noise           # Exclude comments/strings
```

**Level 2: Structure First (Game changer!)**
```bash
qi '*' -f 'file.py' --toc      # Always start with TOC
qi "func" -i func -e           # See implementations with -e
qi "Class" -i class -e         # Expand class definitions
```

**Level 3: Advanced Filtering**
```bash
qi '%' -i func -c '@property'  # Filter by decorator
qi '%' -i func -m async        # Filter by modifier
qi 'save' -i call -p 'form'    # Filter by parent
```

**Level 4: Power Combinations**
```bash
qi 'user' --within 'authenticate'              # Scoped search
qi 'user' 'backend' --and 5                    # Related patterns
qi '%' -i func -m async -c '@%' --limit 20     # Multiple filters
qi '%' -i call -p 'self' --within 'save_base'  # Triple combo
```

**Level 5: Master Workflows**
- Combine TOC + -e + --within for complete understanding
- Use --limit-per-file for distributed sampling
- Stack filters creatively for precision
- Use -v to discover new patterns

### Common Pitfalls to Avoid

**Don't do this:**
```bash
# Reading files instead of using TOC
cat file.py | less

# Using grep instead of qi for code
grep -r "authenticate" .

# Forgetting to exclude noise
qi "error"  # Includes comments/strings

# Not using -e to see code
qi "func" -i func  # Just shows line numbers
```

**Do this instead:**
```bash
# Use TOC for structure
qi '*' -f 'file.py' --toc

# Use qi for semantic search
qi "authenticate" -i func

# Always exclude noise for code symbols
qi "error" -x noise

# See code inline
qi "func" -i func -e
```

### Efficiency Tips

1. **Start with TOC**: `qi '*' -f 'file.py' --toc` shows structure instantly
2. **Use -e liberally**: Seeing code inline is faster than jumping to files
3. **Combine filters**: `-i func -m async -c '@%'` is more powerful than separate queries
4. **Use --within**: Understanding what a function does without reading entire file
5. **Sample with --limit-per-file**: Get distributed examples across codebase
6. **Exclude noise by default**: Add `-x noise` to most queries

### When to Use What

| Task | Best Tool |
|------|-----------|
| File structure | `qi '*' -f 'file.py' --toc` |
| See function code | `qi "func" -i func -e` |
| How objects are used | `qi '%' -i call -p 'object'` |
| What function does | `qi '%' --within 'function'` |
| Related patterns | `qi 'a' 'b' --and 5` |
| Sample across files | `qi '%' --limit-per-file 3` |
| Async functions | `qi '%' -i func -m async` |
| Decorated functions | `qi '%' -i func -c '@%'` |
| Literal text search | `grep` (not qi) |

---

## Comparison with Other Tools

### qi vs grep

```bash
# grep: Text-based, many false positives
grep -r "authenticate" .

# qi: Semantic, finds only real functions
qi "authenticate" -i func

# grep: Can't distinguish decorators
grep -r "@property" .

# qi: Finds decorated functions
qi '%' -i func -c '@property'
```

### qi vs IDE "Find Usages"

```bash
# IDE: Great for active development
# qi: Great for codebase-wide analysis, scriptable

# IDE: Click, wait, see results
# qi: Command line, composable with shell tools

# qi advantages:
qi '%' -i func -m async --files          # All async modules
qi '%' -i func -c '@%' --columns clue    # All decorators
```

---

## Troubleshooting

### Pattern Not Matching

```bash
# Problem: Symbol not found
qi "MyFunction" -i func
# No results

# Solution 1: Try wildcard
qi "%function%" -i func

# Solution 2: Search without context filter
qi "MyFunction"

# Solution 3: Try case variations
qi "%Function" -i func
```

### Too Many Results

```bash
# Problem: Thousands of matches
qi "test"
# Found 5000 matches

# Solution 1: Add context filter
qi "test" -i func

# Solution 2: Exclude noise
qi "test" -x noise

# Solution 3: Add file filter
qi "test" -i func -f '%views.py'

# Solution 4: Use limit
qi "test" -i func --limit 20
```

### Symbol is a Keyword

```bash
# Problem: Keyword filtered
qi "import"
# Note: 'import' is in keywords.txt

# Solution: Use wildcard
qi "%import%"

# Or search in strings
qi "import" -i string
```

---

## Quick Command Reference

| Task | Command |
|------|---------|
| Find function | `qi "name" -i func` |
| Find class | `qi "Name" -i class` |
| Find async functions | `qi '%' -i func -m async` |
| Find decorators | `qi '%' -i func -c '@%'` |
| Find @property | `qi '%' -i func -c '@property'` |
| Find generators | `qi '%' -c 'yield'` |
| Find walrus operator | `qi '%' -c ':='` |
| Find lambdas | `qi '%' -c 'lambda'` |
| Find exceptions | `qi '%Error' -i class` |
| Expand definition | `qi "name" -i func -e` |
| Table of contents | `qi '*' -f 'file.py' --toc` |
| Show context | `qi "name" -C 20` |
| Exclude noise | `qi "name" -x noise` |
| Limit results | `qi '%' --limit 50` |
| Limit per file | `qi '%' --limit-per-file 3` |
| Filter by parent | `qi 'save' -i call -p 'form'` |
| Search within function | `qi 'user' --within 'authenticate'` |
| Related patterns (same line) | `qi 'request' 'user' --and` |
| Related patterns (N lines) | `qi 'error' 'raise' --and 5` |
| Custom columns | `qi '%' --columns line,symbol,clue` |

---

*This guide covers qi with Python-specific clue support (decorators, async, yield, walrus, etc.). Last updated: December 2025.*
