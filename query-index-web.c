/*
 * Incremental WASM extraction surface for query-index.
 *
 * This file starts small on purpose. Extract exact WEB_SAFE helpers here first,
 * keep behavior aligned with query-index.c, and introduce host bridges only at
 * explicit runtime boundaries.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#include "query-index-web.h"

/* strndup compat for targets where POSIX 2008 is not available (e.g. wasm) */
#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
static char *strndup(const char *s, size_t n) {
    size_t len = 0;
    while (len < n && s[len]) len++;
    char *new_str = malloc(len + 1);
    if (new_str) {
        memcpy(new_str, s, len);
        new_str[len] = '\0';
    }
    return new_str;
}
#endif

/* -- sqlite3: use real headers when linking, forward-declare for smoke build -- */
#ifndef QI_WEB_LINKED
struct sqlite3;
struct sqlite3_stmt;
typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;
typedef void (*sqlite3_destructor_type)(void*);

#define SQLITE_OK    0
#define SQLITE_ROW   100
#define SQLITE_STATIC ((sqlite3_destructor_type)0)

int sqlite3_prepare_v2(sqlite3 *db, const char *sql, int nByte,
                       sqlite3_stmt **ppStmt, const char **pzTail);
int sqlite3_step(sqlite3_stmt *stmt);
int sqlite3_finalize(sqlite3_stmt *stmt);
const unsigned char *sqlite3_column_text(sqlite3_stmt *stmt, int iCol);
int sqlite3_column_int(sqlite3_stmt *stmt, int iCol);
const char *sqlite3_errmsg(sqlite3 *db);
int sqlite3_bind_text(sqlite3_stmt *stmt, int idx, const char *val, int n,
                      void(*destructor)(void*));
int sqlite3_bind_int(sqlite3_stmt *stmt, int idx, int val);
int sqlite3_exec(sqlite3 *db, const char *sql,
                 int (*callback)(void*,int,char**,char**), void *arg,
                 char **errmsg);
char *sqlite3_mprintf(const char *fmt, ...);
void sqlite3_free(void *ptr);
#endif /* QI_WEB_LINKED */

/* -- sqlite3_mprintf / sqlite3_free shims for web builds (no real sqlite3 linked) -- */
#if !defined(QI_WEB_LINKED) && !defined(SQLITE3_API)

/* Minimal printf that understands %s, %d, and SQLite's %q (quote-string).
 * When size==0, operates in measuring mode: counts chars without writing. */
static int qi_sql_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    size_t pos = 0;
    int measuring = (size == 0);
    while (*fmt && (measuring || pos < size)) {
        if (*fmt != '%') {
            if (!measuring && pos + 1 < size) buf[pos] = *fmt;
            pos++;
            fmt++;
            continue;
        }
        fmt++; /* skip '%' */
        if (*fmt == '%') {
            if (!measuring && pos + 1 < size) buf[pos] = '%';
            pos++;
            fmt++;
            continue;
        }
        if (*fmt == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            for (; *s; s++) {
                if (!measuring && pos + 1 < size) buf[pos] = *s;
                pos++;
            }
            fmt++;
            continue;
        }
        if (*fmt == 'd') {
            int d = va_arg(ap, int);
            char tmp[32];
            int n = snprintf(tmp, sizeof(tmp), "%d", d);
            for (int i = 0; i < n; i++) {
                if (!measuring && pos + 1 < size) buf[pos] = tmp[i];
                pos++;
            }
            fmt++;
            continue;
        }
        if (*fmt == 'q') {
            /* SQLite %q: quote string for SQL. NULL → "NULL", else 'escaped' */
            const char *s = va_arg(ap, const char *);
            if (!s) {
                for (const char *p = "NULL"; *p; p++) {
                    if (!measuring && pos + 1 < size) buf[pos] = *p;
                    pos++;
                }
            } else {
                if (!measuring && pos + 1 < size) buf[pos] = '\'';
                pos++;
                for (; *s; s++) {
                    if (*s == '\'') {
                        if (!measuring && pos + 1 < size) buf[pos] = '\'';
                        pos++;
                        if (!measuring && pos + 1 < size) buf[pos] = '\'';
                        pos++;
                    } else {
                        if (!measuring && pos + 1 < size) buf[pos] = *s;
                        pos++;
                    }
                }
                if (!measuring && pos + 1 < size) buf[pos] = '\'';
                pos++;
            }
            fmt++;
            continue;
        }
        /* Unknown format specifier — skip */
        fmt++;
    }
    if (!measuring && pos < size) buf[pos] = '\0';
    else if (!measuring && size > 0) buf[size - 1] = '\0';
    return (int)pos;
}

char *sqlite3_mprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int needed = qi_sql_vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) return NULL;
    char *buf = malloc((size_t)needed + 1);
    if (!buf) return NULL;
    va_start(ap, fmt);
    qi_sql_vsnprintf(buf, (size_t)needed + 1, fmt, ap);
    va_end(ap);
    return buf;
}

void sqlite3_free(void *ptr) {
    free(ptr);
}
#endif

