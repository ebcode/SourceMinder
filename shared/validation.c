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
/* validation.c - Configuration file validation implementation */

#include "validation.h"
#include "file_opener.h"
#include "string_utils.h"
#include "paths.h"
#include "constants.h"
#include <string.h>
#include <stdlib.h>

/* Validate that a file exists and can be opened */
ValidationResult validate_file_exists(const char *filepath) {
    ValidationResult result = {0};
    snprintf(result.filepath, sizeof(result.filepath), "%s", filepath);

    FILE *fp = safe_fopen(filepath, "r", 1);
    if (!fp) {
        result.code = VALIDATE_FILE_MISSING;
        snprintf(result.message, sizeof(result.message),
                "File does not exist or cannot be opened");
        return result;
    }

    fclose(fp);
    result.code = VALIDATE_OK;
    return result;
}

/* Validate that a file is readable (same as exists for now) */
ValidationResult validate_file_readable(const char *filepath) {
    return validate_file_exists(filepath);
}

/* Validate line count does not exceed maximum */
ValidationResult validate_line_count(const char *filepath, size_t max_lines, int skip_comments) {
    ValidationResult result = {0};
    snprintf(result.filepath, sizeof(result.filepath), "%s", filepath);
    result.max_allowed = max_lines;

    FILE *fp = safe_fopen(filepath, "r", 1);
    if (!fp) {
        result.code = VALIDATE_FILE_MISSING;
        snprintf(result.message, sizeof(result.message), "Cannot open file");
        return result;
    }

    char line[LINE_BUFFER_LARGE];
    size_t count = 0;
    int line_num = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        line[strcspn(line, "\n")] = 0;

        /* Skip empty lines */
        if (strnlength(line, sizeof(line)) == 0) continue;

        /* Skip comments if requested */
        if (skip_comments && line[0] == '#') continue;

        count++;

        if (count > max_lines) {
            result.code = VALIDATE_TOO_MANY_LINES;
            result.line = line_num;
            result.actual_value = count;
            snprintf(result.message, sizeof(result.message),
                    "Too many lines: found %zu, maximum allowed is %zu",
                    count, max_lines);
            fclose(fp);
            return result;
        }
    }

    fclose(fp);
    result.code = VALIDATE_OK;
    result.actual_value = count;
    return result;
}

/* Validate no line exceeds maximum length */
ValidationResult validate_line_length(const char *filepath, size_t max_length) {
    ValidationResult result = {0};
    snprintf(result.filepath, sizeof(result.filepath), "%s", filepath);
    result.max_allowed = max_length;

    FILE *fp = safe_fopen(filepath, "r", 1);
    if (!fp) {
        result.code = VALIDATE_FILE_MISSING;
        snprintf(result.message, sizeof(result.message), "Cannot open file");
        return result;
    }

    char line[LINE_BUFFER_LARGE];
    int line_num = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        line[strcspn(line, "\n")] = 0;

        /* Skip empty lines */
        if (strnlength(line, sizeof(line)) == 0) continue;

        size_t line_len = strnlength(line, sizeof(line));
        if (line_len >= max_length) {
            result.code = VALIDATE_LINE_TOO_LONG;
            result.line = line_num;
            result.actual_value = line_len;
            snprintf(result.message, sizeof(result.message),
                    "Line too long: found %zu characters, maximum allowed is %zu",
                    line_len, max_length - 1);
            fclose(fp);
            return result;
        }
    }

    fclose(fp);
    result.code = VALIDATE_OK;
    return result;
}

/* Validate file contains at least one non-empty line */
ValidationResult validate_file_not_empty(const char *filepath) {
    ValidationResult result = {0};
    snprintf(result.filepath, sizeof(result.filepath), "%s", filepath);

    FILE *fp = safe_fopen(filepath, "r", 1);
    if (!fp) {
        result.code = VALIDATE_FILE_MISSING;
        snprintf(result.message, sizeof(result.message), "Cannot open file");
        return result;
    }

    char line[LINE_BUFFER_LARGE];
    int has_content = 0;

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = 0;
        if (strnlength(line, sizeof(line)) > 0) {
            has_content = 1;
            break;
        }
    }

    fclose(fp);

    if (!has_content) {
        result.code = VALIDATE_EMPTY_FILE;
        snprintf(result.message, sizeof(result.message), "File is empty");
        return result;
    }

    result.code = VALIDATE_OK;
    return result;
}

