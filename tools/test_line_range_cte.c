/*
 * Test prototype for --and with line range using CTE self-join approach
 *
 * Usage: ./test_line_range_cte ANCHOR_PATTERN PATTERN2 [PATTERN3...] RANGE
 * Example: ./test_line_range_cte malloc free 10
 *
 * Finds ANCHOR_PATTERN, then searches for all other patterns within ±RANGE lines
 * Uses single SQL query with CTEs instead of N+1 queries
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
    char directory[PATH_MAX_LENGTH];
    char filename[PATH_MAX_LENGTH];
    int anchor_line;
    int line_number;
    char symbol[256];
    char context_type[32];
} ResultRow;

/* Print a result row */
void print_result_row(const ResultRow *row) {
    printf("  %s%s:%d | %s | %s\n",
           row->directory,
           row->filename,
           row->line_number,
           row->symbol,
           row->context_type);
}

/* Build CTE-based single query for line range search */
char* build_cte_query(const char *anchor_pattern, const PatternList *secondary_patterns,
                      int range, char *buffer, size_t buffer_size) {
    int offset = 0;

    /* CTE 1: Find all anchors */
    offset += snprintf(buffer + offset, buffer_size - offset,
        "WITH anchors AS (\n"
        "  SELECT directory, filename, line_number as anchor_line, symbol\n"
        "  FROM code_index\n"
        "  WHERE symbol LIKE ?\n"
        "),\n");

    /* CTE 2: Build secondaries with UNION ALL for each pattern */
    offset += snprintf(buffer + offset, buffer_size - offset,
        "secondaries AS (\n");

    for (int i = 0; i < secondary_patterns->count; i++) {
        if (i > 0) {
            offset += snprintf(buffer + offset, buffer_size - offset, "  UNION ALL\n");
        }
        offset += snprintf(buffer + offset, buffer_size - offset,
            "  SELECT directory, filename, line_number, symbol, context_type, ? as pattern_name\n"
            "  FROM code_index\n"
            "  WHERE symbol LIKE ?\n");
    }
    offset += snprintf(buffer + offset, buffer_size - offset, ")\n");

    /* Main query: Return both anchors and secondaries for matching locations */
    offset += snprintf(buffer + offset, buffer_size - offset,
        "SELECT\n"
        "  a.directory,\n"
        "  a.filename,\n"
        "  a.anchor_line,\n"
        "  a.anchor_line as line_number,\n"
        "  a.symbol,\n"
        "  'ANCHOR' as context_type\n"
        "FROM anchors a\n"
        "WHERE EXISTS (\n"
        "  SELECT 1\n"
        "  FROM secondaries s\n"
        "  WHERE s.directory = a.directory\n"
        "    AND s.filename = a.filename\n"
        "    AND s.line_number BETWEEN (a.anchor_line - ?) AND (a.anchor_line + ?)\n"
        "  GROUP BY s.directory, s.filename\n"
        "  HAVING COUNT(DISTINCT s.pattern_name) = ?\n"
        ")\n"
        "UNION ALL\n"
        "SELECT\n"
        "  s.directory,\n"
        "  s.filename,\n"
        "  a.anchor_line,\n"
        "  s.line_number,\n"
        "  s.symbol,\n"
        "  s.context_type\n"
        "FROM anchors a\n"
        "INNER JOIN secondaries s\n"
        "  ON s.directory = a.directory\n"
        "  AND s.filename = a.filename\n"
        "  AND s.line_number BETWEEN (a.anchor_line - ?) AND (a.anchor_line + ?)\n"
        "WHERE EXISTS (\n"
        "  SELECT 1\n"
        "  FROM secondaries s2\n"
        "  WHERE s2.directory = a.directory\n"
        "    AND s2.filename = a.filename\n"
        "    AND s2.line_number BETWEEN (a.anchor_line - ?) AND (a.anchor_line + ?)\n"
        "  GROUP BY s2.directory, s2.filename\n"
        "  HAVING COUNT(DISTINCT s2.pattern_name) = ?\n"
        ")\n"
        "ORDER BY directory, filename, anchor_line, line_number");

    return buffer;
}

