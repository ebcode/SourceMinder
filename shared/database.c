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
#include "database.h"
#include "string_utils.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

const char *context_to_string(ContextType type, int compact) {
    switch (type) {
        case CONTEXT_CLASS: return compact ? "CLASS" : "CLASS";
        case CONTEXT_INTERFACE: return compact ? "IFACE" : "INTERFACE";
        case CONTEXT_FUNCTION: return compact ? "FUNC" : "FUNCTION";
        case CONTEXT_ARGUMENT: return compact ? "ARG" : "ARGUMENT";
        case CONTEXT_VARIABLE: return compact ? "VAR" : "VARIABLE";
        case CONTEXT_EXCEPTION: return compact ? "EXC" : "EXCEPTION";
        case CONTEXT_TYPE: return compact ? "TYPE" : "TYPE";
        case CONTEXT_PROPERTY: return compact ? "PROP" : "PROPERTY";
        case CONTEXT_COMMENT: return compact ? "COM" : "COMMENT";
        case CONTEXT_STRING: return compact ? "STR" : "STRING";
        case CONTEXT_FILENAME: return compact ? "FILE" : "FILENAME";
        case CONTEXT_IMPORT: return compact ? "IMP" : "IMPORT";
        case CONTEXT_EXPORT: return compact ? "EXP" : "EXPORT";
        case CONTEXT_CALL: return compact ? "CALL" : "CALL";
        case CONTEXT_NAMESPACE: return compact ? "NS" : "NAMESPACE";
        case CONTEXT_ENUM: return compact ? "ENUM" : "ENUM";
        case CONTEXT_ENUM_CASE: return compact ? "CASE" : "CASE";
        case CONTEXT_TRAIT: return compact ? "TRAIT" : "TRAIT";
        case CONTEXT_LAMBDA: return compact ? "LAM" : "LAMBDA";
        case CONTEXT_LABEL: return compact ? "LABEL" : "LABEL";
        case CONTEXT_GOTO: return compact ? "GOTO" : "GOTO";
        default: return compact ? "UNKNOWN" : "UNKNOWN";
    }
}

ContextType string_to_context(const char *str) {
    if (strcmp(str, "CLASS") == 0) return CONTEXT_CLASS;
    if (strcmp(str, "INTERFACE") == 0 || strcmp(str, "IFACE") == 0) return CONTEXT_INTERFACE;
    if (strcmp(str, "FUNCTION") == 0 || strcmp(str, "FUNC") == 0) return CONTEXT_FUNCTION;
    if (strcmp(str, "ARGUMENT") == 0 || strcmp(str, "ARG") == 0) return CONTEXT_ARGUMENT;
    if (strcmp(str, "VARIABLE") == 0 || strcmp(str, "VAR") == 0) return CONTEXT_VARIABLE;
    if (strcmp(str, "EXCEPTION") == 0 || strcmp(str, "EXC") == 0) return CONTEXT_EXCEPTION;
    if (strcmp(str, "TYPE") == 0) return CONTEXT_TYPE;
    if (strcmp(str, "PROPERTY") == 0 || strcmp(str, "PROP") == 0) return CONTEXT_PROPERTY;
    if (strcmp(str, "COMMENT") == 0 || strcmp(str, "COM") == 0) return CONTEXT_COMMENT;
    if (strcmp(str, "STRING") == 0 || strcmp(str, "STR") == 0) return CONTEXT_STRING;
    if (strcmp(str, "FILENAME") == 0 || strcmp(str, "FILE") == 0) return CONTEXT_FILENAME;
    if (strcmp(str, "IMPORT") == 0 || strcmp(str, "IMP") == 0) return CONTEXT_IMPORT;
    if (strcmp(str, "EXPORT") == 0 || strcmp(str, "EXP") == 0) return CONTEXT_EXPORT;
    if (strcmp(str, "CALL") == 0) return CONTEXT_CALL;
    if (strcmp(str, "NAMESPACE") == 0 || strcmp(str, "NS") == 0) return CONTEXT_NAMESPACE;
    if (strcmp(str, "ENUM") == 0) return CONTEXT_ENUM;
    if (strcmp(str, "CASE") == 0) return CONTEXT_ENUM_CASE;
    if (strcmp(str, "TRAIT") == 0) return CONTEXT_TRAIT;
    if (strcmp(str, "LAMBDA") == 0 || strcmp(str, "LAM") == 0) return CONTEXT_LAMBDA;
    if (strcmp(str, "LABEL") == 0) return CONTEXT_LABEL;
    if (strcmp(str, "GOTO") == 0) return CONTEXT_GOTO;
    return CONTEXT_CLASS; /* default */
}

