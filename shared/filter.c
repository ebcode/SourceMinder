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
#include "filter.h"
#include "file_opener.h"
#include "string_utils.h"
#include "paths.h"
#include "constants.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int load_file_extensions(FileExtensions *exts, const char *config_path) {
    exts->count = 0;

    FILE *f = safe_fopen(config_path, "r", 0);
    if (!f) {
        return -1;
    }

    char line[LINE_BUFFER_SMALL];
    while (fgets(line, sizeof(line), f) && exts->count < MAX_FILE_EXTENSIONS) {
        /* Remove trailing newline and whitespace */
        size_t len = strnlength(line, sizeof(line));
        while (len > 0 && isspace(line[len - 1])) {
            line[--len] = '\0';
        }

        /* Skip empty lines */
        if (len == 0) continue;

        /* Ensure extension starts with . */
        if (line[0] == '.') {
            /* Use strnlength to safely truncate if needed */
            size_t copy_len = strnlength(line, FILE_EXTENSION_MAX_LENGTH - 1);
            memcpy(exts->extensions[exts->count], line, copy_len);
            exts->extensions[exts->count][copy_len] = '\0';
            exts->count++;
        }
    }

    fclose(f);
    return 0;
}

static int load_word_list(WordSet *set, const char *filepath) {
    FILE *fp = safe_fopen(filepath, "r", 0);
    if (!fp) {
        fprintf(stderr, "Warning: Could not open %s\n", filepath);
        return -1;
    }

    set->count = 0;
    char line[WORD_MAX_LENGTH];

    while (fgets(line, sizeof(line), fp) && set->count < MAX_FILTER_WORDS) {
        /* Remove trailing newline */
        line[strcspn(line, "\n")] = '\0';

        /* Skip empty lines */
        if (line[0] == '\0') {
            continue;
        }

        /* Copy to word set */
        snprintf(set->words[set->count], WORD_MAX_LENGTH, "%s", line);
        set->count++;
    }

    fclose(fp);
    return 0;
}

static int load_regex_patterns(RegexSet *set, const char *filepath) {
    FILE *fp = safe_fopen(filepath, "r", 0);
    if (!fp) {
        fprintf(stderr, "Warning: Could not open %s\n", filepath);
        return -1;
    }

    set->count = 0;
    char line[LINE_BUFFER_LARGE];

    while (fgets(line, sizeof(line), fp) && set->count < MAX_REGEX_PATTERNS) {
        /* Remove trailing newline */
        line[strcspn(line, "\n")] = '\0';

        /* Skip empty lines and comments */
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        /* Compile the regex pattern */
        int ret = regcomp(&set->patterns[set->count], line, REG_EXTENDED | REG_NOSUB);
        if (ret != 0) {
            char error_buf[ERROR_MESSAGE_BUFFER];
            regerror(ret, &set->patterns[set->count], error_buf, sizeof(error_buf));
            fprintf(stderr, "Warning: Failed to compile regex '%s': %s\n", line, error_buf);
            continue;
        }

        set->count++;
    }

    fclose(fp);
    return 0;
}

static int is_in_set(WordSet *set, const char *word) {
    for (int i = 0; i < set->count; i++) {
        if (strcmp(set->words[i], word) == 0) {
            return 1;
        }
    }
    return 0;
}

static int matches_regex_pattern(RegexSet *set, const char *word) {
    for (int i = 0; i < set->count; i++) {
        if (regexec(&set->patterns[i], word, 0, NULL, 0) == 0) {
            return 1;
        }
    }
    return 0;
}

