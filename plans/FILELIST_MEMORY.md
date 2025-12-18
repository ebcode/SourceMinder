# FileList Memory Optimization Plan

## Current State: Memory Usage Analysis

### Memory Consumption

```
Structure            Current Size    Usage Pattern
-------------------- --------------- ----------------------------------
FileList             97.66 MB        Per-indexer run (stack allocated)
```

### Key Constants

```c
MAX_FILES            100,000         // Files in directory traversal
DIRECTORY_MAX_LENGTH 1,024           // Path length per file
```

### Current Memory Impact

- **Each indexer run:** ~97 MB minimum (FileList in directory mode)
- **Current implementation:** Fixed array of 1,024-byte strings
- **Actual usage:** Most projects have < 1,000 files

## The Problem

### Real-World Requirements

1. **Typical projects:** 50-500 files (using < 0.5 MB, wasting 97 MB)
2. **Large projects:** 1,000-10,000 files (using ~10 MB, still wasteful)
3. **Massive codebases:** TypeScript compiler ~50,000 files (would exceed limit!)
4. **No user intervention:** Must "just work" without recompilation

### Current Approach: Fixed Allocation

**Structure** (shared/file_walker.h:7-10):
```c
typedef struct {
    char files[MAX_FILES][DIRECTORY_MAX_LENGTH];  // 100,000 × 1,024 bytes
    int count;
} FileList;
```

**Philosophy:** Pre-allocate everything, no malloc/free complexity

**Advantages:**
- Simple, predictable
- No allocation failures mid-traversal

**Disadvantages:**
- Wastes 97 MB for tiny projects (99% waste for 50-file projects)
- Still fails on huge projects (>100,000 files)
- Each path uses 1,024 bytes even if it's only 20 bytes long

## Solution: Dynamic Allocation with Growth Strategy

### Implementation Details

**Updated FileList Structure:**

```c
// Before (fixed, 97 MB always)
typedef struct {
    char files[MAX_FILES][DIRECTORY_MAX_LENGTH];  // 100,000 paths
    int count;
} FileList;

// After (dynamic, grows as needed)
typedef struct {
    char **files;       // Array of string pointers (each malloc'd separately)
    int count;          // Current number of files
    int capacity;       // Current allocated capacity
} FileList;
```

**Growth Strategy:**

```c
#define FILE_LIST_INITIAL_CAPACITY 100
#define FILE_LIST_GROWTH_FACTOR 2

void init_file_list(FileList *list) {
    list->files = malloc(FILE_LIST_INITIAL_CAPACITY * sizeof(char*));
    if (!list->files) {
        fprintf(stderr, "FATAL: Failed to allocate FileList\n");
        exit(EXIT_FAILURE);
    }
    list->count = 0;
    list->capacity = FILE_LIST_INITIAL_CAPACITY;
}

void free_file_list(FileList *list) {
    // Free each individual path string
    for (int i = 0; i < list->count; i++) {
        free(list->files[i]);
    }
    // Free the array of pointers
    free(list->files);
    list->files = NULL;
    list->count = 0;
    list->capacity = 0;
}

static int ensure_file_list_capacity(FileList *list) {
    if (list->count < list->capacity) {
        return 0;  // Still have space
    }

    // Need to grow the pointer array
    int new_capacity = list->capacity * FILE_LIST_GROWTH_FACTOR;
    if (new_capacity <= list->capacity) {
        // Overflow protection
        return -1;
    }

    char **new_files = realloc(list->files, new_capacity * sizeof(char*));
    if (!new_files) {
        // Allocation failure - fatal error
        return -1;
    }

    list->files = new_files;
    list->capacity = new_capacity;
    return 0;
}

void add_file_to_list(FileList *list, const char *path) {
    // Ensure we have capacity for one more pointer
    if (ensure_file_list_capacity(list) != 0) {
        fprintf(stderr, "FATAL: Failed to grow FileList at %d files\n", list->count);
        fprintf(stderr, "       Cannot add file: %s\n", path);
        exit(EXIT_FAILURE);
    }

    // Allocate exact size for this specific path (not 1,024 bytes!)
    list->files[list->count] = strdup(path);
    if (!list->files[list->count]) {
        fprintf(stderr, "FATAL: Failed to allocate path string: %s\n", path);
        exit(EXIT_FAILURE);
    }

    list->count++;
}
```

### Memory Usage Comparison

**Typical project (50 files, avg 40 chars/path):**
- Current: 97.66 MB (100% waste)
- Dynamic: ~0.003 MB (99.997% savings)
  - Pointer array: 100 pointers × 8 bytes = 800 bytes
  - Path strings: 50 × 40 bytes = 2,000 bytes
  - Total: ~2.8 KB vs 97.66 MB

