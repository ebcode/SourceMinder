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

/* -- Output accumulator (replaces printf for web capture) -- */

#define WO_INITIAL_CAP 4096
#define WO_GROW_FACTOR 2

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} WebOutput;

static int wo_init(WebOutput *wo) {
    wo->cap = WO_INITIAL_CAP;
    wo->buf = malloc(wo->cap);
    if (!wo->buf) return -1;
    wo->buf[0] = '\0';
    wo->len = 0;
    return 0;
}

static int wo_grow(WebOutput *wo, size_t needed) {
    size_t new_cap = wo->cap;
    while (new_cap < wo->len + needed + 1)
        new_cap *= WO_GROW_FACTOR;
    char *nb = realloc(wo->buf, new_cap);
    if (!nb) return -1;
    wo->buf = nb;
    wo->cap = new_cap;
    return 0;
}

static int wo_printf(WebOutput *wo, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) return -1;

    if (wo->len + (size_t)needed + 1 > wo->cap) {
        if (wo_grow(wo, (size_t)needed) != 0) return -1;
    }

    va_start(ap, fmt);
    vsnprintf(wo->buf + wo->len, wo->cap - wo->len, fmt, ap);
    va_end(ap);
    wo->len += (size_t)needed;
    return 0;
}

static char *wo_steal(WebOutput *wo) {
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
    int error;
    const char *error_msg;
} WebCommand;

static void free_command(WebCommand *cmd) {
    for (int i = 0; i < cmd->pattern_count; i++) free(cmd->patterns[i]);
    for (int i = 0; i < cmd->include_count; i++) free(cmd->includes[i]);
    for (int i = 0; i < cmd->exclude_count; i++) free(cmd->excludes[i]);
    for (int i = 0; i < cmd->file_count; i++) free(cmd->files[i]);
    memset(cmd, 0, sizeof(*cmd));
    cmd->definition = -1;
    cmd->limit = 25;
}

static int is_flag(const char *token) {
    return token[0] == '-';
}

static WebCommand parse_command(const char *input) {
    WebCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.definition = -1;
    cmd.limit = 25;

    int tc = 0;
    char **tokens = tokenize(input, &tc);
    if (!tokens || tc == 0) {
        cmd.error = 1;
        cmd.error_msg = "Empty command.";
        return cmd;
    }

    int i = (tc > 0 && strcmp(tokens[0], "qi") == 0) ? 1 : 0;

    while (i < tc) {
        const char *t = tokens[i];

        if (!is_flag(t)) {
            if (cmd.pattern_count >= MAX_PATTERNS) {
                cmd.error = 1;
                cmd.error_msg = "Too many patterns.";
                goto done;
            }
            cmd.patterns[cmd.pattern_count++] = strdup(t);
            i++;
            continue;
        }

        if (strcmp(t, "--def") == 0) {
            cmd.definition = 1;
            i++;
            continue;
        }
        if (strcmp(t, "--usage") == 0) {
            cmd.definition = 0;
            i++;
            continue;
        }

        if (strcmp(t, "--limit") == 0) {
            if (i + 1 >= tc) {
                cmd.error = 1;
                cmd.error_msg = "--limit requires a number.";
                goto done;
            }
            cmd.limit = atoi(tokens[i + 1]);
            if (cmd.limit <= 0) {
                cmd.error = 1;
                cmd.error_msg = "--limit must be positive.";
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
                                cmd.includes[cmd.include_count++] = strdup("COM");
                                cmd.includes[cmd.include_count++] = strdup("STR");
                            }
                        } else {
                            if (cmd.exclude_count + 2 <= MAX_CONTEXT_TYPES) {
                                cmd.excludes[cmd.exclude_count++] = strdup("COM");
                                cmd.excludes[cmd.exclude_count++] = strdup("STR");
                            }
                        }
                    } else {
                        const char *mapped = map_context(val);
                        if (!mapped) {
                            cmd.error = 1;
                            cmd.error_msg = "Unknown context type.";
                            goto done;
                        }
                        if (is_include) {
                            if (cmd.include_count < MAX_CONTEXT_TYPES)
                                cmd.includes[cmd.include_count++] = strdup(mapped);
                        } else {
                            if (cmd.exclude_count < MAX_CONTEXT_TYPES)
                                cmd.excludes[cmd.exclude_count++] = strdup(mapped);
                        }
                    }
                } else {
                    if (cmd.file_count < MAX_CONTEXT_TYPES)
                        cmd.files[cmd.file_count++] = strdup(val);
                }
                i++;
            }
            continue;
        }

        /* Unsupported flag - ignore */
        i++;
    }