/* -- Forward declarations from shared helpers (not in query-index-web.h) -- */
int parse_source_location(const char *source_location, int *start_line,
                          int *start_column, int *end_line, int *end_column);
void to_lowercase_copy(const char *src, char *dst, size_t size);
void to_upper(char *str);
ContextType string_to_context(const char *str);
const char *context_to_string(ContextType type, int compact);
char *try_strdup_ctx(const char *str, const char *err_msg);
char *safe_strdup_ctx(const char *str, const char *err_msg);
/* ==========================================================================
 * WEB_SAFE helper functions - exact extractions from query-index.c
 * ========================================================================== */

/* WEB_SAFE: exact extraction from query-index.c for wildcard translation into SQL LIKE syntax. */
void convert_wildcards_web(const char *pattern, char *output, size_t output_size) {
    size_t j = 0;
    for (size_t i = 0; pattern[i] && j < output_size - 1; i++) {
        /* Check if this is an escaped character */
        if (pattern[i] == '\\' && (pattern[i + 1] == '*' || pattern[i + 1] == '.' ||
                                   pattern[i + 1] == '%' || pattern[i + 1] == '_' ||
                                   pattern[i + 1] == '\\')) {
            /* Handle escaped characters:
             * \* and \. -> just the literal character (no escaping needed in SQL)
             * \%, \_, \\ -> keep the backslash (SQL ESCAPE clause handles these) */
            if (pattern[i + 1] == '*' || pattern[i + 1] == '.') {
                /* Drop the backslash, just copy the literal character */
                output[j++] = pattern[i + 1];
            } else {
                /* Keep backslash for SQL wildcards and backslash itself */
                output[j++] = '\\';
                if (j < output_size - 1) {
                    output[j++] = pattern[i + 1];
                }
            }
            i++; /* Skip the escaped character */
        } else if (pattern[i] == '*') {
            /* Convert unescaped * to % (SQL multi-char wildcard) */
            output[j++] = '%';
        } else if (pattern[i] == '.') {
            /* Convert unescaped . to _ (SQL single-char wildcard) */
            output[j++] = '_';
        } else {
            /* Copy everything else as-is */
            output[j++] = pattern[i];
        }
    }
    output[j] = '\0';
}

/* WEB_SAFE: exact extraction from query-index.c for browser-side file filter normalization. */
int process_file_pattern_web(const char *input, char **dir_out, char **file_out) {
    /* Convert shell-style wildcards (*) to SQL LIKE wildcards (%) first */
    char converted_input[PATH_MAX_LENGTH];
    convert_wildcards_web(input, converted_input, sizeof(converted_input));

    /* Handle shorthand: .c -> %.c, .h -> %.h, etc. */
    if ((converted_input[0] == '_' || converted_input[0] == '.') && converted_input[1] != '/' && converted_input[1] != '.') {
        /* Extension shorthand detected */
        size_t pattern_len = strlen(converted_input) + 2; /* % + extension + \0 */
        char *expanded = malloc(pattern_len);
        if (!expanded) {
            fprintf(stderr, "Error: Failed to allocate memory for file pattern\n");
            *dir_out = NULL;
            *file_out = NULL;
            return -1;
        }
        snprintf(expanded, pattern_len, "%%%s", converted_input);
        *dir_out = NULL;
        *file_out = expanded;
        return 0;
    }

    const char *last_slash = strrchr(converted_input, '/');

    if (!last_slash) {
        /* No slash - filename only */
        *dir_out = NULL;
        *file_out = try_strdup_ctx(converted_input, "Failed to allocate memory for filename");
        if (!*file_out) {
            return -1;
        }
        return 0;
    }

    /* Split on last slash */
    size_t dir_len = (size_t)(last_slash - converted_input);
    char *dir_part = strndup(converted_input, dir_len);
    if (!dir_part) {
        fprintf(stderr, "Error: Failed to allocate memory for directory part\n");
        return -1;
    }
    const char *file_after_slash = last_slash + 1;

    /* If empty filename (trailing slash), use % wildcard for all files */
    char *file_part = strlen(file_after_slash) > 0 ?
        try_strdup_ctx(file_after_slash, "Failed to allocate memory for file part") :
        try_strdup_ctx("%", "Failed to allocate memory for file part");

    if (!file_part) {
        free(dir_part);
        return -1;
    }

    /* Normalize directory part */
    int needs_prefix = 1;

    /* Check if starts with ./ or ../ or / (absolute) */
    if (dir_part[0] == '.' && (dir_part[1] == '/' ||
        (dir_part[1] == '.' && dir_part[2] == '/'))) {
        needs_prefix = 0;  /* Explicit relative path */
    } else if (dir_part[0] == '/') {
        needs_prefix = 0;  /* Absolute path */
    }

    /* Add prefix for boundary matching.
     * Single-component names (e.g. "perl") use %/ to avoid matching "myperl/".
     * Multi-component paths (e.g. "tools/sources/perl") use % only - the path
     * is already specific enough, and %/ would require a character before the
     * first component, failing to match top-level relative paths. */
    if (needs_prefix) {
        int is_multi = (strchr(dir_part, '/') != NULL);
        size_t new_len = strlen(dir_part) + 4;  /* %/ + / + \0 */
        char *prefixed = malloc(new_len);
        if (!prefixed) {
            fprintf(stderr, "Error: Failed to allocate memory for directory prefix\n");
            free(dir_part);
            free(file_part);
            return -1;
        }
        snprintf(prefixed, new_len, is_multi ? "%%%s/" : "%%/%s/", dir_part);
        free(dir_part);
        dir_part = prefixed;
    } else {
        /* Add trailing slash to explicit paths too */
        size_t new_len = strlen(dir_part) + 2;  /* / + \0 */
        char *with_slash = malloc(new_len);
        if (!with_slash) {
            fprintf(stderr, "Error: Failed to allocate memory for directory slash\n");
            free(dir_part);
            free(file_part);
            return -1;
        }
        snprintf(with_slash, new_len, "%s/", dir_part);
        free(dir_part);
        dir_part = with_slash;
    }

    *dir_out = dir_part;
    *file_out = file_part;
    return 0;
}

