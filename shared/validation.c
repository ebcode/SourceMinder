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
#include "preflight_report.h"
#include "file_opener.h"
#include "string_utils.h"
#include "paths.h"
#include "constants.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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
static void report_validation_error(PreflightReport *report,
                                    const ValidationResult *result) {
    if (result->line > 0 && result->actual_value > 0) {
        preflight_report_add(report, PF_ERROR,
            "%s: %s (line %d, found %zu, max %zu)",
            result->filepath, result->message, result->line,
            result->actual_value, result->max_allowed);
    } else if (result->line > 0) {
        preflight_report_add(report, PF_ERROR, "%s: %s (line %d)",
            result->filepath, result->message, result->line);
    } else if (result->actual_value > 0) {
        preflight_report_add(report, PF_ERROR,
            "%s: %s (found %zu, max %zu)",
            result->filepath, result->message,
            result->actual_value, result->max_allowed);
    } else {
        preflight_report_add(report, PF_ERROR, "%s: %s",
            result->filepath, result->message);
    }
}

int preflight_validation_start(const char *lang_data_dir, int verbose,
                               PreflightReport *report) {
    char filepath[PATH_MAX_LENGTH];
    char resolved_path[PATH_MAX_LENGTH];
    ValidationResult result;

    (void)verbose; /* output is deferred; verbose controls end output only */

    /* --- Required Files --- */

    snprintf(filepath, sizeof(filepath), "%s/%s", SHARED_CONFIG_DIR, STOPWORDS_FILENAME);
    if (resolve_data_file(filepath, resolved_path, sizeof(resolved_path)) == 0) {
        result = validate_word_list_file(resolved_path, MAX_FILTER_WORDS, WORD_MAX_LENGTH, 1);
        if (result.code != VALIDATE_OK)
            report_validation_error(report, &result);
        else
            preflight_report_add(report, PF_OK, "stopwords.txt (%zu words)",
                                 result.actual_value);
    } else {
        preflight_report_add(report, PF_ERROR,
                             "Cannot find required file: %s", filepath);
    }

    snprintf(filepath, sizeof(filepath), "%s/%s", lang_data_dir, KEYWORDS_FILENAME);
    if (resolve_data_file(filepath, resolved_path, sizeof(resolved_path)) == 0) {
        result = validate_word_list_file(resolved_path, MAX_FILTER_WORDS, WORD_MAX_LENGTH, 0);
        if (result.code != VALIDATE_OK)
            report_validation_error(report, &result);
        else
            preflight_report_add(report, PF_OK, "keywords.txt (%zu words)",
                                 result.actual_value);
    } else {
        preflight_report_add(report, PF_ERROR,
                             "Cannot find required file: %s", filepath);
    }

    snprintf(filepath, sizeof(filepath), "%s/%s", lang_data_dir, FILE_EXTENSIONS_FILENAME);
    if (resolve_data_file(filepath, resolved_path, sizeof(resolved_path)) == 0) {
        result = validate_file_extensions(resolved_path);
        if (result.code != VALIDATE_OK)
            report_validation_error(report, &result);
        else
            preflight_report_add(report, PF_OK, "file-extensions.txt (%zu extensions)",
                                 result.actual_value);
    } else {
        preflight_report_add(report, PF_ERROR,
                             "Cannot find required file: %s", filepath);
    }

    /* --- Optional Files --- */

    snprintf(filepath, sizeof(filepath), "%s/%s", lang_data_dir, IGNORE_FILES_FILENAME);
    if (resolve_data_file(filepath, resolved_path, sizeof(resolved_path)) == 0) {
        result = validate_file_exists(resolved_path);
        if (result.code == VALIDATE_OK) {
            result = validate_word_list_file(resolved_path, MAX_FILTER_WORDS, WORD_MAX_LENGTH, 1);
            if (result.code != VALIDATE_OK)
                report_validation_error(report, &result);
            else
                preflight_report_add(report, PF_OK, "ignore_files.txt (%zu entries)",
                                     result.actual_value);
        }
    } else {
        preflight_report_add(report, PF_WARNING,
                             "ignore_files.txt not found (optional, using empty list)");
    }

    snprintf(filepath, sizeof(filepath), "%s/%s", SHARED_CONFIG_DIR, REGEX_PATTERNS_FILENAME);
    if (resolve_data_file(filepath, resolved_path, sizeof(resolved_path)) == 0) {
        result = validate_file_exists(resolved_path);
        if (result.code == VALIDATE_OK) {
            result = validate_regex_patterns(resolved_path);
            if (result.code != VALIDATE_OK)
                report_validation_error(report, &result);
            else
                preflight_report_add(report, PF_OK, "regex-patterns.txt (%zu patterns)",
                                     result.actual_value);
        }
    } else {
        preflight_report_add(report, PF_WARNING,
                             "regex-patterns.txt not found (optional, using empty list)");
    }

    /* --- Compile-time constants (redundant with _Static_assert, runtime feedback) --- */

    if (PATH_MAX_LENGTH < DIRECTORY_MAX_LENGTH) {
        preflight_report_add(report, PF_ERROR,
            "PATH_MAX_LENGTH (%d) < DIRECTORY_MAX_LENGTH (%d) — should never happen",
            PATH_MAX_LENGTH, DIRECTORY_MAX_LENGTH);
    } else {
        preflight_report_add(report, PF_OK, "PATH_MAX_LENGTH (%d) >= DIRECTORY_MAX_LENGTH (%d)",
                             PATH_MAX_LENGTH, DIRECTORY_MAX_LENGTH);
    }

    if (SYMBOL_MAX_LENGTH <= MIN_SYMBOL_LENGTH) {
        preflight_report_add(report, PF_ERROR,
            "SYMBOL_MAX_LENGTH (%d) <= MIN_SYMBOL_LENGTH (%d) — should never happen",
            SYMBOL_MAX_LENGTH, MIN_SYMBOL_LENGTH);
    } else {
        preflight_report_add(report, PF_OK, "SYMBOL_MAX_LENGTH (%d) > MIN_SYMBOL_LENGTH (%d)",
                             SYMBOL_MAX_LENGTH, MIN_SYMBOL_LENGTH);
    }

    return report->error_count;
}

int preflight_validation_end(const PreflightReport *report, int verbose) {
    int failed = report->error_count > 0;

    if (verbose) {
        printf("=== Preflight Validation ===\n\n");
        for (int i = 0; i < report->count; i++) {
            const PreflightMessage *m = &report->messages[i];
            switch (m->severity) {
                case PF_OK:      printf("  OK: %s\n",      m->text); break;
                case PF_WARNING: printf("  WARNING: %s\n", m->text); break;
                case PF_ERROR:   printf("  FAIL: %s\n",    m->text); break;
                case PF_HINT:    printf("  %s\n",           m->text); break;
            }
        }
        printf("\n=== Preflight Validation %s ===\n", failed ? "FAILED" : "PASSED");
    }

    if (failed) {
        if (!verbose) {
            /* In non-verbose mode errors were not shown above — print them now */
            for (int i = 0; i < report->count; i++) {
                const PreflightMessage *m = &report->messages[i];
                if (m->severity == PF_ERROR || m->severity == PF_HINT)
                    fprintf(stderr, "  %s\n", m->text);
            }
        }
        fprintf(stderr, "\nPreflight validation failed. Please fix the errors above.\n");
        return -1;
    }

    return 0;
}
