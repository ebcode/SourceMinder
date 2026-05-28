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

#include "toc-web.h"
#include "shared/sql_builder.h"
#include "shared/constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

/* Forward declarations for sqlite3 web shim (defined in query-index-web.c) */
char *sqlite3_mprintf(const char *fmt, ...);
void sqlite3_free(void *ptr);

/* ---- Output accumulator (same pattern as qi-web-entry.c) ---- */

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
        return NULL;
    }
    char *result = wo->buf;
    wo->buf = NULL;
    return result;
}

static void wo_free(WebOutput *wo) {
    free(wo->buf);
    wo->buf = NULL;
    wo->len = 0;
    wo->cap = 0;
}

/* Find a line starting with "KEY|" in build_info, return pointer past the '|' */
static const char *toc_find_build_line(const char *build_info, const char *key) {
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

/* =================================================================
 * TOC SQL builder
 * ================================================================= */

char *build_toc_web_sql(const TocWebConfig *config) {
    SqlQueryBuilder b;
    if (init_sql_builder(&b) != 0) return NULL;

#define SA(fmt, ...) if (sql_append(&b, fmt, ##__VA_ARGS__) != 0) goto cleanup

    SA("SELECT DISTINCT full_symbol, line, source_location, "
       "context, directory, filename "
       "FROM code_index "
       "WHERE (is_definition = 1 OR context = 'IMP' OR context = 'FILE') "
       "AND context IN (");

    /* Dynamic context type list */
    for (int i = 0; TOC_ALLOWED_CONTEXTS_WEB[i] != NULL; i++) {
        char *escaped = sqlite3_mprintf("%q", TOC_ALLOWED_CONTEXTS_WEB[i]);
        if (!escaped) goto cleanup;
        if (sql_append(&b, "%s%s", escaped,
               TOC_ALLOWED_CONTEXTS_WEB[i + 1] != NULL ? ", " : "") != 0) {
            sqlite3_free(escaped);
            goto cleanup;
        }
        sqlite3_free(escaped);
    }
    SA(") ");

    /* File pattern filter */
    if (config->file_pattern_count > 0) {
        SA("AND (");
        for (int i = 0; i < config->file_pattern_count; i++) {
            if (config->file_patterns[i].directory != NULL) {
                char *dir_esc  = sqlite3_mprintf("%q", config->file_patterns[i].directory);
                char *file_esc = sqlite3_mprintf("%q", config->file_patterns[i].filename);
                if (!dir_esc || !file_esc) {
                    sqlite3_free(dir_esc);
                    sqlite3_free(file_esc);
                    goto cleanup;
                }
                if (sql_append(&b,
                        "(directory LIKE %s ESCAPE '\\' AND filename LIKE %s ESCAPE '\\')",
                        dir_esc, file_esc) != 0) {
                    sqlite3_free(dir_esc);
                    sqlite3_free(file_esc);
                    goto cleanup;
                }
                sqlite3_free(dir_esc);
                sqlite3_free(file_esc);
            } else {
                char *file_esc = sqlite3_mprintf("%q", config->file_patterns[i].filename);
                if (!file_esc) goto cleanup;
                if (sql_append(&b, "filename LIKE %s ESCAPE '\\'", file_esc) != 0) {
                    sqlite3_free(file_esc);
                    goto cleanup;
                }
                sqlite3_free(file_esc);
            }
            if (i < config->file_pattern_count - 1)
                SA(" OR ");
        }
        SA(") ");
    }

    /* Context filters */
    if (config->include_context_count > 0) {
        SA("AND context IN (");
        for (int i = 0; i < config->include_context_count; i++) {
            char upper[64];
            const char *ctx = config->include_contexts[i];
            int j = 0;
            while (ctx[j] && j < 63) {
                upper[j] = (char)toupper((unsigned char)ctx[j]);
                j++;
            }
            upper[j] = '\0';
            char *escaped = sqlite3_mprintf("%q", upper);
            if (!escaped) goto cleanup;
            if (sql_append(&b, "%s%s", escaped,
                    i < config->include_context_count - 1 ? ", " : "") != 0) {
                sqlite3_free(escaped);
                goto cleanup;
            }
            sqlite3_free(escaped);
        }
        SA(") ");
    }

    /* Symbol pattern filters */
    if (config->symbol_pattern_count > 0) {
        SA("AND (");
        for (int i = 0; i < config->symbol_pattern_count; i++) {
            char *escaped = sqlite3_mprintf("%q", config->symbol_patterns[i]);
            if (!escaped) goto cleanup;
            if (sql_append(&b, "symbol LIKE %s%s", escaped,
                    i < config->symbol_pattern_count - 1 ? " OR " : "") != 0) {
                sqlite3_free(escaped);
                goto cleanup;
            }
            sqlite3_free(escaped);
        }
        SA(") ");
    }

    SA("ORDER BY directory, filename, line");

#undef SA

    {
        char *result = b.sql;
        b.sql = NULL;
        free_sql_builder(&b);
        return result;
    }

cleanup:
    free_sql_builder(&b);
    return NULL;
}

/* =================================================================
 * TOC output formatter
 * ================================================================= */

/* Internal: one TOC symbol entry */
typedef struct {
    char *symbol;
    int line;
    char *context;
} TocWebEntry;

/* Internal: file group */
typedef struct {
    char *filepath;
    TocWebEntry *entries;
    int count;
    int cap;
} TocWebFile;

#define TOC_FILE_INITIAL_CAP 4

/* Compare TocWebEntry pointers by line (for qsort) */
static int toc_cmp_by_line(const void *a, const void *b) {
    const TocWebEntry *ea = *(const TocWebEntry *const *)a;
    const TocWebEntry *eb = *(const TocWebEntry *const *)b;
    return ea->line - eb->line;
}

/* Parse source_location like "1:0 - 10:0" → extract line. Fallback to line_col. */
static int toc_parse_line(const char *source_loc, int line_col) {
    if (!source_loc || !source_loc[0]) return line_col;
    int sl, sc, el, ec;
    if (sscanf(source_loc, "%d:%d - %d:%d", &sl, &sc, &el, &ec) == 4)
        return sl;
    return line_col;
}

/* Find or create file group.
 * Rows arrive ordered by (directory, filename, line) from the TOC SQL,
 * so consecutive rows almost always share the same file.  The last_idx
 * cache turns the common case into a single strcmp instead of scanning
 * every prior file on every row. */
static TocWebFile *toc_find_or_add_file(TocWebFile **files, int *file_count,
                                         const char *filepath, int *last_idx) {
    if (*last_idx >= 0 && *last_idx < *file_count &&
        strcmp((*files)[*last_idx].filepath, filepath) == 0)
        return &(*files)[*last_idx];

    for (int i = 0; i < *file_count; i++) {
        if (strcmp((*files)[i].filepath, filepath) == 0) {
            *last_idx = i;
            return &(*files)[i];
        }
    }
    TocWebFile *temp = realloc(*files, sizeof(TocWebFile) * (size_t)(*file_count + 1));
    if (!temp) return NULL;
    *files = temp;
    TocWebFile *f = &(*files)[*file_count];
    memset(f, 0, sizeof(*f));
    f->filepath = strdup(filepath);
    if (!f->filepath) return NULL;
    f->entries = NULL;
    f->count = 0;
    f->cap = 0;
    *last_idx = *file_count;
    (*file_count)++;
    return f;
}

/* Add entry to file group. Returns 0 on success, -1 on allocation failure. */
static int toc_add_entry(TocWebFile *file, const char *symbol, int line,
                          const char *context) {
    if (file->count >= file->cap) {
        int nc = file->cap == 0 ? TOC_FILE_INITIAL_CAP : file->cap * 2;
        TocWebEntry *temp = realloc(file->entries, sizeof(TocWebEntry) * (size_t)nc);
        if (!temp) return -1;
        file->entries = temp;
        file->cap = nc;
    }
    TocWebEntry *e = &file->entries[file->count];
    e->symbol = strdup(symbol);
    e->line = line;
    e->context = strdup(context);
    if (!e->symbol || !e->context) {
        free(e->symbol);
        free(e->context);
        return -1;
    }
    file->count++;
    return 0;
}

/* Free file groups */
static void toc_free_files(TocWebFile *files, int file_count) {
    for (int i = 0; i < file_count; i++) {
        free(files[i].filepath);
        for (int j = 0; j < files[i].count; j++) {
            free(files[i].entries[j].symbol);
            free(files[i].entries[j].context);
        }
        free(files[i].entries);
    }
    free(files);
}

/* Print a section of TOC entries matching context_filter */
static void toc_print_section(WebOutput *wo, const char *title,
                               TocWebFile *file, const char *context_filter) {
    int count = 0;
    TocWebEntry **filtered = malloc(sizeof(TocWebEntry *) * (size_t)file->count);
    if (!filtered) return;

    for (int i = 0; i < file->count; i++) {
        if (strcmp(file->entries[i].context, context_filter) == 0)
            filtered[count++] = &file->entries[i];
    }
    if (count == 0) { free(filtered); return; }

    qsort(filtered, (size_t)count, sizeof(TocWebEntry *), toc_cmp_by_line);

    wo_printf(wo, "%s (%d):\n", title, count);
    for (int i = 0; i < count; i++) {
        int name_len = (int)strlen(filtered[i]->symbol);
        int line_len = snprintf(NULL, 0, "%d", filtered[i]->line);
        int dots = 70 - name_len - line_len - 3;
        if (dots < 1) dots = 1;

        /* Emit padding dots via %.*s against a fixed literal.  Clamp to
         * the literal length so %.*s never reads past the buffer. */
#define TOC_DOT_PAD "......................................................................"
#define TOC_DOT_PAD_LEN ((int)(sizeof(TOC_DOT_PAD) - 1))
        if (dots > TOC_DOT_PAD_LEN) dots = TOC_DOT_PAD_LEN;
        wo_printf(wo, "  %s %.*s %d\n",
                  filtered[i]->symbol, dots, TOC_DOT_PAD,
                  filtered[i]->line);
#undef TOC_DOT_PAD
#undef TOC_DOT_PAD_LEN
    }
    wo_printf(wo, "\n");
    free(filtered);
}

/* Simple open-addressing hash set for import dedup.
 * Import count per file is small, so a fixed-size 64-slot table with
 * linear probing gives O(1) average lookup while preserving first-seen order. */

#define IMP_HASH_SIZE 256

static unsigned imp_hash_str(const char *s) {
    unsigned h = 5381;
    while (*s) h = ((h << 5) + h) + (unsigned char)*s++;
    return h;
}

static int imp_hash_seen(char **slots, const char *key) {
    unsigned h = imp_hash_str(key) % IMP_HASH_SIZE;
    int steps = 0;
    while (slots[h] && steps < IMP_HASH_SIZE) {
        if (strcmp(slots[h], key) == 0) return 1;
        h = (h + 1) % IMP_HASH_SIZE;
        steps++;
    }
    return 0;
}

static void imp_hash_add(char **slots, const char *key) {
    unsigned h = imp_hash_str(key) % IMP_HASH_SIZE;
    int steps = 0;
    while (slots[h] && steps < IMP_HASH_SIZE) {
        if (strcmp(slots[h], key) == 0) return;
        h = (h + 1) % IMP_HASH_SIZE;
        steps++;
    }
    if (steps < IMP_HASH_SIZE)
        slots[h] = (char *)key;
}

/* Print imports (deduped) on one line, preserving first-seen order. */
static void toc_print_imports(WebOutput *wo, TocWebFile *file) {
    TocWebEntry **imports = malloc(sizeof(TocWebEntry *) * (size_t)file->count);
    if (!imports) return;
    int import_count = 0;

    char *seen[IMP_HASH_SIZE];
    memset(seen, 0, sizeof(seen));

    for (int i = 0; i < file->count; i++) {
        if (strcmp(file->entries[i].context, "IMP") != 0) continue;
        const char *sym = file->entries[i].symbol;
        if (imp_hash_seen(seen, sym)) continue;
        imp_hash_add(seen, sym);
        imports[import_count++] = &file->entries[i];
    }

    if (import_count == 0) { free(imports); return; }

    wo_printf(wo, "IMPORTS: ");
    for (int i = 0; i < import_count; i++) {
        wo_printf(wo, "%s", imports[i]->symbol);
        if (i < import_count - 1) wo_printf(wo, ", ");
    }
    wo_printf(wo, "\n\n");

    free(imports);
}

char *format_toc_web(const char *build_info, const char *rows_tsv,
                     int total_shown, int total_available,
                     const char *context_counts) {
    WebOutput wo;
    if (wo_init(&wo) != 0) return NULL;

    if (!rows_tsv || !rows_tsv[0]) {
        wo_printf(&wo, "No definitions found matching the criteria.\n");
        { char *r = wo_steal(&wo); return r ? r : strdup("No definitions found."); }
    }

    /* Phase 1: parse rows, group by file */
    TocWebFile *files = NULL;
    int file_count = 0;
    int last_idx = -1;
    int parse_error = 0;

    {
        char *copy = strdup(rows_tsv);
        if (!copy) { wo_free(&wo); return strdup("Error: out of memory."); }
        char *line_ptr = copy;
        int row_num = 0;

        while (*line_ptr && row_num < total_shown) {
            char *nl = strchr(line_ptr, '\n');
            if (nl) *nl = '\0';

            /* Parse 6 TSV fields: symbol \t line \t source_location \t context \t dir \t file */
            char *fields[6];
            memset(fields, 0, sizeof(fields));
            char *tok = line_ptr;
            int fc = 0;
            while (fc < 6) {
                char *tab = strchr(tok, '\t');
                if (tab) *tab = '\0';
                fields[fc++] = tok;
                if (tab) tok = tab + 1;
                else break;
            }
            if (fc < 1) continue; /* skip completely empty rows */

            const char *symbol      = fields[0] ? fields[0] : "";
            const char *line_str    = fields[1] ? fields[1] : "0";
            const char *source_loc  = fields[2] ? fields[2] : "";
            const char *context     = fields[3] ? fields[3] : "FUNC";
            const char *directory   = fields[4] ? fields[4] : "";
            const char *filename    = fields[5] ? fields[5] : "";

            int line_col = atoi(line_str);
            int line = toc_parse_line(source_loc, line_col);

            /* Build filepath with separator normalization.
             * directory always has a trailing '/' from the indexer, but the
             * formatter shouldn't depend on that implicit contract. */
            char filepath[PATH_MAX_LENGTH];
            {
                size_t dlen = strlen(directory);
                if (dlen == 0) {
                    snprintf(filepath, sizeof(filepath), "%s", filename);
                } else {
                    int has_slash = (directory[dlen - 1] == '/');
                    snprintf(filepath, sizeof(filepath), "%s%s%s",
                             directory, has_slash ? "" : "/", filename);
                }
            }

            TocWebFile *f = toc_find_or_add_file(&files, &file_count, filepath, &last_idx);
            if (!f || toc_add_entry(f, symbol, line, context) != 0) {
                parse_error = 1;
                break;
            }

            row_num++;
            if (nl) line_ptr = nl + 1;
            else break;
        }
        free(copy);
    }

    if (parse_error) {
        toc_free_files(files, file_count);
        wo_free(&wo);
        return strdup("Error: out of memory.");
    }

    if (file_count == 0) {
        /* Check if any requested include context is unsupported for --toc */
        const char *includes = toc_find_build_line(build_info, "TOC_INCLUDES");
        if (includes && includes[0]) {
            int has_unsupported = 0;
            char tsv_buf[1024];
            const char *line_end = strchr(includes, '\n');
            size_t include_len = line_end ? (size_t)(line_end - includes) : strlen(includes);
            if (include_len >= sizeof(tsv_buf)) {
                include_len = sizeof(tsv_buf) - 1;
            }
            memcpy(tsv_buf, includes, include_len);
            tsv_buf[include_len] = '\0';
            char *saveptr;
            char *tok = strtok_r(tsv_buf, " ", &saveptr);
            while (tok) {
                /* Upper-case for comparison */
                char upper[64];
                int j = 0;
                while (tok[j] && j < 63) {
                    upper[j] = (char)toupper((unsigned char)tok[j]);
                    j++;
                }
                upper[j] = '\0';

                /* Check against allowed contexts (with aliases) */
                int is_allowed = 0;
                for (int k = 0; TOC_ALLOWED_CONTEXTS_WEB[k] != NULL; k++) {
                    if (strcmp(upper, TOC_ALLOWED_CONTEXTS_WEB[k]) == 0) {
                        is_allowed = 1;
                        break;
                    }
                    if ((strcmp(upper, "FUNCTION") == 0 && strcmp(TOC_ALLOWED_CONTEXTS_WEB[k], "FUNC") == 0) ||
                        (strcmp(upper, "IMPORT") == 0 && strcmp(TOC_ALLOWED_CONTEXTS_WEB[k], "IMP") == 0)) {
                        is_allowed = 1;
                        break;
                    }
                }
                if (!is_allowed) { has_unsupported = 1; break; }
                tok = strtok_r(NULL, " ", &saveptr);
            }
            if (has_unsupported) {
                wo_printf(&wo, "--toc does not support all requested context types.\n");
                wo_printf(&wo, "Allowed context types for --toc: ");
                for (int k = 0; TOC_ALLOWED_CONTEXTS_WEB[k] != NULL; k++) {
                    if (strcmp(TOC_ALLOWED_CONTEXTS_WEB[k], "FILE") != 0) {
                        wo_printf(&wo, "%s%s", TOC_ALLOWED_CONTEXTS_WEB[k],
                                  TOC_ALLOWED_CONTEXTS_WEB[k + 1] != NULL &&
                                  strcmp(TOC_ALLOWED_CONTEXTS_WEB[k + 1], "FILE") != 0 ? ", " : "");
                    }
                }
                wo_printf(&wo, "\nTo see all symbols in a file, use without --toc: qi %% -f <file>\n");
                { char *r = wo_steal(&wo); return r ? r : strdup("No definitions found."); }
            }
        }
        wo_printf(&wo, "No definitions found matching the criteria.\n");
        { char *r = wo_steal(&wo); return r ? r : strdup("No definitions found."); }
    }

    /* Phase 2: print summary */
    if (context_counts && context_counts[0]) {
        wo_printf(&wo, "Result breakdown: ");
        /* Parse "TYPE:COUNT\nTYPE:COUNT\n..." */
        char *cc_copy = strdup(context_counts);
        if (cc_copy) {
            char *saveptr;
            char *line = strtok_r(cc_copy, "\n", &saveptr);
            int first = 1;
            while (line) {
                char *colon = strchr(line, ':');
                if (colon) {
                    if (!first) wo_printf(&wo, ", ");
                    *colon = '\0';
                    wo_printf(&wo, "%s (%s)", line, colon + 1);
                    first = 0;
                }
                line = strtok_r(NULL, "\n", &saveptr);
            }
            wo_printf(&wo, "\n");
            free(cc_copy);
        }
        wo_printf(&wo, "\n");
    }

    /* Phase 3: print per-file sections */
    for (int i = 0; i < file_count; i++) {
        TocWebFile *file = &files[i];
        wo_printf(&wo, "%s:\n\n", file->filepath);

        toc_print_imports(&wo, file);

        /* Section order: CLASS, FUNC, ENUM, TYPE */
        toc_print_section(&wo, "CLASSES",   file, "CLASS");
        toc_print_section(&wo, "FUNCTIONS", file, "FUNC");
        toc_print_section(&wo, "ENUMS",     file, "ENUM");
        toc_print_section(&wo, "TYPES",     file, "TYPE");

        if (i < file_count - 1)
            wo_printf(&wo, "\n");
    }

    if (total_available > total_shown)
        wo_printf(&wo, "\n[Limit reached: %d shown, %d total]\n",
                  total_shown, total_available);

    toc_free_files(files, file_count);
    { char *r = wo_steal(&wo); return r ? r : strdup("No definitions found."); }
}