/* WEB_SAFE: exact extraction from query-index.c for indexed metadata filter construction. */
int build_common_filters_web(SqlQueryBuilder *builder,
                                    ContextTypeList *include, ContextTypeList *exclude,
                                    QueryFilters *filters, FileFilterList *file_filter,
                                    WithinRangeList *within_ranges, int debug) {
    /* Add file filter (directory + filename) */
    if (file_filter && file_filter->count > 0) {
        if (sql_append(builder, " AND (") != 0) return -1;
        for (int i = 0; i < file_filter->count; i++) {
            if (i > 0) {
                if (sql_append(builder, " OR ") != 0) return -1;
            }

            if (file_filter->patterns[i].directory != NULL) {
                /* Has directory part - filter both columns */
                char *escaped_dir = sqlite3_mprintf("%q", file_filter->patterns[i].directory);
                char *escaped_file = sqlite3_mprintf("%q", file_filter->patterns[i].filename);
                int ret = sql_append(builder,
                    "(directory LIKE %s ESCAPE '\\' AND filename LIKE %s ESCAPE '\\')",
                    escaped_dir, escaped_file);
                sqlite3_free(escaped_dir);
                sqlite3_free(escaped_file);
                if (ret != 0) return -1;
            } else {
                /* No directory part - filter filename only */
                char *escaped_file = sqlite3_mprintf("%q", file_filter->patterns[i].filename);
                int ret = sql_append(builder,
                    "filename LIKE %s ESCAPE '\\'",
                    escaped_file);
                sqlite3_free(escaped_file);
                if (ret != 0) return -1;
            }
        }
        if (sql_append(builder, ")") != 0) return -1;
    }

    /* Add line range filter */
    if (filters && filters->line_start >= 0) {
        if (filters->line_end == filters->line_start) {
            if (sql_append(builder, " AND line = %d", filters->line_start) != 0) return -1;
        } else {
            if (sql_append(builder, " AND line BETWEEN %d AND %d",
                filters->line_start, filters->line_end) != 0) return -1;
        }
    }

    /* Add within filter - restrict to specific file/line ranges */
    if (within_ranges && within_ranges->count > 0) {

        if (debug) {
            fprintf(stderr, "DEBUG: WITHIN RANGES: %d\n", within_ranges->count);
        }

        if (sql_append(builder, " AND (") != 0) return -1;
        for (int i = 0; i < within_ranges->count; i++) {
            if (i > 0) {
                if (sql_append(builder, " OR ") != 0) return -1;
            }
            char *escaped_dir = sqlite3_mprintf("%q", within_ranges->ranges[i].directory);
            char *escaped_file = sqlite3_mprintf("%q", within_ranges->ranges[i].filename);
            int ret = sql_append(builder,
                "(directory = %s AND filename = %s AND line BETWEEN %d AND %d)",
                escaped_dir, escaped_file,
                within_ranges->ranges[i].line_start, within_ranges->ranges[i].line_end);
            sqlite3_free(escaped_dir);
            sqlite3_free(escaped_file);
            if (ret != 0) return -1;
        }
        if (sql_append(builder, ")") != 0) return -1;
    } else {
        if (debug) {
            fprintf(stderr, "DEBUG: NOT WITHIN RANGES:\n");
        }
    }

    /* Add include filter - database now uses compact form */
    if (include && include->count > 0) {
        if (sql_append(builder, " AND context IN (") != 0) return -1;
        for (int i = 0; i < include->count; i++) {
            if (sql_append(builder, "%s'%s'",
                i > 0 ? ", " : "", context_to_string(include->types[i], 1)) != 0) return -1;
        }
        if (sql_append(builder, ")") != 0) return -1;
    }

    /* Add exclude filter - database now uses compact form */
    if (exclude && exclude->count > 0) {
        if (sql_append(builder, " AND context NOT IN (") != 0) return -1;
        for (int i = 0; i < exclude->count; i++) {
            if (sql_append(builder, "%s'%s'",
                i > 0 ? ", " : "", context_to_string(exclude->types[i], 1)) != 0) return -1;
        }
        if (sql_append(builder, ")") != 0) return -1;
    }

    /* X-Macro: Add extensible column filters (using LIKE for pattern matching) */
#define COLUMN(name, ...) \
    if (filters && filters->name.count > 0) { \
        if (sql_append(builder, " AND (") != 0) return -1; \
        for (int i = 0; i < filters->name.count; i++) { \
            char *escaped_value = sqlite3_mprintf("%q", filters->name.values[i]); \
            int ret = sql_append(builder, "%s" #name " LIKE %s ESCAPE '\\\\'", \
                i > 0 ? " OR " : "", escaped_value); \
            sqlite3_free(escaped_value); \
            if (ret != 0) return -1; \
        } \
        if (sql_append(builder, ")") != 0) return -1; \
    }
#define INT_COLUMN(name, ...) \
    if (filters && filters->name.count > 0) { \
        if (sql_append(builder, " AND (") != 0) return -1; \
        for (int i = 0; i < filters->name.count; i++) { \
            char *escaped_value = sqlite3_mprintf("%q", filters->name.values[i]); \
            int ret = sql_append(builder, "%s" #name " LIKE %s ESCAPE '\\\\'", \
                i > 0 ? " OR " : "", escaped_value); \
            sqlite3_free(escaped_value); \
            if (ret != 0) return -1; \
        } \
        if (sql_append(builder, ")") != 0) return -1; \
    }
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN

    return 0;
}

