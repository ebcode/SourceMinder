# Memory Optimization Plan - SourceMinder Indexer

## Current State: Memory Usage Analysis

### Biggest Memory Consumers

```
Structure            Current Size    Usage Pattern
-------------------- --------------- ----------------------------------
ParseResult          379.56 MB       Per-file parse (stack allocated)
FileList             97.66 MB        Per-indexer run (stack allocated)
IndexEntry           3,980 bytes     Building block for ParseResult
```

### Key Constants
```c
MAX_PARSE_ENTRIES    100,000         // Entries per single file parse
MAX_FILES            100,000         // Files in directory traversal
MAX_PATTERNS         512             // Query patterns (minimal impact)
```

### Total Memory Impact
- **Each indexer run:** ~477 MB minimum (ParseResult + FileList)
- **Already optimized:** IndexEntry reduced from ~5,420 to 3,980 bytes (26.6% savings via buffer sizing)

## The Problem

### Real-World Requirements
1. **Large test files:** Some projects have auto-generated test files with 10,000+ symbols
2. **Massive codebases:** TypeScript compiler has ~50,000 files
3. **No user intervention:** Must "just work" without recompilation or config changes
4. **No crashes:** Current fixed-size arrays silently drop data when limits are exceeded

### Current Approach: Fixed Allocation
- **Philosophy:** Pre-allocate everything, no malloc/free complexity
- **Advantage:** Simple, predictable, no allocation failures mid-parse
- **Disadvantage:** Wastes 477 MB even for tiny projects

## Solution Options

### Option A: Reduce Constants (REJECTED)

**Changes:**
- MAX_PARSE_ENTRIES: 100,000 → 10,000 (saves 341 MB)
- MAX_FILES: 100,000 → 10,000 (saves 88 MB)

**Pros:**
- Simple one-line change
- Immediate 429 MB savings

**Cons:**
- ❌ Silently drops data for large files (>10,000 symbols)
- ❌ Fails on huge projects (>10,000 files)
- ❌ Requires recompilation for edge cases
- ❌ Violates "just work" requirement

**Decision: REJECT** - Fails core requirements

---

### Option B: Dynamic Allocation with Growth Strategy (RECOMMENDED)

**Changes:**
1. Convert `ParseResult.entries` from `IndexEntry[100000]` to `IndexEntry *entries`
2. Convert `FileList.files` from `char[100000][1024]` to `char **files`
3. Add init/free functions and realloc() growth logic

#### Implementation Details

**ParseResult - Dynamic Array:**
```c
// Before (fixed, 379 MB always)
typedef struct ParseResult {
    IndexEntry entries[MAX_PARSE_ENTRIES];  // 100,000 entries
    int count;
} ParseResult;

// After (dynamic, grows as needed)
typedef struct ParseResult {
    IndexEntry *entries;     // malloc'd, grows with realloc
    int count;               // Current number of entries
    int capacity;            // Current allocated capacity
} ParseResult;

// Growth strategy
#define PARSE_RESULT_INITIAL_CAPACITY 1000
#define PARSE_RESULT_GROWTH_FACTOR 2

void init_parse_result(ParseResult *result) {
    result->entries = malloc(PARSE_RESULT_INITIAL_CAPACITY * sizeof(IndexEntry));
    result->count = 0;
    result->capacity = PARSE_RESULT_INITIAL_CAPACITY;
    // Error handling: if malloc fails, set capacity=0, count=0
}

void ensure_capacity(ParseResult *result, int needed) {
    if (needed <= result->capacity) return;

    int new_capacity = result->capacity;
    while (new_capacity < needed) {
        new_capacity *= PARSE_RESULT_GROWTH_FACTOR;
    }

    IndexEntry *new_entries = realloc(result->entries, new_capacity * sizeof(IndexEntry));
    if (new_entries) {
        result->entries = new_entries;
        result->capacity = new_capacity;
    } else {
        // Handle allocation failure - stop adding entries
        fprintf(stderr, "Warning: Memory allocation failed at %d entries\n", result->count);
    }
}

void free_parse_result(ParseResult *result) {
    free(result->entries);
    result->entries = NULL;
    result->count = 0;
    result->capacity = 0;
}
```

