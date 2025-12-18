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
#include "toc.h"
#include "file_utils.h"
#include "string_utils.h"
#include "constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Internal structure for TOC entries */
typedef struct {
    char *symbol;
    int start_line;
    int end_line;
    char *context;
} TocEntry;

/* Internal structure for grouping by file */
typedef struct {
    char *filepath;
    TocEntry *entries;
    int entry_count;
    int entry_capacity;
} FileToc;

/* Forward declarations */
static char *build_toc_query(const TocConfig *config);
static int compare_by_line(const void *a, const void *b);
static void print_toc(FileToc *files, int file_count);
static void print_section(const char *title, TocEntry *entries, int count, const char *context_filter);
static void print_imports(TocEntry *entries, int count);
static void cleanup_files(FileToc *files, int file_count);
static int add_entry_to_file(FileToc **files, int *file_count, const char *filepath,
                             const char *symbol, int start_line, int end_line, const char *context, int limit_per_file);

/* Build SQL query for TOC */
static char *build_toc_query(const TocConfig *config) {
    static char query[8192];
    char *pos = query;
    int remaining = sizeof(query);
    int written;

    /* Base query - filter to relevant definition types only */
    written = snprintf(pos, remaining,
        "SELECT DISTINCT "
        "  full_symbol, "
        "  line, "
        "  source_location, "
        "  context, "
        "  directory, "
        "  filename "
        "FROM code_index "
        "WHERE (is_definition = 1 OR context = 'IMP' OR context = 'FILE') "
        "AND context IN (");
    pos += written;
    remaining -= written;

    /* Build dynamic context type list from TOC_ALLOWED_CONTEXTS */
    for (int i = 0; TOC_ALLOWED_CONTEXTS[i] != NULL; i++) {
        written = snprintf(pos, remaining, "'%s'%s",
                         TOC_ALLOWED_CONTEXTS[i],
                         TOC_ALLOWED_CONTEXTS[i + 1] != NULL ? ", " : "");
        pos += written;
        remaining -= written;
    }

    written = snprintf(pos, remaining, ") ");
    pos += written;
    remaining -= written;

    /* File pattern filter (required) - support multiple patterns with OR */
    if (config->file_pattern_count > 0) {
        written = snprintf(pos, remaining, "AND (");
        pos += written;
        remaining -= written;

        for (int i = 0; i < config->file_pattern_count; i++) {
            if (config->file_patterns[i].directory != NULL) {
                /* Has directory part - filter both columns */
                written = snprintf(pos, remaining,
                    "(directory LIKE '%s' ESCAPE '\\' AND filename LIKE '%s' ESCAPE '\\')",
                    config->file_patterns[i].directory,
                    config->file_patterns[i].filename);
            } else {
                /* Filename only */
                written = snprintf(pos, remaining,
                    "filename LIKE '%s' ESCAPE '\\'",
                    config->file_patterns[i].filename);
            }
            pos += written;
            remaining -= written;

            if (i < config->file_pattern_count - 1) {
                written = snprintf(pos, remaining, " OR ");
                pos += written;
                remaining -= written;
            }
        }

        written = snprintf(pos, remaining, ") ");
        pos += written;
        remaining -= written;
    }

    /* Context filters (if provided) */
    if (config->include_context_count > 0) {
        written = snprintf(pos, remaining, "AND context IN (");
        pos += written;
        remaining -= written;

        for (int i = 0; i < config->include_context_count; i++) {
            /* Convert short form to DB form if needed */
            const char *ctx = config->include_contexts[i];
            char ctx_upper[64];

            /* Simple uppercase conversion */
            for (int j = 0; ctx[j] && j < 63; j++) {
                ctx_upper[j] = (ctx[j] >= 'a' && ctx[j] <= 'z') ? ctx[j] - 32 : ctx[j];
                ctx_upper[j + 1] = '\0';
            }

            written = snprintf(pos, remaining, "'%s'%s",
                             ctx_upper,
                             i < config->include_context_count - 1 ? ", " : "");
            pos += written;
            remaining -= written;
        }

        written = snprintf(pos, remaining, ") ");
        pos += written;
        remaining -= written;
    }

    /* Symbol pattern filters (if provided) */
    if (config->symbol_pattern_count > 0) {
        written = snprintf(pos, remaining, "AND (");
        pos += written;
        remaining -= written;

        for (int i = 0; i < config->symbol_pattern_count; i++) {
            written = snprintf(pos, remaining, "symbol LIKE '%s'%s",
                             config->symbol_patterns[i],
                             i < config->symbol_pattern_count - 1 ? " OR " : "");
            pos += written;
            remaining -= written;
        }

        written = snprintf(pos, remaining, ") ");
        pos += written;
        remaining -= written;
    }

    /* Order by file and line */
    snprintf(pos, remaining, "ORDER BY directory, filename, line");

    return query;
}

