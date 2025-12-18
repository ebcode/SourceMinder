/*
 * Test prototype for --and with line range
 *
 * Usage: ./test_line_range ANCHOR_PATTERN PATTERN2 [PATTERN3...] RANGE
 * Example: ./test_line_range malloc free 10
 *
 * Finds ANCHOR_PATTERN, then searches for all other patterns within ±RANGE lines
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

#define MAX_PATTERNS 10
#define PATH_MAX_LENGTH 4096

typedef struct {
    const char *patterns[MAX_PATTERNS];
    int count;
} PatternList;

typedef struct {
    char filepath[PATH_MAX_LENGTH];
    int anchor_line;
    const char *anchor_symbol;
} AnchorMatch;

/* Print a result row */
void print_row(sqlite3_stmt *stmt) {
    const char *symbol = (const char *)sqlite3_column_text(stmt, 0);
    const char *directory = (const char *)sqlite3_column_text(stmt, 1);
    const char *filename = (const char *)sqlite3_column_text(stmt, 2);
    int line = sqlite3_column_int(stmt, 3);
    const char *context = (const char *)sqlite3_column_text(stmt, 4);

    printf("  %s%s:%d | %s | %s\n",
           directory ? directory : "",
           filename ? filename : "",
           line, symbol, context);
}

/* Find all occurrences of the anchor pattern */
int find_anchor_matches(sqlite3 *db, const char *anchor_pattern, AnchorMatch **matches, int *match_count) {
    char sql[1024];
    snprintf(sql, sizeof(sql),
             "SELECT symbol, directory, filename, line_number, context_type "
             "FROM code_index "
             "WHERE symbol LIKE ? "
             "ORDER BY directory, filename, line_number");

    printf("\n=== Finding anchor pattern: '%s' ===\n", anchor_pattern);
    printf("SQL: %s\n\n", sql);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare anchor query: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, anchor_pattern, -1, SQLITE_STATIC);

    /* Allocate initial matches array */
    int capacity = 100;
    *matches = malloc(sizeof(AnchorMatch) * capacity);
    *match_count = 0;

    printf("Anchor matches:\n");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *directory = (const char *)sqlite3_column_text(stmt, 1);
        const char *filename = (const char *)sqlite3_column_text(stmt, 2);
        int line = sqlite3_column_int(stmt, 3);
        const char *symbol = (const char *)sqlite3_column_text(stmt, 0);

        /* Expand array if needed */
        if (*match_count >= capacity) {
            capacity *= 2;
            *matches = realloc(*matches, sizeof(AnchorMatch) * capacity);
        }

        /* Store match */
        AnchorMatch *m = &(*matches)[*match_count];
        snprintf(m->filepath, sizeof(m->filepath), "%s%s",
                 directory ? directory : "", filename ? filename : "");
        m->anchor_line = line;
        m->anchor_symbol = strdup(symbol);
        (*match_count)++;

        print_row(stmt);
    }

    sqlite3_finalize(stmt);
    printf("Found %d anchor matches\n", *match_count);
    return 0;
}