**FileList - Dynamic Array of Strings:**
```c
// Before (fixed, 97.66 MB always)
typedef struct {
    char files[MAX_FILES][DIRECTORY_MAX_LENGTH];  // 100,000 paths
    int count;
} FileList;

// After (dynamic, grows as needed)
typedef struct {
    char **files;       // Array of string pointers
    int count;
    int capacity;
} FileList;

#define FILE_LIST_INITIAL_CAPACITY 100
#define FILE_LIST_GROWTH_FACTOR 2

void init_file_list(FileList *list) {
    list->files = malloc(FILE_LIST_INITIAL_CAPACITY * sizeof(char*));
    list->count = 0;
    list->capacity = FILE_LIST_INITIAL_CAPACITY;
}

void add_file(FileList *list, const char *path) {
    if (list->count >= list->capacity) {
        int new_capacity = list->capacity * FILE_LIST_GROWTH_FACTOR;
        char **new_files = realloc(list->files, new_capacity * sizeof(char*));
        if (new_files) {
            list->files = new_files;
            list->capacity = new_capacity;
        } else {
            fprintf(stderr, "Warning: Cannot add more files (limit: %d)\n", list->count);
            return;
        }
    }

    // Allocate string for this specific path (exact size)
    list->files[list->count] = strdup(path);
    list->count++;
}

void free_file_list(FileList *list) {
    for (int i = 0; i < list->count; i++) {
        free(list->files[i]);
    }
    free(list->files);
    list->files = NULL;
    list->count = 0;
    list->capacity = 0;
}
```

#### Memory Usage Comparison

**Typical small project (50 files, 100 symbols/file):**
- Current: 477 MB (100% waste)
- Dynamic: ~0.4 MB (99.9% savings)

**Large project (10,000 files, 1,000 symbols/file):**
- Current: 477 MB
- Dynamic: ~40 MB (91% savings)

**Extreme project (50,000 files, 5,000 symbols/file max):**
- Current: 477 MB (silently drops data!)
- Dynamic: ~150 MB (69% savings, no data loss)

#### Error Handling Strategy

**Allocation Failure Modes:**
1. **Initial allocation fails:** Set capacity=0, continue without crashing (graceful degradation)
2. **Growth realloc fails:** Keep existing data, stop adding new entries, warn user
3. **Individual strdup fails:** Skip that entry, continue processing

**Philosophy:** Never crash - always try to produce partial results.

---

### Option C: Hybrid Approach

**Strategy:** Use moderate fixed sizes with dynamic overflow

**Implementation:**
- Start with fixed arrays (e.g., 1,000 entries)
- Switch to malloc when limit exceeded
- Requires `union` or flag to track allocation mode

**Pros:**
- Fast path avoids malloc for typical cases
- Still handles huge files

**Cons:**
- More complex code (two code paths)
- Still wastes memory for small projects
- Harder to maintain

**Decision: REJECT** - Complexity not worth it vs pure dynamic

---

## Pros and Cons of Dynamic Allocation

### PROS

1. **Scales to any size**
   - No hard limits on file count or symbol count
   - Handles TypeScript compiler (50,000 files) ✓
   - Handles auto-generated test files (10,000+ symbols) ✓

2. **Minimal memory for small projects**
   - 50-file project: 0.4 MB instead of 477 MB (99.9% savings)
   - Typical project: 40 MB instead of 477 MB (91% savings)

3. **"Just works" philosophy**
   - No recompilation needed
   - No config file tuning
   - No silent data loss

4. **Better error handling**
   - Can detect and report allocation failures
   - Graceful degradation instead of silent truncation

5. **Memory proportional to actual usage**
   - Pay for what you use
   - Better for memory-constrained systems

### CONS

1. **Increased code complexity**
   - Need init/free functions for every structure
   - Must track capacity separately from count
   - Realloc logic in multiple places
   - More potential for memory leaks

2. **Allocation failure handling required**
   - malloc/realloc can fail
   - Need graceful degradation strategy
   - More error paths to test

3. **Performance considerations**
   - realloc() can be expensive (copies data)
   - Memory fragmentation possible
   - Not as cache-friendly as contiguous stack allocation

