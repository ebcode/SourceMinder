/*
 * Benchmark: Optimized struct layout (hot fields first)
 * Hot fields grouped in first cache line (64 bytes)
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdint.h>

//define ITERATIONS 10000000
#define ITERATIONS 1000000

/* Optimized layout - hot fields in first cache line */
typedef struct {
    /* HOT FIELDS - First cache line (64 bytes) */
    int line;                      // 4 bytes - ACCESSED IN FILTERS
    int context;                   // 4 bytes - ACCESSED IN FILTERS
    int is_definition;             // 4 bytes - ACCESSED IN FILTERS
    int _padding;                  // 4 bytes - alignment padding
    // 48 bytes remaining in cache line (could add pointers here)
    char _cache_padding[48];       // Pad to 64 bytes

    /* COLD FIELDS - Subsequent cache lines */
    char symbol[512];              // Rarely accessed during filtering
    char directory[1024];          // Rarely accessed during filtering
    char filename[256];            // Rarely accessed during filtering
    char full_symbol[512];         // Rarely accessed during filtering
    char source_location[128];     // Rarely accessed during filtering
    char parent_symbol[512];       // Rarely accessed during filtering
} IndexEntryOptimized;

/* Global counter to prevent optimization */
volatile uint64_t g_matches = 0;

/* Simulate filtering operation - accesses hot fields only */
static inline int filter_entry(const IndexEntryOptimized *entry, int min_line, int max_line, int target_context) {
    // This is what happens in query filtering - we only access hot fields
    if (entry->line >= min_line && entry->line <= max_line &&
        entry->context == target_context &&
        entry->is_definition == 1) {
        return 1;
    }
    return 0;
}

int main(void) {
    // Allocate array of entries
    IndexEntryOptimized *entries = malloc(ITERATIONS * sizeof(IndexEntryOptimized));
    if (!entries) {
        fprintf(stderr, "Failed to allocate memory\n");
        return 1;
    }

    // Initialize with random data
    srand(42); // Fixed seed for reproducibility (same as original)
    for (int i = 0; i < ITERATIONS; i++) {
        entries[i].line = rand() % 1000;
        entries[i].context = rand() % 20;
        entries[i].is_definition = rand() % 2;
        snprintf(entries[i].symbol, sizeof(entries[i].symbol), "symbol_%d", i);
        snprintf(entries[i].directory, sizeof(entries[i].directory), "/path/to/dir_%d/", i);
        snprintf(entries[i].filename, sizeof(entries[i].filename), "file_%d.c", i);
    }

    // Benchmark - filter through entries (only accessing hot fields)
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < ITERATIONS; i++) {
        if (filter_entry(&entries[i], 100, 900, 5)) {
            g_matches++;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    // Calculate elapsed time
    double elapsed = (end.tv_sec - start.tv_sec) +
                    (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("Optimized layout: %.3f seconds (%.0f Mops/sec)\n",
           elapsed, ITERATIONS / elapsed / 1e6);
    printf("Matches: %lu, Struct size: %zu bytes\n",
           (unsigned long)g_matches, sizeof(IndexEntryOptimized));

    free(entries);
    return 0;
}
