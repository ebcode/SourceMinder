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
#include "source-render-web.h"

#include <string.h>
#include <strings.h>  /* strncasecmp */
#include <stdio.h>    /* sscanf */

/* ANSI color codes: dark green background for match highlighting (matches CLI) */
static const char *GREEN = "\033[42m";
static const char *RESET = "\033[0m";

/* A literal pattern to highlight, with its length precomputed once per call. */
typedef struct {
    const char *s;
    size_t len;
} LiteralPattern;

/* Emit "<line_no>:" then line[from..to], clamped to the line, guaranteeing the
 * rendered line ends with '\n' so a column-trimmed line can't glue onto the
 * next.  Mirrors the CLI's "add a newline unless the cut point is already a
 * newline" rule, but uses the real line length instead of the CLI's
 * unconditional (out-of-range-prone) line[end_column] read. */
static void emit_trimmed(WebOutput *wo, int line_no, const char *line, size_t len,
                         int from, int to) {
    wo_printf(wo, "%d:", line_no);
    if (from < 0) from = 0;
    int last = -1;
    for (int i = from; i <= to && (size_t)i < len; i++) {
        wo_putc(wo, line[i]);
        last = i;
    }
    if (last < 0 || line[last] != '\n') wo_putc(wo, '\n');
}

/* Iterate lines over an in-memory file buffer, mirroring fgets():
 * returns a pointer to the start of the next line and sets *line_len to its
 * length *including* a trailing '\n' when present.  Advances *cursor.  Returns
 * NULL at end of buffer.  A file ending in '\n' yields no trailing empty line,
 * exactly as fgets()+EOF behaves -- this is what keeps line counting in step
 * with the CLI.
 *
 * NUL-terminated input (what the ccall string boundary gives us); embedded NULs
 * would truncate, same as the rest of the web bridge.  The deferred HEAPU8
 * (pointer+length) path would swap strchr for memchr. */
static const char *next_line(const char **cursor, size_t *line_len) {
    const char *p = *cursor;
    if (*p == '\0') return NULL;
    const char *nl = strchr(p, '\n');
    if (nl) {
        *line_len = (size_t)(nl - p) + 1;  /* include the '\n' */
        *cursor = nl + 1;
    } else {
        *line_len = strlen(p);
        *cursor = p + *line_len;
    }
    return p;
}

/* Twin of parse_source_location (shared/file_utils.c).  Re-implemented here --
 * rather than linking file_utils.c -- to keep host file I/O (safe_fopen,
 * print_lines_range) out of the WASM module.  Format: "start:col - end:col". */
static int parse_source_location_web(const char *s, int *sl, int *sc, int *el, int *ec) {
    if (!s || !sl || !sc || !el || !ec) return -1;
    if (sscanf(s, "%d:%d - %d:%d", sl, sc, el, ec) != 4) return -1;
    /* Reject nonsensical/stale metadata so it can't drive out-of-range trims. */
    if (*sl < 1 || *el < *sl || *sc < 0 || *ec < 0) return -1;
    return 0;
}

void print_lines_range_web(WebOutput *wo, const char *content, const char *filepath,
                           int start_line, int end_line,
                           int start_column, int end_column, int raw) {
    if (start_line < 1 || end_line < start_line) return;
    if (!content) {
        wo_printf(wo, "Warning: Could not read file '%s' for full definition\n",
                  filepath ? filepath : "");
        return;
    }

    const char *cursor = content;
    const char *line;
    size_t len;
    int current_line = 0;

    if (!raw) wo_printf(wo, "--\n");

    while ((line = next_line(&cursor, &len)) != NULL) {
        current_line++;
        if (current_line >= start_line && current_line <= end_line) {
            if (raw) {
                /* Raw mode: full lines, no prefix or column trimming */
                wo_write(wo, line, len);
            } else if (current_line == start_line && current_line == end_line) {
                /* Single line: respect both start and end columns */
                emit_trimmed(wo, current_line, line, len, start_column, end_column);
            } else if (current_line == start_line) {
                /* First line: skip characters before start_column.  As the first
                 * of a multi-line span it always carries its '\n'; the else
                 * guards stale start_column past EOL so it can't glue. */
                wo_printf(wo, "%d:", current_line);
                if ((size_t)start_column < len)
                    wo_write(wo, line + start_column, len - (size_t)start_column);
                else
                    wo_putc(wo, '\n');
            } else if (current_line == end_line) {
                /* Last line: print up to end_column */
                emit_trimmed(wo, current_line, line, len, 0, end_column);
            } else {
                /* Middle lines: print entire line */
                wo_printf(wo, "%d:", current_line);
                wo_write(wo, line, len);
            }
        }
        if (current_line > end_line) break;
    }
}