**Medium project (1,000 files, avg 60 chars/path):**
- Current: 97.66 MB (99.4% waste)
- Dynamic: ~0.068 MB (99.93% savings)
  - Pointer array: 1,024 pointers × 8 bytes = 8,192 bytes
  - Path strings: 1,000 × 60 bytes = 60,000 bytes
  - Total: ~68 KB vs 97.66 MB

**Large project (10,000 files, avg 80 chars/path):**
- Current: 97.66 MB (99.2% waste)
- Dynamic: ~0.85 MB (99.1% savings)
  - Pointer array: 16,384 pointers × 8 bytes = 131 KB
  - Path strings: 10,000 × 80 bytes = 800 KB
  - Total: ~0.85 MB vs 97.66 MB

**Extreme project (50,000 files, avg 100 chars/path):**
- Current: 97.66 MB (silently drops files!)
- Dynamic: ~5.4 MB (94.5% savings, no data loss)
  - Pointer array: 65,536 pointers × 8 bytes = 512 KB
  - Path strings: 50,000 × 100 bytes = 5 MB
  - Total: ~5.4 MB vs 97.66 MB (and no limit exceeded!)

### Error Handling Strategy

**Fail-Fast Philosophy:**
- Initial allocation fails → Exit with clear error
- Growth realloc fails → Exit with clear error
- Individual strdup fails → Exit with clear error

**No silent failures** - consistent with ParseResult implementation and project architecture.

---

## Implementation Plan

### Phase 1: Update FileList Structure (shared/file_walker.h)

**File:** `shared/file_walker.h`

**Changes:**
```c
// Update struct definition
typedef struct {
    char **files;       // Dynamic array of string pointers
    int count;          // Current number of files
    int capacity;       // Allocated capacity
} FileList;

// Add function declarations
int init_file_list(FileList *list);
void free_file_list(FileList *list);
void add_file_to_list(FileList *list, const char *path);
```

**Impact:** Header only, no implementation yet

---

### Phase 2: Implement Core Functions (shared/file_walker.c)

**File:** `shared/file_walker.c`

**Changes:**

1. Add growth constants at top of file
2. Implement `init_file_list()`
3. Implement `free_file_list()`
4. Implement `ensure_file_list_capacity()` (static helper)
5. Implement `add_file_to_list()`

**Key Implementation Notes:**
- Use `strdup()` for exact-size path allocation
- Fail fast on any allocation failure (exit(EXIT_FAILURE))
- Follow same pattern as ParseResult implementation

---

### Phase 3: Update find_files() Function (shared/file_walker.c)

**Current usage pattern:**
```c
// In find_files(), currently does:
snprintf(files->files[files->count], DIRECTORY_MAX_LENGTH, "%s", full_path);
files->count++;
```

**New usage pattern:**
```c
// Will change to:
add_file_to_list(files, full_path);
```

**Files to check:** Use `qi` to find all places that access `files->files`:

```bash
qi 'files->files' -f shared/file_walker.c
```

**Expected locations:**
- `find_files()` - main usage, adds files to list
- Any helper functions that iterate over the file list

---

### Phase 4: Update Callers in indexer_main.c

**File:** `shared/indexer_main.c`

**Current pattern (line 498):**
```c
FileList *files = malloc(sizeof(FileList));
if (!files) {
    fprintf(stderr, "Failed to allocate memory for file list\n");
    // error handling...
}
```

**New pattern:**
```c
FileList *files = malloc(sizeof(FileList));
if (!files) {
    fprintf(stderr, "Failed to allocate memory for file list\n");
    // error handling...
}

if (init_file_list(files) != 0) {
    fprintf(stderr, "Failed to initialize file list\n");
    free(files);
    // error handling...
}
```

**Cleanup pattern (line 535 - currently just `free(files)`):**
```c
free_file_list(files);
free(files);
```

**Search for cleanup locations:**
```bash
qi files -f shared/indexer_main.c -w indexer_main | grep -E "(malloc|free)"
```

---

### Phase 5: Update File List Iteration Patterns

**Current iteration pattern:**
```c
for (int i = 0; i < files->count; i++) {
    // files->files[i] is a char[1024]
    do_something(files->files[i]);
}
```

**New iteration pattern (unchanged!):**
```c
for (int i = 0; i < files->count; i++) {
    // files->files[i] is now a char*
    do_something(files->files[i]);  // Same syntax!
}
```

**Note:** The syntax is identical, so most code won't need changes.

**However, verify all accesses:**
```bash
qi 'files->files[' -f shared/indexer_main.c shared/file_walker.c
```

---

### Phase 6: Update Constants Documentation

**File:** `shared/constants.h`

