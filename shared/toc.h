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
 * along with Foobar. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef TOC_H
#define TOC_H

#include <stddef.h>
#include <sqlite3.h>

/* Context types allowed in --toc output
 * These are the context types that represent file structure/definitions
 * Other types like VAR, ARG, CALL, etc. are too numerous for TOC display
 */
static const char * const TOC_ALLOWED_CONTEXTS[] = {
    "FILE",
    "CLASS",
    "FUNC",
    "ENUM",
    "TYPE",
    "IMP",
    NULL  /* Sentinel */
};

/* Count of allowed context types (excluding sentinel) */
#define TOC_ALLOWED_CONTEXT_COUNT 6

/* File pattern for TOC */
typedef struct {
    const char *directory;              /* Directory part (can be NULL) */
    const char *filename;               /* Filename part */
} TocFilePattern;

/* TOC configuration */
typedef struct {
    TocFilePattern *file_patterns;      /* Array of file patterns */
    int file_pattern_count;             /* Number of file patterns */
    const char **symbol_patterns;       /* Optional: symbol name patterns */
    int symbol_pattern_count;
    const char **include_contexts;      /* Optional: context filters (func, struct, etc) */
    int include_context_count;
    int limit;                          /* Optional: limit total symbols (0 = no limit) */
    int limit_per_file;                 /* Optional: limit symbols per file (0 = no limit) */
    const char *db_path;                /* Database path */
} TocConfig;

/* Generate and print table of contents
 * Returns 0 on success, -1 on error
 */
int build_toc(const TocConfig *config);

#endif /* TOC_H */
