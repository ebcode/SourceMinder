#include <stdio.h>
#include <stddef.h>
#include "../shared/database.h"

int main() {
    printf("IndexEntry struct analysis:\n");
    printf("==============================\n");
    printf("Total size: %zu bytes\n\n", sizeof(IndexEntry));

    IndexEntry e;
    printf("Field offsets (hot fields should be at start):\n");
    printf("  line:          offset %3zu (size %zu)\n", offsetof(IndexEntry, line), sizeof(e.line));
    printf("  context:       offset %3zu (size %zu)\n", offsetof(IndexEntry, context), sizeof(e.context));
    printf("  is_definition: offset %3zu (size %zu)\n", offsetof(IndexEntry, is_definition), sizeof(e.is_definition));
    printf("\n");
    printf("  symbol:        offset %3zu (size %zu)\n", offsetof(IndexEntry, symbol), sizeof(e.symbol));
    printf("  directory:     offset %3zu (size %zu)\n", offsetof(IndexEntry, directory), sizeof(e.directory));
    printf("  filename:      offset %3zu (size %zu)\n", offsetof(IndexEntry, filename), sizeof(e.filename));
    printf("\n");

    // Calculate which cache line each hot field is in
    int line_cacheline = offsetof(IndexEntry, line) / 64;
    int context_cacheline = offsetof(IndexEntry, context) / 64;
    int is_def_cacheline = offsetof(IndexEntry, is_definition) / 64;

    printf("Cache line analysis (64-byte cache lines):\n");
    printf("  Hot fields in cache line(s): %d, %d, %d\n",
           line_cacheline, context_cacheline, is_def_cacheline);

    if (line_cacheline == 0 && context_cacheline == 0 && is_def_cacheline == 0) {
        printf("  ✓ All hot fields in first cache line!\n");
    } else {
        printf("  ✗ Hot fields span multiple cache lines (suboptimal)\n");
    }

    return 0;
}
