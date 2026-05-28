/*
 * qi-web-entry.c -- Browser/WASM bridge for the qi query tool.
 *
 * NO sqlite3 linked.  JS owns the DB (@sqlite.org/sqlite-wasm).
 * This module builds SQL and formats qi-style output from raw result rows.
 *
 * Exports:
 *   qi_web_build(command)       -> build-info string (SQL, patterns, limit)
 *   qi_web_format(build_info, rows_tsv, total, shown) -> formatted qi output
 *   qi_web_free_result(ptr)     -> free a result string
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

/* Forward declarations for sqlite3 shim (defined in query-index-web.c, linked together) */
char *sqlite3_mprintf(const char *fmt, ...);
void sqlite3_free(void *ptr);

/* -- Output accumulator (replaces printf for web capture) -- */

#define WO_INITIAL_CAP 4096
#define WO_GROW_FACTOR 2

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    int error;
} WebOutput;

static int wo_init(WebOutput *wo) {
    wo->cap = WO_INITIAL_CAP;
    wo->buf = malloc(wo->cap);
    if (!wo->buf) { wo->error = 1; return -1; }
    wo->buf[0] = '\0';
    wo->len = 0;
    wo->error = 0;
    return 0;
}

static int wo_grow(WebOutput *wo, size_t needed) {
    if (wo->error) return -1;
    size_t new_cap = wo->cap;
    while (new_cap < wo->len + needed + 1)
        new_cap *= WO_GROW_FACTOR;
    char *nb = realloc(wo->buf, new_cap);
    if (!nb) { wo->error = 1; return -1; }
    wo->buf = nb;
    wo->cap = new_cap;
    return 0;
}

static int wo_printf(WebOutput *wo, const char *fmt, ...) {
    if (wo->error) return -1;
    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) { wo->error = 1; return -1; }

    if (wo->len + (size_t)needed + 1 > wo->cap) {
        if (wo_grow(wo, (size_t)needed) != 0) { wo->error = 1; return -1; }
    }

    va_start(ap, fmt);
    vsnprintf(wo->buf + wo->len, wo->cap - wo->len, fmt, ap);
    va_end(ap);
    wo->len += (size_t)needed;
    return 0;
}

static char *wo_steal(WebOutput *wo) {
    if (wo->error) {
        free(wo->buf);
        wo->buf = NULL;
        wo->len = 0;
        wo->cap = 0;
        return NULL;
    }
    char *result = wo->buf;
    wo->buf = NULL;
    wo->len = 0;
    wo->cap = 0;
    return result;
}

static void wo_free(WebOutput *wo) {
    free(wo->buf);
    wo->buf = NULL;
    wo->len = 0;
    wo->cap = 0;
}

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

