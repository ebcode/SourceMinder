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
/* validation.h - Configuration file validation utilities
 *
 * Provides preflight validation for all configuration files before loading.
 * Validates file existence, line counts, line lengths, and system constraints.
 */

#ifndef VALIDATION_H
#define VALIDATION_H

#include <stdio.h>
#include "constants.h"

/* Validation result codes */
#define VALIDATE_OK 0
#define VALIDATE_FILE_MISSING 1
#define VALIDATE_FILE_UNREADABLE 2
#define VALIDATE_TOO_MANY_LINES 3
#define VALIDATE_LINE_TOO_LONG 4
#define VALIDATE_EMPTY_FILE 5
#define VALIDATE_BUFFER_TOO_SMALL 6

/* Validation result structure */
typedef struct {
    int code;
    int line;      /* Which line failed (0 if not line-specific) */
    size_t actual_value;  /* Actual count or length */
    size_t max_allowed;   /* Maximum allowed */
    char filepath[FILENAME_MAX_LENGTH];
    char message[ERROR_MESSAGE_BUFFER];    /* Human-readable error message */
} ValidationResult;

/* Individual validation checks */
ValidationResult validate_file_exists(const char *filepath);
ValidationResult validate_file_readable(const char *filepath);
ValidationResult validate_line_count(const char *filepath, size_t max_lines, int skip_comments);
ValidationResult validate_line_length(const char *filepath, size_t max_length);
ValidationResult validate_file_not_empty(const char *filepath);

/* Combined validation for word list files (stopwords, keywords, ignore_dirs) */
ValidationResult validate_word_list_file(const char *filepath, size_t max_words,
                                          size_t max_word_length, int allow_empty);

/* Validation for file extensions */
ValidationResult validate_file_extensions(const char *filepath);

/* Validation for regex patterns */
ValidationResult validate_regex_patterns(const char *filepath);

/* Print validation error (detailed, user-friendly) */
void print_validation_error(const ValidationResult *result);

/* Preflight validation: check ALL configuration before proceeding
 *
 * Validates all configuration files in lang_data_dir:
 * - stopwords.txt (required)
 * - keywords.txt (required)
 * - file-extensions.txt (required)
 * - ignore_files.txt (optional)
 * - regex-patterns.txt (optional)
 *
 * Also validates compile-time constants are sane.
 *
 * Returns 0 on success, -1 if any validation fails.
 */
int preflight_validation(const char *lang_data_dir, int verbose);

#endif /* VALIDATION_H */
