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
#ifndef INDEXER_MAIN_H
#define INDEXER_MAIN_H

#include "database.h"
#include "filter.h"
#include "parse_result.h"

/* Function pointer types for language-specific operations */
typedef void* (*ParserInitFunc)(SymbolFilter *filter);
typedef int (*ParserParseFunc)(void *parser, const char *filepath, const char *project_root, ParseResult *result);
typedef void (*ParserFreeFunc)(void *parser);

/* Configuration for a specific language indexer */
typedef void (*ParserSetDebugFunc)(void *parser, int debug);

typedef struct IndexerConfig {
    const char *name;                 /* Indexer name (e.g., "index-ts", "index-c") */
    const char *data_dir;             /* Language data directory (e.g., "typescript/data") */
    ParserInitFunc parser_init;       /* Function to initialize parser */
    ParserParseFunc parser_parse;     /* Function to parse a file */
    ParserFreeFunc parser_free;       /* Function to free parser resources */
    ParserSetDebugFunc parser_set_debug; /* Optional function to enable debug mode */
} IndexerConfig;

/* Main indexer entry point
 *
 * Handles argument parsing, database initialization, file walking,
 * and indexing logic. Language-specific behavior is provided via
 * the IndexerConfig callbacks.
 *
 * Returns: 0 on success, 1 on error
 */
int indexer_main(int argc, char *argv[], const IndexerConfig *config);

#endif /* INDEXER_MAIN_H */
