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
#ifndef DATABASE_H
#define DATABASE_H

#include "../config.h"
#include <sqlite3.h>
#include "constants.h"

typedef enum {
    CONTEXT_CLASS,
    CONTEXT_INTERFACE,
    CONTEXT_FUNCTION,
    CONTEXT_ARGUMENT,
    CONTEXT_VARIABLE,
    CONTEXT_EXCEPTION,
    CONTEXT_TYPE,
    CONTEXT_PROPERTY,
    CONTEXT_COMMENT,
    CONTEXT_STRING,
    CONTEXT_FILENAME,
    CONTEXT_IMPORT,
    CONTEXT_EXPORT,
    CONTEXT_CALL,
    CONTEXT_NAMESPACE,
    CONTEXT_ENUM,
    CONTEXT_ENUM_CASE,
    CONTEXT_TRAIT,
    CONTEXT_LAMBDA,
    CONTEXT_LABEL,
    CONTEXT_GOTO
} ContextType;

/* Extensible columns for add_entry() - supports OOP and language-specific features
 *
 * These columns are defined in column_schema.def using X-Macros for:
 * - Database schema generation (database.c)
 * - CLI flag parsing (query-index.c)
 * - SQL filter generation (query-index.c)
 * - Display column management (query-index.c)
 * - Struct field generation (this file)
 *
 * To add a new extensible column:
 * 1. column_schema.def - add COLUMN() line
 * 2. Language indexers - implement extraction
 *
 * Everything else auto-generates from the X-Macro!
 *
 * Note: Uses cli_long parameter for field names (parent, scope, modifier, etc.)
 *       to keep the API clean and match existing indexer code.
 */
typedef struct {
#define COLUMN(name, sql_type, c_type, width, full, compact, cli_long, cli_short, ...) \
    const char *cli_long;
#define INT_COLUMN(name, sql_type, c_type, width, full, compact, cli_long, cli_short, ...) \
    const char *cli_long;
#include "column_schema.def"
#undef COLUMN
#undef INT_COLUMN
} ExtColumns;

/* Convenience macro for entries with no extensible columns */
#define NO_EXTENSIBLE_COLUMNS NULL

/* Database index entry structure
 *
 * Infrastructure columns (not in X-Macro):
 *   - symbol, directory, filename: indexing internals
 * Core display columns (not in X-Macro):
 *   - line_number, context_type, full_symbol: always shown, no filter flags
 * Extensible filterable columns (X-Macro generated from column_schema.def):
 *   - Auto-generated from column_schema.def
 */
typedef struct {
    /* HOT FIELDS - First cache line (accessed in every filter operation) */
    int line;
    ContextType context;
    /* X-Macro: Extensible filterable columns (INTEGER) - HOT */
#define COLUMN(name, ...)  /* skip */
#define INT_COLUMN(name, ...) int name;
#include "column_schema.def"
#undef COLUMN
#undef INT_COLUMN
    /* COLD FIELDS - Subsequent cache lines (rarely accessed during filtering) */
    /* Infrastructure columns */
    char symbol[SYMBOL_MAX_LENGTH];
    char directory[DIRECTORY_MAX_LENGTH];
    char filename[FILENAME_MAX_LENGTH];
    /* Core display columns */
    char full_symbol[SYMBOL_MAX_LENGTH];
    char source_location[SOURCE_LOCATION_MAX_LENGTH];
    /* X-Macro: Extensible filterable columns (TEXT) - COLD */
#define COLUMN(name, sql_type, c_type, width, full, compact, cli_long, cli_short, max_len, ...) char name[max_len];
#define INT_COLUMN(name, ...)  /* skip */
#include "column_schema.def"
#undef COLUMN
#undef INT_COLUMN
} IndexEntry;

typedef struct {
    sqlite3 *db;
    sqlite3_stmt *insert_stmt;  /* Prepared INSERT statement for reuse */
} CodeIndexDatabase;

/* Database operations */
int db_init(CodeIndexDatabase *db, const char *db_path);
int db_enable_concurrent_writes(CodeIndexDatabase *db);
void db_close(CodeIndexDatabase *db);
int db_begin_transaction(CodeIndexDatabase *db);
int db_commit_transaction(CodeIndexDatabase *db);
int db_insert(CodeIndexDatabase *db, const IndexEntry *entry);
int db_delete_by_file(CodeIndexDatabase *db, const char *directory, const char *filename);
const char *context_to_string(ContextType type, int compact);
ContextType string_to_context(const char *str);

#endif
/* test */
/* test daemon reindex */