/* Validate word list file (stopwords, keywords, ignore_dirs) */
ValidationResult validate_word_list_file(const char *filepath, size_t max_words,
                                          size_t max_word_length, int allow_empty) {
    ValidationResult result = {0};
    snprintf(result.filepath, sizeof(result.filepath), "%s", filepath);

    /* Check file exists */
    result = validate_file_exists(filepath);
    if (result.code != VALIDATE_OK) return result;

    /* Check not empty (unless allowed) */
    if (!allow_empty) {
        result = validate_file_not_empty(filepath);
        if (result.code != VALIDATE_OK) return result;
    }

    /* Check line count */
    result = validate_line_count(filepath, max_words, 0);
    if (result.code != VALIDATE_OK) return result;

    /* Check line length */
    ValidationResult length_result = validate_line_length(filepath, max_word_length);
    if (length_result.code != VALIDATE_OK) return length_result;

    /* Return count in actual_value */
    return result;
}

/* Validate file extensions configuration */
ValidationResult validate_file_extensions(const char *filepath) {
    return validate_word_list_file(filepath, MAX_FILE_EXTENSIONS,
                                     FILE_EXTENSION_MAX_LENGTH, 0);
}

/* Validate regex patterns configuration */
ValidationResult validate_regex_patterns(const char *filepath) {
    /* Check count */
    ValidationResult result = validate_line_count(filepath, MAX_FILTER_WORDS, 0);
    if (result.code != VALIDATE_OK) return result;

    /* Regex patterns can be quite long, use larger buffer */
    return validate_line_length(filepath, LINE_BUFFER_LARGE);
}

/* Print detailed validation error message */
void print_validation_error(const ValidationResult *result) {
    fprintf(stderr, "\nERROR: Validation failed for %s\n", result->filepath);
    fprintf(stderr, "       %s\n", result->message);

    if (result->line > 0) {
        fprintf(stderr, "       Line: %d\n", result->line);
    }

    if (result->actual_value > 0 && result->max_allowed > 0) {
        fprintf(stderr, "       Found: %zu, Maximum: %zu\n",
                result->actual_value, result->max_allowed);
    }

    fprintf(stderr, "\n");
}

