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
#ifndef WEB_OUTPUT_H
#define WEB_OUTPUT_H

/* Growable output accumulator shared by the web/WASM bridge.
 *
 * The WASM build has no stdout; format and source-render code append text here
 * instead of calling printf, and the entry point hands the buffer to JS via
 * wo_steal().  Defined here (rather than file-static in each module) so the
 * source-render twins can append directly into qi_web_format's buffer.
 *
 * Header-only static inline: each translation unit gets its own copy with no
 * linker conflict, while every unit agrees on the WebOutput layout. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WO_INITIAL_CAP 4096
#define WO_GROW_FACTOR 2

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    int error;     /* sticky: once set, every op is a no-op and wo_steal yields NULL */
} WebOutput;

static inline int wo_init(WebOutput *wo) {
    wo->cap = WO_INITIAL_CAP;
    wo->buf = malloc(wo->cap);
    if (!wo->buf) { wo->error = 1; return -1; }
    wo->buf[0] = '\0';
    wo->len = 0;
    wo->error = 0;
    return 0;
}

static inline int wo_grow(WebOutput *wo, size_t needed) {
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

static inline int wo_printf(WebOutput *wo, const char *fmt, ...) {
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

/* Append raw bytes verbatim (no NUL needed in src, no format parsing).
 * Used for source lines, which contain arbitrary bytes including '%'. */
static inline int wo_write(WebOutput *wo, const char *data, size_t n) {
    if (wo->error) return -1;
    if (n == 0) return 0;
    if (wo->len + n + 1 > wo->cap) {
        if (wo_grow(wo, n) != 0) { wo->error = 1; return -1; }
    }
    memcpy(wo->buf + wo->len, data, n);
    wo->len += n;
    wo->buf[wo->len] = '\0';
    return 0;
}

static inline int wo_putc(WebOutput *wo, char c) {
    return wo_write(wo, &c, 1);
}

static inline char *wo_steal(WebOutput *wo) {
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

static inline void wo_free(WebOutput *wo) {
    free(wo->buf);
    wo->buf = NULL;
    wo->len = 0;
    wo->cap = 0;
}

#endif /* WEB_OUTPUT_H */