/* Add entry to appropriate file group
 * Returns: 1 = entry added, 0 = skipped (limit reached), -1 = error */
static int add_entry_to_file(FileToc **files, int *file_count, const char *filepath,
                             const char *symbol, int start_line, int end_line, const char *context, int limit_per_file) {
    /* Find or create file group */
    FileToc *target_file = NULL;
    for (int i = 0; i < *file_count; i++) {
        if (strcmp((*files)[i].filepath, filepath) == 0) {
            target_file = &(*files)[i];
            break;
        }
    }

    if (!target_file) {
        /* Create new file group */
        FileToc *temp = realloc(*files, sizeof(FileToc) * (size_t)(*file_count + 1));
        if (!temp) {
            fprintf(stderr, "Error: Failed to allocate memory for file group\n");
            return -1;
        }
        *files = temp;
        target_file = &(*files)[*file_count];
        target_file->filepath = try_strdup_ctx(filepath, "Failed to allocate memory for filepath");
        if (!target_file->filepath) {
            return -1;
        }
        target_file->entries = NULL;
        target_file->entry_count = 0;
        target_file->entry_capacity = 0;
        (*file_count)++;
    }

    /* Check per-file limit */
    if (limit_per_file > 0 && target_file->entry_count >= limit_per_file) {
        return 0;  /* Skip this entry - file has reached its limit */
    }

    /* Add entry to file */
    if (target_file->entry_count >= target_file->entry_capacity) {
        int new_capacity = target_file->entry_capacity == 0 ? TOC_INITIAL_CAPACITY : target_file->entry_capacity * 2;
        TocEntry *temp = realloc(target_file->entries, sizeof(TocEntry) * (size_t)new_capacity);
        if (!temp) {
            fprintf(stderr, "Error: Failed to allocate memory for TOC entries\n");
            return -1;
        }
        target_file->entries = temp;
        target_file->entry_capacity = new_capacity;
    }

    TocEntry *entry = &target_file->entries[target_file->entry_count];
    entry->symbol = try_strdup_ctx(symbol, "Failed to allocate memory for symbol");
    if (!entry->symbol) {
        return -1;
    }
    entry->start_line = start_line;
    entry->end_line = end_line;
    entry->context = try_strdup_ctx(context, "Failed to allocate memory for context");
    if (!entry->context) {
        free(entry->symbol);
        return -1;
    }
    target_file->entry_count++;
    return 1;  /* Entry was added */
}

/* Compare function for sorting by line number */
static int compare_by_line(const void *a, const void *b) {
    TocEntry *ea = *(TocEntry * const *)a;
    TocEntry *eb = *(TocEntry * const *)b;
    return ea->start_line - eb->start_line;
}

/* Print a section of TOC entries */
static void print_section(const char *title, TocEntry *entries, int count, const char *context_filter) {
    /* Collect matching entries */
    TocEntry **filtered = malloc(sizeof(TocEntry*) * (size_t)count);
    if (!filtered) {
        fprintf(stderr, "Error: Failed to allocate memory for TOC filtered entries\n");
        return;
    }
    int filtered_count = 0;

    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i].context, context_filter) == 0) {
            filtered[filtered_count++] = &entries[i];
        }
    }

    if (filtered_count == 0) {
        free(filtered);
        return;  /* Skip empty sections */
    }

    /* Sort by line number */
    qsort(filtered, (size_t)filtered_count, sizeof(TocEntry*), compare_by_line);

    printf("%s (%d):\n", title, filtered_count);
    for (int i = 0; i < filtered_count; i++) {
        /* Calculate padding for dots */
        int symbol_len = (int)strlen(filtered[i]->symbol);
        int line_len = snprintf(NULL, 0, "%d", filtered[i]->start_line);
        int dots_needed = 70 - symbol_len - line_len - 3; /* 3 for spaces */
        if (dots_needed < 1) dots_needed = 1;

        printf("  %s ", filtered[i]->symbol);
        for (int j = 0; j < dots_needed; j++) {
            printf(".");
        }
        printf(" %d\n", filtered[i]->start_line);
    }
    printf("\n");

    free(filtered);
}