/* Preflight validation: Check ALL configuration before proceeding */
int preflight_validation(const char *lang_data_dir, int verbose) {
    char filepath[PATH_MAX_LENGTH];
    char resolved_path[PATH_MAX_LENGTH];
    ValidationResult result;
    int failed = 0;

    if (verbose) {
        printf("=== Preflight Validation ===\n");
        printf("Data directory: %s\n\n", lang_data_dir);
    }

    /* --- Required Files --- */

    /* 1. Validate stopwords.txt (shared, language-agnostic) */
    snprintf(filepath, sizeof(filepath), "%s/%s", SHARED_CONFIG_DIR, STOPWORDS_FILENAME);
    if (verbose) printf("Checking %s...\n", filepath);

    if (resolve_data_file(filepath, resolved_path, sizeof(resolved_path)) == 0) {
        result = validate_word_list_file(resolved_path, MAX_FILTER_WORDS, WORD_MAX_LENGTH, 1);
        if (result.code != VALIDATE_OK) {
            print_validation_error(&result);
            failed = 1;
        } else if (verbose) {
            printf("  VALID (%zu words)\n", result.actual_value);
        }
    } else {
        fprintf(stderr, "ERROR: Cannot find required file: %s\n", filepath);
        failed = 1;
    }

    /* 2. Validate keywords.txt (language-specific) */
    snprintf(filepath, sizeof(filepath), "%s/%s", lang_data_dir, KEYWORDS_FILENAME);
    if (verbose) printf("Checking %s...\n", filepath);

    if (resolve_data_file(filepath, resolved_path, sizeof(resolved_path)) == 0) {
        result = validate_word_list_file(resolved_path, MAX_FILTER_WORDS, WORD_MAX_LENGTH, 0);
        if (result.code != VALIDATE_OK) {
            print_validation_error(&result);
            failed = 1;
        } else if (verbose) {
            printf("  VALID (%zu words)\n", result.actual_value);
        }
    } else {
        fprintf(stderr, "ERROR: Cannot find required file: %s\n", filepath);
        failed = 1;
    }

    /* 3. Validate file-extensions.txt */
    snprintf(filepath, sizeof(filepath), "%s/%s", lang_data_dir, FILE_EXTENSIONS_FILENAME);
    if (verbose) printf("Checking %s...\n", filepath);

    if (resolve_data_file(filepath, resolved_path, sizeof(resolved_path)) == 0) {
        result = validate_file_extensions(resolved_path);
        if (result.code != VALIDATE_OK) {
            print_validation_error(&result);
            failed = 1;
        } else if (verbose) {
            printf("  VALID (%zu extensions)\n", result.actual_value);
        }
    } else {
        fprintf(stderr, "ERROR: Cannot find required file: %s\n", filepath);
        failed = 1;
    }

    /* --- Optional Files (warnings only) --- */

    /* 4. Validate ignore_files.txt (optional) */
    snprintf(filepath, sizeof(filepath), "%s/%s", lang_data_dir, IGNORE_FILES_FILENAME);
    if (verbose) printf("Checking %s...\n", filepath);

    if (resolve_data_file(filepath, resolved_path, sizeof(resolved_path)) == 0) {
        result = validate_file_exists(resolved_path);
        if (result.code == VALIDATE_OK) {
            result = validate_word_list_file(resolved_path, MAX_FILTER_WORDS, WORD_MAX_LENGTH, 1);
            if (result.code != VALIDATE_OK) {
                print_validation_error(&result);
                failed = 1;
            } else if (verbose) {
                printf("  VALID (%zu directories)\n", result.actual_value);
            }
        }
    } else if (verbose) {
        printf("  WARNING: Not found (optional, will use empty list)\n");
    }

    /* 5. Validate regex-patterns.txt (optional) */
    snprintf(filepath, sizeof(filepath), "%s/%s", SHARED_CONFIG_DIR, REGEX_PATTERNS_FILENAME);
    if (verbose) printf("Checking %s...\n", filepath);

    if (resolve_data_file(filepath, resolved_path, sizeof(resolved_path)) == 0) {
        result = validate_file_exists(resolved_path);
        if (result.code == VALIDATE_OK) {
            result = validate_regex_patterns(resolved_path);
            if (result.code != VALIDATE_OK) {
                print_validation_error(&result);
                failed = 1;
            } else if (verbose) {
                printf("  VALID (%zu patterns)\n", result.actual_value);
            }
        }
    } else if (verbose) {
        printf("  WARNING: Not found (optional, will use empty list)\n");
    }

    /* --- System Constraints --- */

    /* 6. Validate buffer sizes are sane */
    if (verbose) printf("\nChecking compile-time constants...\n");

    /* These checks are redundant with _Static_assert but provide runtime feedback */
    if (PATH_MAX_LENGTH < DIRECTORY_MAX_LENGTH) {
        fprintf(stderr, "FATAL ERROR: PATH_MAX_LENGTH (%d) < DIRECTORY_MAX_LENGTH (%d)\n",
                PATH_MAX_LENGTH, DIRECTORY_MAX_LENGTH);
        fprintf(stderr, "This should never happen (caught by _Static_assert)\n");
        failed = 1;
    } else if (verbose) {
        printf("  OK: PATH_MAX_LENGTH (%d) >= DIRECTORY_MAX_LENGTH (%d)\n",
               PATH_MAX_LENGTH, DIRECTORY_MAX_LENGTH);
    }

    if (SYMBOL_MAX_LENGTH <= MIN_SYMBOL_LENGTH) {
        fprintf(stderr, "FATAL ERROR: SYMBOL_MAX_LENGTH (%d) <= MIN_SYMBOL_LENGTH (%d)\n",
                SYMBOL_MAX_LENGTH, MIN_SYMBOL_LENGTH);
        fprintf(stderr, "This should never happen (caught by _Static_assert)\n");
        failed = 1;
    } else if (verbose) {
        printf("  OK: SYMBOL_MAX_LENGTH (%d) > MIN_SYMBOL_LENGTH (%d)\n",
               SYMBOL_MAX_LENGTH, MIN_SYMBOL_LENGTH);
    }

    /* --- Final Report --- */

    if (verbose) {
        printf("\n=== Preflight Validation %s ===\n", failed ? "FAILED" : "PASSED");
    }

    if (failed) {
        fprintf(stderr, "\nPreflight validation failed. Please fix the errors above.\n");
        return -1;
    }

    return 0;
}
