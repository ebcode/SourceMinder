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
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <sys/stat.h>

#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
/* Windows/MinGW: provide POSIX compatibility */
#define strcasecmp _stricmp
#define strncasecmp _strnicmp

/* strndup is not available on Windows */
static char *strndup(const char *s, size_t n) {
    size_t len = strnlen(s, n);
    char *new_str = malloc(len + 1);
    if (new_str) {
        memcpy(new_str, s, len);
        new_str[len] = '\0';
    }
    return new_str;
}
#else
#include <strings.h>
#endif

#include <regex.h>
#include "shared/database.h"
#include "shared/constants.h"
#include "shared/file_opener.h"
#include "shared/file_utils.h"
#include "shared/string_utils.h"
#include "shared/extensions.h"
#include "shared/filter.h"
#include "shared/paths.h"
#include "shared/toc.h"
#include "shared/version.h"
#include "shared/sql_builder.h"

typedef struct {
    ContextType types[MAX_CONTEXT_TYPES];
    int count;
} ContextTypeList;

typedef struct {
    char *patterns[MAX_PATTERNS];
    int count;
} PatternList;

/* Generic string list for extensible column filters */
typedef struct {
    char *values[MAX_CONTEXT_TYPES];
    int count;
} StringList;

/* File pattern with separate directory and filename parts */
typedef struct {
    char *directory;  /* NULL if no directory part */
    char *filename;   /* Always present */
} FilePattern;

/* List of file patterns for filtering */
typedef struct {
    FilePattern patterns[MAX_CONTEXT_TYPES];
    int count;
} FileFilterList;

/* Query filters structure with X-Macro generated fields */
typedef struct {
    /* Traditional context filters */
    ContextTypeList include;
    ContextTypeList exclude;
    /* Line range filter */
    int line_start;  /* -1 = not set */
    int line_end;    /* -1 = not set */
    /* X-Macro: Extensible filterable column filters */
#define COLUMN(name, ...) StringList name;
#define INT_COLUMN(name, ...) StringList name;
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN
    /* Virtual filter (no backing column): --parent-type resolves
     * parent_symbol to its same-file definition and matches that
     * definition's declared type */
    StringList parent_type;
} QueryFilters;

/* Within filter - stores symbol names for --within flag */
typedef struct {
    char *symbols[MAX_PATTERNS];  /* Array of symbol names to look up */
    int count;                     /* Number of symbols */
} WithinFilter;

/* Within ranges - stores per-file line ranges from definition lookup */
typedef struct {
    char directory[DIRECTORY_MAX_LENGTH];
    char filename[FILENAME_MAX_LENGTH];
    int line_start;
    int line_end;
} WithinRange;

typedef struct {
    WithinRange ranges[MAX_PATTERNS];  /* Max 32 ranges */
    int count;
} WithinRangeList;

/* Show column flags structure with X-Macro generated fields */
typedef struct {
#define COLUMN(name, ...) int name;
#define INT_COLUMN(name, ...) int name;
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN
} ShowColumns;

/* Row data structure - holds all column values */
typedef struct {
    /* Internal/core columns (always present from database) */
    const char *directory;
    const char *filename;
    const char *source_location;
    const char *symbol;        /* lowercase 'symbol' from database */
    int line;
    const char *context;
    const char *full_symbol;
    /* X-Macro: Extensible filterable columns */
#define COLUMN(name, ...) const char *name;
#define INT_COLUMN(name, ...) int name;
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN
} RowData;

/* Column type for proper serialization */
typedef enum {
    COL_TYPE_INT,
    COL_TYPE_STRING
} ColumnType;

/* Column getter function - extracts data from RowData */
typedef const void* (*ColumnGetter)(RowData *data);

/* Column specification with getter function */
typedef struct {
    const char *name;           /* CLI name: "line", "context", etc. */
    const char *header;         /* Display header (full name) */
    const char *header_compact; /* Display header (compact name) */
    int width;                  /* Column width (0 = variable) */
    ColumnType type;            /* Data type */
    ColumnGetter getter;        /* Function to extract this column's data */
} ColumnSpec;

/* Active column instance */
typedef struct {
    ColumnSpec *spec;
    int enabled;
} ActiveColumn;

/* Convert context type string between compact and full form */
static const char* display_context(const char *context_type, int compact) {
    /* Database now stores compact form, so if compact mode, return as-is */
    if (compact) return context_type;

    /* Convert compact to full: convert string to enum and back with full flag */
    char upper[CONTEXT_TYPE_MAX_LENGTH];
    snprintf(upper, sizeof(upper), "%s", context_type);
    to_upper(upper);
    ContextType type = string_to_context(upper);
    return context_to_string(type, 0);
}

/* Print an "unrecognized context type" error with the accepted names, for a
 * bad -i/-x argument. `flag` is "-i" or "-x"; `given` is the user's raw arg. */
static void report_invalid_context(const char *flag, const char *given) {
    fprintf(stderr, "Error: unrecognized context type '%s' for %s.\n", given, flag);
    fprintf(stderr, "Valid types: class iface func arg var exc type prop com str file "
                    "imp exp call ns enum case trait lam label goto macro\n");
    fprintf(stderr, "Note: Go structs index as 'class', interfaces as 'iface'.\n");
}

/* Flag presence bits */
typedef enum {
    FLAG_COLUMNS = 1 << 0,
    FLAG_VERBOSE = 1 << 1,
    FLAG_LIMIT   = 1 << 2,
    FLAG_INCLUDE = 1 << 3,
    FLAG_EXCLUDE = 1 << 4,
    FLAG_COMPACT = 1 << 5,
    FLAG_FILE    = 1 << 6,
    FLAG_SAME_LINE = 1 << 7,
    FLAG_CONTEXT_AFTER  = 1 << 8,
    FLAG_CONTEXT_BEFORE = 1 << 9,
    FLAG_CONTEXT_BOTH   = 1 << 10,
    FLAG_FILES_ONLY = 1 << 11,
    FLAG_DB_FILE = 1 << 12,
    /* X-Macro: Generate FLAG bits for extensible columns */
#define COLUMN(name, sql_type, c_type, width, full, compact, long_flag, short_flag, ...) \
    FLAG_##long_flag = 1 << (__COUNTER__ + 13),
#define INT_COLUMN(name, sql_type, c_type, width, full, compact, long_flag, short_flag, ...) \
    FLAG_##long_flag = 1 << (__COUNTER__ + 13),
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN
    FLAG_LAST  /* Sentinel */
} CliFlags;

/* Check which flags are present in CLI args */
static int scan_cli_flags(int argc, char *argv[]) {
    int flags = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--columns") == 0) flags |= FLAG_COLUMNS;
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) flags |= FLAG_VERBOSE;
        else if (strcmp(argv[i], "--limit") == 0 || strcmp(argv[i], "-l") == 0) flags |= FLAG_LIMIT;
        else if (strcmp(argv[i], "--files") == 0) flags |= FLAG_FILES_ONLY;
        else if (strcmp(argv[i], "--db-file") == 0) flags |= FLAG_DB_FILE;
        else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--include-context") == 0) flags |= FLAG_INCLUDE;
        else if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--exclude-context") == 0) flags |= FLAG_EXCLUDE;
        else if (strcmp(argv[i], "--compact") == 0) flags |= FLAG_COMPACT;
        else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file") == 0) flags |= FLAG_FILE;
        else if (strcmp(argv[i], "--and") == 0 || strcmp(argv[i], "--same-line") == 0) flags |= FLAG_SAME_LINE;
        else if (strcmp(argv[i], "-A") == 0) flags |= FLAG_CONTEXT_AFTER;
        else if (strcmp(argv[i], "-B") == 0) flags |= FLAG_CONTEXT_BEFORE;
        else if (strcmp(argv[i], "-C") == 0) flags |= FLAG_CONTEXT_BOTH;
        /* X-Macro: Check for extensible column flags */
