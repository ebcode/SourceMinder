/*
 * qi-web-entry.c -- Browser/WASM bridge for the qi query tool.
 *
 * NO sqlite3 linked.  JS owns the DB (@sqlite.org/sqlite-wasm).
 * This module builds SQL and formats qi-style output from raw result rows.
 *
 * Exports:
 *   qi_web_build(command)            -> build-info string (SQL, patterns, limit)
 *   qi_web_format(build_info, rows_tsv, total, shown) -> formatted qi output
 *   qi_web_format_breakdown(tsv)     -> "Result breakdown: ..." + Tip line
 *   qi_web_format_files(tsv, shown, total) -> file list + "Found N files"
 *   qi_web_free_result(ptr)          -> free a result string
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#include <emscripten.h>
#include "query-index-web.h"
#include "shared/sql_builder.h"
#include "shared-web/toc-web.h"
#include "shared-web/source-render-web.h"

/* Forward declarations for sqlite3 shim (defined in query-index-web.c, linked together) */
char *sqlite3_mprintf(const char *fmt, ...);
void sqlite3_free(void *ptr);

/* Output accumulator shared with toc-web.c and source-render-web.c. */
#include "web_output.h"

/* -- Tokenizer (mirrors html/app.js tokenizeCommand) -- */

static char **tokenize(const char *input, int *out_count) {
    int cap = 8;
    char **tokens = malloc((size_t)cap * sizeof(char *));
    if (!tokens) { *out_count = 0; return NULL; }
    int count = 0;

    const char *p = input;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;

        char quote = 0;
        char buf[4096];
        int bi = 0;

        if (*p == '"' || *p == '\'') {
            quote = *p++;
        }

        while (*p) {
            if (quote) {
                if (*p == '\\' && p[1]) { buf[bi++] = p[1]; p += 2; continue; }
                if (*p == quote) { p++; break; }
                buf[bi++] = *p++;
            } else {
                if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') break;
                if (*p == '\\' && p[1]) { buf[bi++] = p[1]; p += 2; continue; }
                buf[bi++] = *p++;
            }
            if (bi >= 4095) break;
        }
        buf[bi] = '\0';

        if (count >= cap) {
            cap *= 2;
            char **nt = realloc(tokens, (size_t)cap * sizeof(char *));
            if (!nt) { *out_count = count; return tokens; }
            tokens = nt;
        }
        tokens[count++] = strdup(buf);
    }

    *out_count = count;
    return tokens;
}

static void free_tokens(char **tokens, int count) {
    for (int i = 0; i < count; i++) free(tokens[i]);
    free(tokens);
}

/* -- Context alias mapping (mirrors html/app.js CONTEXT_ALIASES) -- */

/* -- Command parser -- */

typedef struct {
    char *values[MAX_CONTEXT_TYPES];
    int count;
    int show;
} WebColFilter;

typedef struct {
    WebColFilter parent;
    WebColFilter scope;
    WebColFilter ns;    /* namespace */
    WebColFilter modifier;
    WebColFilter clue;
    WebColFilter type;
    WebColFilter definition;
} WebColFlags;

typedef struct {
    char *patterns[MAX_PATTERNS];
    int pattern_count;
    char *includes[MAX_CONTEXT_TYPES];
    int include_count;
    char *excludes[MAX_CONTEXT_TYPES];
    int exclude_count;
    char *files[MAX_CONTEXT_TYPES];
    int file_count;
    int definition; /* -1=none, 0=usage, 1=def */
    int limit;
    int verbose;
    int compact;
    int debug;
    char *column_names[MAX_CONTEXT_TYPES];
    int column_count;
    WebColFlags cf;
    int line_range;               /* -1=none, 0=and-same-line, >0=and-with-range */
    char *within_symbols[MAX_PATTERNS];  /* --within symbols */
    int within_count;
    int error;
    char *error_msg;
    int error_msg_malloced;  /* 1 if error_msg was strdup'd and must be freed */
    int oom;    /* set when any strdup fails during parsing */
    int toc_mode;
    int files_mode;      /* --files: show only unique file paths */
    int expand;          /* -e/--expand: expand full definitions */
    int context_before;  /* -B / -C: lines of context before each match */
    int context_after;   /* -A / -C: lines of context after each match */
    int raw;             /* --raw: bare source only, suppress all framing */
} WebCommand;

static void free_col_filter(WebColFilter *f) {
    for (int i = 0; i < f->count; i++) free(f->values[i]);
    f->count = 0;
    f->show = 0;
}

/* Allocate a copy of s; on failure, mark the command as OOM and return NULL.
 * Wrapped strdup calls in parse_command pass through here so a single NULL
 * check at the done: label catches all allocation failures without
 * littering individual NULL checks everywhere. */
static char *cmd_strdup(WebCommand *cmd, const char *s) {
    char *p = strdup(s);
    if (!p) cmd->oom = 1;
    return p;
}

/* Set an error message on the command.  error_msg_malloced tracks whether
 * the pointer came from strdup and must be freed. */
#define SET_CMD_ERROR(cmd, s) do { \
    (cmd)->error = 1; \
    (cmd)->error_msg = strdup(s); \
    (cmd)->error_msg_malloced = 1; \
} while(0)

static void free_command(WebCommand *cmd) {
    for (int i = 0; i < cmd->pattern_count; i++) free(cmd->patterns[i]);
    for (int i = 0; i < cmd->include_count; i++) free(cmd->includes[i]);
    for (int i = 0; i < cmd->exclude_count; i++) free(cmd->excludes[i]);
    for (int i = 0; i < cmd->file_count; i++) free(cmd->files[i]);
    for (int i = 0; i < cmd->column_count; i++) free(cmd->column_names[i]);
    for (int i = 0; i < cmd->within_count; i++) free(cmd->within_symbols[i]);
    if (cmd->error_msg_malloced) free(cmd->error_msg);
    free_col_filter(&cmd->cf.parent);
    free_col_filter(&cmd->cf.scope);
    free_col_filter(&cmd->cf.ns);
    free_col_filter(&cmd->cf.modifier);
    free_col_filter(&cmd->cf.clue);
    free_col_filter(&cmd->cf.type);
    free_col_filter(&cmd->cf.definition);
    memset(cmd, 0, sizeof(*cmd));
    cmd->definition = -1;
    cmd->limit = 25;
    cmd->line_range = -1;
    cmd->toc_mode = 0;
}

static int is_flag(const char *token) {
    return token[0] == '-';
}

/* Parse values for a column filter flag.  Always sets show=1, then
 * collects any following non-flag tokens as filter values. */
static void parse_col_flag_values(WebColFilter *cf, char **tokens, int tc, int *i,
                                   WebCommand *cmd) {
    cf->show = 1;
    while (*i + 1 < tc && !is_flag(tokens[*i + 1])) {
        (*i)++;
        if (cf->count < MAX_CONTEXT_TYPES)
            cf->values[cf->count++] = cmd_strdup(cmd, tokens[*i]);
    }
}

