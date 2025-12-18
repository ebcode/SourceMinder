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
#ifndef FILTER_H
#define FILTER_H

#include <regex.h>
#include "constants.h"

typedef struct {
    char words[MAX_FILTER_WORDS][WORD_MAX_LENGTH];
    int count;
} WordSet;

typedef struct {
    regex_t patterns[MAX_REGEX_PATTERNS];
    int count;
} RegexSet;

typedef struct {
    char extensions[MAX_FILE_EXTENSIONS][FILE_EXTENSION_MAX_LENGTH];
    int count;
} FileExtensions;

typedef struct {
    WordSet stopwords;
    WordSet ts_keywords;
    WordSet ignore_dirs;
    RegexSet regex_patterns;
    FileExtensions file_extensions;
} SymbolFilter;

/* Initialize filter by loading word lists from files */
int filter_init(SymbolFilter *filter, const char *data_dir);

/* Check if symbol should be indexed (returns 1 if yes, 0 if no) */
int filter_should_index(SymbolFilter *filter, const char *symbol);

/* Clean symbol for strings/comments: preserve path characters (dots, slashes, hyphens, colons) */
void filter_clean_string_symbol(const char *src, char *dst, size_t dst_size);

/* Get pointer to file extensions (for use with file_walker) */
const FileExtensions* filter_get_extensions(const SymbolFilter *filter);

/* Get pointer to ignore directories (for use with file_walker) */
const WordSet* filter_get_ignore_dirs(const SymbolFilter *filter);

/* Free internal filter resources (compiled regex patterns) */
void filter_free_regex(SymbolFilter *filter);

#endif
/* final test */
