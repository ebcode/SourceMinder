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
#include "string_utils.h"
#include "constants.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <execinfo.h>  /* For backtrace() */
#include <unistd.h>    /* For readlink() */

size_t strnlength(const char *s, size_t n)
{
    const char *found = memchr(s, '\0', n);
    return found ? (size_t)(found - s) : n;
}

void to_upper(char *str) {
    for (int i = 0; str[i]; i++) {
        str[i] = TOUPPERCASE(str[i]);
    }
}

void to_lower(char *str) {
    for (int i = 0; str[i]; i++) {
        str[i] = TOLOWERCASE(str[i]);
    }
}

void to_lowercase_copy(const char *src, char *dst, size_t size) {
    size_t i;
    for (i = 0; src[i] && i < size - 1; i++) {
        dst[i] = TOLOWERCASE(src[i]);
    }
    dst[i] = '\0';
}

void safe_extract_node_text(const char *source_code, TSNode node, char *buffer,
                            size_t buffer_size, const char *filename) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    uint32_t length = end - start;

    /* Check if text will fit (need length + 1 for null terminator) */
    if (length >= buffer_size) {
        /* Extract preview for error message */
        char preview[100];
        uint32_t preview_len = length < sizeof(preview) - 1 ? length : sizeof(preview) - 1;
        memcpy(preview, source_code + start, preview_len);
        preview[preview_len] = '\0';

        TSPoint start_point = ts_node_start_point(node);
        const char *node_type = ts_node_type(node);

        fprintf(stderr, "\n========== EXTRACTION ERROR ==========\n");
        fprintf(stderr, "File: %s\n", filename ? filename : "<unknown>");
        fprintf(stderr, "Location: line %u, column %u\n", start_point.row + 1, start_point.column + 1);
        fprintf(stderr, "Node type: %s\n", node_type ? node_type : "<unknown>");
        fprintf(stderr, "Text length: %u bytes\n", length);
        fprintf(stderr, "Buffer size: %zu bytes (max: %zu)\n", buffer_size, buffer_size - 1);
        fprintf(stderr, "Preview: %s%s\n", preview, length >= sizeof(preview) - 1 ? "..." : "");
        fprintf(stderr, "\nThis text is too long to fit in the buffer.\n");
        fprintf(stderr, "Buffer size is controlled by the calling code.\n");
        fprintf(stderr, "If this seems like a bug, the extraction logic may need fixing.\n");

        /* Print stack trace to help identify the caller */
        fprintf(stderr, "\nStack trace:\n");
        void *stack_frames[20];
        int frame_count = backtrace(stack_frames, 20);
        char **symbols = backtrace_symbols(stack_frames, frame_count);

        if (symbols) {
            for (int i = 0; i < frame_count; i++) {
                /* Parse the symbol string to extract offset
                 * Format: ./index-go(+0x24f42) [0x59e67167ff42]
                 * We want the +0x24f42 part */
                char *open_paren = strchr(symbols[i], '(');
                char *close_paren = strchr(symbols[i], ')');

                fprintf(stderr, "  [%d] %s\n", i, symbols[i]);

                if (open_paren && close_paren && close_paren > open_paren) {
                    /* Extract the offset */
                    char offset_str[64];
                    size_t offset_len = (size_t)(close_paren - open_paren - 1);
                    if (offset_len > 0 && offset_len < sizeof(offset_str)) {
                        memcpy(offset_str, open_paren + 1, offset_len);
                        offset_str[offset_len] = '\0';

                        /* Get executable path */
                        char exe_path[EXECUTABLE_PATH_BUFFER];
                        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
                        if (len != -1) {
                            exe_path[len] = '\0';

                            /* Run addr2line with the offset */
                            char cmd[2048];
                            snprintf(cmd, sizeof(cmd), "addr2line -e '%s' -f -C -p %s 2>/dev/null",
                                     exe_path, offset_str);

                            FILE *fp = popen(cmd, "r");
                            if (fp) {
                                char line[LINE_BUFFER_LARGE];
                                if (fgets(line, sizeof(line), fp)) {
                                    /* Remove trailing newline */
                                    size_t line_len = strlen(line);
                                    if (line_len > 0 && line[line_len - 1] == '\n') {
                                        line[line_len - 1] = '\0';
                                    }
                                    /* Only print if it's not just "??" (unknown) */
                                    if (strstr(line, "??") == NULL) {
                                        fprintf(stderr, "      -> %s\n", line);
                                    }
                                }
                                pclose(fp);
                            }
                        }
                    }
                }
            }
            free(symbols);
        } else {
            fprintf(stderr, "  (backtrace_symbols failed)\n");
        }

        fprintf(stderr, "======================================\n");
        exit(1);
    }

    /* Safe to copy */
    memcpy(buffer, source_code + start, length);
    buffer[length] = '\0';
}

void format_source_location(TSNode node, char *buffer, size_t buffer_size) {
    TSPoint start = ts_node_start_point(node);
    TSPoint end = ts_node_end_point(node);
    snprintf(buffer, buffer_size, "%u:%u - %u:%u",
             start.row + 1, start.column,
             end.row + 1, end.column);
}

char *safe_strdup_ctx(const char *str, const char *err_msg) {
    if (!str) {
        fprintf(stderr, "Error: Attempting to duplicate NULL string\n");
        if (err_msg) {
            fprintf(stderr, "Context: %s\n", err_msg);
        }
        exit(1);
    }

    char *result = strdup(str);
    if (!result) {
        fprintf(stderr, "Error: %s\n", err_msg ? err_msg : "Failed to allocate memory");
        exit(1);
    }

    return result;
}

char *try_strdup_ctx(const char *str, const char *err_msg) {
    if (!str) {
        fprintf(stderr, "Error: Attempting to duplicate NULL string\n");
        if (err_msg) {
            fprintf(stderr, "Context: %s\n", err_msg);
        }
        return NULL;
    }

    char *result = strdup(str);
    if (!result) {
        fprintf(stderr, "Error: %s\n", err_msg ? err_msg : "Failed to allocate memory");
        return NULL;
    }

    return result;
}

const char *skip_leading_char(const char *str, char ch) {
    if (str && str[0] == ch && str[1] != '\0') {
        return str + 1;
    }
    return str;
}