/* Parse the optional NUM after -A/-B/-C: an integer in [0, MAXIMUM_CONTEXT_RANGE],
 * or DEFAULT_CONTEXT_RANGE when absent (mirrors the native CLI).  Consumes the
 * number token when present.  On an invalid/out-of-range value, sets the command
 * error and returns -1. */
static int parse_context_value(char **tokens, int tc, int *i, WebCommand *cmd) {
    if (*i + 1 < tc && !is_flag(tokens[*i + 1])) {
        const char *arg = tokens[*i + 1];
        int valid = (arg[0] != '\0');
        for (int ci = 0; arg[ci]; ci++)
            if (!isdigit((unsigned char)arg[ci])) { valid = 0; break; }
        if (!valid) {
            SET_CMD_ERROR(cmd, "context flag requires a non-negative integer (0-100).");
            return -1;
        }
        int val = atoi(arg);
        if (val > MAXIMUM_CONTEXT_RANGE) {
            SET_CMD_ERROR(cmd, "context value cannot exceed 100.");
            return -1;
        }
        (*i)++;
        return val;
    }
    return DEFAULT_CONTEXT_RANGE;
}

static WebCommand parse_command(const char *input) {
    WebCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.definition = -1;
    cmd.limit = 25;
    cmd.compact = 1;
    cmd.line_range = -1;

    int tc = 0;
    char **tokens = tokenize(input, &tc);
    if (!tokens || tc == 0) {
        cmd.error = 1;
        SET_CMD_ERROR(&cmd, "Empty command.");
        return cmd;
    }

    /* The leading "qi" is mandatory: the command list mirrors a real qi
     * invocation, so the first token must name the program. */
    if (strcmp(tokens[0], "qi") != 0) {
        SET_CMD_ERROR(&cmd, "Commands must start with 'qi'. Example: qi malloc -f foo.c");
        goto done;
    }
    int i = 1;

    while (i < tc) {
        const char *t = tokens[i];

        if (!is_flag(t)) {
            if (cmd.pattern_count >= MAX_PATTERNS) {
                cmd.error = 1;
                SET_CMD_ERROR(&cmd, "Too many patterns.");
                goto done;
            }
            cmd.patterns[cmd.pattern_count++] = cmd_strdup(&cmd, t);
            i++;
            continue;
        }

        if (strcmp(t, "--def") == 0) {
            cmd.definition = 1;
            cmd.cf.definition.show = 1;
            i++;
            continue;
        }
        if (strcmp(t, "--usage") == 0) {
            cmd.definition = 0;
            cmd.cf.definition.show = 1;
            i++;
            continue;
        }
        if (strcmp(t, "-v") == 0 || strcmp(t, "--verbose") == 0) {
            cmd.verbose = 1;
            i++;
            continue;
        }
        if (strcmp(t, "--compact") == 0) {
            cmd.compact = 1;
            i++;
            continue;
        }
        if (strcmp(t, "--debug") == 0) {
            cmd.debug = 1;
            i++;
            continue;
        }
        if (strcmp(t, "--toc") == 0) {
            cmd.toc_mode = 1;
            i++;
            continue;
        }
        if (strcmp(t, "--files") == 0) {
            cmd.files_mode = 1;
            i++;
            continue;
        }
        if (strcmp(t, "--and") == 0) {
            if (cmd.pattern_count < 2) {
                cmd.error = 1;
                SET_CMD_ERROR(&cmd, "--and requires at least 2 search patterns. Example: qi malloc free --and 10");
                goto done;
            }
            if (i + 1 < tc && !is_flag(tokens[i + 1])) {
                /* Validate the range argument is numeric */
                const char *arg = tokens[i + 1];
                int valid = 1;
                for (int ci = 0; arg[ci]; ci++) {
                    if (!isdigit((unsigned char)arg[ci])) { valid = 0; break; }
                }
                if (!valid) {
                    cmd.error = 1;
                    SET_CMD_ERROR(&cmd, "--and range must be a positive integer.");
                    goto done;
                }
                cmd.line_range = atoi(arg);
                if (cmd.line_range < 0) cmd.line_range = 0;
                i += 2;
            } else {
                cmd.line_range = 0;
                i++;
            }
            continue;
        }
        if (strcmp(t, "-w") == 0 || strcmp(t, "--within") == 0) {
            i++;
            while (i < tc && !is_flag(tokens[i])) {
                if (cmd.within_count < MAX_PATTERNS) {
                    cmd.within_symbols[cmd.within_count++] = cmd_strdup(&cmd, tokens[i]);
                }
                i++;
            }
            continue;
        }

        if (strcmp(t, "--columns") == 0) {
            i++;
            while (i < tc && !is_flag(tokens[i])) {
                if (cmd.column_count < MAX_CONTEXT_TYPES) {
                    cmd.column_names[cmd.column_count++] = cmd_strdup(&cmd, tokens[i]);
                }
                i++;
            }
            continue;
        }

        if (strcmp(t, "--limit") == 0) {
            if (i + 1 >= tc) {
                cmd.error = 1;
                SET_CMD_ERROR(&cmd, "--limit requires a number.");
                goto done;
            }
            {
                const char *arg = tokens[i + 1];
                int valid = 1;
                for (int ci = 0; arg[ci]; ci++) {
                    if (!isdigit((unsigned char)arg[ci])) { valid = 0; break; }
                }
                if (!valid) {
                    cmd.error = 1;
                    SET_CMD_ERROR(&cmd, "--limit must be a positive integer.");
                    goto done;
                }
                cmd.limit = atoi(arg);
            }
            if (cmd.limit <= 0) {
                cmd.error = 1;
                SET_CMD_ERROR(&cmd, "--limit must be positive.");
                goto done;
            }
            i += 2;
            continue;
        }

        if (strcmp(t, "-i") == 0 || strcmp(t, "--include-context") == 0 ||
            strcmp(t, "-x") == 0 || strcmp(t, "--exclude-context") == 0 ||
            strcmp(t, "-f") == 0 || strcmp(t, "--file") == 0) {

            int is_include = (strcmp(t, "-i") == 0 || strcmp(t, "--include-context") == 0);
            int is_exclude = (strcmp(t, "-x") == 0 || strcmp(t, "--exclude-context") == 0);

            i++;
            while (i < tc && !is_flag(tokens[i])) {
                const char *val = tokens[i];
                if (is_include || is_exclude) {
                    if (strcasecmp(val, "noise") == 0) {
                        if (is_include) {
                            if (cmd.include_count + 2 <= MAX_CONTEXT_TYPES) {
                                cmd.includes[cmd.include_count++] = cmd_strdup(&cmd, "COM");
                                cmd.includes[cmd.include_count++] = cmd_strdup(&cmd, "STR");
                            }
                        } else {
                            if (cmd.exclude_count + 2 <= MAX_CONTEXT_TYPES) {
                                cmd.excludes[cmd.exclude_count++] = cmd_strdup(&cmd, "COM");
                                cmd.excludes[cmd.exclude_count++] = cmd_strdup(&cmd, "STR");
                            }
                        }
                    } else {
                        const char *mapped = map_context_web(val);
                        if (!mapped) {
                            cmd.error = 1;
                            SET_CMD_ERROR(&cmd, "Unknown context type.");
                            goto done;
                        }
                        if (is_include) {
                            if (cmd.include_count < MAX_CONTEXT_TYPES)
                                cmd.includes[cmd.include_count++] = cmd_strdup(&cmd, mapped);
                        } else {
                            if (cmd.exclude_count < MAX_CONTEXT_TYPES)
                                cmd.excludes[cmd.exclude_count++] = cmd_strdup(&cmd, mapped);
                        }
                    }
                } else {
                    if (cmd.file_count < MAX_CONTEXT_TYPES)
                        cmd.files[cmd.file_count++] = cmd_strdup(&cmd, val);
                }
                i++;
            }
            continue;
        }

        /* Column filter flags: -p/--parent, -s/--scope, -ns/--namespace,
         * -m/--modifier, -c/--clue, -t/--type, -d/--definition */
        if (strcmp(t, "-p") == 0 || strcmp(t, "--parent") == 0) {
            parse_col_flag_values(&cmd.cf.parent, tokens, tc, &i, &cmd);
            i++; continue;
        }
        if (strcmp(t, "-s") == 0 || strcmp(t, "--scope") == 0) {
            parse_col_flag_values(&cmd.cf.scope, tokens, tc, &i, &cmd);
            i++; continue;
        }
        if (strcmp(t, "-ns") == 0 || strcmp(t, "--namespace") == 0) {
            parse_col_flag_values(&cmd.cf.ns, tokens, tc, &i, &cmd);
            i++; continue;
        }
        if (strcmp(t, "-m") == 0 || strcmp(t, "--modifier") == 0) {
            parse_col_flag_values(&cmd.cf.modifier, tokens, tc, &i, &cmd);
            i++; continue;
        }
        if (strcmp(t, "-c") == 0 || strcmp(t, "--clue") == 0) {
            parse_col_flag_values(&cmd.cf.clue, tokens, tc, &i, &cmd);
            i++; continue;
        }
        if (strcmp(t, "-t") == 0 || strcmp(t, "--type") == 0) {
            parse_col_flag_values(&cmd.cf.type, tokens, tc, &i, &cmd);
            i++; continue;
        }
        if (strcmp(t, "-d") == 0 || strcmp(t, "--definition") == 0) {
            parse_col_flag_values(&cmd.cf.definition, tokens, tc, &i, &cmd);
            i++; continue;
        }

        /* Source-backed flags (-e/-C/-A/-B) and --raw */
        if (strcmp(t, "-e") == 0 || strcmp(t, "--expand") == 0) {
            cmd.expand = 1;
            i++; continue;
        }
        if (strcmp(t, "--raw") == 0) {
            cmd.raw = 1;
            i++; continue;
        }
        if (strcmp(t, "-A") == 0 || strcmp(t, "--after-context") == 0) {
            int v = parse_context_value(tokens, tc, &i, &cmd);
            if (cmd.error) goto done;
            cmd.context_after = v;
            i++; continue;
        }
        if (strcmp(t, "-B") == 0 || strcmp(t, "--before-context") == 0) {
            int v = parse_context_value(tokens, tc, &i, &cmd);
            if (cmd.error) goto done;
            cmd.context_before = v;
            i++; continue;
        }
        if (strcmp(t, "-C") == 0 || strcmp(t, "--context") == 0) {
            int v = parse_context_value(tokens, tc, &i, &cmd);
            if (cmd.error) goto done;
            cmd.context_before = cmd.context_after = v;
            i++; continue;
        }

        /* Unknown flag — report error instead of silently ignoring */
        {
            char errbuf[256];
            snprintf(errbuf, sizeof(errbuf), "Unknown flag: %s", t);
            errbuf[sizeof(errbuf) - 1] = '\0';
            cmd.error = 1;
            cmd.error_msg = strdup(errbuf);
            cmd.error_msg_malloced = 1;
            if (!cmd.error_msg) {
                cmd.error_msg = strdup("Unknown flag.");
                cmd.error_msg_malloced = 1;
            }
            goto done;
        }
    }

done:
    free_tokens(tokens, tc);

    if (cmd.oom) {
        cmd.error = 1;
        /* Literal string — not strdup'd, must not be freed */
        cmd.error_msg = "out of memory";
        cmd.error_msg_malloced = 0;
    }
    if (!cmd.error && cmd.pattern_count == 0) {
        cmd.error = 1;
        SET_CMD_ERROR(&cmd, "At least one search pattern is required.");
    }
    return cmd;
}