static const char *map_context(const char *token) {
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

    int i = (tc > 0 && strcmp(tokens[0], "qi") == 0) ? 1 : 0;

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
                        const char *mapped = map_context(val);
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

    /* Derive COUNT_SQL from main SQL: SELECT * → SELECT COUNT(*), strip ORDER BY */
    {
        const char *select_star = "SELECT * ";
        const char *star = strstr(builder.sql, select_star);
        const char *order = star ? strstr(builder.sql, "ORDER BY") : NULL;
        if (star && order && order > star) {
            size_t after_star = (size_t)((star - builder.sql) + strlen(select_star));
            size_t where_len = (size_t)(order - (builder.sql + after_star));
            wo_printf(&wo, "\nCOUNT_SQL|SELECT COUNT(*) %.*s",
                (int)where_len, builder.sql + after_star);
        }
    }

    wo_printf(&wo, "\nSQL|%s\nLIMIT|%d", builder.sql, cmd.limit);
    if (cmd.verbose)
        wo_printf(&wo, "\nVERBOSE|1");
    wo_printf(&wo, "\nCOMPACT|%d", cmd.compact);
    if (cmd.debug)
        wo_printf(&wo, "\nDEBUG|1");
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

EMSCRIPTEN_KEEPALIVE
char *qi_web_format(const char *build_info, const char *rows_tsv,
                    int total, int shown) {
    WebOutput wo;
    if (wo_init(&wo) != 0) return strdup("Error: out of memory.");

    int compact = parse_flag(build_info, "COMPACT");

    /* Build active column list from COLUMNS| metadata */
    ActiveWebCol active[MAX_WEB_COLS];
    int num_cols = build_active_columns(build_info, compact, active, MAX_WEB_COLS);
    if (num_cols == 0) {
        wo_free(&wo);
        return strdup("Error: no columns configured.");
    }

    /* Search header */
    char patterns_buf[4096] = "";
    parse_patterns(build_info, patterns_buf, sizeof(patterns_buf));

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

    if (!rows_tsv || !rows_tsv[0]) {
        if (total == 0)
            wo_printf(&wo, "No results\n");
        { char *r = wo_steal(&wo); return r ? r : strdup("ERROR|out of memory"); }
    }

    /* First pass: compute max column widths from data */
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

    /* Second pass: output rows with file grouping */
    {
        char *rows_copy = strdup(rows_tsv);
        if (!rows_copy) {
            wo_free(&wo);
            return strdup("Error: out of memory.");
        }

        char current_file[PATH_MAX_LENGTH] = "";
        char *line_ptr = rows_copy;
        int row_count = 0;

        while (*line_ptr && row_count < shown) {
            char *nl = strchr(line_ptr, '\n');
            if (nl) *nl = '\0';

            char *fields[TSV_FIELDS] = {NULL};
            parse_one_tsv_line(line_ptr, fields, TSV_FIELDS);

            /* Build filepath from directory (TSV 1) + filename (TSV 2) */
            const char *dir  = fields[1] ? fields[1] : "";
            const char *file = fields[2] ? fields[2] : "";
            if (dir[0] && dir[strlen(dir)-1] != '/') {
                char filepath[PATH_MAX_LENGTH];
                snprintf(filepath, sizeof(filepath), "%s/%s", dir, file);
                if (strcmp(filepath, current_file) != 0) {
                    if (current_file[0]) wo_printf(&wo, "\n");
                    wo_printf(&wo, "%s:\n", filepath);
                    snprintf(current_file, sizeof(current_file), "%s", filepath);
                }
            } else {
                char filepath_no_slash[PATH_MAX_LENGTH];
                snprintf(filepath_no_slash, sizeof(filepath_no_slash), "%s%s", dir, file);
                if (strcmp(filepath_no_slash, current_file) != 0) {
                    if (current_file[0]) wo_printf(&wo, "\n");
                    wo_printf(&wo, "%s:\n", filepath_no_slash);
                    snprintf(current_file, sizeof(current_file), "%s", filepath_no_slash);
                }
            }

            /* Print selected columns */
            for (int ci = 0; ci < num_cols; ci++) {
                if (ci > 0) wo_printf(&wo, " | ");
                int ti = active[ci].spec->tsv_index;
                const char *val = (ti < TSV_FIELDS && fields[ti]) ? fields[ti] : "";
                if (active[ci].spec->is_int) {
                    wo_printf(&wo, "%-*s", active[ci].max_width, val);
                } else {
                    wo_printf(&wo, "%-*s", active[ci].max_width, val);
                }
            }
            wo_printf(&wo, "\n");
            row_count++;

            if (nl) line_ptr = nl + 1;
            else break;
        }
        free(rows_copy);
    }

    wo_printf(&wo, "\nFound %d matches", total);
    if (total > shown) {
        wo_printf(&wo, " (showing first %d)", shown);
    }
    wo_printf(&wo, "\n");
    wo_printf(&wo, "Tip: Use -i <context> to narrow results\n");

    { char *r = wo_steal(&wo); return r ? r : strdup("ERROR|out of memory"); }
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
