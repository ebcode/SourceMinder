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

#include "config.h"
#include "shared/constants.h"
#include "shared/sql_builder.h"

/* Temporary local declarations to keep the extracted web slice buildable
 * before the broader database header is split away from sqlite3.h. */
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
    CONTEXT_GOTO,
    CONTEXT_MACRO
} ContextType;

const char *context_to_string(ContextType type, int compact);
char *sqlite3_mprintf(const char *fmt, ...);
void sqlite3_free(void *ptr);
char *try_strdup_ctx(const char *str, const char *err_msg);

/* Exact extractions of query-side types that are still private to query-index.c. */
typedef struct {
    ContextType types[MAX_CONTEXT_TYPES];
    int count;
} ContextTypeList;

typedef struct {
    char *patterns[MAX_PATTERNS];
    int count;
} PatternList;

typedef struct {
    char *values[MAX_CONTEXT_TYPES];
    int count;
} StringList;

typedef struct {
    char *directory;  /* NULL if no directory part */
    char *filename;   /* Always present */
} FilePattern;

typedef struct {
    FilePattern patterns[MAX_CONTEXT_TYPES];
    int count;
} FileFilterList;

typedef struct {
    int line_start;  /* -1 = not set */
    int line_end;    /* -1 = not set */
#define COLUMN(name, ...) StringList name;
#define INT_COLUMN(name, ...) StringList name;
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN
} QueryFilters;

typedef struct {
    char directory[DIRECTORY_MAX_LENGTH];
    char filename[FILENAME_MAX_LENGTH];
    int line_start;
    int line_end;
} WithinRange;

typedef struct {
    WithinRange ranges[MAX_PATTERNS];
    int count;
} WithinRangeList;

/* WEB_SAFE: exact extraction from query-index.c for wildcard translation into SQL LIKE syntax. */
static void convert_wildcards_web(const char *pattern, char *output, size_t output_size) {
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
static int process_file_pattern_web(const char *input, char **dir_out, char **file_out) {
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
static int build_common_filters_web(SqlQueryBuilder *builder,
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
                    "(directory LIKE '%s' ESCAPE '\\' AND filename LIKE '%s' ESCAPE '\\')",
                    escaped_dir, escaped_file);
                sqlite3_free(escaped_dir);
                sqlite3_free(escaped_file);
                if (ret != 0) return -1;
            } else {
                /* No directory part - filter filename only */
                char *escaped_file = sqlite3_mprintf("%q", file_filter->patterns[i].filename);
                int ret = sql_append(builder,
                    "filename LIKE '%s' ESCAPE '\\'",
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
                "(directory = '%s' AND filename = '%s' AND line BETWEEN %d AND %d)",
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
            int ret = sql_append(builder, "%s" #name " LIKE '%s' ESCAPE '\\\\'", \
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
            int ret = sql_append(builder, "%s" #name " LIKE '%s' ESCAPE '\\\\'", \
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
static int build_query_filters_web(SqlQueryBuilder *builder, PatternList *patterns,
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
                "SELECT directory, filename, line FROM code_index WHERE symbol LIKE '%s' ESCAPE '\\'",
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
            int ret = sql_append(builder, "symbol LIKE '%s' ESCAPE '\\'", escaped_pattern);
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
            if (sql_append(builder, "(symbol LIKE ? ESCAPE '\\')") != 0) return -1;
        }
        if (sql_append(builder, ")") != 0) return -1;

        /* Add filters once at the end for OR mode */
        if (build_common_filters_web(builder, include, exclude, filters, file_filter, within_ranges, debug) != 0) return -1;
    }

    return 0;
}