/* =================================================================
 * Exported API 1: build SQL from command text
 * Returns: "PATTERNS|p1 p2\nSQL|...\nLIMIT|20\nERROR|OK"
 *   or:   "ERROR|message"
 * ================================================================= */

EMSCRIPTEN_KEEPALIVE
char *qi_web_build(const char *command) {
    WebOutput wo;
    if (wo_init(&wo) != 0) return strdup("ERROR|out of memory");

    if (!command || !command[0]) {
        wo_free(&wo);
        return strdup("ERROR|Empty command.");
    }

    WebCommand cmd = parse_command(command);

    /* In TOC mode patterns are optional symbol filters -- override the
     * "no patterns" error from parse_command. */
    if (cmd.toc_mode && cmd.error && cmd.error_msg &&
        strcmp(cmd.error_msg, "At least one search pattern is required.") == 0) {
        if (cmd.error_msg_malloced) { free(cmd.error_msg); cmd.error_msg_malloced = 0; }
        cmd.error_msg = NULL;
        cmd.error = 0;
    }

    if (cmd.error) {
        wo_printf(&wo, "ERROR|%s", cmd.error_msg);
        free_command(&cmd);
        { char *r = wo_steal(&wo); return r ? r : strdup("ERROR|out of memory"); }
    }

    /* -- TOC mode: build TOC SQL and return early -- */
    if (cmd.toc_mode) {
        if (cmd.file_count == 0) {
            wo_printf(&wo, "ERROR|--toc requires -f <file_pattern>");
            free_command(&cmd);
            { char *r = wo_steal(&wo); return r ? r : strdup("ERROR|out of memory"); }
        }

        /* Build file patterns (normalized LIKE patterns) */
        TocWebFilePattern *toc_fps = malloc(sizeof(TocWebFilePattern) * (size_t)cmd.file_count);
        if (!toc_fps) {
            wo_printf(&wo, "ERROR|out of memory");
            free_command(&cmd);
            { char *r = wo_steal(&wo); return r ? r : strdup("ERROR|out of memory"); }
        }
        int toc_fp_count = 0;
        for (int i = 0; i < cmd.file_count; i++) {
            char *dir = NULL, *file = NULL;
            if (process_file_pattern_web(cmd.files[i], &dir, &file) == 0 && file) {
                toc_fps[toc_fp_count].directory = dir;
                toc_fps[toc_fp_count].filename = file;
                toc_fp_count++;
            }
        }

        /* Build symbol patterns (wildcard-converted) */
        const char **sym_pats = NULL;
        int sym_pat_count = 0;
        int sym_pat_error = 0;
        if (cmd.pattern_count > 0) {
            sym_pats = malloc(sizeof(const char *) * (size_t)cmd.pattern_count);
            if (!sym_pats) {
                sym_pat_error = 1;
            } else {
                for (int i = 0; i < cmd.pattern_count; i++) {
                    char *conv = malloc(SYMBOL_MAX_LENGTH);
                    if (!conv) {
                        sym_pat_error = 1;
                        break;
                    }
                    convert_wildcards_web(cmd.patterns[i], conv, SYMBOL_MAX_LENGTH);
                    sym_pats[i] = conv;
                    sym_pat_count++;
                }
            }
            if (sym_pat_error) {
                for (int i = 0; i < sym_pat_count; i++)
                    free((void *)sym_pats[i]);
                free(sym_pats);
                sym_pats = NULL;
                sym_pat_count = 0;
            }
        }
        if (sym_pat_error) {
            for (int i = 0; i < toc_fp_count; i++) {
                free((void *)toc_fps[i].directory);
                free((void *)toc_fps[i].filename);
            }
            free(toc_fps);
            wo_printf(&wo, "ERROR|out of memory");
            free_command(&cmd);
            { char *r = wo_steal(&wo); return r ? r : strdup("ERROR|out of memory"); }
        }

        TocWebConfig config = {
            .file_patterns       = toc_fps,
            .file_pattern_count  = toc_fp_count,
            .symbol_patterns     = (const char **)sym_pats,
            .symbol_pattern_count = sym_pat_count,
            .include_contexts    = (const char **)cmd.includes,
            .include_context_count = cmd.include_count,
            .limit               = cmd.limit,
        };

        char *toc_sql = build_toc_web_sql(&config);

        /* Emit build_info */
        wo_printf(&wo, "MODE|toc\n");
        wo_printf(&wo, "PATTERNS|");
        for (int i = 0; i < cmd.pattern_count; i++) {
            if (i > 0) wo_printf(&wo, " ");
            wo_printf(&wo, "%s", cmd.patterns[i]);
        }
        if (toc_sql) {
            wo_printf(&wo, "\nTOC_SQL|%s", toc_sql);
            /* COUNT breakdown SQL */
            wo_printf(&wo, "\nTOC_COUNT_SQL|SELECT context, COUNT(*) FROM (%s) "
                             "GROUP BY context", toc_sql);
            free(toc_sql);
        } else {
            wo_printf(&wo, "\nTOC_SQL|");
            wo_printf(&wo, "\nTOC_COUNT_SQL|");
        }
        wo_printf(&wo, "\nLIMIT|%d", cmd.limit);
        if (cmd.include_count > 0) {
            wo_printf(&wo, "\nTOC_INCLUDES|");
            for (int i = 0; i < cmd.include_count; i++) {
                if (i > 0) wo_printf(&wo, " ");
                wo_printf(&wo, "%s", cmd.includes[i]);
            }
        }
        wo_printf(&wo, "\nERROR|OK");

        /* Cleanup */
        for (int i = 0; i < toc_fp_count; i++) {
            free((void *)toc_fps[i].directory);
            free((void *)toc_fps[i].filename);
        }
        free(toc_fps);
        if (sym_pats) {
            for (int i = 0; i < sym_pat_count; i++) free((void *)sym_pats[i]);
            free(sym_pats);
        }
        free_command(&cmd);
        { char *r = wo_steal(&wo); return r ? r : strdup("ERROR|out of memory"); }
    }

    /* Build PatternList (with wildcard conversion) */
    PatternList patterns;
    patterns.count = cmd.pattern_count;
    for (int i = 0; i < cmd.pattern_count; i++) {
        char converted[SYMBOL_MAX_LENGTH];
        convert_wildcards_web(cmd.patterns[i], converted, sizeof(converted));
        patterns.patterns[i] = strdup(converted);
    }

    /* Build ContextTypeLists */
    ContextTypeList include = {0};
    ContextTypeList exclude = {0};
    for (int i = 0; i < cmd.include_count; i++) {
        char upper[CONTEXT_TYPE_MAX_LENGTH];
        snprintf(upper, sizeof(upper), "%s", cmd.includes[i]);
        to_upper(upper);
        include.types[i] = string_to_context(upper);
        include.count++;
    }
    for (int i = 0; i < cmd.exclude_count; i++) {
        char upper[CONTEXT_TYPE_MAX_LENGTH];
        snprintf(upper, sizeof(upper), "%s", cmd.excludes[i]);
        to_upper(upper);
        exclude.types[i] = string_to_context(upper);
        exclude.count++;
    }

    /* Build FileFilterList */
    FileFilterList file_filter = {0};
    for (int i = 0; i < cmd.file_count && i < MAX_CONTEXT_TYPES; i++) {
        char *dir = NULL, *file = NULL;
        if (process_file_pattern_web(cmd.files[i], &dir, &file) == 0 && file) {
            file_filter.patterns[file_filter.count].directory = dir;
            file_filter.patterns[file_filter.count].filename = file;
            file_filter.count++;
        }
    }

    /* Build QueryFilters */
    QueryFilters filters;
    memset(&filters, 0, sizeof(filters));
    filters.line_start = -1;
    filters.line_end = -1;

    /* Populate extensible column filters from parsed flags */
#define POPULATE_COL_FILTER(field, src) do { \
    for (int _j = 0; _j < (src).count; _j++) { \
        if ((field).count < MAX_CONTEXT_TYPES) { \
            char _conv[SYMBOL_MAX_LENGTH]; \
            convert_wildcards_web((src).values[_j], _conv, sizeof(_conv)); \
            (field).values[(field).count++] = strdup(_conv); \
        } \
    } \
} while(0)
    POPULATE_COL_FILTER(filters.parent_symbol, cmd.cf.parent);
    POPULATE_COL_FILTER(filters.scope, cmd.cf.scope);
    POPULATE_COL_FILTER(filters.namespace, cmd.cf.ns);
    POPULATE_COL_FILTER(filters.modifier, cmd.cf.modifier);
    POPULATE_COL_FILTER(filters.clue, cmd.cf.clue);
    POPULATE_COL_FILTER(filters.type, cmd.cf.type);
    POPULATE_COL_FILTER(filters.is_definition, cmd.cf.definition);