/* WEB_SAFE: exact extraction from query-index.c for pattern predicate composition. */
int build_query_filters_web(SqlQueryBuilder *builder, PatternList *patterns,
                                   ContextTypeList *include, ContextTypeList *exclude,
                                   QueryFilters *filters, FileFilterList *file_filter,
                                   WithinRangeList *within_ranges, int line_range, int debug) {

    if (line_range >= 0 && patterns->count > 1) {
        /* INTERSECT-based query for same-line or proximity matching (line_range >= 0) */
        if (sql_append(builder, "(directory, filename, line) IN (") != 0) return -1;

        for (int i = 0; i < patterns->count; i++) {
            if (i > 0) {
                if (sql_append(builder, " INTERSECT ") != 0) return -1;
            }

            char *escaped_pattern = sqlite3_mprintf("%q", patterns->patterns[i]);
            int ret = sql_append(builder,
                "SELECT directory, filename, line FROM code_index WHERE symbol LIKE %s ESCAPE '\\'",
                escaped_pattern);
            sqlite3_free(escaped_pattern);
            if (ret != 0) return -1;

            /* Add all filters to each INTERSECT subquery */
            if (build_common_filters_web(builder, include, exclude, filters, file_filter, within_ranges, debug) != 0) return -1;
        }

        if (sql_append(builder, ") AND (") != 0) return -1;

        /* Only show symbols that match one of the search patterns */
        for (int i = 0; i < patterns->count; i++) {
            if (i > 0) {
                if (sql_append(builder, " OR ") != 0) return -1;
            }
            char *escaped_pattern = sqlite3_mprintf("%q", patterns->patterns[i]);
            int ret = sql_append(builder, "symbol LIKE %s ESCAPE '\\'", escaped_pattern);
            sqlite3_free(escaped_pattern);
            if (ret != 0) return -1;
        }

        if (sql_append(builder, "))") != 0) return -1;
    } else {
        /* Original OR-based query for any-pattern matching */
        for (int i = 0; i < patterns->count; i++) {
            if (i > 0) {
                if (sql_append(builder, " OR ") != 0) return -1;
            }
            char *escaped_pattern = sqlite3_mprintf("%q", patterns->patterns[i]);
            int ret = sql_append(builder, "(symbol LIKE %s ESCAPE '\\')", escaped_pattern);
            sqlite3_free(escaped_pattern);
            if (ret != 0) return -1;
        }
        if (sql_append(builder, ")") != 0) return -1;

        /* Add filters once at the end for OR mode */
        if (build_common_filters_web(builder, include, exclude, filters, file_filter, within_ranges, debug) != 0) return -1;
    }

    return 0;
}

/* ==========================================================================
 * WEB_SAFE query execution layer
 * ========================================================================== */

/* WEB_SAFE: counts indexed files using SQLite filters only. */
int count_distinct_files_web(CodeIndexDatabase *db,
                                     ContextTypeList *include, ContextTypeList *exclude,
                                     QueryFilters *filters, FileFilterList *file_filter,
                                     WithinRangeList *within_ranges, int debug) {
    /* Build SQL query to count distinct files - apply all filters EXCEPT symbol patterns */
    SqlQueryBuilder builder;
    if (init_sql_builder(&builder) != 0) {
        fprintf(stderr, "Error: Failed to initialize SQL query builder\n");
        return 0;
    }

    if (sql_append(&builder, "SELECT COUNT(DISTINCT directory || filename) FROM code_index WHERE 1=1") != 0) {
        free_sql_builder(&builder);
        return 0;
    }

    /* Apply filters (file, context types, extensible columns) but NOT symbol search */
    if (build_common_filters_web(&builder, include, exclude, filters, file_filter, within_ranges, debug) != 0) {
        free_sql_builder(&builder);
        return 0;
    }

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->db, builder.sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare file count query: %s\n", sqlite3_errmsg(db->db));
        free_sql_builder(&builder);
        return 0;
    }

    int file_count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        file_count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    free_sql_builder(&builder);
    return file_count;
}

