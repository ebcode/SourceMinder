#include <stdio.h>
#include "../shared/database.h"
#include "../shared/constants.h"
#include "../shared/parse_result.h"

int main() {
    printf("Memory Usage Analysis\n");
    printf("=====================\n\n");
    
    printf("Core Structures:\n");
    printf("  IndexEntry: %zu bytes\n", sizeof(IndexEntry));
    printf("  ParseResult: %zu bytes (%.2f MB)\n", 
           sizeof(ParseResult), 
           sizeof(ParseResult) / (1024.0 * 1024.0));
    printf("    - Array: %d entries × %zu bytes = %.2f MB\n",
           MAX_PARSE_ENTRIES,
           sizeof(IndexEntry),
           (MAX_PARSE_ENTRIES * sizeof(IndexEntry)) / (1024.0 * 1024.0));
    
    printf("\nConstants:\n");
    printf("  MAX_PARSE_ENTRIES: %d\n", MAX_PARSE_ENTRIES);
    printf("  MAX_FILES: %d\n", MAX_FILES);
    printf("  MAX_PATTERNS: %d\n", MAX_PATTERNS);
    
    return 0;
}