#undef POPULATE_COL_FILTER

    /* -- Files mode: SELECT DISTINCT directory, filename with same WHERE clause -- */
    if (cmd.files_mode) {
        SqlQueryBuilder builder;
        if (init_sql_builder(&builder) != 0) {
            wo_printf(&wo, "ERROR|SQL builder init failed");
            goto cleanup;
        }
        if (sql_append(&builder, "SELECT DISTINCT directory, filename FROM code_index WHERE (") != 0 ||
            build_query_filters_web(&builder, &patterns, &include, &exclude,
                                    &filters, &file_filter, NULL, cmd.line_range, cmd.debug) != 0 ||
            sql_append(&builder, " ORDER BY directory, filename") != 0) {
            wo_printf(&wo, "ERROR|SQL build failed");
            free_sql_builder(&builder);
            goto cleanup;
        }
        wo_printf(&wo, "MODE|files\nPATTERNS|");
        for (int i = 0; i < cmd.pattern_count; i++) {
            if (i > 0) wo_printf(&wo, " ");
            wo_printf(&wo, "%s", cmd.patterns[i]);
        }
        wo_printf(&wo, "\nFILES_SQL|%s", builder.sql);
        wo_printf(&wo, "\nLIMIT|%d\nERROR|OK", cmd.limit);
        free_sql_builder(&builder);
        goto cleanup;
    }

    /* Build SQL */
    SqlQueryBuilder builder;
    if (init_sql_builder(&builder) != 0) {
        wo_printf(&wo, "ERROR|SQL builder init failed");
        goto cleanup;
    }

    if (build_query_sql_web(&builder, &patterns, &include, &exclude,
                            &filters, &file_filter, NULL, cmd.line_range, cmd.debug) != 0) {
        wo_printf(&wo, "ERROR|SQL build failed");
        free_sql_builder(&builder);
        goto cleanup;
    }

    /* Inject --def/--usage filter */
    if (cmd.definition >= 0) {
        char *order_pos = strstr(builder.sql, "ORDER BY");
        if (order_pos) {
            char *saved = strdup(order_pos);
            if (!saved) {
                wo_printf(&wo, "ERROR|out of memory");
                goto cleanup;
            }
            *order_pos = '\0';
            builder.offset = (int)(order_pos - builder.sql);
            sql_append(&builder, " AND is_definition = %d ", cmd.definition);
            sql_append(&builder, "%s", saved);
            free(saved);
        }
    }

    /* Build output: PATTERNS line, COUNT_SQL line, SQL line, LIMIT line, metadata, ERROR line */
    wo_printf(&wo, "PATTERNS|");
    for (int i = 0; i < cmd.pattern_count; i++) {
        if (i > 0) wo_printf(&wo, " ");
        wo_printf(&wo, "%s", cmd.patterns[i]);
    }

    /* Derive COUNT_SQL and BREAKDOWN_SQL from main SQL: strip ORDER BY, swap SELECT */
    {
        const char *select_star = "SELECT * ";
        const char *star = strstr(builder.sql, select_star);
        const char *order = star ? strstr(builder.sql, "ORDER BY") : NULL;
        if (star && order && order > star) {
            size_t after_star = (size_t)((star - builder.sql) + strlen(select_star));
            size_t where_len = (size_t)(order - (builder.sql + after_star));
            wo_printf(&wo, "\nCOUNT_SQL|SELECT COUNT(*) %.*s",
                (int)where_len, builder.sql + after_star);
            /* Proximity queries alias the table as "ci"; use qualified column name */
            int has_alias = (strstr(builder.sql, "code_index ci") != NULL);
            const char *ctx = has_alias ? "ci.context" : "context";
            wo_printf(&wo, "\nBREAKDOWN_SQL|SELECT %s, COUNT(*) as cnt %.*s"
                           "GROUP BY %s ORDER BY cnt DESC",
                ctx, (int)where_len, builder.sql + after_star, ctx);
        }
    }

    wo_printf(&wo, "\nSQL|%s\nLIMIT|%d", builder.sql, cmd.limit);
    if (cmd.verbose)
        wo_printf(&wo, "\nVERBOSE|1");
    wo_printf(&wo, "\nCOMPACT|%d", cmd.compact);
    if (cmd.debug)
        wo_printf(&wo, "\nDEBUG|1");
    /* Source-backed flags: the worker fetches files when NEEDS_SOURCE is set,
     * and qi_web_format renders -e/-C/-A/-B/--raw from the fetched content.
     * --raw alone fetches nothing (it only modifies -e/-C/-A/-B rendering). */
    if (cmd.expand || cmd.context_before > 0 || cmd.context_after > 0)
        wo_printf(&wo, "\nNEEDS_SOURCE|1");
    if (cmd.expand)
        wo_printf(&wo, "\nEXPAND|1");
    if (cmd.context_before > 0)
        wo_printf(&wo, "\nCONTEXT_BEFORE|%d", cmd.context_before);
    if (cmd.context_after > 0)
        wo_printf(&wo, "\nCONTEXT_AFTER|%d", cmd.context_after);
    if (cmd.raw)
        wo_printf(&wo, "\nRAW|1");
    wo_printf(&wo, "\nCOLUMNS|");
    if (cmd.column_count > 0) {
        for (int i = 0; i < cmd.column_count; i++) {
            if (i > 0) wo_printf(&wo, " ");
            wo_printf(&wo, "%s", cmd.column_names[i]);
        }
    } else {
        wo_printf(&wo, "line symbol");
#define SHOW_IF(flag, col_name, verbose_col) \
    if (cmd.verbose || (flag).show) wo_printf(&wo, " " col_name);
        SHOW_IF(cmd.cf.parent,     "parent",     1);
        SHOW_IF(cmd.cf.scope,       "scope",       1);
        SHOW_IF(cmd.cf.ns,          "namespace",   1);
        SHOW_IF(cmd.cf.modifier,    "modifier",    1);
        SHOW_IF(cmd.cf.clue,        "clue",        1);
        SHOW_IF(cmd.cf.type,        "type",        1);
        SHOW_IF(cmd.cf.definition,  "definition",  1);
#undef SHOW_IF
        wo_printf(&wo, " context");
    }
    wo_printf(&wo, "\nERROR|OK");

    if (cmd.within_count > 0) {
        wo_printf(&wo, "\nWITHIN_SYMBOLS|");
        for (int i = 0; i < cmd.within_count; i++) {
            if (i > 0) wo_printf(&wo, " ");
            wo_printf(&wo, "%s", cmd.within_symbols[i]);
        }

        SqlQueryBuilder wb;
        if (init_sql_builder(&wb) == 0) {
            for (int i = 0; i < cmd.within_count; i++) {
                if (i > 0) sql_append(&wb, " UNION ALL ");
                char lower[SYMBOL_MAX_LENGTH];
                {
                    int li = 0;
                    const char *src = cmd.within_symbols[i];
                    while (src[li] && li < (int)sizeof(lower) - 1) {
                        lower[li] = (char)tolower((unsigned char)src[li]);
                        li++;
                    }
                    lower[li] = '\0';
                }
                char *escaped = sqlite3_mprintf("%q", lower);
                if (escaped) {
                    sql_append(&wb,
                        "SELECT directory, filename, source_location, %s FROM code_index "
                        "WHERE symbol = %s AND is_definition = 1 AND source_location IS NOT NULL",
                        escaped, escaped);
                    sqlite3_free(escaped);
                }
            }
            wo_printf(&wo, "\nWITHIN_SQL|%s", wb.sql);
            free_sql_builder(&wb);
        }
    }

    if (cmd.line_range >= 0)
        wo_printf(&wo, "\nLINE_RANGE|%d", cmd.line_range);

    free_sql_builder(&builder);

