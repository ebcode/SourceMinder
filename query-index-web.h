/*
 * Public header for the WEB_SAFE query-index extraction surface.
 *
 * Types and function declarations shared between the smoke target
 * (query-index-web.c compiled standalone) and the linked WASM module
 * (qi-web-entry.c + query-index-web.c).
 *
 * This header is deliberately free of sqlite3.h and tree-sitter includes.
 */
#ifndef QUERY_INDEX_WEB_H
#define QUERY_INDEX_WEB_H

#include "config.h"

/* config.h may unconditionally #define ENABLE_TYPESCRIPT to 0, overriding the
 * -D flag passed to emcc.  The web bridge needs scope/namespace columns. */
#undef ENABLE_TYPESCRIPT
#define ENABLE_TYPESCRIPT 1
#include "shared/constants.h"
#include "shared/sql_builder.h"

/* -- Types extracted from query-index.c -- */

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

/* Database wrapper - forward-declared without sqlite3.h for header cleanliness */
struct sqlite3;
struct sqlite3_stmt;
typedef struct {
    struct sqlite3 *db;
    struct sqlite3_stmt *insert_stmt;
} CodeIndexDatabase;

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

/* -- Forward declarations from shared helpers (needed by web entry point) -- */

/* from shared/string_utils.h */
void to_upper(char *str);
void to_lowercase_copy(const char *src, char *dst, size_t size);
void pluralize_common_word(const char *word, char *output, size_t output_size);
char *try_strdup_ctx(const char *str, const char *err_msg);
char *safe_strdup_ctx(const char *str, const char *err_msg);

/* from shared/database.h */
ContextType string_to_context(const char *str);
const char *context_to_string(ContextType type, int compact);

/* from shared/file_utils.h */
int parse_source_location(const char *source_location, int *start_line,
                          int *start_column, int *end_line, int *end_column);

/* -- WEB_SAFE function declarations (exact extractions from query-index.c) -- */

const char *map_context_web(const char *token);
void convert_wildcards_web(const char *pattern, char *output, size_t output_size);
int  process_file_pattern_web(const char *input, char **dir_out, char **file_out);
int  build_common_filters_web(SqlQueryBuilder *builder,
                              ContextTypeList *include, ContextTypeList *exclude,
                              QueryFilters *filters, FileFilterList *file_filter,
                              WithinRangeList *within_ranges, int debug,
                              const char *col_prefix);
int  build_query_filters_web(SqlQueryBuilder *builder, PatternList *patterns,
                             ContextTypeList *include, ContextTypeList *exclude,
                             QueryFilters *filters, FileFilterList *file_filter,
                             WithinRangeList *within_ranges, int line_range, int debug);
int  build_query_sql_web(SqlQueryBuilder *builder, PatternList *patterns,
                         ContextTypeList *include, ContextTypeList *exclude,
                         QueryFilters *filters, FileFilterList *file_filter,
                         WithinRangeList *within_ranges, int line_range, int debug);

#endif /* QUERY_INDEX_WEB_H */