int filter_init(SymbolFilter *filter, const char *lang_data_dir) {
    char path[LINE_BUFFER_LARGE];
    char resolved_path[PATH_MAX_LENGTH];

    /* Load file extensions (language-specific) */
    snprintf(path, sizeof(path), "%s/%s", lang_data_dir, FILE_EXTENSIONS_FILENAME);
    if (resolve_data_file(path, resolved_path, sizeof(resolved_path)) == 0) {
        if (load_file_extensions(&filter->file_extensions, resolved_path) != 0) {
            filter->file_extensions.count = 0;
        }
    } else {
        filter->file_extensions.count = 0;
    }

    /* Load ignore directories (language-specific) */
    snprintf(path, sizeof(path), "%s/%s", lang_data_dir, IGNORE_FILES_FILENAME);
    if (resolve_data_file(path, resolved_path, sizeof(resolved_path)) == 0) {
        if (load_word_list(&filter->ignore_dirs, resolved_path) != 0) {
            filter->ignore_dirs.count = 0;
        }
    } else {
        filter->ignore_dirs.count = 0;
    }

    /* Load stopwords (shared) */
    snprintf(path, sizeof(path), "%s/%s", SHARED_CONFIG_DIR, STOPWORDS_FILENAME);
    if (resolve_data_file(path, resolved_path, sizeof(resolved_path)) == 0) {
        if (load_word_list(&filter->stopwords, resolved_path) != 0) {
            filter->stopwords.count = 0;
        }
    } else {
        filter->stopwords.count = 0;
    }

    /* Load language keywords (language-specific) */
    snprintf(path, sizeof(path), "%s/%s", lang_data_dir, KEYWORDS_FILENAME);
    if (resolve_data_file(path, resolved_path, sizeof(resolved_path)) == 0) {
        if (load_word_list(&filter->ts_keywords, resolved_path) != 0) {
            filter->ts_keywords.count = 0;
        }
    } else {
        filter->ts_keywords.count = 0;
    }

    /* Load regex patterns (shared) */
    snprintf(path, sizeof(path), "%s/%s", SHARED_CONFIG_DIR, REGEX_PATTERNS_FILENAME);
    if (resolve_data_file(path, resolved_path, sizeof(resolved_path)) == 0) {
        if (load_regex_patterns(&filter->regex_patterns, resolved_path) != 0) {
            filter->regex_patterns.count = 0;
        }
    } else {
        filter->regex_patterns.count = 0;
    }

    return 0;
}

int filter_should_index(SymbolFilter *filter, const char *symbol) {
    if (!symbol || !symbol[0]) {
        return 0;
    }

    /* Skip excluded code characters (exact match only) */
    const char *excluded[] = {"[", "]", "{", "}", "(", ")", "=", "+", "+=", "++", ";", ",", NULL};
    for (int j = 0; excluded[j] != NULL; j++) {
        if (strcmp(symbol, excluded[j]) == 0) {
            return 0;
        }
    }

    /* Convert to lowercase for comparison */
    char lower[SYMBOL_MAX_LENGTH];
    int i;
    for (i = 0; symbol[i] && i < SYMBOL_MAX_LENGTH - 1; i++) {
        lower[i] = (char)tolower((unsigned char)symbol[i]);
    }
    lower[i] = '\0';

    /* Skip very short symbols (< 2 chars) */
    if (strnlength(lower, sizeof(lower)) < MIN_SYMBOL_LENGTH) {
        return 0;
    }

    /* Skip pure numbers */
    int all_digits = 1;
    for (i = 0; lower[i]; i++) {
        if (!isdigit(lower[i])) {
            all_digits = 0;
            break;
        }
    }
    if (all_digits) {
        return 0;
    }

    /* Skip language keywords */
    if (is_in_set(&filter->ts_keywords, lower)) {
        return 0;
    }

    /* Skip common English stopwords */
    if (is_in_set(&filter->stopwords, lower)) {
        return 0;
    }

    /* Skip symbols matching regex patterns */
    if (matches_regex_pattern(&filter->regex_patterns, symbol)) {
        return 0;
    }

    return 1;
}

void filter_clean_string_symbol(const char *src, char *dst, size_t dst_size) {
    /* For strings/comments: preserve most special characters except parens/braces/quotes */
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dst_size - 1; i++) {
        char c = src[i];
        /* Allow: alphanumeric, underscore, and most special chars that appear in strings/comments */
        if (isalnum(c) || c == '_' || c == '.' || c == '/' || c == '-' || c == ':' ||
            c == '@' || c == '#' || c == '?' || c == '&' || c == '=' || c == '+' ||
            c == '^' || c == '$' || c == '!' || c == '~' || c == '<' || c == '>' ||
            c == '[' || c == ']' || c == '%') {
            dst[j++] = c;
        }
    }
    dst[j] = '\0';
}

const FileExtensions* filter_get_extensions(const SymbolFilter *filter) {
    return &filter->file_extensions;
}

const WordSet* filter_get_ignore_dirs(const SymbolFilter *filter) {
    return &filter->ignore_dirs;
}

void filter_free_regex(SymbolFilter *filter) {
    /* Free compiled regex patterns */
    for (int i = 0; i < filter->regex_patterns.count; i++) {
        regfree(&filter->regex_patterns.patterns[i]);
    }
}
/* test */