/* WEB_SAFE: counts pattern matches against the indexed symbol column. */
int count_pattern_matches_web(CodeIndexDatabase *db, const char *pattern) {
    SqlQueryBuilder builder;
    if (init_sql_builder(&builder) != 0) {
        return -1;
    }

    if (sql_append(&builder, "SELECT COUNT(*) FROM code_index WHERE full_symbol LIKE ? ESCAPE '\\'") != 0) {
        free_sql_builder(&builder);
        return -1;
    }

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->db, builder.sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        free_sql_builder(&builder);
        return -1;  /* Error */
    }
    free_sql_builder(&builder);

    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

/* WEB_SAFE: computes total match count from indexed data via SQL. */
int get_total_count_web(CodeIndexDatabase *db, PatternList *patterns,
                                ContextTypeList *include, ContextTypeList *exclude,
                                QueryFilters *filters, FileFilterList *file_filter,
                                WithinRangeList *within_ranges, int line_range, int debug) {
    /* Build SQL query with COUNT(*) */
    SqlQueryBuilder builder;
    if (init_sql_builder(&builder) != 0) {
        return 0;
    }

    /* For proximity search, query temp table; otherwise query code_index */
    if (line_range > 0 && patterns->count > 1) {
        if (sql_append(&builder, "SELECT COUNT(*) FROM proximity_results") != 0) {
            free_sql_builder(&builder);
            return 0;
        }
    } else {
        if (sql_append(&builder, "SELECT COUNT(*) FROM code_index WHERE (") != 0) {
            free_sql_builder(&builder);
            return 0;
        }
        if (build_query_filters_web(&builder, patterns, include, exclude, filters, file_filter, within_ranges, line_range, debug) != 0) {
            free_sql_builder(&builder);
            return 0;
        }
    }

    if (debug) {
        fprintf(stderr, "SQL: [Get total count] %s\n", builder.sql);
    }

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->db, builder.sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        free_sql_builder(&builder);
        return 0;  /* Return 0 if query fails */
    }

    /* Bind pattern parameters (only for OR mode) */
    if (line_range < 0) {
        for (int i = 0; i < patterns->count; i++) {
            sqlite3_bind_text(stmt, i + 1, patterns->patterns[i], -1, SQLITE_STATIC);
        }
    }

    int total = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        total = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    free_sql_builder(&builder);
    return total;
}

/* WEB_SAFE: counts distinct indexed files matching the full query. */
int get_total_file_count_web(CodeIndexDatabase *db, PatternList *patterns,
                                     ContextTypeList *include, ContextTypeList *exclude,
                                     QueryFilters *filters, FileFilterList *file_filter,
                                     WithinRangeList *within_ranges, int line_range, int debug) {
    /* Build SQL query with COUNT(DISTINCT ...) */
    SqlQueryBuilder builder;
    if (init_sql_builder(&builder) != 0) {
        return 0;
    }

    if (sql_append(&builder, "SELECT COUNT(DISTINCT directory || filename) FROM code_index WHERE (") != 0) {
        free_sql_builder(&builder);
        return 0;
    }
    if (build_query_filters_web(&builder, patterns, include, exclude, filters, file_filter, within_ranges, line_range, debug) != 0) {
        free_sql_builder(&builder);
        return 0;
    }

    if (debug) {
        fprintf(stderr, "SQL: [Get total file count] %s\n", builder.sql);
    }

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->db, builder.sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        free_sql_builder(&builder);
        return 0;  /* Return 0 if query fails */
    }

    /* Bind pattern parameters (only for OR mode) */
    if (line_range < 0) {
        for (int i = 0; i < patterns->count; i++) {
            sqlite3_bind_text(stmt, i + 1, patterns->patterns[i], -1, SQLITE_STATIC);
        }
    }

    int total = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        total = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    free_sql_builder(&builder);
    return total;
}

/* WEB_SAFE: aggregates context-type counts from indexed data via SQL GROUP BY.
 * In the web path, results accumulate into a ContextSummary struct instead of
 * printing to stdout (the CLI print behaviour lives in query-index.c). */