/* For each anchor match, find secondary patterns within range */
int search_within_range(sqlite3 *db, AnchorMatch *anchor, PatternList *secondary_patterns,
                        int range, int debug) {
    /* Build SQL to find ALL secondary patterns within range */
    char sql[4096];
    int offset = 0;

    /* We need to find lines that contain ALL secondary patterns within range */
    /* Strategy: Use INTERSECT to find lines that match all patterns */

    offset += snprintf(sql + offset, sizeof(sql) - offset,
                      "SELECT symbol, directory, filename, line_number, context_type "
                      "FROM code_index WHERE ");

    /* File and line range constraint */
    offset += snprintf(sql + offset, sizeof(sql) - offset,
                      "filename = ? AND directory = ? AND "
                      "line_number BETWEEN ? AND ? AND (");

    /* OR together all secondary patterns */
    for (int i = 0; i < secondary_patterns->count; i++) {
        if (i > 0) {
            offset += snprintf(sql + offset, sizeof(sql) - offset, " OR ");
        }
        offset += snprintf(sql + offset, sizeof(sql) - offset, "symbol LIKE ?");
    }
    offset += snprintf(sql + offset, sizeof(sql) - offset, ") ORDER BY line_number");

    if (debug) {
        printf("\n--- Searching within range of anchor at line %d ---\n", anchor->anchor_line);
        printf("SQL: %s\n", sql);
    }

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare range query: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    /* Extract directory and filename from filepath */
    const char *filename = strrchr(anchor->filepath, '/');
    char directory[PATH_MAX_LENGTH] = "";
    if (filename) {
        filename++; /* Skip the '/' */
        size_t dir_len = filename - anchor->filepath;
        strncpy(directory, anchor->filepath, dir_len);
        directory[dir_len] = '\0';
    } else {
        filename = anchor->filepath;
    }

    /* Calculate line range (ensure non-negative) */
    int min_line = anchor->anchor_line - range;
    if (min_line < 1) min_line = 1;  /* Line numbers start at 1 */
    int max_line = anchor->anchor_line + range;

    if (debug) {
        printf("Binding: filename='%s', directory='%s', range=%d-%d\n",
               filename, directory, min_line, max_line);
    }

    /* Bind parameters */
    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, directory, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, min_line);
    sqlite3_bind_int(stmt, 4, max_line);

    /* Bind pattern parameters */
    for (int i = 0; i < secondary_patterns->count; i++) {
        sqlite3_bind_text(stmt, 5 + i, secondary_patterns->patterns[i], -1, SQLITE_STATIC);
    }

    /* Collect matches for each pattern to verify ALL are present */
    int pattern_found[MAX_PATTERNS] = {0};
    int found_count = 0;

    printf("\nMatches within range:\n");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *symbol = (const char *)sqlite3_column_text(stmt, 0);

        /* Check which pattern this matches */
        for (int i = 0; i < secondary_patterns->count; i++) {
            /* Simple LIKE comparison - could be more sophisticated */
            if (strstr(symbol, secondary_patterns->patterns[i]) != NULL ||
                strcmp(symbol, secondary_patterns->patterns[i]) == 0) {
                if (!pattern_found[i]) {
                    pattern_found[i] = 1;
                    found_count++;
                }
            }
        }

        print_row(stmt);
    }

    sqlite3_finalize(stmt);

    /* Check if ALL patterns were found */
    int all_found = (found_count == secondary_patterns->count);
    if (debug) {
        printf("Patterns found: %d/%d - %s\n",
               found_count, secondary_patterns->count,
               all_found ? "MATCH!" : "incomplete");
    }

    return all_found ? 1 : 0;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s ANCHOR_PATTERN PATTERN2 [PATTERN3...] RANGE\n", argv[0]);
        fprintf(stderr, "Example: %s malloc free 10\n", argv[0]);
        fprintf(stderr, "         %s malloc free realloc 15\n", argv[0]);
        return 1;
    }

    /* Parse arguments */
    const char *anchor_pattern = argv[1];
    int range = atoi(argv[argc - 1]);

    PatternList secondary;
    secondary.count = 0;
    for (int i = 2; i < argc - 1; i++) {
        if (secondary.count >= MAX_PATTERNS) {
            fprintf(stderr, "Too many patterns (max %d)\n", MAX_PATTERNS);
            return 1;
        }
        secondary.patterns[secondary.count++] = argv[i];
    }

    printf("=== Line Range Search Test ===\n");
    printf("Anchor pattern: %s\n", anchor_pattern);
    printf("Secondary patterns (%d): ", secondary.count);
    for (int i = 0; i < secondary.count; i++) {
        printf("%s%s", secondary.patterns[i], i < secondary.count - 1 ? ", " : "");
    }
    printf("\nRange: ±%d lines\n", range);

    /* Open database */
    sqlite3 *db;
    if (sqlite3_open("code-index.db", &db) != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    /* Step 1: Find all anchor matches */
    AnchorMatch *anchors = NULL;
    int anchor_count = 0;

    if (find_anchor_matches(db, anchor_pattern, &anchors, &anchor_count) != 0) {
        sqlite3_close(db);
        return 1;
    }

    if (anchor_count == 0) {
        printf("\nNo anchor matches found.\n");
        sqlite3_close(db);
        return 0;
    }

    /* Step 2: For each anchor, search for secondary patterns within range */
    printf("\n=== Searching for secondary patterns within range ===\n");
    int total_matches = 0;

    for (int i = 0; i < anchor_count; i++) {
        int matches = search_within_range(db, &anchors[i], &secondary, range, 1);
        if (matches) {
            printf("\n✓ Complete match at %s:%d\n",
                   anchors[i].filepath, anchors[i].anchor_line);
            total_matches++;
        }
    }

    printf("\n=== Summary ===\n");
    printf("Anchor matches: %d\n", anchor_count);
    printf("Complete matches (all patterns found): %d\n", total_matches);

    /* Cleanup */
    for (int i = 0; i < anchor_count; i++) {
        free((void *)anchors[i].anchor_symbol);
    }
    free(anchors);
    sqlite3_close(db);

    return 0;
}