cleanup:
    /* Free allocations */
    for (int i = 0; i < file_filter.count; i++) {
        free(file_filter.patterns[i].directory);
        free(file_filter.patterns[i].filename);
    }
    for (int i = 0; i < patterns.count; i++) {
        free(patterns.patterns[i]);
    }
    free_command(&cmd);
    { char *r = wo_steal(&wo); return r ? r : strdup("ERROR|out of memory"); }
}

/* =================================================================
 * Exported API 2: format raw rows as qi-style output
 *
 * build_info: output from qi_web_build (contains PATTERNS|, COLUMNS|,
 *             COMPACT|, VERBOSE|, LIMIT|)
 * rows_tsv:   tab-separated rows, one per line, all 14 DB columns
 *             canonical order: symbol|directory|filename|line|context|
 *             full_symbol|source_location|parent_symbol|scope|
 *             namespace|modifier|clue|type|is_definition
 * total:      total match count
 * shown:      number of rows actually shown
 * Returns:    malloc'd qi output string
 * ================================================================= */

/* Column spec for web output - mirrors CLI column_registry */
typedef struct {
    const char *name;           /* CLI name: "line", "symbol", "parent", etc. */
    const char *header;         /* Full header: "LINE", "SYMBOL", "PARENT" */
    const char *header_compact; /* Compact header: "LINE", "SYM", "PAR" */
    int tsv_index;              /* Index into the 14-field canonical TSV */
    int default_width;          /* Minimum column width */
    int is_int;                 /* 1 = integer column */
} WebColSpec;