void print_context_lines_web(WebOutput *wo, const char *content, const char *filepath,
                             int target_line, char **patterns, int pattern_count,
                             int before, int after, int raw) {
    if (!content) {
        wo_printf(wo, "Warning: Could not read file '%s' for context lines "
                      "(file may have moved or permissions changed)\n",
                  filepath ? filepath : "");
        return;
    }

    int start_line = (target_line - before > 0) ? target_line - before : 1;
    int end_line = target_line + after;

    /* Precompute the literal patterns to highlight once, rather than re-scanning
     * patterns[] for every byte of every line.  Skip wildcard patterns (the CLI
     * highlights only literals), NULL entries, and empty strings -- an empty
     * pattern would match at every position and never advance the cursor. */
    LiteralPattern *lits = NULL;
    int nlits = 0;
    if (patterns && pattern_count > 0) {
        lits = malloc((size_t)pattern_count * sizeof(*lits));
        if (lits) {
            for (int p = 0; p < pattern_count; p++) {
                const char *pat = patterns[p];
                if (!pat || pat[0] == '\0') continue;
                if (strchr(pat, '%') || strchr(pat, '_')) continue;
                lits[nlits].s = pat;
                lits[nlits].len = strlen(pat);
                nlits++;
            }
        }
        /* malloc failure simply degrades to no highlighting, not a crash. */
    }
    const size_t glen = strlen(GREEN);
    const size_t rlen = strlen(RESET);

    if (!raw) wo_printf(wo, "--\n");

    const char *cursor = content;
    const char *line;
    size_t len;
    int current_line = 0;

    while ((line = next_line(&cursor, &len)) != NULL) {
        current_line++;
        if (current_line >= start_line && current_line <= end_line) {
            if (raw) {
                wo_write(wo, line, len);
            } else {
                wo_printf(wo, "%d:", current_line);
                /* Walk the line, highlighting literal matches.  We emit directly
                 * to the WebOutput rather than into a fixed buffer (the CLI's
                 * output[]), which avoids its length cap. */
                size_t i = 0;
                while (i < len) {
                    int matched = 0;
                    for (int k = 0; k < nlits; k++) {
                        /* Bound the match to the current line so a pattern can't
                         * straddle the '\n' into the next line's bytes. */
                        if (i + lits[k].len <= len &&
                            strncasecmp(line + i, lits[k].s, lits[k].len) == 0) {
                            wo_write(wo, GREEN, glen);
                            wo_write(wo, line + i, lits[k].len);
                            wo_write(wo, RESET, rlen);
                            i += lits[k].len;  /* len > 0 guaranteed: always advances */
                            matched = 1;
                            break;
                        }
                    }
                    if (!matched) {
                        wo_putc(wo, line[i]);
                        i++;
                    }
                }
            }
        }
        if (current_line > end_line) break;
    }

    free(lits);
}

void print_expansion_or_context_web(WebOutput *wo, const char *content, const char *filepath,
                                    int line, const char *source_location, int is_definition,
                                    int expand, int context_before, int context_after,
                                    char **patterns, int pattern_count, int raw) {
    if (expand && is_definition == 1 &&
        source_location && source_location[0] != '\0') {
        int start_line, start_column, end_line, end_column;
        if (parse_source_location_web(source_location, &start_line, &start_column,
                                      &end_line, &end_column) == 0) {
            print_lines_range_web(wo, content, filepath,
                                  start_line, end_line, start_column, end_column, raw);
            if (!raw) wo_printf(wo, "--\n");  /* Closing separator after definition */
        }
    } else if (context_before > 0 || context_after > 0) {
        print_context_lines_web(wo, content, filepath, line, patterns, pattern_count,
                                context_before, context_after, raw);
        if (!raw) wo_printf(wo, "--\n");  /* Closing separator after context */
    }
}