done:
    free_tokens(tokens, tc);

    if (!cmd.error && cmd.pattern_count == 0) {
        cmd.error = 1;
        cmd.error_msg = "At least one search pattern is required.";
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
    if (cmd.error) {
        wo_printf(&wo, "ERROR|%s", cmd.error_msg);
        free_command(&cmd);
        return wo_steal(&wo);
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

    /* Build SQL */
    SqlQueryBuilder builder;
    if (init_sql_builder(&builder) != 0) {
        wo_printf(&wo, "ERROR|SQL builder init failed");
        goto cleanup;
    }

    if (build_query_sql_web(&builder, &patterns, &include, &exclude,
                            &filters, &file_filter, NULL, -1, 0) != 0) {
        wo_printf(&wo, "ERROR|SQL build failed");
        free_sql_builder(&builder);
        goto cleanup;
    }

    /* Inject --def/--usage filter */
    if (cmd.definition >= 0) {
        char *order_pos = strstr(builder.sql, "ORDER BY");
        if (order_pos) {
            char saved[8192];
            strncpy(saved, order_pos, sizeof(saved));
            saved[sizeof(saved)-1] = '\0';
            *order_pos = '\0';
            builder.offset = (int)(order_pos - builder.sql);
            sql_append(&builder, " AND is_definition = %d ", cmd.definition);
            sql_append(&builder, "%s", saved);
        }
    }

    /* Build output: PATTERNS line, SQL line, LIMIT line, ERROR line */
    wo_printf(&wo, "PATTERNS|");
    for (int i = 0; i < cmd.pattern_count; i++) {
        if (i > 0) wo_printf(&wo, " ");
        wo_printf(&wo, "%s", cmd.patterns[i]);
    }
    wo_printf(&wo, "\nSQL|%s\nLIMIT|%d\nERROR|OK", builder.sql, cmd.limit);

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
    return wo_steal(&wo);
}

/* =================================================================
 * Exported API 2: format raw rows as qi-style output
 *
 * build_info: output from qi_web_build
 * rows_tsv:   tab-separated rows, one per line
 *             format: "line\tcontext\tsymbol\tdirectory\tfilename\n"
 * total:      total match count
 * shown:      number of rows actually shown
 * Returns:    malloc'd qi output string
 * ================================================================= */

EMSCRIPTEN_KEEPALIVE
char *qi_web_format(const char *build_info, const char *rows_tsv,
                    int total, int shown) {
    WebOutput wo;
    if (wo_init(&wo) != 0) return strdup("Error: out of memory.");

    /* Parse build_info for patterns */
    char patterns_buf[4096] = "";
    {
        const char *p = strstr(build_info, "PATTERNS|");
        if (p) {
            p += 9;
            const char *end = strchr(p, '\n');
            size_t len = end ? (size_t)(end - p) : strlen(p);
            if (len < sizeof(patterns_buf)) {
                memcpy(patterns_buf, p, len);
                patterns_buf[len] = '\0';
            }
        }
    }

    wo_printf(&wo, "Searching for: %s\n\n", patterns_buf);

    if (!rows_tsv || !rows_tsv[0]) {
        if (total == 0) {
            wo_printf(&wo, "No results\n");
        }
        return wo_steal(&wo);
    }

    /* First pass: compute max column widths */
    int max_line_w = 4, max_sym_w = 3, max_ctx_w = 3;
    {
        char *scan_copy = strdup(rows_tsv);
        if (scan_copy) {
            char *sp = scan_copy;
            while (*sp) {
                char *nl = strchr(sp, '\n');
                if (nl) *nl = '\0';

                char *fields[5] = {NULL};
                int fc = 0;
                char *tok = sp;
                char *tab;
                while (fc < 5 && (tab = strchr(tok, '\t')) != NULL) {
                    *tab = '\0';
                    fields[fc++] = tok;
                    tok = tab + 1;
                }
                if (fc < 5) fields[fc++] = tok;

                int lw = (int)strlen(fields[0] ? fields[0] : "");
                int sw = (int)strlen(fields[2] ? fields[2] : "");
                int cw = (int)strlen(fields[1] ? fields[1] : "");
                if (lw > max_line_w) max_line_w = lw;
                if (sw > max_sym_w) max_sym_w = sw;
                if (cw > max_ctx_w) max_ctx_w = cw;

                if (nl) sp = nl + 1;
                else break;
            }
            free(scan_copy);
        }
    }

    /* Table header */
    wo_printf(&wo, "%-*s | %-*s | %-*s\n",
              max_line_w, "LINE", max_sym_w, "SYM", max_ctx_w, "CTX");

    /* Separator */
    for (int i = 0; i < max_line_w; i++) wo_printf(&wo, "-");
    wo_printf(&wo, "-+-");
    for (int i = 0; i < max_sym_w; i++) wo_printf(&wo, "-");
    wo_printf(&wo, "-+-");
    for (int i = 0; i < max_ctx_w; i++) wo_printf(&wo, "-");
    wo_printf(&wo, "\n");

    /* Second pass: output rows */
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

            char *fields[5] = {NULL};
            int fc = 0;
            char *tok = line_ptr;
            char *tab;
            while (fc < 5 && (tab = strchr(tok, '\t')) != NULL) {
                *tab = '\0';
                fields[fc++] = tok;
                tok = tab + 1;
            }
            if (fc < 5) fields[fc++] = tok;

            const char *rline    = fields[0] ? fields[0] : "";
            const char *rcontext = fields[1] ? fields[1] : "";
            const char *rsymbol  = fields[2] ? fields[2] : "";
            const char *rdir     = fields[3] ? fields[3] : "";
            const char *rfile    = fields[4] ? fields[4] : "";

            char filepath[PATH_MAX_LENGTH];
            snprintf(filepath, sizeof(filepath), "%s%s", rdir, rfile);

            if (strcmp(filepath, current_file) != 0) {
                if (current_file[0]) wo_printf(&wo, "\n");
                wo_printf(&wo, "%s:\n", filepath);
                snprintf(current_file, sizeof(current_file), "%s", filepath);
            }

            wo_printf(&wo, "%-*s | %-*s | %-*s\n",
                      max_line_w, rline, max_sym_w, rsymbol, max_ctx_w, rcontext);
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

    return wo_steal(&wo);
}

EMSCRIPTEN_KEEPALIVE
void qi_web_free_result(char *ptr) {
    free(ptr);
}
