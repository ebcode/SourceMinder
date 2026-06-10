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
#include <strings.h>
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
    /* Handle extension shorthand (.c, .h, .py, etc.) on raw input BEFORE
     * wildcard conversion.  If conversion happened first, '.' → '_' and
     * the pattern would incorrectly become a single-char LIKE wildcard. */
    if (input[0] == '.' && input[1] != '\0' && input[1] != '/' && input[1] != '.') {
        size_t pattern_len = strlen(input) + 2; /* % + extension + \0 */
        char *expanded = malloc(pattern_len);
        if (!expanded) {
            fprintf(stderr, "Error: Failed to allocate memory for file pattern\n");
            *dir_out = NULL;
            *file_out = NULL;
            return -1;
        }
        snprintf(expanded, pattern_len, "%%%s", input);
        *dir_out = NULL;
        *file_out = expanded;
        return 0;
    }

    /* Convert shell-style wildcards (*) to SQL LIKE wildcards (%) */
    char converted_input[PATH_MAX_LENGTH];
    convert_wildcards_web(input, converted_input, sizeof(converted_input));

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

/* WEB_SAFE: exact extraction from query-index.c for indexed metadata filter
 * construction.  col_prefix is prepended to column names to support table
 * aliases (e.g. "ci.") in self-join proximity queries; pass "" for
 * unaliased queries. */
int build_common_filters_web(SqlQueryBuilder *builder,
                                    ContextTypeList *include, ContextTypeList *exclude,
                                    QueryFilters *filters, FileFilterList *file_filter,
                                    WithinRangeList *within_ranges, int debug,
                                    const char *col_prefix) {
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
                    "(%sdirectory LIKE %s ESCAPE '\\' AND %sfilename LIKE %s ESCAPE '\\')",
                    col_prefix, escaped_dir, col_prefix, escaped_file);
                sqlite3_free(escaped_dir);
                sqlite3_free(escaped_file);
                if (ret != 0) return -1;
            } else {
                /* No directory part - filter filename only */
                char *escaped_file = sqlite3_mprintf("%q", file_filter->patterns[i].filename);
                int ret = sql_append(builder,
                    "%sfilename LIKE %s ESCAPE '\\'",
                    col_prefix, escaped_file);
                sqlite3_free(escaped_file);
                if (ret != 0) return -1;
            }
        }
        if (sql_append(builder, ")") != 0) return -1;
    }

    /* Add line range filter */
    if (filters && filters->line_start >= 0) {
        if (filters->line_end == filters->line_start) {
            if (sql_append(builder, " AND %sline = %d", col_prefix, filters->line_start) != 0) return -1;
        } else {
            if (sql_append(builder, " AND %sline BETWEEN %d AND %d",
                col_prefix, filters->line_start, filters->line_end) != 0) return -1;
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
                "(%sdirectory = %s AND %sfilename = %s AND %sline BETWEEN %d AND %d)",
                col_prefix, escaped_dir, col_prefix, escaped_file, col_prefix,
                within_ranges->ranges[i].line_start, within_ranges->ranges[i].line_end);
            sqlite3_free(escaped_dir);
            sqlite3_free(escaped_file);
            if (ret != 0) return -1;
        }
        if (sql_append(builder, ")") != 0) return -1;
    }

    /* Add include filter - database now uses compact form */
    if (include && include->count > 0) {
        if (sql_append(builder, " AND %scontext IN (", col_prefix) != 0) return -1;
        for (int i = 0; i < include->count; i++) {
            if (sql_append(builder, "%s'%s'",
                i > 0 ? ", " : "", context_to_string(include->types[i], 1)) != 0) return -1;
        }
        if (sql_append(builder, ")") != 0) return -1;
    }

    /* Add exclude filter - database now uses compact form */
    if (exclude && exclude->count > 0) {
        if (sql_append(builder, " AND %scontext NOT IN (", col_prefix) != 0) return -1;
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
            int ret = sql_append(builder, "%s%s" #name " LIKE %s ESCAPE '\\'", \
                i > 0 ? " OR " : "", col_prefix, escaped_value); \
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
            int ret = sql_append(builder, "%s%s" #name " LIKE %s ESCAPE '\\'", \
                i > 0 ? " OR " : "", col_prefix, escaped_value); \
            sqlite3_free(escaped_value); \
            if (ret != 0) return -1; \
        } \
        if (sql_append(builder, ")") != 0) return -1; \
    }
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN

    /* Virtual filter: --parent-type. Resolve parent_symbol to its
     * definition in the same file (variable, argument, or struct field)
     * and match that definition's declared type.  Outer-row references
     * inside the correlated subquery must be qualified — bare names would
     * resolve to def — so fall back to the code_index table name when no
     * alias prefix is in play. */
    if (filters && filters->parent_type.count > 0) {
        const char *pt_prefix = (col_prefix && col_prefix[0]) ? col_prefix : "code_index.";
        if (sql_append(builder,
            " AND %sparent_symbol <> '' AND EXISTS ("
            "SELECT 1 FROM code_index def"
            " WHERE def.symbol = %sparent_symbol"
            " AND def.directory = %sdirectory"
            " AND def.filename = %sfilename"
            " AND def.is_definition = 1"
            " AND def.context IN ('VAR', 'ARG', 'PROP')"
            " AND (", pt_prefix, pt_prefix, pt_prefix, pt_prefix) != 0) return -1;
        for (int i = 0; i < filters->parent_type.count; i++) {
            char *escaped_value = sqlite3_mprintf("%q", filters->parent_type.values[i]);
            int ret = sql_append(builder, "%sdef.type LIKE %s ESCAPE '\\'",
                i > 0 ? " OR " : "", escaped_value);
            sqlite3_free(escaped_value);
            if (ret != 0) return -1;
        }
        if (sql_append(builder, "))") != 0) return -1;
    }

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
            if (build_common_filters_web(builder, include, exclude, filters, file_filter, within_ranges, debug, "") != 0) return -1;
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

        /* Re-apply metadata filters to the outer rows so only
         * matching rows at intersection lines are returned */
        if (build_common_filters_web(builder, include, exclude, filters,
                                     file_filter, within_ranges, debug, "") != 0)
            return -1;

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
        if (build_common_filters_web(builder, include, exclude, filters, file_filter, within_ranges, debug, "") != 0) return -1;
    }

    return 0;
}