int db_init(CodeIndexDatabase *db, const char *db_path) {
    db->insert_stmt = NULL;  /* Initialize to NULL */

    int rc = sqlite3_open(db_path, &db->db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db->db));
        return rc;
    }

    /* Set busy timeout BEFORE any SQL operations to handle concurrent access */
    sqlite3_busy_timeout(db->db, 5000);

    const char *schema =
        "CREATE TABLE IF NOT EXISTS code_index ("
        /* Infrastructure columns (traditional) */
        "  symbol TEXT NOT NULL,"
        "  directory TEXT NOT NULL,"
        "  filename TEXT NOT NULL,"
        /* Core display columns (traditional) */
        "  line INTEGER NOT NULL,"
        "  context TEXT NOT NULL,"
        "  full_symbol TEXT NOT NULL,"
        "  source_location TEXT"
        /* X-Macro: Extensible filterable columns */
#define COLUMN(name, type, ...) ", " #name " " #type
#define INT_COLUMN(name, type, ...) ", " #name " " #type
#include "column_schema.def"
#undef COLUMN
#undef INT_COLUMN
        ");"
        /* Infrastructure indexes (traditional) */
        "CREATE INDEX IF NOT EXISTS idx_symbol ON code_index(symbol);"
        "CREATE INDEX IF NOT EXISTS idx_directory ON code_index(directory);"
        "CREATE INDEX IF NOT EXISTS idx_filename ON code_index(filename);"
        /* Composite indexes for common query patterns  -- these impact db size (+25%) and indexing time, but faster queries (+40%) */
        "CREATE INDEX IF NOT EXISTS idx_context_definition ON code_index(context, is_definition);"
        "CREATE INDEX IF NOT EXISTS idx_file_location ON code_index(directory, filename);"
        "CREATE INDEX IF NOT EXISTS idx_parent_context ON code_index(parent_symbol, context);"
        
        
        /* X-Macro: Indexes for extensible columns (excluding those in composite indexes) */
#define COLUMN(name, ...) "CREATE INDEX IF NOT EXISTS idx_" #name " ON code_index(" #name ");"
#define INT_COLUMN(name, ...) /* Skip is_definition - it's in composite index */
#include "column_schema.def"
#undef COLUMN
#undef INT_COLUMN
        ;

    char *err_msg = NULL;
    rc = sqlite3_exec(db->db, schema, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return rc;
    }

    /* Prepare INSERT statement for reuse */
    const char *insert_sql =
        "INSERT INTO code_index ("
        "symbol, directory, filename, line, context, full_symbol, source_location"
        /* X-Macro: Add extensible column names */
#define COLUMN(name, ...) ", " #name
#define INT_COLUMN(name, ...) ", " #name
#include "column_schema.def"
#undef COLUMN
#undef INT_COLUMN
        ") VALUES (?, ?, ?, ?, ?, ?, ?"
        /* X-Macro: Add parameter placeholders */
#define COLUMN(...) ", ?"
#define INT_COLUMN(...) ", ?"
#include "column_schema.def"
#undef COLUMN
#undef INT_COLUMN
        ")";

    rc = sqlite3_prepare_v2(db->db, insert_sql, -1, &db->insert_stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare INSERT statement: %s\n", sqlite3_errmsg(db->db));
        return rc;
    }

    return SQLITE_OK;
}

