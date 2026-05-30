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
#ifndef SOURCE_RENDER_WEB_H
#define SOURCE_RENDER_WEB_H

#include "web_output.h"

/* Web twins of query-index.c's source-rendering functions for the -C/-A/-B
 * (context), -e (expand), and --raw flags.
 *
 * The native CLI reads source from disk via FILE/fgets and writes to stdout.
 * These twins read from an in-memory file buffer (the JS worker fetches the
 * whole file and hands it across) and write to a WebOutput.  Rendering itself
 * -- line-number prefixes, ANSI highlighting, column trimming for -e, and the
 * grep-style "--" separators -- is reproduced exactly so browser output
 * matches the CLI.
 *
 * `content` is the entire file as a NUL-terminated string, or NULL if the file
 * could not be fetched.  On NULL the twins emit the same "Could not read file"
 * warning the native CLI prints (to the terminal, since the browser has no
 * separate stderr).  `filepath` is used only for that message. */

/* Expand a definition span [start_line, end_line] with -e column trimming. */
void print_lines_range_web(WebOutput *wo, const char *content, const char *filepath,
                           int start_line, int end_line,
                           int start_column, int end_column, int raw);

/* Print context lines around target_line, highlighting literal patterns. */
void print_context_lines_web(WebOutput *wo, const char *content, const char *filepath,
                             int target_line, char **patterns, int pattern_count,
                             int before, int after, int raw);

/* Orchestrator: expand the definition when -e applies, else show context.
 * Mirrors print_expansion_or_context in query-index.c. */
void print_expansion_or_context_web(WebOutput *wo, const char *content, const char *filepath,
                                    int line, const char *source_location, int is_definition,
                                    int expand, int context_before, int context_after,
                                    char **patterns, int pattern_count, int raw);

#endif /* SOURCE_RENDER_WEB_H */