/* Print imports on a single line */
static void print_imports(TocEntry *entries, int count) {
    /* Collect import entries */
    TocEntry **imports = malloc(sizeof(TocEntry*) * (size_t)count);
    if (!imports) {
        fprintf(stderr, "Error: Failed to allocate memory for TOC imports\n");
        return;
    }
    int import_count = 0;

    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i].context, "IMP") == 0) {
            /* Check for duplicates */
            int is_duplicate = 0;
            for (int j = 0; j < import_count; j++) {
                if (strcmp(imports[j]->symbol, entries[i].symbol) == 0) {
                    is_duplicate = 1;
                    break;
                }
            }
            if (!is_duplicate) {
                imports[import_count++] = &entries[i];
            }
        }
    }

    if (import_count == 0) {
        free(imports);
        return;
    }

    printf("IMPORTS: ");
    for (int i = 0; i < import_count; i++) {
        printf("%s", imports[i]->symbol);
        if (i < import_count - 1) printf(", ");
    }
    printf("\n\n");

    free(imports);
}

/* Print complete TOC */
static void print_toc(FileToc *files, int file_count) {
    for (int i = 0; i < file_count; i++) {
        FileToc *file = &files[i];

        printf("%s:\n\n", file->filepath);

        /* Print imports first (special formatting) */
        print_imports(file->entries, file->entry_count);

        /* Print sections in order */
        print_section("CLASSES", file->entries, file->entry_count, "CLASS");
        print_section("FUNCTIONS", file->entries, file->entry_count, "FUNC");
        print_section("ENUMS", file->entries, file->entry_count, "ENUM");
        print_section("TYPES", file->entries, file->entry_count, "TYPE");

        if (i < file_count - 1) {
            printf("\n");  /* Blank line between files */
        }
    }
}

/* Cleanup allocated memory */
static void cleanup_files(FileToc *files, int file_count) {
    for (int i = 0; i < file_count; i++) {
        free(files[i].filepath);
        for (int j = 0; j < files[i].entry_count; j++) {
            free(files[i].entries[j].symbol);
            free(files[i].entries[j].context);
        }
        free(files[i].entries);
    }
    free(files);
}