#define MAX_WEB_COLS 16
#define TSV_FIELDS 14

static const WebColSpec web_col_registry[] = {
    {"line",       "LINE",     "LINE",   3,  4, 0},
    {"context",    "CONTEXT",  "CTX",    4,  7, 0},
    {"symbol",     "SYMBOL",   "SYM",    5,  0, 0},
    {"parent",     "PARENT",   "PAR",    7,  8, 0},
    {"scope",      "SCOPE",    "SCOPE",  8,  8, 0},
    {"namespace",  "NAMESPACE","NS",     9, 10, 0},
    {"modifier",   "MODIFIER", "MOD",   10,  8, 0},
    {"clue",       "CLUE",     "CLUE",  11,  8, 0},
    {"type",       "TYPE",     "TYPE",  12, 20, 0},
    {"definition", "DEF",      "D",     13,  1, 1},
    {NULL, NULL, NULL, 0, 0, 0}
};

static const WebColSpec *find_web_col(const char *name) {
    for (int i = 0; web_col_registry[i].name; i++) {
        if (strcmp(web_col_registry[i].name, name) == 0)
            return &web_col_registry[i];
    }
    return NULL;
}

static const WebColSpec *find_web_col_by_alias(const char *name) {
    const WebColSpec *col = find_web_col(name);
    if (col) return col;
    if (strcasecmp(name, "sym") == 0)  return find_web_col("symbol");
    if (strcasecmp(name, "ctx") == 0)  return find_web_col("context");
    if (strcasecmp(name, "par") == 0)  return find_web_col("parent");
    if (strcasecmp(name, "mod") == 0)  return find_web_col("modifier");
    if (strcasecmp(name, "ns") == 0)   return find_web_col("namespace");
    if (strcasecmp(name, "d") == 0)    return find_web_col("definition");
    return NULL;
}

/* Active column for display */
typedef struct {
    const WebColSpec *spec;
    int max_width;
} ActiveWebCol;

static int parse_one_tsv_line(char *line, char *fields[], int max_fields) {
    int fc = 0;
    char *tok = line;
    char *tab;
    while (fc < max_fields && (tab = strchr(tok, '\t')) != NULL) {
        *tab = '\0';
        fields[fc++] = tok;
        tok = tab + 1;
    }
    if (fc < max_fields) fields[fc++] = tok;
    return fc;
}

/* Parse a line like "COLUMNS|line symbol context" into an ActiveWebCol array.
 * Returns number of columns parsed. */
static int build_active_columns(const char *build_info, int compact,
                                ActiveWebCol *active, int max_cols) {
    const char *cols_line = NULL;
    {
        const char *p = build_info;
        while (p) {
            if (strncmp(p, "COLUMNS|", 8) == 0) {
                cols_line = p + 8;
                break;
            }
            p = strchr(p, '\n');
            if (p) p++;
        }
        if (cols_line) {
            const char *end = strchr(cols_line, '\n');
            size_t len = end ? (size_t)(end - cols_line) : strlen(cols_line);
            if (len < 4096) {
                static char buf[4096];
                memcpy(buf, cols_line, len);
                buf[len] = '\0';
                cols_line = buf;
            } else {
                cols_line = NULL;
            }
        }
    }
    if (!cols_line || !cols_line[0]) return 0;

    int count = 0;
    char *saveptr;
    char line_buf[4096];
    strncpy(line_buf, cols_line, sizeof(line_buf) - 1);
    line_buf[sizeof(line_buf) - 1] = '\0';

    char *token = strtok_r(line_buf, " ", &saveptr);
    while (token && count < max_cols) {
        const WebColSpec *spec = find_web_col_by_alias(token);
        if (spec) {
            int w = spec->default_width;
            if (w == 0) w = (int)strlen(compact ? spec->header_compact : spec->header);
            active[count].spec = spec;
            active[count].max_width = w;
            count++;
        }
        token = strtok_r(NULL, " ", &saveptr);
    }
    return count;
}

/* Find a line starting with "KEY|" in build_info, return pointer to value portion */
static const char *find_build_line(const char *build_info, const char *key) {
    size_t klen = strlen(key);
    const char *p = build_info;
    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '|')
            return p + klen + 1;
        p = strchr(p, '\n');
        if (p) p++;
    }
    return NULL;
}

/* Parse a key|value line from build_info, return 1 if value=="1" */
static int parse_flag(const char *build_info, const char *key) {
    const char *val = find_build_line(build_info, key);
    return val && val[0] == '1';
}

/* Parse patterns string from build_info */
static void parse_patterns(const char *build_info, char *out, size_t out_size) {
    const char *val = find_build_line(build_info, "PATTERNS");
    if (val) {
        const char *end = strchr(val, '\n');
        size_t len = end ? (size_t)(end - val) : strlen(val);
        if (len < out_size) {
            memcpy(out, val, len);
            out[len] = '\0';
        }
    }
}

/* Parse an integer "KEY|N" line from build_info; returns 0 if absent. */
static int parse_int_value(const char *build_info, const char *key) {
    const char *val = find_build_line(build_info, key);
    return val ? atoi(val) : 0;
}

/* Build the canonical filepath for a row from its directory + filename fields.
 * This formula is the lookup key for fetched source, so the JS worker MUST
 * construct file paths identically (see QI_WEB_FILE_PLAN.md). */
static void build_row_filepath(const char *dir, const char *file, char *out, size_t n) {
    if (dir[0] && dir[strlen(dir) - 1] != '/')
        snprintf(out, n, "%s/%s", dir, file);
    else
        snprintf(out, n, "%s%s", dir, file);
}