#define COLUMN(name, sql_type, c_type, width, full, compact, long_flag, short_flag, ...) \
        else if (strcmp(argv[i], "--" #long_flag) == 0 || strcmp(argv[i], "-" #short_flag) == 0) flags |= FLAG_##long_flag;
#define INT_COLUMN(name, sql_type, c_type, width, full, compact, long_flag, short_flag, ...) \
        else if (strcmp(argv[i], "--" #long_flag) == 0 || strcmp(argv[i], "-" #short_flag) == 0) flags |= FLAG_##long_flag;
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN
    }
    return flags;
}

/* Check if config line should be skipped based on CLI flags */
static int should_skip_config_line(const char *line, int cli_flags) {
    if ((cli_flags & FLAG_COLUMNS) && strstr(line, "--columns") == line) return 1;
    if ((cli_flags & FLAG_VERBOSE) && (strstr(line, "-v") == line || strstr(line, "--verbose") == line)) return 1;
    if ((cli_flags & FLAG_LIMIT) && (strstr(line, "--limit") == line || strstr(line, "-l") == line)) return 1;
    if ((cli_flags & FLAG_FILES_ONLY) && strstr(line, "--files") == line) return 1;
    if ((cli_flags & FLAG_DB_FILE) && strstr(line, "--db-file") == line) return 1;
    if ((cli_flags & FLAG_INCLUDE) && (strstr(line, "-i") == line || strstr(line, "--include-context") == line)) return 1;
    if ((cli_flags & FLAG_EXCLUDE) && (strstr(line, "-x") == line || strstr(line, "--exclude-context") == line)) return 1;
    if ((cli_flags & FLAG_COMPACT) && strstr(line, "--compact") == line) return 1;
    if ((cli_flags & FLAG_FILE) && (strstr(line, "-f") == line || strstr(line, "--file") == line)) return 1;
    if ((cli_flags & FLAG_SAME_LINE) && (strstr(line, "--and") == line || strstr(line, "--same-line") == line)) return 1;
    if ((cli_flags & FLAG_CONTEXT_AFTER) && strstr(line, "-A") == line) return 1;
    if ((cli_flags & FLAG_CONTEXT_BEFORE) && strstr(line, "-B") == line) return 1;
    if ((cli_flags & FLAG_CONTEXT_BOTH) && strstr(line, "-C") == line) return 1;
    /* X-Macro: Skip config lines for extensible column flags */
#define COLUMN(name, sql_type, c_type, width, full, compact, long_flag, short_flag, ...) \
    if ((cli_flags & FLAG_##long_flag) && (strstr(line, "-" #short_flag) == line || strstr(line, "--" #long_flag) == line)) return 1;
#define INT_COLUMN(name, sql_type, c_type, width, full, compact, long_flag, short_flag, ...) \
    if ((cli_flags & FLAG_##long_flag) && (strstr(line, "-" #short_flag) == line || strstr(line, "--" #long_flag) == line)) return 1;
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN
    return 0;
}

/* Load .smconfig (./.smconfig preferred, else ~/.smconfig) and append its args
 * to argv (config args go after the CLI args, so CLI flags take precedence). */
/* HOST_ONLY: reads CLI defaults from the cwd/HOME and the local filesystem. */
static int load_config_file(int *argc_ptr, char ***argv_ptr, int cli_flags) {
    /* Security note: We trust HOME environment variable for config file location.
     * If an attacker can set HOME, they can already execute arbitrary code in this
     * process context. Config file is optional and only affects query defaults. */
    char config_path[PATH_MAX_LENGTH];
    if (!resolve_smconfig_path(config_path, sizeof(config_path))) return 0;

    FILE *f = safe_fopen(config_path, "r", 1);
    if (!f) return 0;  /* No config file is fine */

    /* Count lines and allocate space for new argv */
    char line[LINE_BUFFER_MEDIUM];
    int config_arg_count = 0;
    char *config_args[MAX_PATTERNS * 2];  /* Max config arguments */

    /* Track if we're in the [qi] section */
    int in_qi_section = 0;

    /* Parse config file */
    while (fgets(line, sizeof(line), f)) {
        /* Remove trailing newline */
        size_t len = strnlength(line, sizeof(line));
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        /* Skip empty lines and comments */
        char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        if (*trimmed == '\0' || *trimmed == '#') continue;

        /* Check for section headers */
        if (*trimmed == '[') {
            in_qi_section = (strcmp(trimmed, "[qi]") == 0);
            continue;
        }

        /* Only process lines in the [qi] section */
        if (!in_qi_section) continue;

        /* Skip lines for flags present in CLI */
        if (should_skip_config_line(trimmed, cli_flags)) continue;

        /* Parse line as space-separated arguments (use strtok_r for thread safety) */
        char *saveptr;
        char *token = strtok_r(trimmed, " \t", &saveptr);
        while (token != NULL && config_arg_count < MAX_PATTERNS * 2) {
            config_args[config_arg_count] = try_strdup_ctx(token, "Failed to allocate memory for config argument");
            if (!config_args[config_arg_count]) {
                /* Cleanup already allocated config args */
                for (int j = 0; j < config_arg_count; j++) {
                    free(config_args[j]);
                }
                fclose(f);
                return -1;
            }
            config_arg_count++;
            token = strtok_r(NULL, " \t", &saveptr);
        }
    }
    fclose(f);

    if (config_arg_count == 0) return 0;

    /* Create new argv with: [program name] + [original CLI args] + [config args] */
    int old_argc = *argc_ptr;
    char **old_argv = *argv_ptr;

    int new_argc = old_argc + config_arg_count;
    char **new_argv = (char **)malloc(sizeof(char *) * (size_t)(new_argc + 1));
    if (!new_argv) {
        fprintf(stderr, "Error: Failed to allocate memory for config args\n");
        /* Cleanup config args */
        for (int j = 0; j < config_arg_count; j++) {
            free(config_args[j]);
        }
        return -1;
    }

    /* Copy program name and original CLI args */
    for (int i = 0; i < old_argc; i++) {
        new_argv[i] = old_argv[i];
    }

    /* Append config args */
    for (int i = 0; i < config_arg_count; i++) {
        new_argv[old_argc + i] = config_args[i];
    }

    new_argv[new_argc] = NULL;

    /* Update argc and argv */
    *argc_ptr = new_argc;
    *argv_ptr = new_argv;
    return 0;
}

/* Check if a word (case-insensitive) exists in a filter file */
/*
static int is_in_filter_file(const char *word, const char *filepath) {
    FILE *f = safe_fopen(filepath, "r", 1);
    if (!f) {
        return 0;
    }

    char line[LINE_BUFFER_MEDIUM];
    char lowercase_word[LINE_BUFFER_MEDIUM];
    snprintf(lowercase_word, sizeof(lowercase_word), "%s", word);
    to_lower(lowercase_word);

    while (fgets(line, sizeof(line), f)) {
        // Remove trailing newline/whitespace
        size_t len = strnlength(line, sizeof(line));
        while (len > 0 && isspace(line[len - 1])) {
            line[--len] = '\0';
        }

        // Skip empty lines
        if (len == 0) continue;

        // Convert to lowercase for comparison
        to_lower(line);

        if (strcmp(line, lowercase_word) == 0) {
            fclose(f);
            return 1;
        }
    }

    fclose(f);
    return 0;
}
*/

/* Check if pattern contains unescaped wildcards (% or _) */
static int has_wildcards(const char *pattern) {
    for (size_t i = 0; pattern[i]; i++) {
        /* Check if this is an escaped character */
        if (pattern[i] == '\\' && (pattern[i+1] == '%' || pattern[i+1] == '_' ||
                                   pattern[i+1] == '*' || pattern[i+1] == '.' ||
                                   pattern[i+1] == '\\')) {
            i++; /* Skip the escaped character */
            continue;
        }
        /* Check for unescaped wildcards (SQL LIKE % and _, or shell-style * and .) */
        if (pattern[i] == '%' || pattern[i] == '_' || pattern[i] == '*' || pattern[i] == '.') {
            return 1;
        }
    }
    return 0;
}

/* Remove escape sequences from pattern for filter checking */
static void unescape_pattern(const char *pattern, char *output, size_t output_size) {
    size_t j = 0;
    for (size_t i = 0; pattern[i] && j < output_size - 1; i++) {
        if (pattern[i] == '\\' && (pattern[i+1] == '%' || pattern[i+1] == '_' || pattern[i+1] == '\\')) {
            output[j++] = pattern[i+1]; /* Copy the escaped character */
            i++; /* Skip the backslash */
        } else {
            output[j++] = pattern[i];
        }
    }
    output[j] = '\0';
}

/* Convert shell-style wildcards (*) to SQL LIKE wildcards (%)
 * Also handles escaped characters to preserve \*, \%, \_, \\
 */
static void convert_wildcards(const char *pattern, char *output, size_t output_size) {
    size_t j = 0;
    for (size_t i = 0; pattern[i] && j < output_size - 1; i++) {
        /* Check if this is an escaped character */
        if (pattern[i] == '\\' && (pattern[i+1] == '*' || pattern[i+1] == '.' ||
                                   pattern[i+1] == '%' || pattern[i+1] == '_' ||
                                   pattern[i+1] == '\\')) {
            /* Handle escaped characters:
             * \* and \. -> just the literal character (no escaping needed in SQL)
             * \%, \_, \\ -> keep the backslash (SQL ESCAPE clause handles these) */
            if (pattern[i+1] == '*' || pattern[i+1] == '.') {
                /* Drop the backslash, just copy the literal character */
                output[j++] = pattern[i+1];
            } else {
                /* Keep backslash for SQL wildcards and backslash itself */
                output[j++] = '\\';
                if (j < output_size - 1) {
                    output[j++] = pattern[i+1];
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

/* Split a term on the alternation operator '|' (the grep-ism '\|' is also
 * accepted). Writes up to max_out newly-allocated segments into out[] and
 * returns the count; empty segments produced by a leading, trailing, or
 * doubled separator are dropped. The caller owns and must free each segment.
 * '|' is never a valid identifier character or a qi wildcard, so the split is
 * unambiguous. */
static int split_alternation(const char *term, char *out[], int max_out) {
    int count = 0;
    const char *seg_start = term;
    const char *p = term;
    while (count < max_out) {
        int sep_len = 0;
        if (p[0] == '\\' && p[1] == '|') {
            sep_len = 2;
        } else if (p[0] == '|') {
            sep_len = 1;
        }
        if (sep_len > 0 || *p == '\0') {
            size_t len = (size_t)(p - seg_start);
            if (len > 0) {
                char seg[SYMBOL_MAX_LENGTH];
                if (len >= sizeof(seg)) {
                    len = sizeof(seg) - 1;
                }
                memcpy(seg, seg_start, len);
                seg[len] = '\0';
                out[count++] = safe_strdup_ctx(seg,
                    "Failed to allocate memory for alternation segment");
            }
            if (*p == '\0') {
                break;
            }
            p += sep_len;
            seg_start = p;
        } else {
            p++;
        }
    }
    return count;
}

/* Records the first grep-style alternation that was split, so a single warning
 * can be emitted (with the user's actual terms) after all args are parsed. */
typedef struct {
    char *original;      /* the offending term as entered, e.g. "Renew\|Session" */
    char *alternatives;  /* its split terms re-quoted and space-joined: 'Renew' 'Session' */
    const char *sep;     /* the separator form the user typed: "\\|" or "|" */
} AltWarning;

/* Records a dotted qualified name (e.g. "Some.function") captured from the
 * ORIGINAL argv token at parse time -- before convert_wildcards rewrites '.'
 * to the '_' LIKE wildcard, which would be indistinguishable from a snake_case
 * symbol. Drives the qualified-name auto-retry / Tip in print_results_by_file. */
typedef struct {
    int active;                        /* a dotted qualified name was captured */
    char original[SYMBOL_MAX_LENGTH];  /* the token as entered, e.g. "Some.function" */
    char qualifier[SYMBOL_MAX_LENGTH]; /* before the last dot, e.g. "Some" */
    char symbol[SYMBOL_MAX_LENGTH];    /* after the last dot, e.g. "function" */
} QualifiedName;

/* Capture warning data for the first split term only (keep the message focused
 * on one example). `original` is the term as entered; `segs`/`nseg` are its raw
 * split alternatives. Echoes the separator form the user actually used. */
static void capture_alt_warning(const char *original, char *const segs[], int nseg,
                                AltWarning *w) {
    if (w->original != NULL) {
        return;  /* already captured an earlier term */
    }
    w->sep = strstr(original, "\\|") ? "\\|" : "|";
    w->original = safe_strdup_ctx(original,
        "Failed to allocate memory for alternation warning");

    /* Build "'seg0' 'seg1' ...": two quotes per term, a space between, plus NUL. */
    size_t cap = 1;
    for (int i = 0; i < nseg; i++) {
        cap += strlen(segs[i]) + 3;
    }
    char *buf = malloc(cap);
    if (!buf) {
        fprintf(stderr, "Failed to allocate memory for alternation warning\n");
        exit(1);
    }
    size_t pos = 0;
    for (int i = 0; i < nseg; i++) {
        pos += (size_t)snprintf(buf + pos, cap - pos, "%s'%s'", i ? " " : "", segs[i]);
    }
    w->alternatives = buf;
}

/* Check if a word matches any regex pattern in the regex-patterns file */
/*
static int matches_regex_filter(const char *word, const char *filepath) {
    FILE *f = safe_fopen(filepath, "r", 1);
    if (!f) {
        return 0;
    }

    char line[LINE_BUFFER_MEDIUM];
    regex_t regex;
    int matched = 0;

    while (fgets(line, sizeof(line), f)) {
        // Remove trailing newline
        line[strcspn(line, "\n")] = '\0';

        // Skip empty lines and comments
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        // Compile and test the regex pattern
        int ret = regcomp(&regex, line, REG_EXTENDED | REG_NOSUB);
        if (ret == 0) {
            if (regexec(&regex, word, 0, NULL, 0) == 0) {
                matched = 1;
                regfree(&regex);
                break;
            }
            regfree(&regex);
        }
    }

    fclose(f);
    return matched;
}
*/

/* Column getter functions - extract data from RowData */
static const void* get_line_col(RowData *data) {
    return &data->line;
}

static const void* get_context_col(RowData *data) {
    return data->context;
}

static const void* get_symbol_col(RowData *data) {
    return data->full_symbol;
}

/* X-Macro: Generate getter functions for extensible columns */
#define COLUMN(name, ...) \
    static const void* get_##name##_col(RowData *data) { \
        return data->name; \
    }
#define INT_COLUMN(name, ...) \
    static const void* get_##name##_col(RowData *data) { \
        return &data->name; \
    }
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN

/* Column registry - all available columns */
static ColumnSpec column_registry[] = {
    /* Core columns (traditional) */
    {"line",    "LINE",    "LINE",    4,  COL_TYPE_INT,    get_line_col},
    {"context", "CONTEXT", "CTX",     7,  COL_TYPE_STRING, get_context_col},
    {"symbol",  "SYMBOL",  "SYM",     0,  COL_TYPE_STRING, get_symbol_col},

    /* X-Macro: Extensible filterable columns */
#define COLUMN(name, sql_type, c_type, width, full, compact, long_flag, short_flag, ...) \
    {#long_flag, full, compact, width, c_type, get_##name##_col},
#define INT_COLUMN(name, sql_type, c_type, width, full, compact, long_flag, short_flag, ...) \
    {#long_flag, full, compact, width, c_type, get_##name##_col},
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN

    {NULL, NULL, NULL, 0, 0, NULL}  /* sentinel */
};

/* Active columns - what will be displayed */
static ActiveColumn active_columns[MAX_CONTEXT_TYPES];
static int num_active_columns = 0;

/* Find column by name (supports aliases from compact column names) */
static ColumnSpec* find_column_by_name(const char *name) {
    /* Check for compact name aliases using X-Macro (case-insensitive) */
    const char *full_name = name;

#define COLUMN(db_name, sql_type, c_type, width, full, compact, long_flag, short_flag, ...) \
    if (strcasecmp(name, compact) == 0) { \
        full_name = #long_flag; \
    }
#define INT_COLUMN(db_name, sql_type, c_type, width, full, compact, long_flag, short_flag, ...) \
    if (strcasecmp(name, compact) == 0) { \
        full_name = #long_flag; \
    }
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN

    /* Also check common abbreviations for core columns */
    if (strcasecmp(name, "sym") == 0) full_name = "symbol";
    else if (strcasecmp(name, "ctx") == 0) full_name = "context";

    /* Look up in registry */
    for (int i = 0; column_registry[i].name != NULL; i++) {
        if (strcmp(column_registry[i].name, full_name) == 0) {
            return &column_registry[i];
        }
    }
    return NULL;
}

/* Add a column by name (used when parsing --columns) */
static int add_column_by_name(const char *name) {
    ColumnSpec *spec = find_column_by_name(name);
    if (spec) {
        if (num_active_columns < MAX_CONTEXT_TYPES) {
            active_columns[num_active_columns++] = (ActiveColumn){spec, 1};
            return 1;
        }
    } else {
        fprintf(stderr, "Warning: unknown column '%s' (available: line, context, parent, scope, modifier, clue, namespace, type, definition, symbol)\n", name);
    }
    return 0;
}

/* Setup default columns based on flags */
static void setup_default_columns(int verbose, QueryFilters *filters, ShowColumns *show_columns) {
    num_active_columns = 0;

    /* Always: line, symbol */
    active_columns[num_active_columns++] = (ActiveColumn){find_column_by_name("line"), 1};
    active_columns[num_active_columns++] = (ActiveColumn){find_column_by_name("symbol"), 1};

    /* Verbose: all extensible columns */
    if (verbose) {
        /* X-Macro: Add all extensible columns in verbose mode */
#define COLUMN(name, sql_type, c_type, width, full, compact, long_flag, short_flag, ...) \
        active_columns[num_active_columns++] = (ActiveColumn){find_column_by_name(#long_flag), 1};
#define INT_COLUMN(name, sql_type, c_type, width, full, compact, long_flag, short_flag, ...) \
        active_columns[num_active_columns++] = (ActiveColumn){find_column_by_name(#long_flag), 1};
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN
    } else if (filters || show_columns) {
        /* Non-verbose: Add columns that have active filters OR show flags */
        /* X-Macro: Add columns with active filters or show flags */
#define COLUMN(name, sql_type, c_type, width, full, compact, long_flag, short_flag, ...) \
        if ((filters && filters->name.count > 0) || (show_columns && show_columns->name)) { \
            active_columns[num_active_columns++] = (ActiveColumn){find_column_by_name(#long_flag), 1}; \
        }
#define INT_COLUMN(name, sql_type, c_type, width, full, compact, long_flag, short_flag, ...) \
        if ((filters && filters->name.count > 0) || (show_columns && show_columns->name)) { \
            active_columns[num_active_columns++] = (ActiveColumn){find_column_by_name(#long_flag), 1}; \
        }
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN
    }

    /* Always: context */
    active_columns[num_active_columns++] = (ActiveColumn){find_column_by_name("context"), 1};
}

static void print_table_header(int compact, int quiet) {
    if (!quiet) printf("\n");

    /* Print header row */
    for (int i = 0; i < num_active_columns; i++) {
        if (!active_columns[i].enabled) continue;
        if (i > 0) printf(" | ");

        ColumnSpec *spec = active_columns[i].spec;
        if (!spec) continue;
        const char *header = compact ? spec->header_compact : spec->header;
        if (spec->width > 0) {
            printf("%-*s", spec->width, header);
        } else {
            printf("%s", header);
        }
    }
    printf("\n");

    /* Print separator line (decoration; suppressed in quiet mode) */
    if (!quiet) {
        for (int i = 0; i < num_active_columns; i++) {
            if (!active_columns[i].enabled) continue;
            if (i > 0) printf("-+-");

            ColumnSpec *spec = active_columns[i].spec;
            if (!spec) continue;
            int width = spec->width > 0 ? spec->width : 24;  /* default width for variable columns */
            for (int j = 0; j < width; j++) {
                printf("-");
            }
        }
        printf("\n");
    }
}

static void print_table_row(RowData *data, int compact) {
    for (int i = 0; i < num_active_columns; i++) {
        if (!active_columns[i].enabled) continue;
        if (i > 0) printf(" | ");

        ColumnSpec *spec = active_columns[i].spec;
        if (!spec) continue;
        const void *value = spec->getter(data);

        if (spec->type == COL_TYPE_INT) {
            printf("%-*d", spec->width, *(const int*)value);
        } else {  /* COL_TYPE_STRING */
            const char *str = (const char*)value;
            /* Apply compact display for context column */
            if (strcmp(spec->name, "context") == 0 && str) {
                str = display_context(str, compact);
            }
            if (spec->width > 0) {
                printf("%-*s", spec->width, str ? str : "");
            } else {
                printf("%s", str ? str : "");
            }
        }
    }
    printf("\n");
}

/* Print header for all-columns mode */
static void print_all_columns_header(int quiet) {
    if (!quiet) printf("\n");

    /* Print internal column headers */
    printf("%-24s | %-16s | %-4s | %-20s | %-3s | %-24s | %-20s",
           "DIRECTORY", "FILENAME", "LINE", "SYMBOL", "CTX", "FULL_SYMBOL", "SOURCE_LOCATION");

    /* Print extensible column headers */
    for (int i = 0; column_registry[i].name != NULL; i++) {
        printf(" | %-12s", column_registry[i].header_compact);
    }
    printf("\n");

    /* Print separator line (decoration; suppressed in quiet mode) */
    if (!quiet) {
        printf("------------------------+------------------+------+----------------------+-----+--------------------------+----------------------");
        for (int i = 0; column_registry[i].name != NULL; i++) {
            printf("+-------------");
        }
        printf("\n");
    }
}

/* Print all columns (internal + extensible) - used for --columns all */
static void print_all_columns_row(RowData *data) {
    /* Print internal/core columns in fixed order with proper formatting */
    printf("%-24s | %-16s | %-4d | %-20s | %-3s | %-24s | %-20s",
           data->directory ? data->directory : "",
           data->filename ? data->filename : "",
           data->line,
           data->symbol ? data->symbol : "",
           data->context ? data->context : "",
           data->full_symbol ? data->full_symbol : "",
           data->source_location ? data->source_location : "");

    /* Print extensible columns from registry with proper formatting */
    for (int i = 0; column_registry[i].name != NULL; i++) {
        ColumnSpec *spec = &column_registry[i];
        const void *value = spec->getter(data);

        printf(" | ");
        if (spec->type == COL_TYPE_INT) {
            int int_val = *(const int*)value;
            printf("%-12d", int_val);
        } else {  /* COL_TYPE_STRING */
            const char *str = (const char*)value;
            printf("%-12s", str ? str : "");
        }
    }
    printf("\n");
}

/* Process file pattern into directory and filename parts with %/ boundary matching
 * Returns: 0 on success, -1 on allocation failure */
/* WEB_SAFE: normalizes file-pattern filters without requiring host services. */
static int process_file_pattern(const char *input, char **dir_out, char **file_out) {
    /* Handle extension shorthand BEFORE wildcard conversion: .c → %.c, .h → %.h, etc.
     * Must use raw input so '.' stays literal rather than becoming '_' (LIKE wildcard). */
    if (input[0] == '.' && input[1] != '/' && input[1] != '.') {
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
    convert_wildcards(input, converted_input, sizeof(converted_input));

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
     * Multi-component paths (e.g. "tools/sources/perl") use % only — the path
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

/* Helper function to build common filter clauses (file, context type, extensible columns)
 * Returns: 0 on success, -1 on error (buffer overflow or allocation failure)
 */
/* WEB_SAFE: builds SQL filter clauses over indexed metadata only. */
static int build_common_filters(SqlQueryBuilder *builder,
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

    /* X-Macro: Add extensible column filters (using LIKE for pattern matching).
     * TEXT columns use leading+trailing wildcards so -p Server matches *Server,
     * ServerImpl, etc. without the caller needing to know language-specific
     * prefixes/suffixes. INT_COLUMN (is_definition) keeps exact match. */
#define COLUMN(name, ...) \
    if (filters && filters->name.count > 0) { \
        if (sql_append(builder, " AND (") != 0) return -1; \
        for (int i = 0; i < filters->name.count; i++) { \
            char *escaped_value = sqlite3_mprintf("%q", filters->name.values[i]); \
            int ret = sql_append(builder, "%s" #name " LIKE '%%%s%%' ESCAPE '\\'", \
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
            int ret = sql_append(builder, "%s" #name " LIKE '%s' ESCAPE '\\'", \
                i > 0 ? " OR " : "", escaped_value); \
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
     * and match that definition's declared type. */
    if (filters && filters->parent_type.count > 0) {
        if (sql_append(builder,
            " AND parent_symbol <> '' AND EXISTS ("
            "SELECT 1 FROM code_index def"
            " WHERE def.symbol = code_index.parent_symbol"
            " AND def.directory = code_index.directory"
            " AND def.filename = code_index.filename"
            " AND def.is_definition = 1"
            " AND def.context IN ('VAR', 'ARG', 'PROP')"
            " AND (") != 0) return -1;
        for (int i = 0; i < filters->parent_type.count; i++) {
            char *escaped_value = sqlite3_mprintf("%q", filters->parent_type.values[i]);
            int ret = sql_append(builder, "%sdef.type LIKE '%s' ESCAPE '\\'",
                i > 0 ? " OR " : "", escaped_value);
            sqlite3_free(escaped_value);
            if (ret != 0) return -1;
        }
        if (sql_append(builder, "))") != 0) return -1;
    }

    return 0;
}

/* WEB_SAFE: composes pattern matching and common filters into query predicates. */
static int build_query_filters(SqlQueryBuilder *builder, PatternList *patterns,
                               ContextTypeList *include, ContextTypeList *exclude, QueryFilters *filters, FileFilterList *file_filter,
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
            if (build_common_filters(builder, include, exclude, filters, file_filter, within_ranges, debug) != 0) return -1;
        }

        if (sql_append(builder, ") AND (") != 0) return -1;  /* Close IN clause, add filter for matched symbols */

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

        if (sql_append(builder, ")") != 0) return -1;  /* Close symbol filter */

        /* Re-apply metadata filters to outer result rows */
        if (build_common_filters(builder, include, exclude, filters, file_filter, within_ranges, debug) != 0) return -1;

        if (sql_append(builder, ")") != 0) return -1;  /* Close WHERE clause */
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
        if (build_common_filters(builder, include, exclude, filters, file_filter, within_ranges, debug) != 0) return -1;
    }

    return 0;
}

/* Build column width SQL fragments using X-Macros */
#define COLUMN(name, ...) "MAX(LENGTH(COALESCE(" #name ", ''))), "
#define INT_COLUMN(name, ...) "MAX(LENGTH(CAST(" #name " AS TEXT))), "
static const char *width_columns =
#include "shared/column_schema.def"
    "";
#undef COLUMN
#undef INT_COLUMN

static int build_width_query(SqlQueryBuilder *builder, PatternList *patterns,
                              ContextTypeList *include, ContextTypeList *exclude, QueryFilters *filters, FileFilterList *file_filter,
                              WithinRangeList *within_ranges, int limit, int line_range, int debug) {
    /* For proximity search, query temp table; otherwise query code_index */
    if (line_range > 0 && patterns->count > 1) {
        /* Proximity search: query temp table */
        if (limit > 0) {
            if (sql_append(builder,
                "SELECT "
                "MAX(LENGTH(CAST(line AS TEXT))), "
                "MAX(LENGTH(context)), "
                "%s"
                "MAX(LENGTH(full_symbol)) "
                "FROM (SELECT * FROM proximity_results ORDER BY directory, filename, line LIMIT %d)",
                width_columns, limit) != 0) return -1;
        } else {
            if (sql_append(builder,
                "SELECT "
                "MAX(LENGTH(CAST(line AS TEXT))), "
                "MAX(LENGTH(context)), "
                "%s"
                "MAX(LENGTH(full_symbol)) "
                "FROM proximity_results", width_columns) != 0) return -1;
        }
    } else {
        /* Normal search: query code_index */
        if (limit > 0) {
            if (sql_append(builder,
                "SELECT "
                "MAX(LENGTH(CAST(line AS TEXT))), "
                "MAX(LENGTH(context)), "
                "%s"
                "MAX(LENGTH(full_symbol)) "
                "FROM (SELECT * FROM code_index WHERE (", width_columns) != 0) return -1;

            if (build_query_filters(builder, patterns, include, exclude, filters, file_filter, within_ranges, line_range, debug) != 0) return -1;
            if (sql_append(builder, " ORDER BY directory, filename, line LIMIT %d)", limit) != 0) return -1;
        } else {
            if (sql_append(builder,
                "SELECT "
                "MAX(LENGTH(CAST(line AS TEXT))), "
                "MAX(LENGTH(context)), "
                "%s"
                "MAX(LENGTH(full_symbol)) "
                "FROM code_index WHERE (", width_columns) != 0) return -1;

            if (build_query_filters(builder, patterns, include, exclude, filters, file_filter, within_ranges, line_range, debug) != 0) return -1;
        }
    }

    if (debug) {
        printf("SQL: [Calculate column widths] %s\n", builder->sql);
    }

    return 0;
}

static void update_column_widths(sqlite3_stmt *stmt, int compact) {
    /* stmt returns columns in order: line, context, [extensible cols...], symbol */
    int col_idx = 0;
    (void)sqlite3_column_int(stmt, col_idx++);  /* Skip max_line - line column is fixed width */
    int max_context = sqlite3_column_int(stmt, col_idx++);

    /* X-Macro: Build array of extensible column max widths */
    int extensible_widths[10] = {0};  /* max extensible columns */
    int ext_idx = 0;
#define COLUMN(name, ...) \
    extensible_widths[ext_idx++] = sqlite3_column_int(stmt, col_idx++);
#define INT_COLUMN(name, ...) \
    extensible_widths[ext_idx++] = sqlite3_column_int(stmt, col_idx++);
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN

    int max_symbol = sqlite3_column_int(stmt, col_idx++);

    /* Update each active column's width */
    ext_idx = 0;  /* Reset for matching */
    for (int i = 0; i < num_active_columns; i++) {
        if (!active_columns[i].enabled) continue;
        ColumnSpec *spec = active_columns[i].spec;
        if (!spec) continue;
        const char *header = compact ? spec->header_compact : spec->header;
        int header_len = (int)strnlength(header, CONTEXT_TYPE_MAX_LENGTH);
        int data_len = 0;

        const char *col_name = spec->name;
        if (strcmp(col_name, "line") == 0) {
            /* Line column is hardcoded to 5 (fits up to 99999) - don't resize */
            continue;
        } else if (strcmp(col_name, "context") == 0) {
            data_len = max_context;
        } else if (strcmp(col_name, "symbol") == 0) {
            data_len = max_symbol;
        } else {
            /* X-Macro: Match extensible column names */
            ext_idx = 0;
#define COLUMN(name_param, sql_type, c_type, width, full, compact, long_flag, short_flag, ...) \
            if (strcmp(col_name, #long_flag) == 0) { data_len = extensible_widths[ext_idx]; } \
            ext_idx++;
#define INT_COLUMN(name_param, sql_type, c_type, width, full, compact, long_flag, short_flag, ...) \
            if (strcmp(col_name, #long_flag) == 0) { data_len = extensible_widths[ext_idx]; } \
            ext_idx++;
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN
        }

        spec->width = (data_len > header_len) ? data_len : header_len;
    }
}

/* Pre-pass that sizes table columns to their content via a separate
 * MAX(LENGTH(...)) query over the same WHERE as the main query.
 *
 * Why a second query instead of measuring the rows we print? The result
 * printer streams: it steps the main query and emits each row immediately,
 * so it can serve arbitrarily large outputs (e.g. `qi '*'`, unlimited by
 * default) without buffering millions of rows in memory. But column widths
 * must be known before the first row prints. Streaming and size-to-content
 * are fundamentally two passes; this is the second one.
 *
 * The cost is one extra scan of the matched set. With the NOCASE idx_symbol
 * (see shared/database.c) exact/prefix patterns serve both passes from the
 * index, so this is cheap for normal queries; only leading-wildcard
 * (*contains*) patterns pay a full scan twice. Kept deliberately over
 * buffering to preserve unbounded streaming. */
static void calculate_column_widths_from_query(CodeIndexDatabase *db, PatternList *patterns,
                                                ContextTypeList *include, ContextTypeList *exclude, QueryFilters *filters, FileFilterList *file_filter,
                                                WithinRangeList *within_ranges, int limit, int line_range, int compact, int debug) {
    SqlQueryBuilder builder;
    if (init_sql_builder(&builder) != 0) {
        return;
    }

    if (build_width_query(&builder, patterns, include, exclude, filters, file_filter, within_ranges, limit, line_range, debug) != 0) {
        free_sql_builder(&builder);
        return;
    }

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->db, builder.sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Width calculation query failed: %s\n", sqlite3_errmsg(db->db));
        free_sql_builder(&builder);
        return;
    }
    free_sql_builder(&builder);

    /* Bind pattern parameters (only for OR mode - AND mode embeds patterns in SQL) */
    if (line_range < 0) {
        for (int i = 0; i < patterns->count; i++) {
            sqlite3_bind_text(stmt, i + 1, patterns->patterns[i], -1, SQLITE_STATIC);
        }
    }

    /* Execute and get max lengths */
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        update_column_widths(stmt, compact);
    }

    sqlite3_finalize(stmt);
}

/* Look up definitions for --within filter
 * Returns: 0 on success, -1 on error */
/* WEB_SAFE: resolves --within scopes entirely from indexed definitions. */
static int lookup_within_definitions(CodeIndexDatabase *db, WithinFilter *within_filter,
                                      WithinRangeList *within_ranges, int debug) {
    if (!within_filter || within_filter->count == 0) {
        if (debug) {
            fprintf(stderr, "DEBUG: lookup_within_definitions, count: %d\n", within_filter->count);
        }
        return 0;  /* No within filter, nothing to do */
    }

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
            "WHERE symbol = '%s' AND is_definition = 1 AND source_location IS NOT NULL",
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

/* Print context lines from source file around a match
 *
 * Displays raw source code before/after the match line, including the match line itself.
 * Highlights all matched symbols in green (ANSI color code).
 * Uses simple line-by-line reading - no caching for initial implementation.
 *
 * NOTE: Future optimization opportunity - cache file contents when processing
 * multiple matches from the same file (results are pre-grouped by ORDER BY).
 *
 * filepath: Full path to source file
 * target_line: Line number of the match
 * patterns: Array of search patterns to highlight
 * pattern_count: Number of patterns
 * before: Number of lines to show before match
 * after: Number of lines to show after match
 */
/* HOST_BRIDGED: requires source file contents; CLI reads locally, web must fetch via a host bridge. */
static void print_context_lines(const char *filepath, int target_line,
                                 char **patterns, int pattern_count,
                                 int before, int after, int raw) {
    FILE *fp = safe_fopen(filepath, "r", 1);  /* silent=1 */
    if (!fp) {
        fprintf(stderr, "Warning: Could not read file '%s' for context lines (file may have moved or permissions changed)\n", filepath);
        return;
    }

    char line[LINE_BUFFER_LARGE];
    int current_line = 0;
    int start_line = (target_line - before > 0) ? target_line - before : 1;
    int end_line = target_line + after;

    /* ANSI color codes: dark green background for highlighting */
    const char *GREEN = "\033[42m";  /* Green background (darker than bright green) */
    const char *RESET = "\033[0m";

    /* Print separator before context lines (grep-style) */
    if (!raw) printf("--\n");

    while (fgets(line, sizeof(line), fp)) {
        current_line++;
        /* Print lines in range (including match line) */
        if (current_line >= start_line && current_line <= end_line) {
            if (raw) {
                /* Raw mode: no line number prefix, no color codes */
                printf("%s", line);
            } else {
                /* Highlight all patterns that appear in this line */
                char output[LINE_BUFFER_LARGE * 2];  /* Double size for color codes */
                char *src = line;
                char *dst = output;
                size_t remaining = sizeof(output) - 1;

                while (*src && remaining > 0) {
                    /* Check if any pattern matches at this position */
                    int matched = 0;
                    for (int i = 0; i < pattern_count; i++) {
                        /* Skip SQL wildcards - only highlight literal patterns */
                        if (strchr(patterns[i], '%') != NULL || strchr(patterns[i], '_') != NULL) {
                            continue;
                        }

                        size_t pattern_len = strlen(patterns[i]);
                        /* Case-insensitive comparison */
                        if (strncasecmp(src, patterns[i], pattern_len) == 0) {
                            /* Add highlighting */
                            size_t color_len = strlen(GREEN);
                            if (remaining > color_len) {
                                memcpy(dst, GREEN, color_len);
                                dst += color_len;
                                remaining -= color_len;
                            }

                            /* Copy the actual matched text */
                            size_t copy_len = (pattern_len < remaining) ? pattern_len : remaining;
                            memcpy(dst, src, copy_len);
                            dst += copy_len;
                            src += pattern_len;
                            remaining -= copy_len;

                            /* Add reset code */
                            size_t reset_len = strlen(RESET);
                            if (remaining > reset_len) {
                                memcpy(dst, RESET, reset_len);
                                dst += reset_len;
                                remaining -= reset_len;
                            }

                            matched = 1;
                            break;
                        }
                    }

                    if (!matched && remaining > 0) {
                        /* No pattern matched, copy single character */
                        *dst++ = *src++;
                        remaining--;
                    }
                }
                *dst = '\0';
                printf("%d:%s", current_line, output);
            }
        }
        /* Early termination once we've passed the range */
        if (current_line > end_line) break;
    }
    fclose(fp);
}

/* WEB_SAFE: counts indexed files using SQLite filters only. */
static int count_distinct_files(CodeIndexDatabase *db,
                                  ContextTypeList *include, ContextTypeList *exclude, QueryFilters *filters, FileFilterList *file_filter,
                                  WithinRangeList *within_ranges, int debug) {
    /* Build SQL query to count distinct files - apply all filters EXCEPT symbol patterns */
    SqlQueryBuilder builder;
    if (init_sql_builder(&builder) != 0) {
        fprintf(stderr, "Error: Failed to initialize SQL query builder\n");
        return 0;
    }

    if (sql_append(&builder, "SELECT COUNT(*) FROM (SELECT DISTINCT directory, filename FROM code_index WHERE 1=1") != 0) {
        free_sql_builder(&builder);
        return 0;
    }

    /* Apply filters (file, context types, extensible columns) but NOT symbol search */
    if (build_common_filters(&builder, include, exclude, filters, file_filter, within_ranges, debug) != 0) {
        free_sql_builder(&builder);
        return 0;
    }

    if (sql_append(&builder, ")") != 0) {
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
static int count_pattern_matches(CodeIndexDatabase *db, const char *pattern) {
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

/**
 * Check file filter patterns for unknown extensions and print warning.
 * Called when query returns no results.
 */
static void warn_unknown_extensions(FileFilterList *file_filter, const FileExtensions *known_exts) {
    if (!file_filter || file_filter->count == 0 || !known_exts || known_exts->count == 0) {
        return;
    }

    for (int i = 0; i < file_filter->count; i++) {
        char ext[FILE_EXTENSION_MAX_LENGTH];
        const char *pattern = file_filter->patterns[i].filename;

        if (extract_extension_from_pattern(pattern, ext, sizeof(ext))) {
            if (!is_known_extension(ext, known_exts)) {
                printf("\nWarning: Extension '%s' is not indexed.\n", ext);

                /* Check if this is a prose/documentation file extension */
                if (is_plaintext_extension(ext)) {
                    printf("Note: This tool is designed for indexing source code, not prose or documentation files.\n");
                }

                printf("Indexed extensions:");
                for (int j = 0; j < known_exts->count; j++) {
                    printf(" %s", known_exts->extensions[j]);
                }
                printf("\n");

                /* Only show "use the appropriate indexer" for non-prose files */
                if (!is_plaintext_extension(ext)) {
                    printf("To index %s files, use the appropriate indexer.\n", ext);
                }

                return;  /* Only show warning once */
            }
        }
    }
}

/* Reasons why a pattern might be filtered */
typedef enum {
    FILTER_REASON_VALID = 0,           /* Pattern would be indexed */
    FILTER_REASON_TOO_SHORT,           /* Less than MIN_SYMBOL_LENGTH chars */
    FILTER_REASON_PURE_NUMBER,         /* Only digits */
    FILTER_REASON_STOPWORD,            /* Common English stopword */
    FILTER_REASON_KEYWORD,             /* Language keyword */
    FILTER_REASON_EXCLUSION_PATTERN    /* Matches regex exclusion pattern */
} FilterReason;

/* Check why a pattern would be filtered */
static FilterReason get_filter_reason(SymbolFilter *filter, const char *pattern) {
    if (!pattern || !pattern[0]) {
        return FILTER_REASON_TOO_SHORT;
    }

    /* Check length first (before filter_should_index) */
    size_t len = strlen(pattern);
    if (len < MIN_SYMBOL_LENGTH) {
        return FILTER_REASON_TOO_SHORT;
    }

    /* Check if pure number */
    int all_digits = 1;
    for (size_t i = 0; i < len; i++) {
        if (!isdigit(pattern[i])) {
            all_digits = 0;
            break;
        }
    }
    if (all_digits) {
        return FILTER_REASON_PURE_NUMBER;
    }

    /* Use filter_should_index to check if pattern would be indexed */
    if (filter_should_index(filter, pattern)) {
        return FILTER_REASON_VALID;
    }

    /* Pattern was filtered - determine why by checking specific conditions */
    /* Note: We can't distinguish between stopword/keyword/pattern without access to internal sets,
     * so we do basic checks and return generic reasons */

    /* Check if it looks like a common stopword (very short common words) */
    char lower[SYMBOL_MAX_LENGTH];
    size_t i;
    for (i = 0; pattern[i] && i < SYMBOL_MAX_LENGTH - 1; i++) {
        lower[i] = (char)tolower((unsigned char)pattern[i]);
    }
    lower[i] = '\0';

    /* Heuristic: short common words are likely stopwords */
    if (len <= 4) {
        const char *common_stopwords[] = {"the", "and", "for", "with", "that", "this", "from", "have", NULL};
        for (int j = 0; common_stopwords[j] != NULL; j++) {
            if (strcmp(lower, common_stopwords[j]) == 0) {
                return FILTER_REASON_STOPWORD;
            }
        }
    }

    /* Heuristic: check for common keywords */
    const char *common_keywords[] = {"if", "else", "for", "while", "return", "function", "class", "const", "let", "var", NULL};
    for (int j = 0; common_keywords[j] != NULL; j++) {
        if (strcmp(lower, common_keywords[j]) == 0) {
            return FILTER_REASON_KEYWORD;
        }
    }

    /* If filter rejected but we don't know why, assume exclusion pattern */
    return FILTER_REASON_EXCLUSION_PATTERN;
}

/* Check if any filters are active (context, file, or extensible column filters) */
static int has_any_filters(ContextTypeList *include, ContextTypeList *exclude, QueryFilters *filters, FileFilterList *file_filter) {
    /* Check context filters */
    if (include->count > 0 || exclude->count > 0) {
        return 1;
    }

    /* Check file filter */
    if (file_filter->count > 0) {
        return 1;
    }

    /* Check extensible column filters using X-Macro */
#define COLUMN(name, ...) \
    if (filters->name.count > 0) return 1;
#define INT_COLUMN(name, ...) \
    if (filters->name.count > 0) return 1;
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN

    return 0;
}

/* WEB_SAFE: computes total match count from indexed data via SQL. */
static int get_total_count(CodeIndexDatabase *db, PatternList *patterns,
                          ContextTypeList *include, ContextTypeList *exclude, QueryFilters *filters, FileFilterList *file_filter,
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
        if (build_query_filters(&builder, patterns, include, exclude, filters, file_filter, within_ranges, line_range, debug) != 0) {
            free_sql_builder(&builder);
            return 0;
        }
    }

    if (debug) {
        printf("SQL: [Get total count] %s\n", builder.sql);
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

/* WEB_SAFE: aggregates context-type counts from indexed data via SQL GROUP BY. */
static void get_context_summary(CodeIndexDatabase *db, PatternList *patterns,
                                ContextTypeList *include, ContextTypeList *exclude, QueryFilters *filters, FileFilterList *file_filter,
                                WithinRangeList *within_ranges, int line_range, int debug) {
    /* Build SQL query with GROUP BY context */
    SqlQueryBuilder builder;
    if (init_sql_builder(&builder) != 0) {
        return;
    }

    if (sql_append(&builder, "SELECT context, COUNT(*) as count FROM code_index WHERE (") != 0) {
        free_sql_builder(&builder);
        return;
    }
    if (build_query_filters(&builder, patterns, include, exclude, filters, file_filter, within_ranges, line_range, debug) != 0) {
        free_sql_builder(&builder);
        return;
    }
    if (sql_append(&builder, " GROUP BY context ORDER BY count DESC") != 0) {
        free_sql_builder(&builder);
        return;
    }

    if (debug) {
        printf("SQL: [Get context summary] %s\n", builder.sql);
    }

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->db, builder.sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        free_sql_builder(&builder);
        return;  /* Silently fail if query fails */
    }

    /* Bind pattern parameters (only for OR mode) */
    if (line_range < 0) {
        for (int i = 0; i < patterns->count; i++) {
            sqlite3_bind_text(stmt, i + 1, patterns->patterns[i], -1, SQLITE_STATIC);
        }
    }

    printf("Results CTX Totals: ");
    int first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *context_full = (const char *)sqlite3_column_text(stmt, 0);
        int count = sqlite3_column_int(stmt, 1);

        /* Convert to compact form */
        char upper[CONTEXT_TYPE_MAX_LENGTH];
        snprintf(upper, sizeof(upper), "%s", context_full);
        to_upper(upper);
        ContextType type = string_to_context(upper);
        const char *context_compact = context_to_string(type, 1);  /* 1 = compact */

        if (!first) printf(", ");
        printf("%s (%d)", context_compact, count);
        first = 0;
    }
    printf("\nTip: Use -i CTX to narrow results\n");

    sqlite3_finalize(stmt);
    free_sql_builder(&builder);
}

/* WEB_SAFE: counts distinct indexed files matching the full query. */
static int get_total_file_count(CodeIndexDatabase *db, PatternList *patterns,
                                ContextTypeList *include, ContextTypeList *exclude, QueryFilters *filters, FileFilterList *file_filter, WithinRangeList *within_ranges, int line_range, int debug) {
    /* Build SQL query with COUNT(DISTINCT ...) */
    SqlQueryBuilder builder;
    if (init_sql_builder(&builder) != 0) {
        return 0;
    }

    if (sql_append(&builder, "SELECT COUNT(*) FROM (SELECT DISTINCT directory, filename FROM code_index WHERE (") != 0) {
        free_sql_builder(&builder);
        return 0;
    }
    if (build_query_filters(&builder, patterns, include, exclude, filters, file_filter, within_ranges, line_range, debug) != 0) {
        free_sql_builder(&builder);
        return 0;
    }

    if (sql_append(&builder, ")") != 0) {
        free_sql_builder(&builder);
        return 0;
    }

    if (debug) {
        printf("SQL: [Get total file count] %s\n", builder.sql);
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

/* HOST_ONLY: executes a query but renders CLI-oriented file-only output directly to stdout. */
static void print_files_only(CodeIndexDatabase *db, PatternList *patterns,
                             ContextTypeList *include, ContextTypeList *exclude, QueryFilters *filters, FileFilterList *file_filter,
                             WithinRangeList *within_ranges, int limit, int line_range, int debug) {
    /* Build SQL query for unique files */
    SqlQueryBuilder builder;
    if (init_sql_builder(&builder) != 0) {
        return;
    }

    if (sql_append(&builder, "SELECT DISTINCT directory, filename FROM code_index WHERE (") != 0) {
        free_sql_builder(&builder);
        return;
    }
    if (build_query_filters(&builder, patterns, include, exclude, filters, file_filter, within_ranges, line_range, debug) != 0) {
        free_sql_builder(&builder);
        return;
    }
    if (sql_append(&builder, " ORDER BY directory, filename") != 0) {
        free_sql_builder(&builder);
        return;
    }

    if (debug) {
        printf("SQL: [Files query] %s\n", builder.sql);
    }

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->db, builder.sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Query failed: %s\n", sqlite3_errmsg(db->db));
        free_sql_builder(&builder);
        return;
    }

    /* Bind pattern parameters (only for OR mode) */
    if (line_range < 0) {
        for (int i = 0; i < patterns->count; i++) {
            sqlite3_bind_text(stmt, i + 1, patterns->patterns[i], -1, SQLITE_STATIC);
        }
    }

    int total_files = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *directory = (const char *)sqlite3_column_text(stmt, 0);
        const char *filename = (const char *)sqlite3_column_text(stmt, 1);
        printf("%s%s\n", directory, filename);
        total_files++;

        /* Apply limit if specified */
        if (limit > 0 && total_files >= limit) {
            break;
        }
    }

    sqlite3_finalize(stmt);
    free_sql_builder(&builder);

    /* Get actual total if limit was hit */
    int actual_total = total_files;
    if (limit > 0 && total_files >= limit) {
        actual_total = get_total_file_count(db, patterns, include, exclude, filters, file_filter, within_ranges, line_range, debug);
    }

    char file_word[16];
    if (actual_total == 1) {
        snprintf(file_word, sizeof(file_word), "file");
    } else {
        pluralize_common_word("file", file_word, sizeof(file_word));
    }
    printf("\nFound %d %s", actual_total, file_word);
    if (limit > 0 && total_files >= limit) {
        printf(" (showing first %d)", limit);
    }
    printf("\n");
}

/* Execute two-step proximity search and populate temp table with results */
/* WEB_SAFE: performs indexed proximity matching entirely inside SQLite. */
static int execute_proximity_to_temp_table(CodeIndexDatabase *db, PatternList *patterns,
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
        "SELECT directory, filename, line FROM code_index WHERE symbol LIKE '%s' ESCAPE '\\'",
        escaped_anchor);
    sqlite3_free(escaped_anchor);
    if (ret != 0) {
        free_sql_builder(&anchor_builder);
        return -1;
    }

    /* Add common filters to anchor query */
    if (build_common_filters(&anchor_builder, include, exclude, filters, file_filter, within_ranges, debug) != 0) {
        free_sql_builder(&anchor_builder);
        return -1;
    }
    if (sql_append(&anchor_builder, " ORDER BY directory, filename, line") != 0) {
        free_sql_builder(&anchor_builder);
        return -1;
    }

    if (debug) {
        printf("SQL: [Anchor query] %s\n", anchor_builder.sql);
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
        ret = sql_append(&range_builder, "symbol LIKE '%s' ESCAPE '\\'", escaped_pattern);
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
    if (build_common_filters(&range_builder, include, exclude, filters, file_filter, within_ranges, debug) != 0) {
        free_sql_builder(&range_builder);
        sqlite3_finalize(anchor_stmt);
        return -1;
    }

    if (debug) {
        printf("SQL: [Range query template] %s\n", range_builder.sql);
    }

    /* Pre-compile per-pattern EXISTS check statements (fixes #4 wildcard bug, #5 perf) */
    int num_secondaries = patterns->count - 1;
    sqlite3_stmt **check_stmts = calloc((size_t)num_secondaries, sizeof(sqlite3_stmt *));
    if (!check_stmts) {
        sqlite3_finalize(anchor_stmt);
        free_sql_builder(&range_builder);
        return -1;
    }

    int check_build_failed = 0;
    for (int i = 0; i < num_secondaries; i++) {
        SqlQueryBuilder check_builder;
        if (init_sql_builder(&check_builder) != 0) {
            check_build_failed = 1;
            break;
        }

        char *escaped_pattern = sqlite3_mprintf("%q", patterns->patterns[i + 1]);
        ret = sql_append(&check_builder,
            "SELECT 1 FROM code_index WHERE filename = ? AND directory = ? "
            "AND line BETWEEN ? AND ? AND symbol LIKE '%s' ESCAPE '\\' LIMIT 1",
            escaped_pattern);
        sqlite3_free(escaped_pattern);
        if (ret != 0) {
            free_sql_builder(&check_builder);
            check_build_failed = 1;
            break;
        }

        if (build_common_filters(&check_builder, include, exclude, filters, file_filter, within_ranges, debug) != 0) {
            free_sql_builder(&check_builder);
            check_build_failed = 1;
            break;
        }

        if (sqlite3_prepare_v2(db->db, check_builder.sql, -1, &check_stmts[i], NULL) != SQLITE_OK) {
            free_sql_builder(&check_builder);
            check_build_failed = 1;
            break;
        }
        free_sql_builder(&check_builder);
    }

    if (check_build_failed) {
        for (int i = 0; i < num_secondaries; i++) {
            if (check_stmts[i]) sqlite3_finalize(check_stmts[i]);
        }
        free(check_stmts);
        sqlite3_finalize(anchor_stmt);
        free_sql_builder(&range_builder);
        return -1;
    }

    /* Pre-compile anchor insert statement */
    SqlQueryBuilder insert_builder;
    if (init_sql_builder(&insert_builder) != 0) {
        for (int i = 0; i < num_secondaries; i++) sqlite3_finalize(check_stmts[i]);
        free(check_stmts);
        sqlite3_finalize(anchor_stmt);
        free_sql_builder(&range_builder);
        return -1;
    }

    char *escaped_anchor_insert = sqlite3_mprintf("%q", patterns->patterns[0]);
    ret = sql_append(&insert_builder,
        "INSERT INTO proximity_results "
        "SELECT * FROM code_index WHERE filename = ? AND directory = ? "
        "AND line = ? AND symbol LIKE '%s' ESCAPE '\\'", escaped_anchor_insert);
    sqlite3_free(escaped_anchor_insert);
    if (ret != 0) {
        free_sql_builder(&insert_builder);
        for (int i = 0; i < num_secondaries; i++) sqlite3_finalize(check_stmts[i]);
        free(check_stmts);
        sqlite3_finalize(anchor_stmt);
        free_sql_builder(&range_builder);
        return -1;
    }

    sqlite3_stmt *insert_stmt;
    if (sqlite3_prepare_v2(db->db, insert_builder.sql, -1, &insert_stmt, NULL) != SQLITE_OK) {
        free_sql_builder(&insert_builder);
        for (int i = 0; i < num_secondaries; i++) sqlite3_finalize(check_stmts[i]);
        free(check_stmts);
        sqlite3_finalize(anchor_stmt);
        free_sql_builder(&range_builder);
        return -1;
    }
    free_sql_builder(&insert_builder);

    /* Pre-compile range insert statement for secondaries */
    sqlite3_stmt *range_stmt;
    if (sqlite3_prepare_v2(db->db, range_builder.sql, -1, &range_stmt, NULL) != SQLITE_OK) {
        sqlite3_finalize(insert_stmt);
        for (int i = 0; i < num_secondaries; i++) sqlite3_finalize(check_stmts[i]);
        free(check_stmts);
        sqlite3_finalize(anchor_stmt);
        free_sql_builder(&range_builder);
        return -1;
    }
    free_sql_builder(&range_builder);

    int anchor_count = 0;
    int complete_matches = 0;

    while (sqlite3_step(anchor_stmt) == SQLITE_ROW) {
        const char *directory = (const char *)sqlite3_column_text(anchor_stmt, 0);
        const char *filename = (const char *)sqlite3_column_text(anchor_stmt, 1);
        int anchor_line = sqlite3_column_int(anchor_stmt, 2);
        anchor_count++;

        int min_line = anchor_line - line_range;
        if (min_line < 1) min_line = 1;
        int max_line = anchor_line + line_range;

        /* Check that EACH secondary pattern exists in range */
        int all_found = 1;
        for (int i = 0; i < num_secondaries; i++) {
            sqlite3_reset(check_stmts[i]);
            sqlite3_bind_text(check_stmts[i], 1, filename, -1, SQLITE_STATIC);
            sqlite3_bind_text(check_stmts[i], 2, directory, -1, SQLITE_STATIC);
            sqlite3_bind_int(check_stmts[i], 3, min_line);
            sqlite3_bind_int(check_stmts[i], 4, max_line);

            if (sqlite3_step(check_stmts[i]) != SQLITE_ROW) {
                all_found = 0;
                break;
            }
        }

        if (all_found) {
            complete_matches++;

            /* Insert anchor symbol */
            sqlite3_reset(insert_stmt);
            sqlite3_bind_text(insert_stmt, 1, filename, -1, SQLITE_STATIC);
            sqlite3_bind_text(insert_stmt, 2, directory, -1, SQLITE_STATIC);
            sqlite3_bind_int(insert_stmt, 3, anchor_line);
            sqlite3_step(insert_stmt);

            /* Insert matching secondaries within range */
            sqlite3_reset(range_stmt);
            sqlite3_bind_text(range_stmt, 1, filename, -1, SQLITE_STATIC);
            sqlite3_bind_text(range_stmt, 2, directory, -1, SQLITE_STATIC);
            sqlite3_bind_int(range_stmt, 3, min_line);
            sqlite3_bind_int(range_stmt, 4, max_line);
            sqlite3_step(range_stmt);
        }
    }

    sqlite3_finalize(anchor_stmt);
    sqlite3_finalize(insert_stmt);
    sqlite3_finalize(range_stmt);
    for (int i = 0; i < num_secondaries; i++) sqlite3_finalize(check_stmts[i]);
    free(check_stmts);

    if (debug) {
        printf("Proximity search: %d anchors found, %d complete matches\n",
               anchor_count, complete_matches);
    }

    return 0;
}

/* Helper: Print file header if file changed and reset per-file counter */
static void print_file_header_if_changed(const char *filepath, char *current_file,
                                         size_t current_file_size, int *current_file_count) {
    if (strcmp(filepath, current_file) != 0) {
        if (current_file[0]) {
            printf("\n");  /* Blank line between files */
        }
        printf("%s:\n", filepath);
        snprintf(current_file, current_file_size, "%s", filepath);
        *current_file_count = 0;
    }
}

/* Helper: Print row output based on display mode */
static void print_row_output(RowData *row, int show_all_columns, int compact) {
    if (show_all_columns) {
        print_all_columns_row(row);
    } else {
        print_table_row(row, compact);
    }
}

/* Helper: Print expansion or context lines if requested */
/* HOST_BRIDGED: computes source spans locally but depends on host-provided source content retrieval. */
static void print_expansion_or_context(const char *filepath, int line,
                                      const char *source_location, int is_definition,
                                      int expand, int context_before, int context_after,
                                      PatternList *patterns, int raw) {
    /* Print expanded definition or context lines if requested
     * Note: is_definition is an INT_COLUMN (int), not COLUMN (const char *),
     * so compare directly to 1, not strcmp(is_definition, "1") */
    if (expand && is_definition == 1) {
        /* Expand the definition. Two kinds of row share is_definition=1:
         *   - Body-bearing definitions (functions, structs, enums, impls, ...)
         *     carry a full source span and expand to their whole body.
         *   - Body-less definition sites (let/$x bindings, struct fields, enum
         *     variants, parameters, imports) are legitimate definitions too --
         *     `$x = 1;` is where $x is defined -- but the indexer records no
         *     span for them yet, so fall back to showing the single defining
         *     line at this row's location.
         * Either way -e shows the actual source text. A missing span is an
         * expected, permanent property of these rows, never evidence of a stale
         * index, so we do not warn or guess about freshness. */
        int start_line, start_column, end_line, end_column;
        int have_span = source_location && source_location[0] != '\0' &&
            parse_source_location(source_location, &start_line, &start_column,
                                 &end_line, &end_column) == 0;
        if (!have_span) {
            start_line = end_line = line;  /* fall back to the defining line */
        }
        /* Always whole-line mode (-1 columns): print the complete lines the
         * definition spans, preserving leading indentation. Column-precise
         * trimming would slice the indentation off the first line, leaving it
         * misaligned against the rest of the body. */
        if (context_before > 0 || context_after > 0) {
            int ctx_start = start_line - context_before;
            if (ctx_start < 1) ctx_start = 1;
            print_lines_range(filepath, ctx_start, end_line + context_after, -1, -1, raw);
        } else {
            print_lines_range(filepath, start_line, end_line, -1, -1, raw);
        }
        if (!raw) printf("--\n");  /* Closing separator after definition */
    } else if (context_before > 0 || context_after > 0) {
        /* Fall back to context lines for non-definitions or when expand not set */
        print_context_lines(filepath, line, patterns->patterns, patterns->count,
                          context_before, context_after, raw);
        if (!raw) printf("--\n");  /* Closing separator after context */
    }
}

/* Helper: Build SQL query (proximity vs normal)
 * Returns: 0 on success, -1 on error
 */
/* WEB_SAFE: builds the final SQL query for indexed result retrieval. */
static int build_query_sql(SqlQueryBuilder *builder, PatternList *patterns,
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
        if (build_query_filters(builder, patterns, include, exclude, filters, file_filter, within_ranges, line_range, debug) != 0) return -1;
        if (sql_append(builder, " ORDER BY directory, filename, line") != 0) return -1;
    }
    return 0;
}

/* Helper: Print summary statistics at end of query results */
static void print_summary_stats(CodeIndexDatabase *db, PatternList *patterns,
                                ContextTypeList *include, ContextTypeList *exclude,
                                QueryFilters *filters, FileFilterList *file_filter,
                                WithinRangeList *within_ranges, int line_range, int total_count, int limit, int quiet, int debug) {
    /* Get actual total if limit was hit */
    int actual_total = total_count;
    if (limit > 0 && total_count >= limit) {
        actual_total = get_total_count(db, patterns, include, exclude, filters, file_filter, within_ranges, line_range, debug);
    }

    /* Quiet mode: emit nothing except a terse notice when a limit clipped
     * results, so the agent still knows the output was truncated. */
    if (quiet) {
        if (limit > 0 && total_count >= limit) {
            printf("... %d matches, showing %d\n", actual_total, limit);
        }
        return;
    }

    char match_word[16];
    if (actual_total == 1) {
        snprintf(match_word, sizeof(match_word), "match");
    } else {
        pluralize_common_word("match", match_word, sizeof(match_word));
    }
    printf("\nFound %d %s", actual_total, match_word);
    if (limit > 0 && total_count >= limit) {
        printf(" (showing first %d)", limit);
    }
    printf("\n");

    /* Show context type breakdown when limit is hit */
    if (limit > 0 && total_count >= limit) {
        get_context_summary(db, patterns, include, exclude, filters, file_filter, within_ranges, line_range, debug);
    }
}

/* Print the actual file paths where matches live (ignoring the -f filter).
 * Used in the -f exclusion diagnostic to tell the user where to look. */
static void print_file_filter_hint(CodeIndexDatabase *db, PatternList *patterns,
                                   ContextTypeList *include, ContextTypeList *exclude,
                                   QueryFilters *filters,
                                   WithinRangeList *within_ranges, int line_range, int debug) {
    FileFilterList no_files = {0};

    int total_files = get_total_file_count(db, patterns, include, exclude, filters, &no_files, within_ranges, line_range, debug);
    if (total_files <= 0) return;

    SqlQueryBuilder builder;
    if (init_sql_builder(&builder) != 0) return;

    if (sql_append(&builder, "SELECT DISTINCT directory, filename FROM code_index WHERE (") != 0 ||
        build_query_filters(&builder, patterns, include, exclude, filters, &no_files, within_ranges, line_range, debug) != 0 ||
        sql_append(&builder, " ORDER BY directory, filename LIMIT 3") != 0) {
        free_sql_builder(&builder);
        return;
    }

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db->db, builder.sql, -1, &stmt, NULL) != SQLITE_OK) {
        free_sql_builder(&builder);
        return;
    }
    free_sql_builder(&builder);

    if (line_range < 0) {
        for (int i = 0; i < patterns->count; i++) {
            sqlite3_bind_text(stmt, i + 1, patterns->patterns[i], -1, SQLITE_STATIC);
        }
    }

    int shown = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *dir  = (const char *)sqlite3_column_text(stmt, 0);
        const char *file = (const char *)sqlite3_column_text(stmt, 1);
        if (shown == 0) { printf("  Match found at: "); }
        else            { printf("                  "); }
        printf("%s%s\n", dir ? dir : "", file ? file : "");
        shown++;
    }
    sqlite3_finalize(stmt);

    if (total_files > shown) {
        printf("                  ... plus %d other %s\n",
               total_files - shown, total_files - shown == 1 ? "file" : "files");
    }
}

/* When every match was excluded by filters, find WHICH filter is responsible
 * so the user drops the right one instead of all of them. Re-counts with each
 * narrowing filter (-i include, -f file) cleared in turn, keeping the others.
 * -x (exclude) is special: clearing it would re-add what the user deliberately
 * removed, so when only clearing -x recovers matches we suggest a positive
 * include (-i com str, which overrides -x noise) rather than dropping -x.
 * --within redefines what a match is, so when it is active we fall back to the
 * generic message. */
static void diagnose_filter_exclusion(CodeIndexDatabase *db, PatternList *patterns,
                                      ContextTypeList *include, ContextTypeList *exclude,
                                      QueryFilters *filters, FileFilterList *file_filter,
                                      WithinRangeList *within_ranges, int line_range, int debug) {
    ContextTypeList no_ctx = {0};
    FileFilterList no_files = {0};
    int has_within = (within_ranges && within_ranges->count > 0);

    /* Count with exactly one filter cleared; the others (including -x) stay,
     * so each count isolates that one filter's effect. */
    int n_no_include = (include->count > 0 && !has_within)
        ? get_total_count(db, patterns, &no_ctx, exclude, filters, file_filter, within_ranges, line_range, debug) : 0;
    int n_no_file = (file_filter->count > 0 && !has_within)
        ? get_total_count(db, patterns, include, exclude, filters, &no_files, within_ranges, line_range, debug) : 0;
    int n_no_exclude = (exclude->count > 0 && !has_within)
        ? get_total_count(db, patterns, include, &no_ctx, filters, file_filter, within_ranges, line_range, debug) : 0;

    /* Build per-flag culprit list: each entry is a filter that, cleared alone,
     * recovers results. Sorted by count descending so the most impactful comes first. */
    typedef struct { char flag[6]; int count; } CulpritEntry;
    CulpritEntry culprits[12];
    int n_culprits = 0;

    if (n_no_include > 0)
        culprits[n_culprits++] = (CulpritEntry){"-i", n_no_include};
    if (n_no_file > 0)
        culprits[n_culprits++] = (CulpritEntry){"-f", n_no_file};

#define COLUMN(name, sql_type, c_type, width, full, compact, long_flag, short_flag, ...) \
    if (filters->name.count > 0 && !has_within) { \
        QueryFilters no_col = *filters; \
        no_col.name.count = 0; \
        int n = get_total_count(db, patterns, include, exclude, &no_col, \
                                file_filter, within_ranges, line_range, debug); \
        if (n > 0) \
            culprits[n_culprits++] = (CulpritEntry){"-" #short_flag, n}; \
    }
#define INT_COLUMN COLUMN
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN

    for (int i = 0; i < n_culprits - 1; i++)
        for (int j = i + 1; j < n_culprits; j++)
            if (culprits[j].count > culprits[i].count) {
                CulpritEntry tmp = culprits[i]; culprits[i] = culprits[j]; culprits[j] = tmp;
            }

    const char *pat = patterns->patterns[0];
    int single = (patterns->count == 1);

    if (n_culprits == 1) {
        /* Single culprit: keep the existing terse style. */
        if (single)
            printf("'%s': %d %s excluded by %s. Re-run without %s to see them.\n",
                   pat, culprits[0].count, culprits[0].count == 1 ? "match" : "matches",
                   culprits[0].flag, culprits[0].flag);
        else
            printf("Matches exist but were excluded by %s. Re-run without %s to see them.\n",
                   culprits[0].flag, culprits[0].flag);
        if (n_no_file > 0)
            print_file_filter_hint(db, patterns, include, exclude, filters, within_ranges, line_range, debug);
    } else if (n_culprits > 1) {
        /* Multiple culprits: total count, then each flag's independent recovery count. */
        if (single) {
            int total = count_pattern_matches(db, pat);
            printf("'%s': %d %s excluded by filters",
                   pat, total, total == 1 ? "match" : "matches");
        } else {
            printf("Matches excluded by multiple filters");
        }
        for (int i = 0; i < n_culprits; i++)
            printf("; remove %s to show %d %s",
                   culprits[i].flag, culprits[i].count,
                   culprits[i].count == 1 ? "match" : "matches");
        printf(".\n");
        if (n_no_file > 0)
            print_file_filter_hint(db, patterns, include, exclude, filters, within_ranges, line_range, debug);
    } else if (n_no_exclude > 0) {
        /* Only clearing -x recovers matches. */
        if (single) {
            printf("'%s': %d %s excluded by -x. Remove -x to see them.\n",
                   pat, n_no_exclude, n_no_exclude == 1 ? "match" : "matches");
        } else {
            printf("%d result%s excluded by -x. Remove -x to see them.\n",
                   n_no_exclude, n_no_exclude == 1 ? "" : "s");
        }
    } else {
        /* No single filter explains it -- a combination did. Suggest unfiltered. */
        if (single) {
            printf("'%s' has %d matches, all excluded by filters.\n",
                   pat, count_pattern_matches(db, pat));
        } else {
            printf("All patterns exist, but all matches were excluded by filters.\n");
        }
        printf("Re-run without filters to see them:\n  qi");
        for (int i = 0; i < patterns->count; i++) {
            printf(" %s", patterns->patterns[i]);
        }
        printf("\n");
    }
}

/* If `pattern` looks like a qualified name -- identifier chars with a literal
 * dot that has a word char on both sides, and no % wildcard -- split it at the
 * LAST dot into `qualifier` (the identifier immediately before the dot) and
 * `symbol` (everything after). `qi Some.function` -> qualifier "Some", symbol
 * "function"; `qi a.b.c` -> qualifier "b", symbol "c". Returns 1 on match.
 * `.` is a single-char LIKE wildcard, so a dotted pattern is almost always a
 * qualified name a user typed grep-style rather than an intentional wildcard.
 * Both sides must be >= MIN_SYMBOL_LENGTH: the indexer does not store 1-char
 * symbols, so a shorter side could never match and the retry would be futile. */
static int split_qualified_name(const char *pattern, char *qualifier, size_t qsz,
                                char *symbol, size_t ssz) {
    /* An explicit wildcard (* or %) means the user meant a wildcard, not a
     * qualified name -- don't second-guess them. Runs on the ORIGINAL token
     * (before convert_wildcards turns '.'->'_' and '*'->'%'). */
    if (!pattern || strchr(pattern, '%') || strchr(pattern, '*')) return 0;
    const char *dot = strrchr(pattern, '.');
    if (!dot || dot == pattern || dot[1] == '\0') return 0;  /* no dot, or leading/trailing */
    /* Require a word char on both sides of the dot (a qualified-name separator). */
    if (!isalnum((unsigned char)dot[-1]) && dot[-1] != '_') return 0;
    if (!isalnum((unsigned char)dot[1]) && dot[1] != '_') return 0;
    /* qualifier = the identifier segment immediately before the dot */
    const char *qstart = dot;
    while (qstart > pattern && (isalnum((unsigned char)qstart[-1]) || qstart[-1] == '_')) qstart--;
    size_t qlen = (size_t)(dot - qstart);
    size_t slen = strlen(dot + 1);
    if (qlen < MIN_SYMBOL_LENGTH || slen < MIN_SYMBOL_LENGTH) return 0;  /* 1-char sides never indexed */
    if (qlen >= qsz || slen >= ssz) return 0;
    memcpy(qualifier, qstart, qlen);
    qualifier[qlen] = '\0';
    memcpy(symbol, dot + 1, slen);
    symbol[slen] = '\0';
    return 1;
}

/* Count rows matching `symbol` restricted to a qualifier on one column:
 * parent_symbol when use_namespace==0, namespace when 1. The ambient filters
 * (include/exclude/file/within) are preserved; a shallow copy of *filters gets
 * the single qualifier value appended to the (guaranteed-empty) target column. */
static int count_qualified(CodeIndexDatabase *db, const char *symbol, const char *qualifier,
                           int use_namespace,
                           ContextTypeList *include, ContextTypeList *exclude,
                           QueryFilters *filters, FileFilterList *file_filter,
                           WithinRangeList *within_ranges, int line_range, int debug) {
    /* The casts below borrow const strings into non-const struct fields that
     * are only read, never written; going via uintptr_t keeps -Wcast-qual quiet. */
    PatternList pl = { .count = 1 };
    pl.patterns[0] = (char *)(uintptr_t)symbol;
    QueryFilters f = *filters;  /* shallow copy; we only append to an empty column */
    if (use_namespace)
        f.namespace.values[f.namespace.count++] = (char *)(uintptr_t)qualifier;
    else
        f.parent_symbol.values[f.parent_symbol.count++] = (char *)(uintptr_t)qualifier;
    return get_total_count(db, &pl, include, exclude, &f, file_filter, within_ranges, line_range, debug);
}

/* HOST_ONLY: mixes indexed querying with terminal rendering and host-backed source expansion. */
static void print_results_by_file(CodeIndexDatabase *db, PatternList *patterns,
                                  ContextTypeList *include, ContextTypeList *exclude, QueryFilters *filters, FileFilterList *file_filter,
                                  WithinRangeList *within_ranges, int limit, int limit_per_file, int compact, int line_range, int expand, int context_before, int context_after, int debug, int show_all_columns, int raw, int quiet, const FileExtensions *known_exts, const QualifiedName *qn) {

    /* Two-step proximity search for line_range > 0 */
    if (line_range > 0 && patterns->count > 1) {
        /* Execute proximity search into temp table */
        if (execute_proximity_to_temp_table(db, patterns, include, exclude,
                                            filters, file_filter, within_ranges, line_range, debug) != 0) {
            return;
        }
    }

retry_query:
    ;  /* Empty statement required in C11 before declarations after labels */
    /* Build SQL query */
    SqlQueryBuilder builder;
    if (init_sql_builder(&builder) != 0) {
        return;
    }

    if (build_query_sql(&builder, patterns, include, exclude, filters, file_filter, within_ranges, line_range, debug) != 0) {
        free_sql_builder(&builder);
        return;
    }

    if (debug) {
        printf("SQL: [Main query] %s\n", builder.sql);
    }

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->db, builder.sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Query failed: %s\n", sqlite3_errmsg(db->db));
        free_sql_builder(&builder);
        return;
    }
    free_sql_builder(&builder);

    /* Bind pattern parameters (only for OR mode - AND mode embeds patterns in SQL) */
    if (line_range < 0) {
        for (int i = 0; i < patterns->count; i++) {
            sqlite3_bind_text(stmt, i + 1, patterns->patterns[i], -1, SQLITE_STATIC);
        }
    }

    /* Size columns before streaming rows: intentional second pass, not a
     * redundant query. See calculate_column_widths_from_query for rationale. */
    calculate_column_widths_from_query(db, patterns, include, exclude, filters, file_filter, within_ranges, limit, line_range, compact, debug);

    char current_file[PATH_MAX_LENGTH] = "";
    int total_count = 0;
    int current_file_count = 0;  /* Track results in current file for --limit-per-file */
    int n_skipped_expand = 0;    /* Non-definition rows skipped by -e in raw mode */

    /* Check if there are any results before printing header */
    int first_result = sqlite3_step(stmt);
    if (first_result != SQLITE_ROW) {
        /* No results: run diagnostics first and print the blunt "No results"
         * verdict LAST (below), so the actionable line -- e.g. "'x' has N
         * matches but all were excluded by filters" -- leads instead. (A
         * successful wildcard retry gotos past the trailing verdict, so it no
         * longer prints a misleading "No results" before its matches.) */
        sqlite3_finalize(stmt);

        /* Qualified-name recovery: `qi Some.function` -> retry as
         * `qi function -p Some` (then `-ns Some`). A dotted pattern is almost
         * always a qualified name typed grep-style; `.` is only a single-char
         * wildcard, so the literal search rarely matches what the user meant.
         * We auto-retry with whichever dimension recovers (parent preferred),
         * mirroring the `%word%` auto-retry, so new users get results without
         * having to know -p/-ns. Runs BEFORE the wildcard retry so a real
         * qualified match wins over coincidental `SomeXfunction` noise, and
         * before symbol_filter is allocated so the goto leaks nothing.
         * Skipped when -p/-ns are already set (the user knows the shape). */
        if (qn && qn->active && patterns->count == 1 &&
            filters->parent_symbol.count == 0 && filters->namespace.count == 0) {
            int use_ns = 0;
            int n = count_qualified(db, qn->symbol, qn->qualifier, 0, include, exclude,
                                    filters, file_filter, within_ranges, line_range, debug);
            if (n == 0) {
                int n_ns = count_qualified(db, qn->symbol, qn->qualifier, 1, include, exclude,
                                           filters, file_filter, within_ranges, line_range, debug);
                if (n_ns > 0) { use_ns = 1; n = n_ns; }
            }
            if (n > 0) {
                const char *flag = use_ns ? "-ns" : "-p";
                printf("Retrying as qualified name: qi %s %s %s\n\n", qn->symbol, flag, qn->qualifier);
                /* Rewrite the query in place: pattern -> the after-dot symbol,
                 * qualifier -> a parent/namespace filter, then re-run. */
                free(patterns->patterns[0]);
                patterns->patterns[0] = safe_strdup_ctx(qn->symbol, "Failed to allocate memory for qualified symbol");
                if (use_ns)
                    filters->namespace.values[filters->namespace.count++] =
                        safe_strdup_ctx(qn->qualifier, "Failed to allocate memory for namespace filter");
                else
                    filters->parent_symbol.values[filters->parent_symbol.count++] =
                        safe_strdup_ctx(qn->qualifier, "Failed to allocate memory for parent filter");
                goto retry_query;
            }
        }

        /* Initialize filter to check if patterns are valid symbols */
        SymbolFilter symbol_filter;
        int filter_initialized = 0;

        /* Try to initialize with available language directories */
        const char *lang_dirs[] = {"c/" CONFIG_DIR, "go/" CONFIG_DIR, "php/" CONFIG_DIR, "typescript/" CONFIG_DIR, NULL};
        for (int j = 0; lang_dirs[j] != NULL && !filter_initialized; j++) {
            if (filter_init(&symbol_filter, lang_dirs[j]) == 0) {
                filter_initialized = 1;
                break;
            }
        }

        /* Check each pattern individually to see which ones matched */
        int all_patterns_matched = 1;
        int show_verdict = 1;  /* cleared when a branch already reports the count */
        for (int i = 0; i < patterns->count; i++) {
            int count = count_pattern_matches(db, patterns->patterns[i]);
            if (count == 0) {
                all_patterns_matched = 0;
                /* Suggest wildcard if pattern doesn't already have % wildcard AND it's a valid symbol
                 * Note: We don't check for _ since it's a common character in identifiers */
                if (strchr(patterns->patterns[i], '%') == NULL) {
                    if (filter_initialized) {
                        FilterReason reason = get_filter_reason(&symbol_filter, patterns->patterns[i]);
                        switch (reason) {
                            case FILTER_REASON_VALID:
                                /* Auto-retry with wildcards for valid patterns */
                                {
                                    char wildcard_pattern[SYMBOL_MAX_LENGTH + 3];  /* +2 for %%, +1 for \0 */
                                    snprintf(wildcard_pattern, sizeof(wildcard_pattern), "%%%s%%", patterns->patterns[i]);
                                    int wildcard_count = count_pattern_matches(db, wildcard_pattern);
                                     if (wildcard_count > 0) {
                                        printf("Retrying with partial matches for '*%s*':\n\n", patterns->patterns[i]);
                                        /* Replace pattern with wildcard version and retry the query */
                                        free(patterns->patterns[i]);
                                        patterns->patterns[i] = safe_strdup_ctx(wildcard_pattern, "Failed to allocate memory for wildcard pattern");
                                        goto retry_query;
                                    } else {
                                        printf("No partial matches found for '*%s*'.", patterns->patterns[i]);
                                    }
                                }
                                break;
                            case FILTER_REASON_TOO_SHORT:
                                printf("'%s' is too short. Symbols less than %d characters are not indexed.",
                                       patterns->patterns[i], MIN_SYMBOL_LENGTH);
                                break;
                            case FILTER_REASON_PURE_NUMBER:
                                printf("'%s' is a pure number and is not indexed.", patterns->patterns[i]);
                                break;
                            case FILTER_REASON_STOPWORD:
                                printf("'%s' is a stopword and is not indexed.", patterns->patterns[i]);
                                break;
                            case FILTER_REASON_KEYWORD:
                                printf("'%s' is a language keyword and is not indexed.", patterns->patterns[i]);
                                break;
                            case FILTER_REASON_EXCLUSION_PATTERN:
                                printf("'%s' matches an exclusion pattern and is not indexed.", patterns->patterns[i]);
                                break;
                            default:
                                /* All enum values covered above */
                                break;
                        }
                    }
                }
                printf("\n");
            } else if (patterns->count > 1) {
                /* Show match count for multi-pattern searches */
                printf("Pattern '%s' matched %d occurrences.\n", patterns->patterns[i], count);
            }
        }

        /* If all patterns matched individually, filters must have excluded everything */
        if (all_patterns_matched && patterns->count > 0) {
            if (line_range >= 0) {
                printf("No lines contain ALL patterns together.\n");
            } else if (has_any_filters(include, exclude, filters, file_filter)) {
                /* Name the responsible filter so the user drops the right one,
                 * instead of the blunt "No results" that sends them to grep. */
                diagnose_filter_exclusion(db, patterns, include, exclude, filters,
                                          file_filter, within_ranges, line_range, debug);
                show_verdict = 0;  /* the diagnostic already reports the count */
            }
        }

        /* The blunt verdict, last -- after the diagnostics that explain it.
         * Skipped when the filtered-miss branch already reported the count. */
        if (show_verdict) {
            printf("0 matches\n");
        }
        warn_unknown_extensions(file_filter, known_exts);
        return;
    }

    /* Print table header only if we have results */
    if (!raw) {
        if (show_all_columns) {
            print_all_columns_header(quiet);
        } else {
            print_table_header(compact, quiet);
        }
    }

    /* Process first row */
    do {
        /* Extract core columns from database (SELECT * order) */
        const char *symbol = (const char *)sqlite3_column_text(stmt, 0);
        const char *directory = (const char *)sqlite3_column_text(stmt, 1);
        const char *filename = (const char *)sqlite3_column_text(stmt, 2);
        int line = sqlite3_column_int(stmt, 3);
        const char *context = (const char *)sqlite3_column_text(stmt, 4);
        const char *full_symbol = (const char *)sqlite3_column_text(stmt, 5);
        const char *source_location = (const char *)sqlite3_column_text(stmt, 6);

        /* X-Macro: Extract extensible columns */
        int col_idx = 7;
#define COLUMN(name, ...) \
        const char *name = (const char *)sqlite3_column_text(stmt, col_idx++);
#define INT_COLUMN(name, ...) \
        int name = sqlite3_column_int(stmt, col_idx++);
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN

        char filepath[PATH_MAX_LENGTH];
        snprintf(filepath, sizeof(filepath), "%s%s", directory ? directory : "", filename ? filename : "");

        /* Print file header if file changed */
        if (!raw) print_file_header_if_changed(filepath, current_file, sizeof(current_file), &current_file_count);
        else if (strcmp(filepath, current_file) != 0) { snprintf(current_file, sizeof(current_file), "%s", filepath); current_file_count = 0; }

        /* Skip if we've hit the per-file limit for this file */
        if (limit_per_file > 0 && current_file_count >= limit_per_file) {
            continue;
        }

        /* Populate row data and print */
        RowData row;
        if (show_all_columns) {
            /* Populate ALL fields (internal + extensible) */
            row = (RowData){
                .directory = directory,
                .filename = filename,
                .source_location = source_location,
                .symbol = symbol,
                .line = line,
                .context = context,
                .full_symbol = full_symbol,
                /* X-Macro: Populate extensible column fields */
#define COLUMN(name, ...) .name = name,
#define INT_COLUMN(name, ...) .name = name,
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN
            };
        } else {
            /* Normal mode: populate only standard fields */
            row = (RowData){
                .line = line,
                .context = context,
                .full_symbol = full_symbol,
                /* X-Macro: Populate extensible column fields */
#define COLUMN(name, ...) .name = name,
#define INT_COLUMN(name, ...) .name = name,
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN
            };
        }
        if (!raw) print_row_output(&row, show_all_columns, compact);

        /* Print expansion or context if requested */
        print_expansion_or_context(filepath, line, source_location, is_definition,
                                  expand, context_before, context_after, patterns, raw);

        if (expand && is_definition != 1) n_skipped_expand++;

        total_count++;
        current_file_count++;  /* Increment per-file counter */

        if (limit > 0 && total_count >= limit) {
            break;
        }
    } while (sqlite3_step(stmt) == SQLITE_ROW);

    sqlite3_finalize(stmt);

    /* Warn when -e skipped non-expandable rows (usages, not definitions).
     * In raw mode these rows produce no output at all, which looks like a bug. */
    if (expand && n_skipped_expand > 0) {
        if (n_skipped_expand == total_count) {
            fprintf(stderr, "No expandable definitions found: all %d result%s %s not a definition. "
                    "Try -i func/class to narrow, or remove -e.\n",
                    n_skipped_expand, n_skipped_expand == 1 ? "" : "s",
                    n_skipped_expand == 1 ? "is" : "are");
        } else {
            fprintf(stderr, "%d result%s not expanded (not a definition). "
                    "Try -i func/class to narrow.\n",
                    n_skipped_expand, n_skipped_expand == 1 ? "" : "s");
        }
    }

    /* Print summary statistics */
    if (!raw) print_summary_stats(db, patterns, include, exclude, filters, file_filter, within_ranges,
                       line_range, total_count, limit, quiet, debug);

    /* Results exist but the pattern looks like a qualified name: the `.` was
     * treated as a single-char wildcard, so these matches are coincidental
     * rather than the qualified name the user likely meant. Teach the canonical
     * form (no probe -- keep the success path cheap). The guard is false after a
     * qualified-name auto-retry, since the pattern no longer holds a dot. */
    if (!raw && qn && qn->active && patterns->count == 1 &&
        filters->parent_symbol.count == 0 && filters->namespace.count == 0) {
        printf("\nTip: '.' matched as a single-char wildcard. For the qualified name "
               "%s, try: qi %s -p %s\n", qn->original, qn->symbol, qn->qualifier);
    }
}

static void print_context_types(void) {
    printf("Context Types (use full or abbreviated forms, case insensitive):\n");
    printf("  %-12s %-9s %s\n", "Full Name", "Short", "Description");
    printf("  %-12s %-9s %s\n", "------------", "-----", "-----------");
    printf("  %-12s %-9s %s\n", "argument", "arg", "Function parameters");
    printf("  %-12s %-9s %s\n", "call", "-", "Function/method calls");
    printf("  %-12s %-9s %s\n", "case", "-", "Enum values/cases");
    printf("  %-12s %-9s %s\n", "class", "-", "Class definitions");
    printf("  %-12s %-9s %s\n", "comment", "com", "Words from comments");
    printf("  %-12s %-9s %s\n", "enum", "-", "Enum type definitions");
    printf("  %-12s %-9s %s\n", "exception", "exc", "Exception classes");
    printf("  %-12s %-9s %s\n", "export", "exp", "Export statements");
    printf("  %-12s %-9s %s\n", "filename", "file", "Filename without extension");
    printf("  %-12s %-9s %s\n", "function", "func", "Function/method definitions");
    printf("  %-12s %-9s %s\n", "goto", "-", "Goto statements (C)");
    printf("  %-12s %-9s %s\n", "import", "imp", "Import/include statements");
    printf("  %-12s %-9s %s\n", "interface", "iface", "Interface definitions");
    printf("  %-12s %-9s %s\n", "label", "-", "Labels (for goto in C)");
    printf("  %-12s %-9s %s\n", "lambda", "lam", "Lambda/arrow functions");
    printf("  %-12s %-9s %s\n", "macro", "-", "Preprocessor macros");
    printf("  %-12s %-9s %s\n", "namespace", "ns", "Namespace/package declarations");
    printf("  %-12s %-9s %s\n", "property", "prop", "Class/struct fields");
    printf("  %-12s %-9s %s\n", "string", "str", "Words from string literals");
    printf("  %-12s %-9s %s\n", "trait", "-", "Trait definitions (PHP)");
    printf("  %-12s %-9s %s\n", "type", "-", "Type definitions (struct, enum, etc.)");
    printf("  %-12s %-9s %s\n", "variable", "var", "Variables and constants");
    printf("\n");
    printf("  Examples: -i func, -i function, -i FUNC all work the same\n");
    printf("  Special: -x noise expands to -x comment string\n");
}

static void show_help_compact(void) {
    printf("Usage: qi PATTERN [PATTERN...] [OPTIONS]\n");
    printf("Search indexed code symbols.\n");
    printf("Example: qi getUserById --def -e\n");
    printf("Note: qi searches identifiers and indexed symbol metadata, not arbitrary text.\n");
    printf("\n");

    printf("Quick Start:\n");
    printf("  qi user                       find symbol (exact match)\n");
    printf("  qi user%% -i func var          only functions/variables (starts with user)\n");
    printf("  qi '*user*' -x noise -C 3     skip comments/strings, show 3 lines of context (contains user)\n");
    printf("  qi getUserById --def -e       show full definition (LLMs: add --raw flag for Edit anchors)\n");
    printf("  qi %% -f query-index.c --toc   show file structure\n");
    printf("\n");

    printf("Match:\n");
    printf("  -i, --include-context TYPE...  only these contexts\n");
    printf("  -x, --exclude-context TYPE...  exclude these contexts\n");
    printf("  -x noise                       exclude comments and strings\n");
    printf("      --and [RANGE]              require all patterns on same/nearby lines\n");
    printf("\n");

    printf("Filter:\n");
    printf("  -f, --file PATTERN...          filter files: database.c, .py, shared/, shared/*.c\n");
#define COLUMN(name, sql_type, c_type, width, full, compact, long_flag, short_flag, max_len, help_desc, help_example) \
    { \
        char flag_text[64]; \
        snprintf(flag_text, sizeof(flag_text), "-%s, --%s PATTERN", #short_flag, #long_flag); \
        printf("  %-30s %s\n", flag_text, help_desc); \
    }
#define INT_COLUMN(name, sql_type, c_type, width, full, compact, long_flag, short_flag, help_desc, help_example) \
    { \
        char flag_text[64]; \
        snprintf(flag_text, sizeof(flag_text), "-%s, --%s [0|1]", #short_flag, #long_flag); \
        printf("  %-30s %s\n", flag_text, help_desc); \
    }
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN
    printf("      --parent-type PATTERN...   filter by parent's declared type (resolves parent to its definition)\n");
    printf("      --def                      definitions only\n");
    printf("      --usage                    usages only\n");
    printf("      --lines LINE|START-END     filter line/range\n");
    printf("  -w, --within SYMBOL...         search inside definitions\n");
    printf("  -l, --limit NUM                limit matches\n");
    printf("  -lpf, --limit-per-file NUM     limit matches per file\n");
    printf("\n");

    printf("Display:\n");
    printf("  -e, --expand                   show full definitions\n");
    printf("  -C, --context NUM              lines before and after\n");
    printf("  -A, --after-context NUM        lines after\n");
    printf("  -B, --before-context NUM       lines before\n");
    printf("      --files                    list matching files only\n");
    printf("      --toc                      file table of contents; use with -f\n");
    printf("      --columns COL...           choose columns (supports aliases): line sym ctx");
#define COLUMN(name, sql_type, c_type, width, full, compact, long_flag, short_flag, ...) \
    { \
        char lower[32]; \
        to_lowercase_copy(compact, lower, sizeof(lower)); \
        printf(" %s", lower); \
    }
#define INT_COLUMN(name, sql_type, c_type, width, full, compact, long_flag, short_flag, ...) \
    { \
        char lower[32]; \
        to_lowercase_copy(compact, lower, sizeof(lower)); \
        printf(" %s", lower); \
    }
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN
    printf("\n");
    printf("  -v, --verbose                  all columns\n");
    printf("      --full                     full column names\n");
    printf("      --raw                      source only; useful with -e/-A/-B\n");
    printf("  -q, --quiet                    drop banner/footer/rule chrome; keep header + rows\n");
    printf("\n");

    printf("Database:\n");
    printf("      --db-file PATH             database path\n");
    printf("      --debug                    show SQL\n");
    printf("\n");

    printf("Types: func call class var arg type prop com str; use --list-types for all.\n");
    printf("Patterns: case-insensitive, exact by default; wildcards: %% or * any chars, _ or . one char (* needs shell quoting).\n");
    printf("Filters: case-insensitive, fuzzy by default.\n");
    printf("\n");

    printf("Configuration:\n");
    printf("  Config file: ./.smconfig or ~/.smconfig. CLI flags override config\n");
    printf("  Format: [qi] section header, then one flag per line. Example: --db-file /dev/shm/index.db\n");
    printf("\n");
    printf("Please email bug reports to bugs@sourceminder.org.\n");
}

/* HOST_ONLY: CLI entry point depends on argv parsing, filesystem checks, environment, and terminal output. */
int main(int argc, char *argv[]) {
    int retval = 0;  /* Return value: 0 = success, 1 = error */

    /* Initialize within filter early (before any goto cleanup) */
    WithinFilter within_filter = {0};
    WithinRangeList within_ranges = {0};

    /* Check for --help, --version, --list-types, and --toc flags first */
    int show_help = 0;
    int show_version = 0;
    int show_list_types = 0;
    int show_toc = 0;
    for (int arg_idx = 1; arg_idx < argc; arg_idx++) {
        if (strcmp(argv[arg_idx], "--help") == 0 || strcmp(argv[arg_idx], "-h") == 0) {
            show_help = 1;
            break;
        }
        if (strcmp(argv[arg_idx], "--version") == 0) {
            show_version = 1;
            break;
        }
        if (strcmp(argv[arg_idx], "--list-types") == 0) {
            show_list_types = 1;
            break;
        }
        if (strcmp(argv[arg_idx], "--toc") == 0) {
            show_toc = 1;
        }
    }

    if (show_version) {
        print_version();
        return 0;
    }

    if (show_list_types) {
        print_context_types();
        return 0;
    }

    /* Save original argv and argc for cleanup */
    char **original_argv = argv;
    int original_argc = argc;

    /* Load config file only if not showing help (CLI flags override config) */
    if (!show_help) {
        int cli_flags = scan_cli_flags(argc, argv);
        if (load_config_file(&argc, &argv, cli_flags) != 0) {
            retval = 1;
            goto cleanup;
        }
    }

    if (argc < 2 || show_help) {
        show_help_compact();
        return 0;
    }

    PatternList patterns = { .count = 0 };
    /* Records the first grep-style alternation split, for the educational
     * warning emitted after parsing; .original stays NULL if none occurred. */
    AltWarning alt_warning = { 0 };
    /* First dotted qualified name typed as a single bare pattern, captured from
     * the original token before '.' is rewritten to the '_' LIKE wildcard. */
    QualifiedName qname = { 0 };
    int limit = 0;
    int limit_per_file = 0;  /* Limit results per file */
    int verbose = 0;
    int compact = 1;  /* Compact mode is now the default */
    int full = 0;
    int show_all_columns = 0;  /* Special mode for --columns all */
    int has_custom_columns = 0;
    int line_range = -1;  /* Line range for proximity search (-1 = OR mode, 0 = same line, N = within N lines) */
    int expand = 0;  /* Expand to show full definition */
    int files_only = 0;  /* Files-only mode - show unique file paths */
    int toc_mode = 0;  /* Table of contents mode */
    int context_before = 0;
    int context_after = 0;
    int debug = 0;  /* Debug mode - show SQL queries */
    int raw_mode = 0;  /* Raw mode - suppress all non-source output */
    int quiet = 0;     /* Quiet mode - drop banner + footer + separator-rule chrome; keep header row and match rows */
    int def_only = 0;
    int usage_only = 0;
    const char *db_file = "code-index.db";  /* Default database location */
    CodeIndexDatabase db = {0};  /* Initialize early so cleanup is safe */

    /* Context type filters (traditional - separate from QueryFilters) */
    ContextTypeList include = { .count = 0 };
    ContextTypeList exclude = { .count = 0 };

    /* File filter (directory + filename patterns) */
    FileFilterList file_filter = { .count = 0 };

    /* X-Macro: Initialize QueryFilters for extensible columns */
    QueryFilters filters = {
        .line_start = -1,
        .line_end = -1,
#define COLUMN(name, ...) .name = { .count = 0 },
#define INT_COLUMN(name, ...) .name = { .count = 0 },
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN
        .parent_type = { .count = 0 },
    };

    /* X-Macro: Initialize show flags for extensible columns */
    ShowColumns show_columns = {
#define COLUMN(name, ...) .name = 0,
#define INT_COLUMN(name, ...) .name = 0,
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN
    };

    /* Parse pattern arguments (before flags) */
    int i = 1;
    while (i < argc && argv[i][0] != '-') {
        /* Strip leading backslash if present (escape for patterns starting with '-')
         * Skip leading backslash ONLY if it's escaping a dash (for searching "-flag"
         * symbols). Don't skip for wildcard escaping like \* or \. */
        const char *pattern = argv[i];
        if (pattern[0] == '\\' && pattern[1] == '-') {
            pattern++;  /* Skip the leading backslash */
        }
        /* Split grep-style alternation 'A|B' (or 'A\|B') into separate OR'd terms. */
        char *segs[MAX_PATTERNS];
        int nseg = split_alternation(pattern, segs, MAX_PATTERNS);
        if (nseg > 1) {
            capture_alt_warning(pattern, segs, nseg, &alt_warning);
        }
        for (int s = 0; s < nseg; s++) {
            if (patterns.count < MAX_PATTERNS) {
                /* Convert shell-style wildcards (*) to SQL LIKE wildcards (%) */
                char converted_pattern[SYMBOL_MAX_LENGTH];
                convert_wildcards(segs[s], converted_pattern, sizeof(converted_pattern));
                patterns.patterns[patterns.count] = try_strdup_ctx(converted_pattern, "Failed to allocate memory for pattern");
                if (!patterns.patterns[patterns.count]) {
                    for (int r = s; r < nseg; r++) free(segs[r]);
                    retval = 1;
                    goto cleanup;
                }
                patterns.count++;
                /* Capture a dotted qualified name from the ORIGINAL token (segs[s]
                 * still has its '.'), only for the first bare single-term pattern.
                 * The retry/Tip is gated on patterns->count==1 at query time, so a
                 * second pattern harmlessly disables it. */
                if (nseg == 1 && patterns.count == 1 && !qname.active &&
                    split_qualified_name(segs[s], qname.qualifier, sizeof(qname.qualifier),
                                         qname.symbol, sizeof(qname.symbol))) {
                    snprintf(qname.original, sizeof(qname.original), "%s", segs[s]);
                    qname.active = 1;
                }
            } else {
                fprintf(stderr, "Warning: Maximum pattern limit (%d) reached. Ignoring: %s\n",
                        MAX_PATTERNS, segs[s]);
            }
            free(segs[s]);
        }
        i++;
    }

    /* Pattern requirement: skip for --toc mode (like --help) */
    if (patterns.count == 0 && !show_toc) {
        fprintf(stderr, "Error: At least one search pattern is required\n");
        retval = 1;
        goto cleanup;
    }

    /* Check for patterns with spaces (common user mistake) */
    int has_patterns_with_spaces = 0;
    for (int j = 0; j < patterns.count; j++) {
        if (strchr(patterns.patterns[j], ' ') != NULL) {
            has_patterns_with_spaces = 1;
            break;
        }
    }

    if (has_patterns_with_spaces) {
        /* Extract program name from argv[0] (could be ./qi, qi, /path/to/qi, etc.) */
        const char *prog_name = strrchr(argv[0], '/');
        prog_name = prog_name ? prog_name + 1 : argv[0];

        printf("Note: The index only contains individual symbols, and breaks on word boundaries.\n");
        printf("      You may search for multiple words occurring on the same line with the --and flag:\n\n");
        for (int j = 0; j < patterns.count; j++) {
            if (strchr(patterns.patterns[j], ' ') != NULL) {
                /* Split the pattern by spaces and show example */
                printf("      %s", prog_name);
                char *pattern_copy = try_strdup_ctx(patterns.patterns[j], "Failed to allocate memory for pattern copy");
                if (!pattern_copy) {
                    retval = 1;
                    goto cleanup;
                }
                char *token = strtok(pattern_copy, " ");
                while (token != NULL) {
                    printf(" %s", token);
                    token = strtok(NULL, " ");
                }
                printf(" --and\n");
                free(pattern_copy);
            }
        }
        printf("\n");
        retval = 1;
        goto cleanup;
    }

    /* Parse flag arguments */
    int has_include = 0;
    int has_exclude = 0;

    for (; i < argc; i++) {
      
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        }
        else if (strcmp(argv[i], "--compact") == 0) {
            compact = 1;
        }
        else if (strcmp(argv[i], "--full") == 0) {
            compact = 0;
            full = 1;
        }
        else if (strcmp(argv[i], "--debug") == 0) {
            debug = 1;
        }
        else if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--expand") == 0) {
            expand = 1;
        }
        else if (strcmp(argv[i], "--raw") == 0) {
            raw_mode = 1;
        }
        else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
            quiet = 1;
        }
        else if (strcmp(argv[i], "--files") == 0) {
            files_only = 1;
        }
        else if (strcmp(argv[i], "--toc") == 0) {
            toc_mode = 1;
        }
        else if ((strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--include-context") == 0)) {
            has_include = 1;
            /* Collect all include types until we hit another flag or end */
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                if (include.count < MAX_CONTEXT_TYPES) {
                    /* Check for commas (common mistake) */
                    if (strchr(argv[i + 1], ',') != NULL) {
                        char *suggestion = try_strdup_ctx(argv[i + 1], "Failed to allocate memory for suggestion");
                        if (suggestion) {
                            fprintf(stderr, "Error: Context types should be space-separated, not comma-separated.\n");
                            fprintf(stderr, "Use: -i %s (not -i %s)\n",
                                    strtok(suggestion, ","), argv[i + 1]);
                            free(suggestion);
                        }
                        retval = 1;
                        goto cleanup;
                    }
                    char type_upper[CONTEXT_TYPE_MAX_LENGTH];
                    snprintf(type_upper, sizeof(type_upper), "%s", argv[i + 1]);
                    to_upper(type_upper);
                    ContextType ctx = string_to_context(type_upper);
                    if (ctx == CONTEXT_INVALID) {
                        report_invalid_context("-i", argv[i + 1]);
                        retval = 1;
                        goto cleanup;
                    }
                    include.types[include.count++] = ctx;
                }
                i++;
            }
        }
        else if ((strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--exclude-context") == 0)) {
            has_exclude = 1;
            /* Collect all exclude types until we hit another flag or end */
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                if (exclude.count < MAX_CONTEXT_TYPES) {
                    /* Check for commas (common mistake) */
                    if (strchr(argv[i + 1], ',') != NULL) {
                        char *suggestion = try_strdup_ctx(argv[i + 1], "Failed to allocate memory for suggestion");
                        if (suggestion) {
                            fprintf(stderr, "Error: Context types should be space-separated, not comma-separated.\n");
                            fprintf(stderr, "Use: -x %s (not -x %s)\n",
                                    strtok(suggestion, ","), argv[i + 1]);
                            free(suggestion);
                        }
                        retval = 1;
                        goto cleanup;
                    }
                    char type_upper[CONTEXT_TYPE_MAX_LENGTH];
                    snprintf(type_upper, sizeof(type_upper), "%s", argv[i + 1]);
                    to_upper(type_upper);

                    /* Smart filter: "noise" expands to "comment" and "string" */
                    if (strcmp(type_upper, "NOISE") == 0) {
                        /* Ensure room for both COMMENT and STRING */
                        if (exclude.count + 2 <= MAX_CONTEXT_TYPES) {
                            exclude.types[exclude.count++] = CONTEXT_COMMENT;
                            exclude.types[exclude.count++] = CONTEXT_STRING;
                        } else {
                            fprintf(stderr, "Warning: Not enough space for -x noise expansion (limit: %d)\n",
                                    MAX_CONTEXT_TYPES);
                        }
                    } else {
                        ContextType ctx = string_to_context(type_upper);
                        if (ctx == CONTEXT_INVALID) {
                            report_invalid_context("-x", argv[i + 1]);
                            retval = 1;
                            goto cleanup;
                        }
                        exclude.types[exclude.count++] = ctx;
                    }
                }
                i++;
            }
        }
        else if ((strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file") == 0)) {
            /* Collect all file patterns until we hit another flag or end */
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                if (file_filter.count < MAX_CONTEXT_TYPES) {
                    char *dir = NULL;
                    char *file = NULL;
                    if (process_file_pattern(argv[i + 1], &dir, &file) != 0) {
                        retval = 1;
                        goto cleanup;
                    }
                    file_filter.patterns[file_filter.count].directory = dir;
                    file_filter.patterns[file_filter.count].filename = file;
                    file_filter.count++;
                }
                i++;
            }
        }
        /* X-Macro: Parse extensible column filter flags */
#define COLUMN(name, sql_type, c_type, width, full, compact, long_flag, short_flag, ...) \
        else if (strcmp(argv[i], "--" #long_flag) == 0 || \
                 strcmp(argv[i], "-" #short_flag) == 0) { \
            show_columns.name = 1; \
            while (i + 1 < argc && argv[i + 1][0] != '-') { \
                /* Split grep-style alternation 'A|B' into separate OR'd values. */ \
                char *fsegs[MAX_CONTEXT_TYPES]; \
                int fnseg = split_alternation(argv[i + 1], fsegs, MAX_CONTEXT_TYPES); \
                if (fnseg > 1) capture_alt_warning(argv[i + 1], fsegs, fnseg, &alt_warning); \
                for (int fs = 0; fs < fnseg; fs++) { \
                    if (filters.name.count < MAX_CONTEXT_TYPES) { \
                        char converted_value[SYMBOL_MAX_LENGTH]; \
                        convert_wildcards(fsegs[fs], converted_value, sizeof(converted_value)); \
                        filters.name.values[filters.name.count++] = safe_strdup_ctx(converted_value, "Failed to allocate memory for " #long_flag " filter"); \
                    } else { \
                        fprintf(stderr, "Warning: Maximum filter limit (%d) reached for --%s. Ignoring: %s\n", \
                                MAX_CONTEXT_TYPES, #long_flag, fsegs[fs]); \
                    } \
                    free(fsegs[fs]); \
                } \
                i++; \
            } \
        }
#define INT_COLUMN(name, sql_type, c_type, width, full, compact, long_flag, short_flag, ...) \
        else if (strcmp(argv[i], "--" #long_flag) == 0 || \
                 strcmp(argv[i], "-" #short_flag) == 0) { \
            show_columns.name = 1; \
            while (i + 1 < argc && argv[i + 1][0] != '-') { \
                if (filters.name.count < MAX_CONTEXT_TYPES) { \
                    filters.name.values[filters.name.count++] = safe_strdup_ctx(argv[i + 1], "Failed to allocate memory for " #long_flag " filter"); \
                } else { \
                    fprintf(stderr, "Warning: Maximum filter limit (%d) reached for --%s. Ignoring: %s\n", \
                            MAX_CONTEXT_TYPES, #long_flag, argv[i + 1]); \
                } \
                i++; \
            } \
        }
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN
        /* Virtual filter: --parent-type resolves parent_symbol to its
         * same-file definition and matches that definition's type */
        else if (strcmp(argv[i], "--parent-type") == 0) {
            show_columns.parent_symbol = 1;
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                /* Split grep-style alternation 'A|B' into separate OR'd values. */
                char *fsegs[MAX_CONTEXT_TYPES];
                int fnseg = split_alternation(argv[i + 1], fsegs, MAX_CONTEXT_TYPES);
                if (fnseg > 1) capture_alt_warning(argv[i + 1], fsegs, fnseg, &alt_warning);
                for (int fs = 0; fs < fnseg; fs++) {
                    if (filters.parent_type.count < MAX_CONTEXT_TYPES) {
                        char converted_value[SYMBOL_MAX_LENGTH];
                        convert_wildcards(fsegs[fs], converted_value, sizeof(converted_value));
                        filters.parent_type.values[filters.parent_type.count++] = safe_strdup_ctx(converted_value, "Failed to allocate memory for parent-type filter");
                    } else {
                        fprintf(stderr, "Warning: Maximum filter limit (%d) reached for --parent-type. Ignoring: %s\n",
                                MAX_CONTEXT_TYPES, fsegs[fs]);
                    }
                    free(fsegs[fs]);
                }
                i++;
            }
        }
        /* Convenience aliases for is_definition filter */
        else if (strcmp(argv[i], "--def") == 0) {
            def_only = 1;
            show_columns.is_definition = 1;
            /* --def means show only definitions (is_definition = 1) */
            if (filters.is_definition.count < MAX_CONTEXT_TYPES) {
                filters.is_definition.values[filters.is_definition.count] = try_strdup_ctx("1", "Failed to allocate memory for definition filter");
                if (!filters.is_definition.values[filters.is_definition.count]) {
                    retval = 1;
                    goto cleanup;
                }
                filters.is_definition.count++;
            }
        }
        else if (strcmp(argv[i], "--usage") == 0) {
            usage_only = 1;
            show_columns.is_definition = 1;
            /* --usage means show only usages (is_definition = 0) */
            if (filters.is_definition.count < MAX_CONTEXT_TYPES) {
                filters.is_definition.values[filters.is_definition.count] = try_strdup_ctx("0", "Failed to allocate memory for usage filter");
                if (!filters.is_definition.values[filters.is_definition.count]) {
                    retval = 1;
                    goto cleanup;
                }
                filters.is_definition.count++;
            }
        }
        else if (strcmp(argv[i], "--columns") == 0) {
            has_custom_columns = 1;
            num_active_columns = 0;  /* Reset before adding custom columns */
            /* Collect all column names until we hit another flag or end */
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                const char *arg = argv[i + 1];

                /* Check for "all" keyword - enable special all-columns mode */
                if (strcasecmp(arg, "all") == 0) {
                    show_all_columns = 1;
                    i++;
                    continue;
                }

                /* Check if argument contains commas - if so, split by comma */
                if (strchr(arg, ',') != NULL) {
                    char arg_copy[1024];
                    strncpy(arg_copy, arg, sizeof(arg_copy) - 1);
                    arg_copy[sizeof(arg_copy) - 1] = '\0';

                    /* Use strtok_r for thread safety */
                    char *saveptr;
                    char *token = strtok_r(arg_copy, ",", &saveptr);
                    while (token != NULL) {
                        /* Trim leading/trailing whitespace from token */
                        while (*token == ' ' || *token == '\t') token++;
                        char *end = token + strlen(token) - 1;
                        while (end > token && (*end == ' ' || *end == '\t')) end--;
                        *(end + 1) = '\0';

                        if (*token != '\0') {
                            add_column_by_name(token);
                        }
                        token = strtok_r(NULL, ",", &saveptr);
                    }
                } else {
                    /* No comma, process as single column name */
                    add_column_by_name(arg);
                }
                i++;
            }
        }
        else if (strcmp(argv[i], "--db-file") == 0 && i + 1 < argc) {
            db_file = argv[i + 1];
            i++;
        }
        else if ((strcmp(argv[i], "--limit") == 0 || strcmp(argv[i], "-l") == 0) && i + 1 < argc) {
            char *endptr;
            errno = 0;
            long val = strtol(argv[i + 1], &endptr, 10);

            if (errno != 0 || *endptr != '\0' || val < 0 || val > INT_MAX) {
                fprintf(stderr, "Error: --limit requires a positive integer (0-%d)\n", INT_MAX);
                retval = 1;
                goto cleanup;
            }
            limit = (int)val;
            i++;
        }
        else if ((strcmp(argv[i], "--limit-per-file") == 0 || strcmp(argv[i], "-lpf") == 0) && i + 1 < argc) {
            char *endptr;
            errno = 0;
            long val = strtol(argv[i + 1], &endptr, 10);

            if (errno != 0 || *endptr != '\0' || val < 0 || val > INT_MAX) {
                fprintf(stderr, "Error: --limit-per-file requires a positive integer (0-%d)\n", INT_MAX);
                retval = 1;
                goto cleanup;
            }
            limit_per_file = (int)val;
            i++;
        }
        else if (strcmp(argv[i], "--lines") == 0 && i + 1 < argc) {
            /* Parse line range: "LINE" or "START-END" */
            const char *range_str = argv[i + 1];
            char *dash = strchr(range_str, '-');

            if (dash == NULL) {
                /* Single line number */
                char *endptr;
                errno = 0;
                long val = strtol(range_str, &endptr, 10);

                if (errno != 0 || *endptr != '\0' || val < 1 || val > INT_MAX) {
                    fprintf(stderr, "Error: --lines requires a positive integer line number\n");
                    retval = 1;
                    goto cleanup;
                }
                filters.line_start = (int)val;
                filters.line_end = (int)val;
            } else {
                /* Line range: START-END */
                *dash = '\0';  /* Temporarily split the string */
                char *endptr;
                errno = 0;
                long start = strtol(range_str, &endptr, 10);

                if (errno != 0 || *endptr != '\0' || start < 1 || start > INT_MAX) {
                    fprintf(stderr, "Error: --lines range start must be a positive integer\n");
                    retval = 1;
                    goto cleanup;
                }

                errno = 0;
                long end = strtol(dash + 1, &endptr, 10);

                if (errno != 0 || *endptr != '\0' || end < 1 || end > INT_MAX) {
                    fprintf(stderr, "Error: --lines range end must be a positive integer\n");
                    retval = 1;
        goto cleanup;
                }

                if (start > end) {
                    fprintf(stderr, "Error: --lines range start (%ld) cannot be greater than end (%ld)\n", start, end);
                    retval = 1;
        goto cleanup;
                }

                filters.line_start = (int)start;
                filters.line_end = (int)end;
                *dash = '-';  /* Restore the string */
            }
            i++;
        }
        else if (strcmp(argv[i], "--within") == 0 || strcmp(argv[i], "-w") == 0) {
            /* Collect all non-flag arguments as within symbols */
            if(debug){
              fprintf(stdout, "DEBUG: WITHIN FLAG PASSED\n");
            }
            
            while (i + 1 < argc && argv[i + 1][0] != '-' && within_filter.count < MAX_PATTERNS) {
                within_filter.symbols[within_filter.count] = try_strdup_ctx(argv[i + 1], "Failed to allocate memory for within symbol");
                if (!within_filter.symbols[within_filter.count]) {
                    retval = 1;
                    goto cleanup;
                }
                within_filter.count++;
                i++;
            }
        }
        else if (strcmp(argv[i], "--and") == 0 || strcmp(argv[i], "--same-line") == 0) {
            /* Check if next arg is a number (range value) */
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                char *endptr;
                errno = 0;
                long val = strtol(argv[i + 1], &endptr, 10);
                if (errno == 0 && *endptr == '\0' && val >= 0 && val <= INT_MAX) {
                    line_range = (int)val;
                    i++;  /* Consume the number argument */
                } else {
                    fprintf(stderr, "Error: --and requires a non-negative integer (0-%d)\n", INT_MAX);
                    retval = 1;
                    goto cleanup;
                }
            } else {
                line_range = 0;  /* No number provided, default to same line */
            }
        }
        else if (strcmp(argv[i], "-A") == 0) {
            /* Check if next arg is a number or another flag */
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                char *endptr;
                errno = 0;
                long val = strtol(argv[i + 1], &endptr, 10);
                if (errno == 0 && *endptr == '\0' && val >= 0 && val <= MAXIMUM_CONTEXT_RANGE) {
                    context_after = (int)val;
                    i++;
                } else if (errno == 0 && *endptr == '\0' && val > MAXIMUM_CONTEXT_RANGE) {
                    fprintf(stderr, "Error: -A value cannot exceed %d\n", MAXIMUM_CONTEXT_RANGE);                    
                    retval = 1;
                    goto cleanup;
                } else {
                    fprintf(stderr, "Error: -A requires a non-negative integer (0-%d)\n", MAXIMUM_CONTEXT_RANGE);                    
                    retval = 1;
                    goto cleanup;
                }
            } else {
                /* No argument provided, use default */
                context_after = DEFAULT_CONTEXT_RANGE;
            }
        }
        else if (strcmp(argv[i], "-B") == 0) {
            /* Check if next arg is a number or another flag */
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                char *endptr;
                errno = 0;
                long val = strtol(argv[i + 1], &endptr, 10);
                if (errno == 0 && *endptr == '\0' && val >= 0 && val <= MAXIMUM_CONTEXT_RANGE) {
                    context_before = (int)val;
                    i++;
                } else if (errno == 0 && *endptr == '\0' && val > MAXIMUM_CONTEXT_RANGE) {
                    fprintf(stderr, "Error: -B value cannot exceed %d\n", MAXIMUM_CONTEXT_RANGE);
                    retval = 1;
                    goto cleanup;
                } else {
                    fprintf(stderr, "Error: -B requires a non-negative integer (0-%d)\n", MAXIMUM_CONTEXT_RANGE);
                    retval = 1;
                    goto cleanup;
                }
            } else {
                /* No argument provided, use default */
                context_before = DEFAULT_CONTEXT_RANGE;
            }
        }
        else if (strcmp(argv[i], "-C") == 0) {
            /* Check if next arg is a number or another flag */
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                char *endptr;
                errno = 0;
                long val = strtol(argv[i + 1], &endptr, 10);
                if (errno == 0 && *endptr == '\0' && val >= 0 && val <= MAXIMUM_CONTEXT_RANGE) {
                    context_before = context_after = (int)val;
                    i++;
                } else if (errno == 0 && *endptr == '\0' && val > MAXIMUM_CONTEXT_RANGE) {
                    fprintf(stderr, "Error: -C value cannot exceed %d\n", MAXIMUM_CONTEXT_RANGE);
                    retval = 1;
                    goto cleanup;
                } else {
                    fprintf(stderr, "Error: -C requires a non-negative integer (0-%d)\n", MAXIMUM_CONTEXT_RANGE);
                    retval = 1;
                    goto cleanup;
                }
            } else {
                /* No argument provided, use default */
                context_before = context_after = DEFAULT_CONTEXT_RANGE;
            }
        }
        else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unrecognized option '%s'\n", argv[i]);
            fprintf(stderr, "Run 'qi --help' for a list of valid options.\n");
            retval = 1;
            goto cleanup;
        }
    }

    /* Educate when grep-style alternation '|' (or '\|') was split into OR'd
     * terms, echoing the user's actual terms and separator form. Goes to
     * stderr so it survives -q, which only strips stdout chrome. */
    if (alt_warning.original != NULL) {
        fprintf(stderr,
            "qi: '%s' contains grep-style %s alternation; searching for %s instead.\n"
            "    Pass symbols as separate arguments: qi %s\n",
            alt_warning.original, alt_warning.sep,
            alt_warning.alternatives, alt_warning.alternatives);
        if (line_range >= 0) {
            fprintf(stderr,
                "    --and [N] is used to find matches within a certain number of lines from eachother (0 by default).\n");
        }
    }

    /* Validate --and (--same-line) requires 2+ patterns */
    if (line_range >= 0 && patterns.count < 2) {
        fprintf(stderr, "Error: --and requires at least 2 search patterns for proximity search.\n");
        fprintf(stderr, "Example: qi malloc free --and 10\n");
        retval = 1;
        goto cleanup;
    }

    /* Precedence: -i (include) overrides -x (exclude)
     * This allows config file to set -x noise as default,
     * and CLI -i to override it when user wants specific types */
    if (has_include && has_exclude) {
        /* Clear exclude list - include takes precedence */
        exclude.count = 0;
    }

    /* Handle --toc mode (early exit) */
    if (toc_mode) {
        /* Validate: --toc requires -f flag */
        if (file_filter.count == 0) {
            fprintf(stderr, "Error: --toc requires -f <file_pattern>\n");
            fprintf(stderr, "Example: qi --toc -f c/c_language.c\n");
            retval = 1;
            goto cleanup;
        }

        const char *toc_conflicts[32];
        int toc_conflict_count = 0;

#define ADD_TOC_CONFLICT(flag_name) do { \
            if (toc_conflict_count < (int)(sizeof(toc_conflicts) / sizeof(toc_conflicts[0]))) { \
                toc_conflicts[toc_conflict_count++] = (flag_name); \
            } \
        } while (0)

        if (limit > 0) ADD_TOC_CONFLICT("--limit");
        if (limit_per_file > 0) ADD_TOC_CONFLICT("--limit-per-file");
        if (expand) ADD_TOC_CONFLICT("--expand");
        if (raw_mode) ADD_TOC_CONFLICT("--raw");
        if (context_before > 0) ADD_TOC_CONFLICT("--before-context");
        if (context_after > 0) ADD_TOC_CONFLICT("--after-context");
        if (files_only) ADD_TOC_CONFLICT("--files");
        if (has_custom_columns) ADD_TOC_CONFLICT("--columns");
        if (verbose) ADD_TOC_CONFLICT("--verbose");
        if (full) ADD_TOC_CONFLICT("--full");
        if (def_only) ADD_TOC_CONFLICT("--def");
        if (usage_only) ADD_TOC_CONFLICT("--usage");
        if (filters.line_start >= 0 || filters.line_end >= 0) ADD_TOC_CONFLICT("--lines");
        if (within_filter.count > 0) ADD_TOC_CONFLICT("--within");
#define COLUMN(name, sql_type, c_type, width, full_name, compact_name, long_flag, short_flag, ...) \
        if (filters.name.count > 0 && strcmp(#long_flag, "definition") != 0) ADD_TOC_CONFLICT("--" #long_flag);
#define INT_COLUMN(name, sql_type, c_type, width, full_name, compact_name, long_flag, short_flag, ...) \
        if (filters.name.count > 0 && strcmp(#long_flag, "definition") != 0) ADD_TOC_CONFLICT("--" #long_flag);
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN
#undef ADD_TOC_CONFLICT

        if (validate_toc_compatible_options(toc_conflicts, toc_conflict_count) != 0) {
            retval = 1;
            goto cleanup;
        }

        /* Validate --within flag */
        if (within_filter.count > 0) {
            for (int j = 0; j < within_filter.count; j++) {
                if (within_filter.symbols[j] == NULL || strlen(within_filter.symbols[j]) == 0) {
                    fprintf(stderr, "Error: --within requires non-empty symbol names\n");
                    retval = 1;
                    goto cleanup;
                }
            }
        }

        /* Build TOC config */
        const char **symbol_patterns = NULL;
        const char **include_context_filters = NULL;
        const char **exclude_context_filters = NULL;

        /* Use patterns for symbol filtering if not just "%" */
        if (patterns.count > 0 && strcmp(patterns.patterns[0], "%") != 0) {
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
#endif
            symbol_patterns = (const char **)patterns.patterns;
#ifdef __clang__
#pragma clang diagnostic pop
#endif
        }

        /* Use include contexts if specified */
        if (has_include && include.count > 0) {
            include_context_filters = malloc(sizeof(char *) * (size_t)include.count);
            if (!include_context_filters) {
                fprintf(stderr, "Error: Failed to allocate memory for context filters\n");
                retval = 1;
        goto cleanup;
            }
            for (int j = 0; j < include.count; j++) {
                include_context_filters[j] = context_to_string(include.types[j], 1);  /* Use compact form */
            }
        }

        /* Use exclude contexts if specified */
        if (has_exclude && exclude.count > 0) {
            exclude_context_filters = malloc(sizeof(char *) * (size_t)exclude.count);
            if (!exclude_context_filters) {
                fprintf(stderr, "Error: Failed to allocate memory for context filters\n");
                if (include_context_filters) free(include_context_filters);
                retval = 1;
        goto cleanup;
            }
            for (int j = 0; j < exclude.count; j++) {
                exclude_context_filters[j] = context_to_string(exclude.types[j], 1);  /* Use compact form */
            }
        }

        /* Convert file patterns to TocFilePattern array */
        TocFilePattern *toc_file_patterns = malloc(sizeof(TocFilePattern) * (size_t)file_filter.count);
        if (!toc_file_patterns) {
            fprintf(stderr, "Error: Failed to allocate memory for TOC file patterns\n");
            if (include_context_filters) free(include_context_filters);
            if (exclude_context_filters) free(exclude_context_filters);
            retval = 1;
        goto cleanup;
        }
        for (int j = 0; j < file_filter.count; j++) {
            toc_file_patterns[j].directory = file_filter.patterns[j].directory;
            toc_file_patterns[j].filename = file_filter.patterns[j].filename;
        }

        TocConfig config = {
            .file_patterns = toc_file_patterns,
            .file_pattern_count = file_filter.count,
            .symbol_patterns = symbol_patterns,
            .symbol_pattern_count = (symbol_patterns ? patterns.count : 0),
            .include_contexts = include_context_filters,
            .include_context_count = (has_include ? include.count : 0),
            .exclude_contexts = exclude_context_filters,
            .exclude_context_count = (has_exclude ? exclude.count : 0),
            .limit = limit,
            .limit_per_file = limit_per_file,
            .debug = debug,
            .db_path = db_file
        };

        int result = build_toc(&config);

        /* Cleanup TOC-specific allocations */
        free(toc_file_patterns);
        if (include_context_filters) {
            free(include_context_filters);
        }
        if (exclude_context_filters) {
            free(exclude_context_filters);
        }

        /* Set return value and goto cleanup for proper resource cleanup */
        retval = (result == 0) ? 0 : 1;
        goto cleanup;
    }

    /* Check if patterns are filtered words (no wildcards) */
    for (int j = 0; j < patterns.count; j++) {
        const char *pattern = patterns.patterns[j];
        if (!has_wildcards(pattern)) {
            /* Pattern has no unescaped wildcards - unescape it and check if it's filtered */
            char unescaped[LINE_BUFFER_MEDIUM];
            unescape_pattern(pattern, unescaped, sizeof(unescaped));

            /* COMMENTED OUT: Stopword/keyword filtering should happen at index time, not query time.
             * If a symbol exists in the database (e.g., in comments/strings), let users find it.
             * Uncomment below to restore blocking behavior if needed.
             */

            /* Resolve stopwords file path */
            /*
            char stopwords_file[PATH_MAX_LENGTH];
            char stopwords_path[PATH_MAX_LENGTH];
            snprintf(stopwords_file, sizeof(stopwords_file), "%s/%s", SHARED_CONFIG_DIR, STOPWORDS_FILENAME);
            if (resolve_data_file(stopwords_file, stopwords_path, sizeof(stopwords_path)) == 0) {
                if (is_in_filter_file(unescaped, stopwords_path)) {
                    fprintf(stderr, "\nNote: '%s' is in %s and is not indexed.\n", unescaped, stopwords_file);
                    //fprintf(stderr, "Common words like 'the', 'and', 'is', etc. are automatically filtered.\n");
                    fprintf(stderr, "To search anyway, use wildcards: './query-index \"%%%s%%\"'\n\n", unescaped);
                    retval = 1;
                    goto cleanup;
                }
            }
            */

            /* Check all language-specific keyword files */
            /*
            char c_keywords[PATH_MAX_LENGTH], ts_keywords[PATH_MAX_LENGTH];
            char php_keywords[PATH_MAX_LENGTH], go_keywords[PATH_MAX_LENGTH];
            snprintf(c_keywords, sizeof(c_keywords), "c/%s/%s", CONFIG_DIR, KEYWORDS_FILENAME);
            snprintf(ts_keywords, sizeof(ts_keywords), "typescript/%s/%s", CONFIG_DIR, KEYWORDS_FILENAME);
            snprintf(php_keywords, sizeof(php_keywords), "php/%s/%s", CONFIG_DIR, KEYWORDS_FILENAME);
            snprintf(go_keywords, sizeof(go_keywords), "go/%s/%s", CONFIG_DIR, KEYWORDS_FILENAME);

            const char *keyword_files[] = {
                c_keywords,
                ts_keywords,
                php_keywords,
                go_keywords,
                NULL
            };

            for (int k = 0; keyword_files[k] != NULL; k++) {
                char resolved_keyword_path[PATH_MAX_LENGTH];
                if (resolve_data_file(keyword_files[k], resolved_keyword_path, sizeof(resolved_keyword_path)) == 0) {
                    if (is_in_filter_file(unescaped, resolved_keyword_path)) {
                        fprintf(stderr, "\nNote: '%s' is in %s and is not indexed.\n", unescaped, keyword_files[k]);
                        fprintf(stderr, "Language keywords like 'class', 'function', 'const', etc. are automatically filtered.\n");
                        fprintf(stderr, "To search anyway, use wildcards: './query-index \"%%%s%%\"'\n\n", unescaped);
                        retval = 1;
                        goto cleanup;
                    }
                }
            }
            */

            /* Resolve regex patterns file path - warn but don't block */
            /*
            char regex_patterns_file[PATH_MAX_LENGTH];
            char regex_patterns_path[PATH_MAX_LENGTH];
            snprintf(regex_patterns_file, sizeof(regex_patterns_file), "%s/%s", SHARED_CONFIG_DIR, REGEX_PATTERNS_FILENAME);
            if (resolve_data_file(regex_patterns_file, regex_patterns_path, sizeof(regex_patterns_path)) == 0) {
                if (matches_regex_filter(unescaped, regex_patterns_path)) {
                    fprintf(stderr, "\nNote: '%s' matches a regex pattern in %s and is typically not indexed.\n", unescaped, regex_patterns_file);
                    fprintf(stderr, "Patterns like CSS units (10px), hex colors (#fff), ordinals (1st), etc. are automatically filtered.\n");
                    //fprintf(stderr, "To search anyway, use wildcards: './query-index \"%%%s%%\"'\n\n", unescaped);
                }
            }
            */
        }
    }

    /* Check if database exists (qi is read-only and should not create it) */
    struct stat st;
    if (stat(db_file, &st) != 0) {
        fprintf(stderr, "Error: Database file not found: %s\n", db_file);
        fprintf(stderr, "Tip: Run an indexer first (e.g., index-c ./src)\n");
        retval = 1;
        goto cleanup;
    }

    /* Open read-only: qi never writes, and the schema/index bootstrap in
       db_init() belongs only to the indexer (see indexer_main.c) */
    if (db_open_readonly(&db, db_file) != SQLITE_OK) {
        fprintf(stderr, "Failed to open database: %s\n", db_file);
        retval = 1;
        goto cleanup;
    }

    /* Load known file extensions for validation */
    FileExtensions known_extensions;
    if (load_all_language_extensions(&known_extensions) != 0) {
        /* If no extensions loaded, initialize empty (won't show warnings) */
        known_extensions.count = 0;
    }

    /* Lookup definitions for --within filter */
    if (within_filter.count > 0) {
        if (lookup_within_definitions(&db, &within_filter, &within_ranges, debug) != 0) {
            /* Error already printed by lookup function */
            retval = 1;
            goto cleanup;
        }
    }

    /* Print header (suppressed in raw and quiet modes) */
    if (!raw_mode && !quiet) {
        printf("Searching for:");
        for (int j = 0; j < patterns.count; j++) {
            printf(" %s", patterns.patterns[j]);
        }
        printf("\n");
        if (include.count > 0) {
            printf("Including context types:");
            for (int j = 0; j < include.count; j++) {
                printf(" %s", context_to_string(include.types[j], compact));
            }
            printf("\n");
        }
        if (exclude.count > 0) {
            printf("Excluding context types:");
            for (int j = 0; j < exclude.count; j++) {
                printf(" %s", context_to_string(exclude.types[j], compact));
            }
            printf("\n");
        }
        if (file_filter.count > 0) {
            /* Count and display number of files matching the filter */
            int file_count = count_distinct_files(&db, &include, &exclude, &filters, &file_filter, &within_ranges, debug);
            char file_word[16];
            if (file_count == 1) {
                snprintf(file_word, sizeof(file_word), "file");
            } else {
                pluralize_common_word("file", file_word, sizeof(file_word));
            }
            printf("Filtering by file: %d %s matched\n", file_count, file_word);

            /* Provide suggestions if no files matched */
            if (file_count == 0) {
                printf("\nNo files matched. Try:\n");
                printf("  -f filename.ext       Match specific filename (e.g., -f database.c)\n");
                printf("  -f .ext               Match by extension (e.g., -f .c for all .c files)\n");
                printf("  -f dir/               Match all files in directory (e.g., -f shared/)\n");
                printf("  -f %%pattern%%          Use %% wildcards (e.g., -f shared/%%.c)\n");
                printf("\n");
            }
        }
        /* X-Macro: Print extensible column filters */
#define COLUMN(name, sql_type, c_type, width, full, compact_name, long_flag, short_flag, ...) \
        if (filters.name.count > 0) { \
            printf("Filtering by " #name ":"); \
            for (int j = 0; j < filters.name.count; j++) { \
                printf(" %s", filters.name.values[j]); \
            } \
            printf("\n"); \
        }
#define INT_COLUMN(name, sql_type, c_type, width, full, compact_name, long_flag, short_flag, ...) \
        if (filters.name.count > 0) { \
            printf("Filtering by " #name ":"); \
            for (int j = 0; j < filters.name.count; j++) { \
                printf(" %s", filters.name.values[j]); \
            } \
            printf("\n"); \
        }
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN

        /* Print parent-type filter info */
        if (filters.parent_type.count > 0) {
            printf("Filtering by parent type:");
            for (int j = 0; j < filters.parent_type.count; j++) {
                printf(" %s", filters.parent_type.values[j]);
            }
            printf("\n");
        }

        /* Print within filter info */
        if (within_filter.count > 0) {
            char symbol_word[16];
            if (within_filter.count == 1) {
                snprintf(symbol_word, sizeof(symbol_word), "symbol");
            } else {
                pluralize_common_word("symbol", symbol_word, sizeof(symbol_word));
            }
            printf("Within %s:", symbol_word);
            for (int j = 0; j < within_filter.count; j++) {
                printf(" %s", within_filter.symbols[j]);
            }
            char instance_word[16];
            if (within_ranges.count == 1) {
                snprintf(instance_word, sizeof(instance_word), "instance");
            } else {
                pluralize_common_word("instance", instance_word, sizeof(instance_word));
            }
            printf(" (%d %s)\n", within_ranges.count, instance_word);
        }
    }

    /* Setup columns based on flags */
    if (!has_custom_columns) {
        setup_default_columns(verbose, &filters, &show_columns);
    }

    /* Validate that we have at least one column (unless using --columns all) */
    if (num_active_columns == 0 && !show_all_columns) {
        fprintf(stderr, "Error: No columns specified or all column names were invalid\n");
        retval = 1;
        goto cleanup;
    }

    /* Execute query */
    if (files_only) {
        print_files_only(&db, &patterns, &include, &exclude, &filters, &file_filter, &within_ranges, limit, line_range, debug);
    } else {
        print_results_by_file(&db, &patterns, &include, &exclude, &filters, &file_filter, &within_ranges, limit, limit_per_file, compact, line_range, expand, context_before, context_after, debug, show_all_columns, raw_mode, quiet, &known_extensions, &qname);
    }

cleanup:
    /* Cleanup: free all allocated memory (safe to call even if some allocations failed)
     * Note: free(NULL) is a no-op, so we don't need to check each pointer */
    db_close(&db);

    /* Free alternation-warning capture */
    free(alt_warning.original);
    free(alt_warning.alternatives);

    /* Free allocated patterns */
    for (int j = 0; j < patterns.count; j++) {
        free(patterns.patterns[j]);
    }

    /* Free allocated within filter symbols */
    for (int j = 0; j < within_filter.count; j++) {
        if (within_filter.symbols[j]) free(within_filter.symbols[j]);
    }

    /* Free allocated file filter values */
    for (int j = 0; j < file_filter.count; j++) {
        if (file_filter.patterns[j].directory) {
            free(file_filter.patterns[j].directory);
        }
        if (file_filter.patterns[j].filename) {
            free(file_filter.patterns[j].filename);
        }
    }

    /* X-Macro: Free allocated filter values */
#define COLUMN(name, ...) \
    for (int j = 0; j < filters.name.count; j++) { \
        free(filters.name.values[j]); \
    }
#define INT_COLUMN(name, ...) \
    for (int j = 0; j < filters.name.count; j++) { \
        free(filters.name.values[j]); \
    }
#include "shared/column_schema.def"
#undef COLUMN
#undef INT_COLUMN

    for (int j = 0; j < filters.parent_type.count; j++) {
        free(filters.parent_type.values[j]);
    }

    /* Free config args if we allocated a new argv */
    if (argv != original_argv) {
        /* Free config arg strings (everything after original CLI args) */
        for (int j = original_argc; j < argc; j++) {
            free(argv[j]);
        }
        free(argv);  /* Free the argv array itself */
    }

    return retval;
}

/* test */