/* WEB_SAFE: builds a self-join EXISTS query for proximity search (--and RANGE).
 * Produces single-SQL results equivalent to the temp-table approach used by
 * the native CLI, without requiring multi-statement execution in the bridge.
 *
 * Generates:
 *   SELECT * FROM code_index ci WHERE (
 *     ci.symbol LIKE 'p1' ESCAPE '\' OR ci.symbol LIKE 'p2' ESCAPE '\'
 *   )
 *   [common filters with "ci." prefix]
 *   AND EXISTS (SELECT 1 FROM code_index WHERE symbol LIKE 'p1' ESCAPE '\'
 *     AND directory=ci.directory AND filename=ci.filename
 *     AND ABS(line-ci.line)<=R [common filters with "" prefix])
 *   ...
 *   ORDER BY ci.directory, ci.filename, ci.line */
static int build_query_sql_proximity_web(SqlQueryBuilder *builder,
        PatternList *patterns, int line_range,
        ContextTypeList *include, ContextTypeList *exclude,
        QueryFilters *filters, FileFilterList *file_filter,
        WithinRangeList *within_ranges, int debug) {
    if (sql_append(builder, "SELECT * FROM code_index ci WHERE (") != 0) return -1;

    for (int i = 0; i < patterns->count; i++) {
        if (i > 0) {
            if (sql_append(builder, " OR ") != 0) return -1;
        }
        char *escaped = sqlite3_mprintf("%q", patterns->patterns[i]);
        int ret = sql_append(builder, "ci.symbol LIKE %s ESCAPE '\\'", escaped);
        sqlite3_free(escaped);
        if (ret != 0) return -1;
    }
    if (sql_append(builder, ")") != 0) return -1;

    if (build_common_filters_web(builder, include, exclude, filters,
                                 file_filter, within_ranges, debug, "ci.") != 0)
        return -1;

    for (int i = 0; i < patterns->count; i++) {
        if (sql_append(builder, " AND EXISTS (SELECT 1 FROM code_index WHERE ") != 0) return -1;

        char *escaped = sqlite3_mprintf("%q", patterns->patterns[i]);
        int ret = sql_append(builder,
            "symbol LIKE %s ESCAPE '\\' AND directory=ci.directory AND filename=ci.filename AND ABS(line-ci.line)<=%d",
            escaped, line_range);
        sqlite3_free(escaped);
        if (ret != 0) return -1;

        if (build_common_filters_web(builder, include, exclude, filters,
                                     file_filter, within_ranges, debug, "") != 0)
            return -1;

        if (sql_append(builder, ")") != 0) return -1;
    }

    if (sql_append(builder, " ORDER BY ci.directory, ci.filename, ci.line") != 0) return -1;
    return 0;
}

/* WEB_SAFE: builds the final SQL query for indexed result retrieval. */
int build_query_sql_web(SqlQueryBuilder *builder, PatternList *patterns,
                                ContextTypeList *include, ContextTypeList *exclude,
                                QueryFilters *filters, FileFilterList *file_filter,
                                WithinRangeList *within_ranges, int line_range, int debug) {
    /* For proximity search with range, use self-join EXISTS (single SQL) */
    if (line_range > 0 && patterns->count > 1) {
        return build_query_sql_proximity_web(builder, patterns, line_range,
            include, exclude, filters, file_filter, within_ranges, debug);
    }

    if (sql_append(builder, "SELECT * FROM code_index WHERE (") != 0) return -1;
    if (build_query_filters_web(builder, patterns, include, exclude, filters, file_filter, within_ranges, line_range, debug) != 0) return -1;
    if (sql_append(builder, " ORDER BY directory, filename, line") != 0) return -1;
    return 0;
}

/* Maps a context name or abbreviation to its compact uppercase display code.
 * Used for -i/-x parsing and for formatting the Result breakdown line. */
const char *map_context_web(const char *token) {
    struct { const char *alias; const char *code; } map[] = {
        {"arg","ARG"}, {"argument","ARG"},
        {"call","CALL"}, {"case","CASE"}, {"class","CLASS"},
        {"com","COM"}, {"comment","COM"},
        {"enum","ENUM"},
        {"exc","EXC"}, {"exception","EXC"},
        {"exp","EXP"}, {"export","EXP"},
        {"file","FILE"}, {"filename","FILE"},
        {"func","FUNC"}, {"function","FUNC"},
        {"goto","GOTO"},
        {"iface","IFACE"}, {"interface","IFACE"},
        {"imp","IMP"}, {"import","IMP"},
        {"label","LABEL"},
        {"lam","LAM"}, {"lambda","LAM"},
        {"macro","MACRO"},
        {"ns","NS"}, {"namespace","NS"},
        {"prop","PROP"}, {"property","PROP"},
        {"str","STR"}, {"string","STR"},
        {"trait","TRAIT"}, {"type","TYPE"},
        {"var","VAR"}, {"variable","VAR"},
        {NULL,NULL}
    };
    for (int i = 0; map[i].alias; i++) {
        if (strcasecmp(token, map[i].alias) == 0) return map[i].code;
    }
    return NULL;
}

