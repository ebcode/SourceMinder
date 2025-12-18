#include <stdio.h>
#include "../shared/database.h"
#include "../shared/constants.h"
#include "../shared/parse_result.h"
#include "../shared/file_walker.h"

int main() {
    printf("================================================================================\n");
    printf("MEMORY CONSUMPTION ANALYSIS - SourceMinder Indexer\n");
    printf("================================================================================\n\n");
    
    printf("1. BIGGEST MEMORY CONSUMERS\n");
    printf("   -------------------------\n");
    printf("   ParseResult (per file parse):     %9.2f MB  (%zu bytes)\n", 
           sizeof(ParseResult) / (1024.0 * 1024.0), sizeof(ParseResult));
    printf("     - IndexEntry array:             %9.2f MB  (%d × %zu bytes)\n",
           (MAX_PARSE_ENTRIES * sizeof(IndexEntry)) / (1024.0 * 1024.0),
           MAX_PARSE_ENTRIES, sizeof(IndexEntry));
    printf("\n");
    printf("   FileList (file discovery):        %9.2f MB  (%zu bytes)\n",
           sizeof(FileList) / (1024.0 * 1024.0), sizeof(FileList));
    printf("     - Path array:                   %9.2f MB  (%d × %d bytes)\n",
           (MAX_FILES * DIRECTORY_MAX_LENGTH) / (1024.0 * 1024.0),
           MAX_FILES, DIRECTORY_MAX_LENGTH);
    printf("\n");
    
    printf("2. STRUCT SIZES\n");
    printf("   -------------\n");
    printf("   IndexEntry:                       %9zu bytes\n", sizeof(IndexEntry));
    printf("   FileList:                         %9zu bytes\n", sizeof(FileList));
    printf("   ParseResult:                      %9zu bytes\n", sizeof(ParseResult));
    printf("\n");
    
    printf("3. KEY CONSTANTS\n");
    printf("   --------------\n");
    printf("   MAX_PARSE_ENTRIES:                %9d (entries per file)\n", MAX_PARSE_ENTRIES);
    printf("   MAX_FILES:                        %9d (files in directory scan)\n", MAX_FILES);
    printf("   MAX_PATTERNS:                     %9d (query patterns)\n", MAX_PATTERNS);
    printf("   DIRECTORY_MAX_LENGTH:             %9d bytes\n", DIRECTORY_MAX_LENGTH);
    printf("   FILENAME_MAX_LENGTH:              %9d bytes\n", FILENAME_MAX_LENGTH);
    printf("   SYMBOL_MAX_LENGTH:                %9d bytes\n", SYMBOL_MAX_LENGTH);
    printf("\n");
    
    printf("4. OPTIMIZATION OPPORTUNITIES\n");
    printf("   ---------------------------\n");
    printf("   A. Convert ParseResult to dynamic allocation:\n");
    printf("      Current: %d entries × %zu bytes = %.2f MB (always allocated)\n",
           MAX_PARSE_ENTRIES, sizeof(IndexEntry),
           (MAX_PARSE_ENTRIES * sizeof(IndexEntry)) / (1024.0 * 1024.0));
    printf("      Dynamic: Start with 1,000 entries, grow as needed\n");
    printf("      Savings for typical file: ~%.2f MB (99%%)\n\n",
           ((MAX_PARSE_ENTRIES - 1000) * sizeof(IndexEntry)) / (1024.0 * 1024.0));
    
    printf("   B. Convert FileList to dynamic allocation:\n");
    printf("      Current: %d paths × %d bytes = %.2f MB (always allocated)\n",
           MAX_FILES, DIRECTORY_MAX_LENGTH,
           (MAX_FILES * DIRECTORY_MAX_LENGTH) / (1024.0 * 1024.0));
    printf("      Dynamic: Start with 100 files, grow as needed\n");
    printf("      Savings for typical project: ~%.2f MB (99%%)\n\n",
           ((MAX_FILES - 100) * DIRECTORY_MAX_LENGTH) / (1024.0 * 1024.0));
    
    printf("   C. Reduce MAX_PARSE_ENTRIES from 100,000 to 10,000:\n");
    printf("      Savings: %.2f MB per ParseResult (90%%)\n",
           ((MAX_PARSE_ENTRIES - 10000) * sizeof(IndexEntry)) / (1024.0 * 1024.0));
    printf("      Note: 10,000 entries still handles very large files\n\n");
    
    printf("   D. Reduce MAX_FILES from 100,000 to 10,000:\n");
    printf("      Savings: %.2f MB per FileList (90%%)\n",
           ((MAX_FILES - 10000) * DIRECTORY_MAX_LENGTH) / (1024.0 * 1024.0));
    printf("      Note: 10,000 files is still a large codebase\n\n");
    
    printf("5. TOTAL POTENTIAL SAVINGS (Options C + D - simplest fixes):\n");
    printf("   ----------------------------------------------------------\n");
    float parse_savings = ((MAX_PARSE_ENTRIES - 10000) * sizeof(IndexEntry)) / (1024.0 * 1024.0);
    float file_savings = ((MAX_FILES - 10000) * DIRECTORY_MAX_LENGTH) / (1024.0 * 1024.0);
    printf("   ParseResult reduction:            %.2f MB\n", parse_savings);
    printf("   FileList reduction:               %.2f MB\n", file_savings);
    printf("   TOTAL:                            %.2f MB (per indexer run)\n", 
           parse_savings + file_savings);
    printf("\n");
    
    return 0;
}
