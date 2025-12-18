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
#ifndef SQL_BUILDER_H
#define SQL_BUILDER_H

#include <stddef.h>

/* SQL Query Builder with dynamic growth
 *
 * Provides automatic buffer management for building SQL queries.
 * Starts with small buffer (8KB) and grows by 2× when needed.
 * Maximum size: 100MB (well below SQLite's 1GB limit).
 *
 * Pattern similar to ParseResult dynamic allocation.
 *
 * Usage:
 *   SqlQueryBuilder builder;
 *   if (init_sql_builder(&builder) != 0) { handle error }
 *   if (sql_append(&builder, "SELECT * FROM table") != 0) { handle error }
 *   if (sql_append(&builder, " WHERE id = %d", 123) != 0) { handle error }
 *   // Use builder.sql for the query
 *   free_sql_builder(&builder);
 */

#define SQL_INITIAL_CAPACITY 8192              /* Start at 8KB (typical queries) */
#define SQL_GROWTH_FACTOR 2                    /* Double when full */
#define SQL_MAX_CAPACITY (100 * 1024 * 1024)   /* Cap at 100MB (SQLite max is 1GB) */

typedef struct {
    char *sql;          /* Dynamic SQL buffer */
    size_t capacity;    /* Allocated capacity */
    int offset;         /* Current write position */
} SqlQueryBuilder;

/* Initialize SQL query builder
 * Returns: 0 on success, -1 on allocation failure
 */
int init_sql_builder(SqlQueryBuilder *builder);

/* Append formatted string to SQL query with automatic growth
 * Returns: 0 on success, -1 on allocation failure or max size exceeded
 */
int sql_append(SqlQueryBuilder *builder, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

/* Free SQL query builder resources
 * Safe to call multiple times
 */
void free_sql_builder(SqlQueryBuilder *builder);

#endif /* SQL_BUILDER_H */
