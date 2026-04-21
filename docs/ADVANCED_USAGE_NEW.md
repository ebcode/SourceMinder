# Advanced Filtering (Power User Features)

Beyond basic pattern matching, qi tracks rich metadata about every symbol. These filters enable greater precision.

## Parent Symbol (`-p` / `--parent`)
Find member access, method calls, and struct/class fields:

```bash
qi count -p patterns               # Find patterns->count or patterns.count
qi 'field%' -p 'config%'           # All fields accessed on config objects
qi init -p User                    # Find User->init or User.init methods
qi '*' -p 'request%' --limit 20    # All members of request-like objects
```

**How it works:** Tracks the parent in expressions like `obj->field`, `obj.method()`, `array[i].field`

## Type Annotation (`-t` / `--type`)
Filter by type declarations (for refactoring):

```bash
qi '*' -i arg -t 'int *'           # All int pointer arguments
qi '*' -i var -t 'char *'          # All char pointer variables
qi '*' -i arg -t 'OldType%'        # Find args using deprecated types
qi '*' -t 'uint32_t' --limit 20    # All uint32_t usage
```

**Use case:** Type-based refactoring, finding all uses of a specific type

## Modifier (`-m` / `--modifier`)
Filter by access modifiers and storage classes:

```bash
qi '*' -i func -m static --limit 10  # All static functions
qi '*' -i var -m const               # All const variables
qi '*' -m private                    # Private members
qi '*' -m inline -i func             # Inline functions
```

**Use case:** Understanding code visibility, finding optimization candidates

## Scope (`-s` / `--scope`)
Filter by visibility scope:

```bash
qi '*' -s public -i func             # Public API functions
qi '*' -s private -i prop            # Private properties
qi method -s protected               # Protected methods
```

**Use case:** API analysis, understanding encapsulation

## Scoped Search (`--within`)
Search only within specific functions or classes:

```bash
qi malloc --within handle_request    # malloc calls only in handle_request
qi fprintf --within 'debug%'         # fprintf in debug-related functions
qi strcmp --within parse_args        # strcmp only in parse_args
qi '*' -i var --within main          # All variables declared in main
```

**Use case:** Understanding local behavior, debugging specific functions

## Combining Filters
Stack filters for maximum precision:

```bash
# Find static int pointer arguments in memory functions
qi '*' -i arg -t 'int *' -m static --within 'mem*'

# Find private fields accessed on config objects
qi '*' -p config -s private -i prop

# Find all malloc calls in static helper functions
qi malloc -i call --within '*helper*' -m static
```

**Pro tip:** Use `-v` (verbose) to see all available columns, then craft precise queries