4. **Departure from current philosophy**
   - Current code is simple: "allocate once, use, done"
   - Dynamic requires: "init, grow as needed, remember to free"
   - More cognitive overhead for developers

5. **Potential for use-after-free bugs**
   - Must ensure all code paths call free
   - Especially in error handling (goto cleanup patterns)
   - Valgrind testing becomes essential

6. **API changes throughout codebase**
   - All language parsers need updates
   - File walker callers need updates
   - Indexer main() needs cleanup logic

## Recommendation: Implement Dynamic Allocation

### Rationale

Despite the cons, dynamic allocation is the **only solution** that meets all requirements:

1. ✓ Handles real-world huge projects (TypeScript compiler, Linux kernel)
2. ✓ No user intervention required
3. ✓ Scales memory usage appropriately
4. ✓ No silent data loss

The current 477 MB fixed allocation wastes **99.9% of memory** for typical projects while **still failing** on extreme edge cases.

### Implementation Plan

**Phase 1: ParseResult (Highest Impact)**
1. Update `shared/parse_result.h` - Change struct, add init/free functions
2. Update `shared/parse_result.c` - Implement dynamic growth in `add_entry()`
3. Update all language parsers - Add init/free calls
4. Update `shared/indexer_main.c` - Add cleanup in error paths
5. Test with small and large files

**Phase 2: FileList (High Impact)**
1. Update `shared/file_walker.h` - Change struct, add init/free functions
2. Update `shared/file_walker.c` - Implement dynamic growth
3. Update all indexer main files - Add init/free calls
4. Test with small and large codebases

**Phase 3: Testing & Validation**
1. Run valgrind on all indexers (check for leaks)
2. Test with extreme cases (1M symbol file, 100K file project)
3. Test allocation failure paths (ulimit testing)
4. Measure actual memory usage improvements

### Migration Strategy

**Maintain backward compatibility during transition:**
- Keep MAX_PARSE_ENTRIES and MAX_FILES constants
- Use as capacity hints, not hard limits
- Remove constants only after dynamic allocation is proven stable

**Safety measures:**
- Add compiler warnings if growth exceeds old limits (catch regressions)
- Extensive testing with valgrind
- Document all init/free requirements

## Alternative: Stay with Fixed Allocation

### If We Reject Dynamic Allocation

**Only viable option:** Increase constants and accept the memory cost

```c
#define MAX_PARSE_ENTRIES 100000  // Keep as-is (handles large test files)
#define MAX_FILES         100000  // Keep as-is (handles huge projects)
```

**Accept:**
- 477 MB memory usage for all indexer runs
- Current 22% memory savings from buffer optimizations is good enough
- Simple code is worth the memory cost

**Mitigate:**
- Document memory requirements in README
- Add warning if limits are hit (stderr message)
- Consider mmap for ParseResult (OS handles paging)

## Conclusion

**Recommended Path:** Implement dynamic allocation (Option B)

The current fixed allocation approach was appropriate for initial development (simple, predictable), but the memory cost (477 MB minimum) is untenable for a tool that should "just work" on any codebase.

Dynamic allocation adds complexity but delivers:
- **99% memory savings** for typical projects
- **No hard limits** for extreme projects
- **Professional tool behavior** (scales transparently)

The implementation complexity is manageable with careful init/free discipline and valgrind testing.

---

## Memory Usage After All Optimizations

**Before any optimizations:**
- IndexEntry: ~5,420 bytes
- ParseResult: ~530 MB fixed
- FileList: ~97 MB fixed
- **Total: ~627 MB per indexer run**

**After buffer optimizations (completed):**
- IndexEntry: 3,980 bytes (26.6% reduction)
- ParseResult: 379 MB fixed
- FileList: 97 MB fixed
- **Total: 477 MB per indexer run (24% savings)**

**After dynamic allocation (proposed):**
- Typical small project: 0.4 MB (99.9% savings vs current)
- Large project (10K files): 40 MB (92% savings vs current)
- Extreme project (50K files): 150 MB (69% savings vs current, no data loss)

**Combined impact:** Up to 99.9% memory reduction while supporting unlimited scale.
