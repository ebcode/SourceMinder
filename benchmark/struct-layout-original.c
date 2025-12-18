/*
 * Benchmark: Original struct layout (cold fields first)
 * Simulates IndexEntry with mixed hot/cold fields
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdint.h>

//define ITERATIONS 10000000
#define ITERATIONS 1000000

/* Original layout - large string buffers first (cold data) */
typedef struct {
    char symbol[512];              // Cold (rarely accessed in filters)
    char directory[1024];          // Cold
    char filename[256];            // Cold
    int line;                      // HOT (accessed in every filter)
    int context;                   // HOT (accessed in every filter)
    char full_symbol[512];         // Cold
    char source_location[128];     // Cold
    char parent_symbol[512];       // Cold
    int is_definition;             // HOT (accessed in filters)
} IndexEntryOriginal;

/* Global counter to prevent optimization */
volatile uint64_t g_matches = 0;

/* Simulate filtering operation - accesses hot fields only */
static inline int filter_entry(const IndexEntryOriginal *entry, int min_line, int max_line, int target_context) {
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
    IndexEntryOriginal *entries = malloc(ITERATIONS * sizeof(IndexEntryOriginal));
    if (!entries) {
        fprintf(stderr, "Failed to allocate memory\n");
        return 1;
    }

    // Initialize with random data
    srand(42); // Fixed seed for reproducibility
    for (int i = 0; i < ITERATIONS; i++) {
        snprintf(entries[i].symbol, sizeof(entries[i].symbol), "symbol_%d", i);
        snprintf(entries[i].directory, sizeof(entries[i].directory), "/path/to/dir_%d/", i);
        snprintf(entries[i].filename, sizeof(entries[i].filename), "file_%d.c", i);
        entries[i].line = rand() % 1000;
        entries[i].context = rand() % 20;
        entries[i].is_definition = rand() % 2;
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

    printf("Original layout: %.3f seconds (%.0f Mops/sec)\n",
           elapsed, ITERATIONS / elapsed / 1e6);
    printf("Matches: %lu, Struct size: %zu bytes\n",
           (unsigned long)g_matches, sizeof(IndexEntryOriginal));

    free(entries);
    return 0;
}
