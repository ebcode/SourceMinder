/* SourceMinder
 * Copyright 2025 Eli Bird 
 * 
 * This file is part of SourceMinder.
 * 
 * SourceMinder is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, or (at
 *  your option) any later version.
 *
 * SourceMinder is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * General Public License for more details.
 * You should have received a copy of the GNU General Public License 
 * along with SourceMinder. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef TOC_WEB_H
#define TOC_WEB_H

#include <stddef.h>

/* --toc configuration for the web/WASM bridge (no sqlite3 dependency) -- */

typedef struct {
    const char *directory;
    const char *filename;
} TocWebFilePattern;

typedef struct {
    TocWebFilePattern *file_patterns;   /* Normalized LIKE patterns from process_file_pattern_web */
    int file_pattern_count;
    const char **symbol_patterns;       /* LIKE patterns from convert_wildcards_web */
    int symbol_pattern_count;
    const char **include_contexts;      /* Context type names: "FUNC", "CLASS", etc. */
    int include_context_count;
    const char **exclude_contexts;      /* Context type names to exclude */
    int exclude_context_count;
    int limit;
} TocWebConfig;

/* Context types allowed in --toc output */
static const char * const TOC_ALLOWED_CONTEXTS_WEB[] = {
    "FILE", "CLASS", "FUNC", "ENUM", "TYPE", "MACRO", "IMP",
    NULL
};

/* Build TOC SQL query.  Returns a malloc'd string; caller must free(). */
char *build_toc_web_sql(const TocWebConfig *config);

/* Format TOC output from raw SQL result rows + context count data.
 *
 * build_info:         metadata emitted by qi_web_build
 * rows_tsv:           tab-separated rows: full_symbol \t line \t source_location
 *                     \t context \t directory \t filename
 * total_shown:        rows actually passed
 * total_available:    from COUNT query (may be larger than total_shown)
 * context_counts:     key:value pairs separated by newlines
 *                     e.g. "FUNC:45\nCLASS:12\nENUM:3"
 * Returns:            malloc'd formatted TOC output string.
 */
char *format_toc_web(const char *build_info, const char *rows_tsv,
                     int total_shown, int total_available,
                     const char *context_counts);

#endif /* TOC_WEB_H */
