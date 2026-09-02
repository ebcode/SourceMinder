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

#if defined(__GLIBC__) || defined(__APPLE__)
#include <execinfo.h>  /* For backtrace() */
#include <unistd.h>    /* For readlink() */
#define HAS_BACKTRACE 1
#else
/* execinfo.h/backtrace() are glibc/macOS extensions; not available on
 * Windows or musl (static release builds) */
#define HAS_BACKTRACE 0
#endif

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

void pluralize_common_word(const char *word, char *output, size_t output_size) {
    if (strcmp(word, "match") == 0) {
        snprintf(output, output_size, "matches");
        return;
    }

    snprintf(output, output_size, "%ss", word);
}

void warn_oversized_symbol(const char *desc, size_t length,
                           const char *content, unsigned int line,
                           const char *filename) {
    /* Short preview of the offending content (safe for non-NUL-terminated input).
     * Control bytes become spaces so one warning stays one grep-able line. */
    char preview[17];
    size_t preview_len = length < sizeof(preview) - 1 ? length : sizeof(preview) - 1;
    memcpy(preview, content, preview_len);
    preview[preview_len] = '\0';
    for (size_t i = 0; i < preview_len; i++) {
        if ((unsigned char)preview[i] < 0x20 || (unsigned char)preview[i] == 0x7F) {
            preview[i] = ' ';
        }
    }

    fprintf(stderr,
            "Warning: indexer skipping oversized %s (%zu bytes, over SYMBOL_MAX_LENGTH): "
            "'%s...' on LINE: %u of FILE: %s\n",
            desc ? desc : "<unknown>", length, preview,
            line, filename ? filename : "<unknown>");
}

void safe_extract_node_text(const char *source_code, TSNode node, char *buffer,
                            size_t buffer_size, const char *filename) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    uint32_t length = end - start;

    /* Too long to fit (need length + 1 for the null terminator). Rather than
     * abort the whole index run, warn and hand back an empty string so the
     * caller skips it (empty symbols are never indexed). */
    if (length >= buffer_size) {
        TSPoint pt = ts_node_start_point(node);
        const char *node_type = ts_node_type(node);
        warn_oversized_symbol(node_type ? node_type : "<unknown>", length,
                              source_code + start, pt.row + 1, filename);
        buffer[0] = '\0';
        return;
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

int next_config_line(const char **cursor, char *buf, size_t bufsize) {
    const char *p = *cursor;
    if (!p || *p == '\0' || bufsize == 0) {
        return 0;
    }

    size_t i = 0;
    while (*p != '\0' && *p != '\n' && i < bufsize - 1) {
        buf[i++] = *p++;
    }
    if (*p == '\n') {
        p++;
    }
    buf[i] = '\0';
    *cursor = p;
    return 1;
}
