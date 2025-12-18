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
#ifndef PARSE_RESULT_H
#define PARSE_RESULT_H

#include "database.h"
#include "constants.h"

/* Parse result structure shared by all language parsers */
typedef struct ParseResult {
    IndexEntry *entries;     /* Dynamic array of index entries */
    int count;               /* Current number of entries */
    int capacity;            /* Allocated capacity */
} ParseResult;

/* Initialize a ParseResult with initial capacity
 * Returns: 0 on success, -1 on allocation failure
 */
int init_parse_result(ParseResult *result);

/* Free all memory associated with a ParseResult */
void free_parse_result(ParseResult *result);

/* Add a symbol entry to the parse result
 * Parameters:
 *   result          - ParseResult structure to add entry to
 *   symbol          - Symbol name to index
 *   line            - Line number where symbol appears
 *   context         - Context type (CONTEXT_FUNCTION, etc.)
 *   directory       - Directory path (relative)
 *   filename        - Filename
 *   source_location - Source location range (e.g., "15:0 - 17:1") or NULL
 *   ext             - Extended columns (or NULL for defaults)
 */
void add_entry(ParseResult *result, const char *symbol, int line,
              ContextType context, const char *directory,
              const char *filename, const char *source_location,
              const ExtColumns *ext);

#endif /* PARSE_RESULT_H */