**Update comment:**
```c
/* ============================================================================
 * Array Capacity Limits
 * ============================================================================
 * These define the maximum number of elements in various collections.
 * Exceeding these limits will cause items to be silently dropped or ignored.
 *
 * NOTE: MAX_PARSE_ENTRIES and MAX_FILES are now legacy - both ParseResult and
 * FileList use dynamic allocation with no hard limits. Constants kept for reference.
 */

/* Legacy: Maximum number of index entries from parsing a single file
 * ParseResult now uses dynamic allocation starting at 1,000 entries and
 * growing by 2x as needed. No hard limit. */
#define MAX_PARSE_ENTRIES 100000

/* Legacy: Maximum number of files in directory traversal
 * FileList now uses dynamic allocation starting at 100 files and
 * growing by 2x as needed. No hard limit. */
#define MAX_FILES 100000
```

---

### Phase 7: Testing & Validation

#### Test 1: Small Project (< 100 files)

```bash
# Should use minimal memory
rm -f /tmp/test-filelist.db
valgrind --tool=massif --massif-out-file=massif-small.out \
    ./index-c ./tmp/*.c --once --db-file /tmp/test-filelist.db

ms_print massif-small.out | head -60
```

**Expected:** FileList using < 10 KB

#### Test 2: Medium Project (~1,000 files)

```bash
# Index entire source tree
rm -f /tmp/test-filelist.db
valgrind --tool=massif --massif-out-file=massif-medium.out \
    ./index-c ./c ./typescript ./go ./php ./python ./shared --once \
    --db-file /tmp/test-filelist.db

ms_print massif-medium.out | head -60
```

**Expected:** FileList using ~100-500 KB

#### Test 3: Query Performance

```bash
# Verify indexing still works correctly
./qi '*' --db-file /tmp/test-filelist.db --files | wc -l
./qi test --db-file /tmp/test-filelist.db --limit 10
```

**Expected:** All files indexed, queries work normally

#### Test 4: Extreme File Count

```bash
# Create test with many tiny files
mkdir -p /tmp/filelist-stress
for i in {1..5000}; do
    echo "int test_$i() { return $i; }" > /tmp/filelist-stress/test_$i.c
done

rm -f /tmp/stress.db
./index-c /tmp/filelist-stress --once --db-file /tmp/stress.db

# Check it worked
./qi '*' --db-file /tmp/stress.db --files | wc -l  # Should be 5000
```

**Expected:** Handles 5,000+ files without issue, memory scales appropriately

#### Test 5: Memory Comparison

```bash
# Before and after massif comparison
echo "=== FileList Memory Usage ==="
echo "OLD (from massif-old.out):"
ms_print massif-old.out | grep -A5 "FileList"

echo ""
echo "NEW:"
ms_print massif-medium.out | grep -A5 "file_list"
```

---

## Pros and Cons

### PROS

1. **Massive memory savings**
   - 50-file project: 99.997% reduction (97 MB → 2.8 KB)
   - 1,000-file project: 99.93% reduction (97 MB → 68 KB)
   - 10,000-file project: 99.1% reduction (97 MB → 850 KB)

2. **Scales to unlimited files**
   - No 100,000 file hard limit
   - Can handle TypeScript compiler (50,000 files) ✓
   - Memory proportional to actual project size

3. **Better memory efficiency per-file**
   - OLD: Each path uses 1,024 bytes (even for "a.c")
   - NEW: Each path uses exact size (strdup)
   - Additional savings beyond just array size

4. **Consistent with ParseResult**
   - Same dynamic allocation pattern
   - Same fail-fast error handling
   - Same growth strategy (2x)

5. **"Just works" philosophy**
   - No recompilation needed
   - No config tuning
   - No silent data loss

### CONS

1. **Increased code complexity**
   - Need init/free functions
   - Must track capacity separately
   - More potential for memory leaks
   - Need to free each string individually

2. **Double indirection**
   - `files->files[i]` is now `char*` not `char[1024]`
   - One extra pointer dereference (negligible performance impact)
   - More cache-unfriendly than contiguous array

3. **Fragmentation risk**
   - Each path malloc'd separately
   - Could lead to heap fragmentation
   - Unlikely to be significant in practice

4. **API changes**
   - All callers need init/free calls
   - `snprintf(files->files[i], ...)` → `add_file_to_list()`
   - More error handling required

5. **Testing complexity**
   - Need to test allocation failures
   - Need to verify all cleanup paths
   - Valgrind testing essential

---

## Files to Modify

### Core Implementation (2 files)

1. **shared/file_walker.h**
   - Update `FileList` struct
   - Add function declarations

2. **shared/file_walker.c**
   - Implement `init_file_list()`
   - Implement `free_file_list()`
   - Implement `ensure_file_list_capacity()`
   - Implement `add_file_to_list()`
   - Update `find_files()` to use new functions