int get_context_summary_web(CodeIndexDatabase *db, PatternList *patterns,
                                    ContextTypeList *include, ContextTypeList *exclude,
                                    QueryFilters *filters, FileFilterList *file_filter,
                                    WithinRangeList *within_ranges, int line_range,
                                    int debug, ContextSummary *summary) {
    if (!summary) return -1;
    summary->count = 0;

    /* Build SQL query with GROUP BY context */
    SqlQueryBuilder builder;
    if (init_sql_builder(&builder) != 0) {
        return -1;
    }

    if (sql_append(&builder, "SELECT context, COUNT(*) as count FROM code_index WHERE (") != 0) {
        free_sql_builder(&builder);
        return -1;
    }
    if (build_query_filters_web(&builder, patterns, include, exclude, filters, file_filter, within_ranges, line_range, debug) != 0) {
        free_sql_builder(&builder);
        return -1;
    }
    if (sql_append(&builder, " GROUP BY context ORDER BY count DESC") != 0) {
        free_sql_builder(&builder);
        return -1;
    }

    if (debug) {
        fprintf(stderr, "SQL: [Get context summary] %s\n", builder.sql);
    }

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->db, builder.sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        free_sql_builder(&builder);
        return -1;
    }

    /* Bind pattern parameters (only for OR mode) */
    if (line_range < 0) {
        for (int i = 0; i < patterns->count; i++) {
            sqlite3_bind_text(stmt, i + 1, patterns->patterns[i], -1, SQLITE_STATIC);
        }
    }

    while (sqlite3_step(stmt) == SQLITE_ROW && summary->count < MAX_CONTEXT_TYPES) {
        const char *context_full = (const char *)sqlite3_column_text(stmt, 0);
        int count = sqlite3_column_int(stmt, 1);

        /* Convert to compact form for the summary entry */
        char upper[CONTEXT_TYPE_MAX_LENGTH];
        snprintf(upper, sizeof(upper), "%s", context_full);
        to_upper(upper);
        ContextType type = string_to_context(upper);
        const char *context_compact = context_to_string(type, 1);

        snprintf(summary->entries[summary->count].context,
                 sizeof(summary->entries[summary->count].context),
                 "%s", context_compact);
        summary->entries[summary->count].count = count;
        summary->count++;
    }

    sqlite3_finalize(stmt);
    free_sql_builder(&builder);
    return 0;
}

/* WEB_SAFE: builds the final SQL query for indexed result retrieval. */
int build_query_sql_web(SqlQueryBuilder *builder, PatternList *patterns,
                                ContextTypeList *include, ContextTypeList *exclude,
                                QueryFilters *filters, FileFilterList *file_filter,
                                WithinRangeList *within_ranges, int line_range, int debug) {
    /* For proximity search, query the temp table; otherwise query code_index */
    if (line_range > 0 && patterns->count > 1) {
        if (sql_append(builder,
            "SELECT * FROM proximity_results ORDER BY directory, filename, line") != 0) {
            return -1;
        }
    } else {
        if (sql_append(builder, "SELECT * FROM code_index WHERE (") != 0) return -1;
        if (build_query_filters_web(builder, patterns, include, exclude, filters, file_filter, within_ranges, line_range, debug) != 0) return -1;
        if (sql_append(builder, " ORDER BY directory, filename, line") != 0) return -1;
    }
    return 0;
}

/* WEB_SAFE: resolves --within scopes entirely from indexed definitions. */
int lookup_within_definitions_web(CodeIndexDatabase *db, WithinFilter *within_filter,
                                          WithinRangeList *within_ranges, int debug) {
    if (!within_filter || within_filter->count == 0) {
        if (debug) {
            fprintf(stderr, "DEBUG: lookup_within_definitions, count: %d\n",
                    within_filter ? within_filter->count : -1);
        }
        return 0;  /* No within filter, nothing to do */
    }

    int found_count = 0;

    /* Look up each symbol separately */
    for (int sym_idx = 0; sym_idx < within_filter->count; sym_idx++) {
        const char *symbol = within_filter->symbols[sym_idx];
        char normalized_symbol[SYMBOL_MAX_LENGTH];
        to_lowercase_copy(symbol, normalized_symbol, sizeof(normalized_symbol));

        /* Build SQL query to find all definitions */
        SqlQueryBuilder builder;
        if (init_sql_builder(&builder) != 0) {
            continue;
        }

        char *escaped_symbol = sqlite3_mprintf("%q", normalized_symbol);
        int ret = sql_append(&builder,
            "SELECT directory, filename, source_location FROM code_index "
            "WHERE symbol = %s AND is_definition = 1 AND source_location IS NOT NULL",
            escaped_symbol);
        sqlite3_free(escaped_symbol);
        if (ret != 0) {
            free_sql_builder(&builder);
            continue;
        }

        if (debug) {
            fprintf(stderr, "DEBUG: Within lookup SQL: %s\n", builder.sql);
        }

        sqlite3_stmt *stmt;
        int rc = sqlite3_prepare_v2(db->db, builder.sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Error: Failed to prepare within lookup query: %s\n", sqlite3_errmsg(db->db));
            free_sql_builder(&builder);
            return -1;
        }
        free_sql_builder(&builder);

        /* Execute query and collect ranges */
        int symbol_found = 0;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            if (within_ranges->count >= MAX_PATTERNS) {
                fprintf(stderr, "Warning: Maximum within range limit (%d) reached\n", MAX_PATTERNS);
                break;
            }

            const char *directory = (const char *)sqlite3_column_text(stmt, 0);
            const char *filename = (const char *)sqlite3_column_text(stmt, 1);
            const char *source_location = (const char *)sqlite3_column_text(stmt, 2);

            /* Parse source_location to get line range */
            int start_line, start_column, end_line, end_column;
            if (parse_source_location(source_location, &start_line, &start_column,
                                       &end_line, &end_column) == 0) {
                /* Add range to list */
                WithinRange *range = &within_ranges->ranges[within_ranges->count];
                strncpy(range->directory, directory, DIRECTORY_MAX_LENGTH - 1);
                range->directory[DIRECTORY_MAX_LENGTH - 1] = '\0';
                strncpy(range->filename, filename, FILENAME_MAX_LENGTH - 1);
                range->filename[FILENAME_MAX_LENGTH - 1] = '\0';
                range->line_start = start_line;
                range->line_end = end_line;
                within_ranges->count++;
                symbol_found = 1;
                found_count++;

                if (debug) {
                    fprintf(stderr, "DEBUG: Found definition: %s/%s lines %d-%d\n",
                            directory, filename, start_line, end_line);
                }
            }
        }

        sqlite3_finalize(stmt);

        /* Error if this symbol had no definitions */
        if (!symbol_found) {
            fprintf(stderr, "Error: No definition found for symbol '%s'\n", symbol);
            return -1;
        }
    }

    return 0;
}