int db_enable_concurrent_writes(CodeIndexDatabase *db) {
    char *err_msg = NULL;
    int rc;

    /* Enable WAL mode - allows concurrent readers and writers */
    rc = sqlite3_exec(db->db, "PRAGMA journal_mode=WAL", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to enable WAL mode: %s\n", err_msg);
        sqlite3_free(err_msg);
        return rc;
    }

    /* Reduce fsync calls - faster while still safe */
    sqlite3_exec(db->db, "PRAGMA synchronous=NORMAL", NULL, NULL, NULL);

    /* Set busy timeout to 5 seconds - handles lock contention */
    sqlite3_busy_timeout(db->db, 5000);

    return SQLITE_OK;
}

void db_close(CodeIndexDatabase *db) {
    if (db->insert_stmt) {
        sqlite3_finalize(db->insert_stmt);
        db->insert_stmt = NULL;
    }
    if (db->db) {
        sqlite3_close(db->db);
        db->db = NULL;
    }
}

int db_begin_transaction(CodeIndexDatabase *db) {
    char *err_msg = NULL;
    int rc = sqlite3_exec(db->db, "BEGIN TRANSACTION", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to begin transaction: %s\n", err_msg);
        sqlite3_free(err_msg);
    }
    return rc;
}

int db_commit_transaction(CodeIndexDatabase *db) {
    char *err_msg = NULL;
    int rc = sqlite3_exec(db->db, "COMMIT", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to commit transaction: %s\n", err_msg);
        sqlite3_free(err_msg);
    }
    return rc;
}

int db_insert(CodeIndexDatabase *db, const IndexEntry *entry) {
    /* Reset the prepared statement for reuse */
    sqlite3_reset(db->insert_stmt);
    sqlite3_clear_bindings(db->insert_stmt);

    /* Bind infrastructure + core columns (traditional)
     * Use SQLITE_TRANSIENT to ensure SQLite makes a copy of the strings.
     * SQLITE_STATIC is unsafe with reused prepared statements as the string
     * pointers may become invalid between calls. */
    sqlite3_bind_text(db->insert_stmt, 1, entry->symbol, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(db->insert_stmt, 2, entry->directory, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(db->insert_stmt, 3, entry->filename, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(db->insert_stmt, 4, entry->line);
    sqlite3_bind_text(db->insert_stmt, 5, context_to_string(entry->context, 1), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(db->insert_stmt, 6, entry->full_symbol, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(db->insert_stmt, 7, entry->source_location, -1, SQLITE_TRANSIENT);

    /* X-Macro: Bind extensible filterable columns */
    int param_idx = 8;
    /* TEXT columns */
#define COLUMN(name, ...) \
    sqlite3_bind_text(db->insert_stmt, param_idx++, entry->name, -1, SQLITE_TRANSIENT);
#define INT_COLUMN(name, ...)  /* skip */
#include "column_schema.def"
#undef COLUMN
#undef INT_COLUMN
    /* INTEGER columns */
#define COLUMN(name, ...)  /* skip */
#define INT_COLUMN(name, ...) \
    sqlite3_bind_int(db->insert_stmt, param_idx++, entry->name);
#include "column_schema.def"
#undef COLUMN
#undef INT_COLUMN

    int rc = sqlite3_step(db->insert_stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Execution failed: %s\n", sqlite3_errmsg(db->db));
    }

    return (rc == SQLITE_DONE) ? SQLITE_OK : rc;
}

int db_delete_by_file(CodeIndexDatabase *db, const char *directory, const char *filename) {
    const char *sql = "DELETE FROM code_index WHERE directory = ? AND filename = ?";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db->db));
        return rc;
    }

    sqlite3_bind_text(stmt, 1, directory, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, filename, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE) ? SQLITE_OK : rc;
}