/* -- Source blob: path -> file content, parsed from the worker's fetch --
 *
 * Wire format (NUL-framed, written straight into a WASM heap buffer by the
 * worker -- see marshalSources() in qi-worker.js):
 *     <path>\0<content>\0   (repeated for each present file)
 *
 * The buffer is owned by the worker (it _malloc's it and _free's it after the
 * call); we parse in place, storing pointers INTO the buffer rather than
 * copying each file's content.  Content is NUL-terminated by the framing, so
 * the render twins consume it as an ordinary C string with no extra copy.
 * NUL framing means content carries no embedded NULs -- fine for text source,
 * same constraint the ccall string boundary imposed before. */
typedef struct { const char *path; const char *content; } SourceFile;
typedef struct { SourceFile *files; int count; int last_hit; } SourceMap;

static SourceMap source_map_parse(const char *blob, int blob_len) {
    SourceMap m = { NULL, 0, 0 };
    if (!blob || blob_len <= 0) return m;

    const char *end = blob + blob_len;
    int cap = 8;
    m.files = malloc((size_t)cap * sizeof(*m.files));
    if (!m.files) return m;

    const char *p = blob;
    while (p < end) {
        const char *path = p;
        const char *pz = memchr(p, '\0', (size_t)(end - p));
        if (!pz) break;                                  /* path not terminated */

        const char *content = pz + 1;
        if (content > end) break;
        const char *cz = memchr(content, '\0', (size_t)(end - content));
        if (!cz) break;                                  /* content not terminated */

        if (m.count == cap) {
            int ncap = cap * 2;
            SourceFile *nf = realloc(m.files, (size_t)ncap * sizeof(*m.files));
            if (!nf) break;
            m.files = nf;
            cap = ncap;
        }
        m.files[m.count].path = path;       /* pointers into the worker's buffer */
        m.files[m.count].content = content; /* NUL-terminated in place */
        m.count++;

        p = cz + 1;
    }
    return m;
}

/* Returns the NUL-terminated content for path, or NULL if not present
 * (e.g. the worker could not fetch it -- the render twins then emit the
 * same "could not read file" warning the native CLI prints). */
static const char *source_map_get(SourceMap *m, const char *path) {
    if (m->count == 0) return NULL;
    /* Rows arrive grouped by file (ORDER BY directory, filename, line), so the
     * previous hit is almost always the right file -- check it before scanning
     * (same idea as toc-web.c's last_idx cache). */
    if (m->last_hit < m->count && strcmp(m->files[m->last_hit].path, path) == 0)
        return m->files[m->last_hit].content;
    for (int i = 0; i < m->count; i++) {
        if (strcmp(m->files[i].path, path) == 0) {
            m->last_hit = i;
            return m->files[i].content;
        }
    }
    return NULL;
}

static void source_map_free(SourceMap *m) {
    /* path/content point into the worker-owned heap buffer; only the index
     * array is ours to free. */
    free(m->files);
    m->files = NULL;
    m->count = 0;
}

EMSCRIPTEN_KEEPALIVE
char *qi_web_format(const char *build_info, const char *rows_tsv,
                    int total, int shown,
                    const char *sources_blob, int sources_len) {
    WebOutput wo;
    if (wo_init(&wo) != 0) return strdup("Error: out of memory.");

    int compact = parse_flag(build_info, "COMPACT");
    int raw = parse_flag(build_info, "RAW");
    int needs_source = parse_flag(build_info, "NEEDS_SOURCE");
    int expand = parse_flag(build_info, "EXPAND");
    int ctx_before = parse_int_value(build_info, "CONTEXT_BEFORE");
    int ctx_after = parse_int_value(build_info, "CONTEXT_AFTER");

    SourceMap sources = { NULL, 0, 0 };
    if (needs_source) sources = source_map_parse(sources_blob, sources_len);

    /* Build active column list from COLUMNS| metadata */
    ActiveWebCol active[MAX_WEB_COLS];
    int num_cols = build_active_columns(build_info, compact, active, MAX_WEB_COLS);
    if (num_cols == 0) {
        source_map_free(&sources);
        wo_free(&wo);
        return strdup("Error: no columns configured.");
    }

    /* Search header */
    char patterns_buf[4096] = "";
    parse_patterns(build_info, patterns_buf, sizeof(patterns_buf));

    /* --raw suppresses all non-source framing (header, table, stats). */
    if (!raw) {
        if (parse_flag(build_info, "DEBUG")) {
            const char *p = find_build_line(build_info, "SQL");
            if (p) {
                const char *end = strchr(p, '\n');
                size_t len = end ? (size_t)(end - p) : strlen(p);
                if (len < 8192) {
                    char sql_buf[8192];
                    memcpy(sql_buf, p, len);
                    sql_buf[len] = '\0';
                    wo_printf(&wo, "SQL: %s\n\n", sql_buf);
                }
            }
        }
        wo_printf(&wo, "Searching for: %s\n\n", patterns_buf);
    }

    if (!rows_tsv || !rows_tsv[0]) {
        if (!raw && total == 0)
            wo_printf(&wo, "No results\n");
        source_map_free(&sources);
        { char *r = wo_steal(&wo); return r ? r : strdup("ERROR|out of memory"); }
    }

    /* First pass: compute max column widths from data (table framing only) */
    if (!raw) {
    {
        char *scan_copy = strdup(rows_tsv);
        if (scan_copy) {
            char *sp = scan_copy;
            while (*sp) {
                char *nl = strchr(sp, '\n');
                if (nl) *nl = '\0';

                char *fields[TSV_FIELDS] = {NULL};
                parse_one_tsv_line(sp, fields, TSV_FIELDS);

                for (int ci = 0; ci < num_cols; ci++) {
                    int ti = active[ci].spec->tsv_index;
                    const char *val = (ti < TSV_FIELDS && fields[ti]) ? fields[ti] : "";
                    int w = (int)strlen(val);
                    if (w > active[ci].max_width) active[ci].max_width = w;
                }

                if (nl) sp = nl + 1;
                else break;
            }
            free(scan_copy);
        }
    }

    /* Header row */
    for (int ci = 0; ci < num_cols; ci++) {
        if (ci > 0) wo_printf(&wo, " | ");
        const char *hdr = compact ? active[ci].spec->header_compact : active[ci].spec->header;
        wo_printf(&wo, "%-*s", active[ci].max_width, hdr);
    }
    wo_printf(&wo, "\n");

    /* Separator row */
    for (int ci = 0; ci < num_cols; ci++) {
        if (ci > 0) wo_printf(&wo, "-+-");
        for (int w = 0; w < active[ci].max_width; w++) wo_printf(&wo, "-");
    }
    wo_printf(&wo, "\n");
    }  /* end if (!raw) table framing */

    /* Second pass: output rows with file grouping, then any source block */
    {
        char *rows_copy = strdup(rows_tsv);
        if (!rows_copy) {
            source_map_free(&sources);
            wo_free(&wo);
            return strdup("Error: out of memory.");
        }

        /* Search patterns to highlight in context output (mutates patterns_buf,
         * which is no longer needed after the "Searching for:" header). */
        char *hl_patterns[MAX_PATTERNS];
        int hl_count = 0;
        {
            char *sp2;
            char *t = strtok_r(patterns_buf, " ", &sp2);
            while (t && hl_count < MAX_PATTERNS) {
                hl_patterns[hl_count++] = t;
                t = strtok_r(NULL, " ", &sp2);
            }
        }

        char current_file[PATH_MAX_LENGTH] = "";
        char *line_ptr = rows_copy;
        int row_count = 0;

        while (*line_ptr && row_count < shown) {
            char *nl = strchr(line_ptr, '\n');
            if (nl) *nl = '\0';

            char *fields[TSV_FIELDS] = {NULL};
            parse_one_tsv_line(line_ptr, fields, TSV_FIELDS);

            const char *dir  = fields[1] ? fields[1] : "";
            const char *file = fields[2] ? fields[2] : "";
            char filepath[PATH_MAX_LENGTH];
            build_row_filepath(dir, file, filepath, sizeof(filepath));

            if (!raw) {
                /* File-group header on change */
                if (strcmp(filepath, current_file) != 0) {
                    if (current_file[0]) wo_printf(&wo, "\n");
                    wo_printf(&wo, "%s:\n", filepath);
                    snprintf(current_file, sizeof(current_file), "%s", filepath);
                }
                /* Selected columns */
                for (int ci = 0; ci < num_cols; ci++) {
                    if (ci > 0) wo_printf(&wo, " | ");
                    int ti = active[ci].spec->tsv_index;
                    const char *val = (ti < TSV_FIELDS && fields[ti]) ? fields[ti] : "";
                    wo_printf(&wo, "%-*s", active[ci].max_width, val);
                }
                wo_printf(&wo, "\n");
            }

            /* Source expansion / context lines.  The twin decides per row what
             * to render (-e only for definitions, -C/-A/-B otherwise); a missing
             * file (content == NULL) yields the CLI's "could not read" warning. */
            if (needs_source) {
                const char *content = source_map_get(&sources, filepath);
                int line_no = fields[3] ? atoi(fields[3]) : 0;
                int is_def  = fields[13] ? atoi(fields[13]) : 0;
                const char *srcloc = fields[6] ? fields[6] : "";
                print_expansion_or_context_web(&wo, content, filepath,
                    line_no, srcloc, is_def, expand, ctx_before, ctx_after,
                    hl_patterns, hl_count, raw);
            }

            row_count++;
            if (nl) line_ptr = nl + 1;
            else break;
        }
        free(rows_copy);
    }

    if (!raw) {
        wo_printf(&wo, "\nFound %d matches", total);
        if (total > shown) {
            wo_printf(&wo, " (showing first %d)", shown);
        }
        wo_printf(&wo, "\n");
        wo_printf(&wo, "Tip: Use -i <context> to narrow results\n");
    }

    source_map_free(&sources);
    { char *r = wo_steal(&wo); return r ? r : strdup("ERROR|out of memory"); }
}