/* Execute CTE-based single query and collect results */
int search_with_cte(sqlite3 *db, const char *anchor_pattern, const PatternList *secondary_patterns,
                    int range, ResultRow **results, int *result_count) {
    char sql[8192];
    build_cte_query(anchor_pattern, secondary_patterns, range, sql, sizeof(sql));

    printf("\n=== CTE-based Single Query ===\n");
    printf("SQL:\n%s\n\n", sql);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare CTE query: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    /* Bind parameters */
    int param_idx = 1;

    /* Bind anchor pattern */
    sqlite3_bind_text(stmt, param_idx++, anchor_pattern, -1, SQLITE_STATIC);

    /* Bind secondary patterns (pattern_name and symbol LIKE for each) */
    for (int i = 0; i < secondary_patterns->count; i++) {
        sqlite3_bind_text(stmt, param_idx++, secondary_patterns->patterns[i], -1, SQLITE_STATIC);  /* pattern_name */
        sqlite3_bind_text(stmt, param_idx++, secondary_patterns->patterns[i], -1, SQLITE_STATIC);  /* symbol LIKE */
    }

    /* Bind range values (6 times: 2 in first EXISTS, 2 in UNION JOIN, 2 in second EXISTS) */
    sqlite3_bind_int(stmt, param_idx++, range);
    sqlite3_bind_int(stmt, param_idx++, range);

    /* Bind count of secondary patterns (for first HAVING clause) */
    sqlite3_bind_int(stmt, param_idx++, secondary_patterns->count);

    /* Bind range values for UNION ALL part */
    sqlite3_bind_int(stmt, param_idx++, range);
    sqlite3_bind_int(stmt, param_idx++, range);
    sqlite3_bind_int(stmt, param_idx++, range);
    sqlite3_bind_int(stmt, param_idx++, range);

    /* Bind count of secondary patterns (for second HAVING clause) */
    sqlite3_bind_int(stmt, param_idx++, secondary_patterns->count);

    /* Allocate results array */
    int capacity = 100;
    *results = malloc(sizeof(ResultRow) * capacity);
    *result_count = 0;

    printf("Results:\n");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        /* Expand array if needed */
        if (*result_count >= capacity) {
            capacity *= 2;
            *results = realloc(*results, sizeof(ResultRow) * capacity);
        }

        ResultRow *row = &(*results)[*result_count];

        /* Extract columns */
        const char *dir = (const char *)sqlite3_column_text(stmt, 0);
        const char *file = (const char *)sqlite3_column_text(stmt, 1);
        int anchor_line = sqlite3_column_int(stmt, 2);
        int line_num = sqlite3_column_int(stmt, 3);
        const char *sym = (const char *)sqlite3_column_text(stmt, 4);
        const char *ctx = (const char *)sqlite3_column_text(stmt, 5);

        /* Store in result row */
        snprintf(row->directory, sizeof(row->directory), "%s", dir ? dir : "");
        snprintf(row->filename, sizeof(row->filename), "%s", file ? file : "");
        row->anchor_line = anchor_line;
        row->line_number = line_num;
        snprintf(row->symbol, sizeof(row->symbol), "%s", sym ? sym : "");
        snprintf(row->context_type, sizeof(row->context_type), "%s", ctx ? ctx : "");

        print_result_row(row);
        (*result_count)++;
    }

    sqlite3_finalize(stmt);
    printf("\nTotal results: %d\n", *result_count);

    return 0;
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

    printf("=== Line Range Search Test (CTE Approach) ===\n");
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

    /* Execute single CTE-based query */
    ResultRow *results = NULL;
    int result_count = 0;

    if (search_with_cte(db, anchor_pattern, &secondary, range, &results, &result_count) != 0) {
        sqlite3_close(db);
        return 1;
    }

    if (result_count == 0) {
        printf("\nNo matches found.\n");
        sqlite3_close(db);
        return 0;
    }

    /* Count unique anchor lines (complete matches) */
    int complete_matches = 0;
    int last_anchor_line = -1;
    const char *last_file = "";

    for (int i = 0; i < result_count; i++) {
        char filepath[PATH_MAX_LENGTH];
        snprintf(filepath, sizeof(filepath), "%s%s", results[i].directory, results[i].filename);

        if (results[i].anchor_line != last_anchor_line || strcmp(filepath, last_file) != 0) {
            complete_matches++;
            last_anchor_line = results[i].anchor_line;
            last_file = filepath;
        }
    }

    printf("\n=== Summary ===\n");
    printf("Complete matches (anchor locations with all patterns): %d\n", complete_matches);
    printf("Total symbols returned: %d\n", result_count);

    /* Cleanup */
    free(results);
    sqlite3_close(db);

    return 0;
}