/* Main TOC generation function */
int build_toc(const TocConfig *config) {
    /* Validate config */
    if (config->file_pattern_count == 0) {
        fprintf(stderr, "Error: --toc requires -f <file_pattern>\n");
        return -1;
    }

    /* Open database */
    sqlite3 *db;
    if (sqlite3_open(config->db_path, &db) != SQLITE_OK) {
        fprintf(stderr, "Error: Cannot open database: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    /* Query 1: Get counts for summary (all matching symbols, ignore limit) */
    char *base_query = build_toc_query(config);
    char count_query[9216];
    snprintf(count_query, sizeof(count_query),
             "SELECT context, COUNT(*) FROM (%s) GROUP BY context", base_query);

    sqlite3_stmt *count_stmt;
    if (sqlite3_prepare_v2(db, count_query, -1, &count_stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to prepare count query: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }

    /* Collect counts by context type */
    typedef struct { char type[16]; int count; } ContextCount;
    ContextCount counts[20];
    int count_types = 0;
    int total_symbols = 0;

    while (sqlite3_step(count_stmt) == SQLITE_ROW && count_types < 20) {
        const char *ctx = (const char *)sqlite3_column_text(count_stmt, 0);
        int cnt = sqlite3_column_int(count_stmt, 1);
        strncpy(counts[count_types].type, ctx, sizeof(counts[0].type) - 1);
        counts[count_types].count = cnt;
        count_types++;
        total_symbols += cnt;
    }
    sqlite3_finalize(count_stmt);

    /* Query 2: Get actual symbols
     * Note: Don't apply LIMIT in SQL when using --limit-per-file, since per-file
     * filtering may skip some rows. Apply limit in the loop instead. */
    char limited_query[9216];
    if (config->limit > 0 && config->limit_per_file == 0) {
        /* Only apply SQL LIMIT if not using per-file limiting */
        snprintf(limited_query, sizeof(limited_query), "%s LIMIT %d", base_query, config->limit);
    } else {
        strncpy(limited_query, base_query, sizeof(limited_query) - 1);
    }

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, limited_query, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to prepare query: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }

    /* Collect results grouped by file */
    FileToc *files = NULL;
    int file_count = 0;
    int symbols_shown = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *symbol = (const char *)sqlite3_column_text(stmt, 0);
        int line = sqlite3_column_int(stmt, 1);
        const char *source_loc = (const char *)sqlite3_column_text(stmt, 2);
        const char *context = (const char *)sqlite3_column_text(stmt, 3);
        const char *directory = (const char *)sqlite3_column_text(stmt, 4);
        const char *filename = (const char *)sqlite3_column_text(stmt, 5);

        /* Construct filepath from directory and filename */
        char filepath[PATH_MAX_LENGTH];
        snprintf(filepath, sizeof(filepath), "%s%s", directory, filename);

        /* Parse source location for end line using existing utility */
        int start_line, start_column, end_line, end_column;
        int added = 0;
        if (source_loc && strlen(source_loc) > 0 &&
            parse_source_location(source_loc, &start_line, &start_column, &end_line, &end_column) == 0) {
            /* Successfully parsed source location */
            added = add_entry_to_file(&files, &file_count, filepath,
                            symbol, start_line, end_line, context, config->limit_per_file);
        } else {
            /* Fallback: use line column for single-line display */
            added = add_entry_to_file(&files, &file_count, filepath,
                            symbol, line, line, context, config->limit_per_file);
        }
        if (added < 0) {
            /* Allocation error */
            sqlite3_finalize(stmt);
            sqlite3_close(db);
            /* Free allocated memory */
            for (int i = 0; i < file_count; i++) {
                free(files[i].filepath);
                for (int j = 0; j < files[i].entry_count; j++) {
                    free(files[i].entries[j].symbol);
                    free(files[i].entries[j].context);
                }
                free(files[i].entries);
            }
            free(files);
            return -1;
        }
        if (added > 0) {
            symbols_shown++;
            /* Check if we've hit the global limit */
            if (config->limit > 0 && symbols_shown >= config->limit) {
                break;
            }
        }
    }

    /* Check if we found anything */
    if (file_count == 0) {
        /* Check if user requested unsupported context types */
        if (config->include_context_count > 0) {
            int has_unsupported = 0;
            for (int i = 0; i < config->include_context_count; i++) {
                const char *ctx = config->include_contexts[i];
                /* Convert to uppercase for comparison */
                char ctx_upper[64];
                for (int j = 0; ctx[j] && j < 63; j++) {
                    ctx_upper[j] = (ctx[j] >= 'a' && ctx[j] <= 'z') ? ctx[j] - 32 : ctx[j];
                    ctx_upper[j + 1] = '\0';
                }

                /* Check against TOC_ALLOWED_CONTEXTS */
                int is_allowed = 0;
                for (int k = 0; TOC_ALLOWED_CONTEXTS[k] != NULL; k++) {
                    if (strcmp(ctx_upper, TOC_ALLOWED_CONTEXTS[k]) == 0) {
                        is_allowed = 1;
                        break;
                    }
                    /* Also check common aliases */
                    if ((strcmp(ctx_upper, "FUNCTION") == 0 && strcmp(TOC_ALLOWED_CONTEXTS[k], "FUNC") == 0) ||
                        (strcmp(ctx_upper, "IMPORT") == 0 && strcmp(TOC_ALLOWED_CONTEXTS[k], "IMP") == 0)) {
                        is_allowed = 1;
                        break;
                    }
                }

                if (!is_allowed) {
                    has_unsupported = 1;
                    break;
                }
            }

            if (has_unsupported) {
                printf("--toc does not support all requested context types.\n");
                printf("Allowed context types for --toc: ");
                /* Dynamically build the list from TOC_ALLOWED_CONTEXTS */
                for (int k = 0; TOC_ALLOWED_CONTEXTS[k] != NULL; k++) {
                    /* Skip FILE since it's automatic */
                    if (strcmp(TOC_ALLOWED_CONTEXTS[k], "FILE") != 0) {
                        printf("%s%s", TOC_ALLOWED_CONTEXTS[k],
                               TOC_ALLOWED_CONTEXTS[k + 1] != NULL && strcmp(TOC_ALLOWED_CONTEXTS[k + 1], "FILE") != 0 ? ", " : "");
                    }
                }
                printf("\n");
                printf("To see all symbols in a file, use without --toc: qi %% -f <file>\n");
            } else {
                printf("No definitions found matching the criteria.\n");
            }
        } else {
            printf("No definitions found matching the criteria.\n");
        }
    } else {
        /* Print summary header */
        printf("Result breakdown: ");
        for (int i = 0; i < count_types; i++) {
            printf("%s (%d)%s", counts[i].type, counts[i].count,
                   i < count_types - 1 ? ", " : "\n");
        }
        printf("\n");

        /* Print formatted output */
        print_toc(files, file_count);

        /* Print limit indicator if we hit the limit or per-file limit reduced results */
        if ((config->limit > 0 && symbols_shown >= config->limit && total_symbols > symbols_shown) ||
            (config->limit_per_file > 0 && total_symbols > symbols_shown)) {
            printf("\n[Limit reached: %d symbols shown, %d total available]\n",
                   symbols_shown, total_symbols);
        }
    }

    /* Cleanup */
    cleanup_files(files, file_count);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return 0;
}