/* Formats the "Result breakdown: COM (N), VAR (N), ..." + Tip line.
 * context_tsv: newline-separated rows of "context_name\tcount".
 * Called separately (mirroring the CLI's get_context_summary()) only when
 * results are truncated; caller decides whether to invoke it. */
EMSCRIPTEN_KEEPALIVE
char *qi_web_format_breakdown(const char *context_tsv) {
    WebOutput wo;
    if (wo_init(&wo) != 0) return strdup("Error: out of memory.");

    wo_printf(&wo, "Result breakdown: ");
    int first = 1;
    const char *p = context_tsv ? context_tsv : "";
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t row_len = nl ? (size_t)(nl - p) : strlen(p);
        if (row_len == 0) { p = nl ? nl + 1 : p + strlen(p); continue; }

        const char *tab = memchr(p, '\t', row_len);
        if (tab) {
            size_t ctx_len = (size_t)(tab - p);
            char ctx_name[64];
            size_t copy_len = ctx_len < sizeof(ctx_name) - 1 ? ctx_len : sizeof(ctx_name) - 1;
            memcpy(ctx_name, p, copy_len);
            ctx_name[copy_len] = '\0';

            const char *compact = map_context_web(ctx_name);
            if (!compact) compact = ctx_name;

            const char *cnt_start = tab + 1;
            size_t cnt_len = row_len - ctx_len - 1;
            char cnt_buf[32];
            size_t cnt_copy = cnt_len < sizeof(cnt_buf) - 1 ? cnt_len : sizeof(cnt_buf) - 1;
            memcpy(cnt_buf, cnt_start, cnt_copy);
            cnt_buf[cnt_copy] = '\0';

            if (!first) wo_printf(&wo, ", ");
            wo_printf(&wo, "%s (%s)", compact, cnt_buf);
            first = 0;
        }

        p = nl ? nl + 1 : p + row_len;
    }
    wo_printf(&wo, "\nTip: Use -i <context> to narrow results\n");

    { char *r = wo_steal(&wo); return r ? r : strdup("Error: out of memory."); }
}

/* Formats --files output: one filepath per line, "Found N files" footer.
 * rows_tsv: newline-separated rows of "directory\tfilename".
 * total_available: total distinct files (may exceed total_shown if limit hit). */
EMSCRIPTEN_KEEPALIVE
char *qi_web_format_files(const char *rows_tsv, int total_shown, int total_available) {
    WebOutput wo;
    if (wo_init(&wo) != 0) return strdup("Error: out of memory.");

    const char *p = rows_tsv ? rows_tsv : "";
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t row_len = nl ? (size_t)(nl - p) : strlen(p);
        if (row_len > 0) {
            const char *tab = memchr(p, '\t', row_len);
            if (tab) {
                size_t dir_len  = (size_t)(tab - p);
                size_t file_len = row_len - dir_len - 1;
                char dir[PATH_MAX_LENGTH]  = "";
                char file[PATH_MAX_LENGTH] = "";
                if (dir_len  < sizeof(dir))  { memcpy(dir,  p,       dir_len);  dir[dir_len]   = '\0'; }
                if (file_len < sizeof(file)) { memcpy(file, tab + 1, file_len); file[file_len] = '\0'; }
                char filepath[PATH_MAX_LENGTH];
                build_row_filepath(dir, file, filepath, sizeof(filepath));
                wo_printf(&wo, "%s\n", filepath);
            }
        }
        p = nl ? nl + 1 : p + row_len;
    }

    wo_printf(&wo, "\nFound %d files", total_available);
    if (total_available > total_shown)
        wo_printf(&wo, " (showing first %d)", total_shown);
    wo_printf(&wo, "\n");

    { char *r = wo_steal(&wo); return r ? r : strdup("Error: out of memory."); }
}

EMSCRIPTEN_KEEPALIVE
void qi_web_free_result(char *ptr) {
    free(ptr);
}

EMSCRIPTEN_KEEPALIVE
char *qi_web_toc_format(const char *build_info, const char *rows_tsv,
                        int total_shown, int total_available,
                        const char *context_counts) {
    return format_toc_web(build_info, rows_tsv, total_shown,
                          total_available, context_counts);
}