### Callers (1 file)

3. **shared/indexer_main.c**
   - Add `init_file_list()` call after malloc
   - Replace `free(files)` with `free_file_list(files); free(files);`
   - Update error handling paths

### Documentation (1 file)

4. **shared/constants.h**
   - Update `MAX_FILES` comment to mark as legacy

**Total: 4 files**

---

## Expected Memory Impact

### Combined Savings (ParseResult + FileList)

**Small project (50 files, 100 symbols/file):**
- Before: 477 MB (ParseResult 379 MB + FileList 97 MB)
- After ParseResult only: 9.5 MB (ParseResult 3.8 MB + FileList 97 MB)
- After both: ~0.05 MB (99.99% total savings)

**Medium project (1,000 files, 1,000 symbols/file):**
- Before: 477 MB
- After ParseResult only: 9.5 MB
- After both: ~5 MB (99% total savings)

**Large project (10,000 files, 5,000 symbols/file max):**
- Before: 477 MB
- After ParseResult only: ~100 MB (still limited by FileList)
- After both: ~20 MB (96% total savings)

**Combined with ParseResult optimization:**
- Typical projects: < 1 MB total memory usage
- Large projects: 5-20 MB (still 90-99% savings)
- No hard limits on file count or symbol count

---

## Migration Strategy

### Maintain Safety During Transition

1. **Keep MAX_FILES constant** - Use as capacity hint for testing
2. **Add warning if exceeds old limit** - Catch regressions
3. **Extensive valgrind testing** - Ensure no leaks
4. **Document all init/free requirements** - Clear ownership

### Testing Checklist

- [ ] Small project (< 100 files) works
- [ ] Medium project (~1,000 files) works
- [ ] Large project (> 10,000 files) works
- [ ] No memory leaks (valgrind clean)
- [ ] No performance regression
- [ ] All queries return correct results
- [ ] Error paths don't leak memory

---

## Alternative: Stay with Fixed Allocation

### If We Reject Dynamic Allocation

**Only option:** Keep current implementation

**Accept:**
- 97 MB memory usage for all indexer runs
- 100,000 file hard limit
- 1,024 bytes per path (even for short paths)

**Rationale:**
- FileList is only allocated in directory mode
- File mode (indexing specific files) doesn't use FileList
- 97 MB might be acceptable for modern systems
- Simplicity over memory efficiency

**Counter-argument:**
- We already optimized ParseResult (98% savings)
- Leaving FileList unoptimized is inconsistent
- 97 MB is the new bottleneck (was 477 MB, now 105 MB total)
- Easy optimization with proven pattern

---

## Recommendation

**Proceed with dynamic allocation** for consistency and completeness.

### Rationale

1. **Proven pattern:** ParseResult optimization was successful (98% savings)
2. **Same approach:** FileList can use identical growth strategy
3. **Removes last major bottleneck:** 97 MB → < 1 MB for typical projects
4. **No hard limits:** Handles unlimited files (TypeScript compiler, Linux kernel)
5. **Professional behavior:** Memory scales with actual usage

### Implementation Complexity

**Low-to-Medium:**
- Similar to ParseResult (already done)
- Only 4 files to modify
- Clear API (init/add/free)
- 2-3 hours of work + testing

### Risk Assessment

**Low:**
- Pattern already validated with ParseResult
- Fail-fast error handling (no silent failures)
- Easy to test (valgrind + massif)
- Can validate with stress tests (1,000s of files)

---

## Conclusion

FileList dynamic allocation is the natural next step after ParseResult optimization.

**Benefits:**
- 99%+ memory reduction for typical projects
- Removes 100,000 file hard limit
- Consistent with ParseResult implementation
- Completes the memory optimization effort

**Combined Result:**
- Before any optimizations: ~477 MB
- After ParseResult only: ~9.5 MB (98% savings)
- After both optimizations: < 1 MB (99.8% savings)

**This brings the indexer from "memory hog" to "memory efficient" status.**

---

## Next Steps

1. ✅ Review this plan
2. ⏳ Implement Phase 1 (update header)
3. ⏳ Implement Phase 2 (core functions)
4. ⏳ Implement Phase 3 (update find_files)
5. ⏳ Implement Phase 4 (update callers)
6. ⏳ Implement Phase 5 (verify iteration)
7. ⏳ Implement Phase 6 (update docs)
8. ⏳ Implement Phase 7 (testing & validation)
9. ⏳ Compare massif results (before/after)
10. ⏳ Update MEMORY_PLAN.md with final results

---

**Document Status:** Draft for review
**Last Updated:** 2025-12-05
**Author:** Claude Code (based on ParseResult optimization success)
