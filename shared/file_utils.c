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
 * along with Foobar. If not, see <https://www.gnu.org/licenses/>.
 */
#include "file_utils.h"
#include "string_utils.h"
#include "constants.h"
#include "file_opener.h"
#include <string.h>
#include <stdio.h>

void get_relative_path(const char *filepath, const char *project_root,
                      char *directory, char *filename) {
    const char *rel = filepath;
    size_t root_len = strnlength(project_root, PATH_MAX_LENGTH);
    if (strncmp(filepath, project_root, root_len) == 0) {
        rel = filepath + root_len;
        if (rel[0] == '/') rel++;
    }

    const char *last_slash = strrchr(rel, '/');
    if (last_slash) {
        size_t dir_len = (size_t)(last_slash - rel + 1);
        snprintf(directory, DIRECTORY_MAX_LENGTH, "%.*s", (int)dir_len, rel);
        snprintf(filename, FILENAME_MAX_LENGTH, "%s", last_slash + 1);
    } else {
        /* Files in current directory get './' prefix */
        snprintf(directory, DIRECTORY_MAX_LENGTH, "./");
        snprintf(filename, FILENAME_MAX_LENGTH, "%s", rel);
    }
}

int parse_source_location(const char *source_location, int *start_line, int *start_column,
                         int *end_line, int *end_column) {
    if (!source_location || !start_line || !start_column || !end_line || !end_column) {
        return -1;
    }

    /* Parse format: "startRow:startCol - endRow:endCol" */
    if (sscanf(source_location, "%d:%d - %d:%d",
               start_line, start_column, end_line, end_column) != 4) {
        fprintf(stderr, "Warning: Invalid source_location format: %s\n", source_location);
        return -1;
    }

    return 0;
}

int print_lines_range(const char *filepath, int start_line, int end_line,
                     int start_column, int end_column) {
    if (!filepath || start_line < 1 || end_line < start_line) {
        return -1;
    }

    /* Open file */
    FILE *fp = safe_fopen(filepath, "r", 1);  /* silent=1 */
    if (!fp) {
        fprintf(stderr, "Warning: Could not read file '%s' for full definition\n", filepath);
        return -1;
    }

    /* Read and print lines from start_line to end_line */
    char line[4096];
    int current_line = 0;

    printf("--\n");
    while (fgets(line, sizeof(line), fp)) {
        current_line++;
        if (current_line >= start_line && current_line <= end_line) {
            /* Handle column boundaries */
            if (current_line == start_line && current_line == end_line) {
                /* Single line: respect both start and end columns */
                printf("%d:", current_line);
                for (int i = start_column; i <= end_column && line[i] != '\0'; i++) {
                    putchar(line[i]);
                }
                if (line[end_column] != '\n' && line[end_column] != '\0') {
                    putchar('\n');
                }
            } else if (current_line == start_line) {
                /* First line: skip characters before start_column */
                printf("%d:%s", current_line, &line[start_column]);
            } else if (current_line == end_line) {
                /* Last line: print up to end_column */
                printf("%d:", current_line);
                for (int i = 0; i <= end_column && line[i] != '\0'; i++) {
                    putchar(line[i]);
                }
                if (line[end_column] != '\n' && line[end_column] != '\0') {
                    putchar('\n');
                }
            } else {
                /* Middle lines: print entire line */
                printf("%d:%s", current_line, line);
            }
        }
        if (current_line > end_line) {
            break;  /* No need to continue reading */
        }
    }

    fclose(fp);
    return 0;
}
