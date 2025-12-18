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
#include "sql_builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

/* Ensure sufficient capacity, growing if needed
 * Returns: 0 on success, -1 on allocation failure or max size exceeded
 */
static int ensure_sql_capacity(SqlQueryBuilder *builder, size_t needed) {
    if ((size_t)builder->offset + needed < builder->capacity) {
        return 0;  /* Enough space already */
    }

    /* Calculate new capacity (double until we have enough) */
    size_t new_capacity = builder->capacity;
    while (new_capacity < (size_t)builder->offset + needed) {
        new_capacity *= SQL_GROWTH_FACTOR;

        /* Check if we've exceeded max limit */
        if (new_capacity > SQL_MAX_CAPACITY) {
            fprintf(stderr, "Error: SQL query exceeds maximum size (%zu MB)\n",
                    (size_t)(SQL_MAX_CAPACITY / (1024 * 1024)));
            fprintf(stderr, "       Query is too complex. Consider simplifying filters.\n");
            fprintf(stderr, "       (SQLite max is 1GB, but 100MB queries are unusually large)\n");
            return -1;
        }
    }

    /* Grow the buffer */
    char *new_sql = realloc(builder->sql, new_capacity);
    if (!new_sql) {
        fprintf(stderr, "Error: Failed to allocate %zu bytes for SQL query\n", new_capacity);
        return -1;
    }

    builder->sql = new_sql;
    builder->capacity = new_capacity;
    return 0;
}

int init_sql_builder(SqlQueryBuilder *builder) {
    builder->sql = malloc(SQL_INITIAL_CAPACITY);
    if (!builder->sql) {
        fprintf(stderr, "Error: Failed to allocate initial SQL buffer (%d bytes)\n",
                SQL_INITIAL_CAPACITY);
        builder->capacity = 0;
        builder->offset = 0;
        return -1;
    }
    builder->capacity = SQL_INITIAL_CAPACITY;
    builder->offset = 0;
    builder->sql[0] = '\0';
    return 0;
}

int sql_append(SqlQueryBuilder *builder, const char *format, ...) {
    va_list args;

    /* First pass: figure out how much space we need */
    va_start(args, format);
    int needed = vsnprintf(NULL, 0, format, args);
    va_end(args);

    if (needed < 0) {
        fprintf(stderr, "Error: vsnprintf failed in sql_append\n");
        return -1;
    }

    /* Ensure we have enough capacity (including null terminator) */
    if (ensure_sql_capacity(builder, (size_t)needed + 1) != 0) {
        return -1;
    }

    /* Second pass: do the actual write */
    va_start(args, format);
    int written = vsnprintf(builder->sql + builder->offset,
                           builder->capacity - (size_t)builder->offset,
                           format, args);
    va_end(args);

    if (written < 0 || written != needed) {
        fprintf(stderr, "Error: vsnprintf inconsistent in sql_append\n");
        return -1;
    }

    builder->offset += written;
    return 0;
}

void free_sql_builder(SqlQueryBuilder *builder) {
    free(builder->sql);
    builder->sql = NULL;
    builder->capacity = 0;
    builder->offset = 0;
}