/* WEB_SAFE: performs indexed proximity matching entirely inside SQLite. */
int execute_proximity_to_temp_table_web(CodeIndexDatabase *db, PatternList *patterns,
                                                ContextTypeList *include, ContextTypeList *exclude,
                                                QueryFilters *filters, FileFilterList *file_filter,
                                                WithinRangeList *within_ranges, int line_range, int debug) {
    /* Create temp table with same schema as code_index */
    const char *create_temp =
        "CREATE TEMP TABLE IF NOT EXISTS proximity_results AS "
        "SELECT * FROM code_index LIMIT 0";

    if (sqlite3_exec(db->db, "DROP TABLE IF EXISTS proximity_results", NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "Failed to drop temp table: %s\n", sqlite3_errmsg(db->db));
        return -1;
    }

    if (sqlite3_exec(db->db, create_temp, NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "Failed to create temp table: %s\n", sqlite3_errmsg(db->db));
        return -1;
    }

    /* Step 1: Build and execute anchor query (first pattern) */
    SqlQueryBuilder anchor_builder;
    if (init_sql_builder(&anchor_builder) != 0) {
        return -1;
    }

    char *escaped_anchor = sqlite3_mprintf("%q", patterns->patterns[0]);
    int ret = sql_append(&anchor_builder,
        "SELECT directory, filename, line FROM code_index WHERE symbol LIKE %s ESCAPE '\\'",
        escaped_anchor);
    sqlite3_free(escaped_anchor);
    if (ret != 0) {
        free_sql_builder(&anchor_builder);
        return -1;
    }

    /* Add common filters to anchor query */
    if (build_common_filters_web(&anchor_builder, include, exclude, filters, file_filter, within_ranges, debug) != 0) {
        free_sql_builder(&anchor_builder);
        return -1;
    }
    if (sql_append(&anchor_builder, " ORDER BY directory, filename, line") != 0) {
        free_sql_builder(&anchor_builder);
        return -1;
    }

    if (debug) {
        fprintf(stderr, "SQL: [Anchor query] %s\n", anchor_builder.sql);
    }

    sqlite3_stmt *anchor_stmt;
    if (sqlite3_prepare_v2(db->db, anchor_builder.sql, -1, &anchor_stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Anchor query failed: %s\n", sqlite3_errmsg(db->db));
        free_sql_builder(&anchor_builder);
        return -1;
    }
    free_sql_builder(&anchor_builder);

    /* Step 2: For each anchor, find secondaries within range and insert into temp table */
    SqlQueryBuilder range_builder;
    if (init_sql_builder(&range_builder) != 0) {
        sqlite3_finalize(anchor_stmt);
        return -1;
    }

    if (sql_append(&range_builder,
        "INSERT INTO proximity_results "
        "SELECT * FROM code_index WHERE filename = ? AND directory = ? "
        "AND line BETWEEN ? AND ? AND (") != 0) {
        free_sql_builder(&range_builder);
        sqlite3_finalize(anchor_stmt);
        return -1;
    }

    /* Add all secondary patterns */
    for (int i = 1; i < patterns->count; i++) {
        if (i > 1) {
            if (sql_append(&range_builder, " OR ") != 0) {
                free_sql_builder(&range_builder);
                sqlite3_finalize(anchor_stmt);
                return -1;
            }
        }
        char *escaped_pattern = sqlite3_mprintf("%q", patterns->patterns[i]);
        ret = sql_append(&range_builder, "symbol LIKE %s ESCAPE '\\'", escaped_pattern);
        sqlite3_free(escaped_pattern);
        if (ret != 0) {
            free_sql_builder(&range_builder);
            sqlite3_finalize(anchor_stmt);
            return -1;
        }
    }
    if (sql_append(&range_builder, ")") != 0) {
        free_sql_builder(&range_builder);
        sqlite3_finalize(anchor_stmt);
        return -1;
    }

    /* Add common filters */
    if (build_common_filters_web(&range_builder, include, exclude, filters, file_filter, within_ranges, debug) != 0) {
        free_sql_builder(&range_builder);
        sqlite3_finalize(anchor_stmt);
        return -1;
    }

    if (debug) {
        fprintf(stderr, "SQL: [Range query template] %s\n", range_builder.sql);
    }

    int anchor_count = 0;
    int complete_matches = 0;

    while (sqlite3_step(anchor_stmt) == SQLITE_ROW) {
        const char *directory = (const char *)sqlite3_column_text(anchor_stmt, 0);
        const char *filename = (const char *)sqlite3_column_text(anchor_stmt, 1);
        int anchor_line = sqlite3_column_int(anchor_stmt, 2);
        anchor_count++;

        /* Calculate range bounds */
        int min_line = anchor_line - line_range;
        if (min_line < 1) min_line = 1;
        int max_line = anchor_line + line_range;

        /* First, check if ALL secondary patterns exist in range */
        SqlQueryBuilder check_builder;
        if (init_sql_builder(&check_builder) != 0) {
            continue;
        }

        if (sql_append(&check_builder,
            "SELECT COUNT(DISTINCT symbol) FROM code_index WHERE filename = ? AND directory = ? "
            "AND line BETWEEN ? AND ? AND (") != 0) {
            free_sql_builder(&check_builder);
            continue;
        }

        int check_failed = 0;
        for (int i = 1; i < patterns->count; i++) {
            if (i > 1) {
                if (sql_append(&check_builder, " OR ") != 0) {
                    check_failed = 1;
                    break;
                }
            }
            char *escaped_pattern = sqlite3_mprintf("%q", patterns->patterns[i]);
            ret = sql_append(&check_builder, "symbol LIKE %s ESCAPE '\\'", escaped_pattern);
            sqlite3_free(escaped_pattern);
            if (ret != 0) {
                check_failed = 1;
                break;
            }
        }

        if (check_failed) {
            free_sql_builder(&check_builder);
            continue;
        }

        if (sql_append(&check_builder, ")") != 0) {
            free_sql_builder(&check_builder);
            continue;
        }
        if (build_common_filters_web(&check_builder, include, exclude, filters, file_filter, within_ranges, debug) != 0) {
            free_sql_builder(&check_builder);
            continue;
        }

        sqlite3_stmt *check_stmt;
        if (sqlite3_prepare_v2(db->db, check_builder.sql, -1, &check_stmt, NULL) != SQLITE_OK) {
            free_sql_builder(&check_builder);
            continue;
        }
        free_sql_builder(&check_builder);

        sqlite3_bind_text(check_stmt, 1, filename, -1, SQLITE_STATIC);
        sqlite3_bind_text(check_stmt, 2, directory, -1, SQLITE_STATIC);
        sqlite3_bind_int(check_stmt, 3, min_line);
        sqlite3_bind_int(check_stmt, 4, max_line);

        int distinct_count = 0;
        if (sqlite3_step(check_stmt) == SQLITE_ROW) {
            distinct_count = sqlite3_column_int(check_stmt, 0);
        }
        sqlite3_finalize(check_stmt);

        /* Only insert if ALL secondary patterns found */
        if (distinct_count == patterns->count - 1) {
            complete_matches++;

            /* Insert anchor symbol */
            SqlQueryBuilder insert_builder;
            if (init_sql_builder(&insert_builder) != 0) {
                continue;
            }

            char *escaped_anchor_insert = sqlite3_mprintf("%q", patterns->patterns[0]);
            ret = sql_append(&insert_builder,
                "INSERT INTO proximity_results "
                "SELECT * FROM code_index WHERE filename = ? AND directory = ? "
                "AND line = ? AND symbol LIKE %s ESCAPE '\\'", escaped_anchor_insert);
            sqlite3_free(escaped_anchor_insert);
            if (ret != 0) {
                free_sql_builder(&insert_builder);
                continue;
            }

            sqlite3_stmt *insert_stmt;
            if (sqlite3_prepare_v2(db->db, insert_builder.sql, -1, &insert_stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_text(insert_stmt, 1, filename, -1, SQLITE_STATIC);
                sqlite3_bind_text(insert_stmt, 2, directory, -1, SQLITE_STATIC);
                sqlite3_bind_int(insert_stmt, 3, anchor_line);
                sqlite3_step(insert_stmt);
                sqlite3_finalize(insert_stmt);
            }
            free_sql_builder(&insert_builder);

            /* Insert matching secondaries within range */
            sqlite3_stmt *range_stmt;
            if (sqlite3_prepare_v2(db->db, range_builder.sql, -1, &range_stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_text(range_stmt, 1, filename, -1, SQLITE_STATIC);
                sqlite3_bind_text(range_stmt, 2, directory, -1, SQLITE_STATIC);
                sqlite3_bind_int(range_stmt, 3, min_line);
                sqlite3_bind_int(range_stmt, 4, max_line);
                sqlite3_step(range_stmt);
                sqlite3_finalize(range_stmt);
            }
        }
    }
    sqlite3_finalize(anchor_stmt);
    free_sql_builder(&range_builder);

    if (debug) {
        fprintf(stderr, "Proximity search: %d anchors found, %d complete matches\n",
               anchor_count, complete_matches);
    }

    return 0;
}
