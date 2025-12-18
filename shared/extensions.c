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
#include "extensions.h"
#include "constants.h"
#include "file_opener.h"
#include "string_utils.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>  /* For strcasecmp */
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

/**
 * Load extensions from a single file-extensions.txt file.
 */
static int load_extensions_from_file(const char *filepath, FileExtensions *exts) {
    FILE *f = safe_fopen(filepath, "r", 0);
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
            /* Check for duplicates before adding */
            int duplicate = 0;
            for (int i = 0; i < exts->count; i++) {
                if (strcmp(exts->extensions[i], line) == 0) {
                    duplicate = 1;
                    break;
                }
            }

            if (!duplicate) {
                size_t copy_len = strnlength(line, FILE_EXTENSION_MAX_LENGTH - 1);
                memcpy(exts->extensions[exts->count], line, copy_len);
                exts->extensions[exts->count][copy_len] = '\0';
                exts->count++;
            }
        }
    }

    fclose(f);
    return 0;
}

/**
 * Check if a directory exists.
 */
static int dir_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

int load_all_language_extensions(FileExtensions *all_exts) {
    all_exts->count = 0;

    /* List of language directories to check */
    const char *lang_dirs[] = {"c", "typescript", "php", "go", "python", NULL};

    /* Try to load from each language's config directory */
    for (int i = 0; lang_dirs[i] != NULL; i++) {
        char path[PATH_MAX_LENGTH];
        snprintf(path, sizeof(path), "%s/%s/%s", lang_dirs[i], CONFIG_DIR, FILE_EXTENSIONS_FILENAME);

        /* Check if language directory exists first */
        if (dir_exists(lang_dirs[i])) {
            /* Try to load extensions (silently skip if file doesn't exist) */
            load_extensions_from_file(path, all_exts);
        }
    }

    return (all_exts->count > 0) ? 0 : -1;
}

int extract_extension_from_pattern(const char *pattern, char *ext_out, size_t ext_size) {
    if (!pattern || !ext_out || ext_size == 0) {
        return 0;
    }

    /* Find the last dot in the pattern */
    const char *last_dot = strrchr(pattern, '.');
    if (!last_dot) {
        return 0;
    }

    /* Extract everything from the dot to the end, stopping at SQL wildcards */
    size_t ext_len = 0;
    const char *p = last_dot;
    while (*p && ext_len < ext_size - 1) {
        /* Stop at SQL LIKE wildcards */
        if (*p == '%' || *p == '_') {
            break;
        }
        ext_out[ext_len++] = *p;
        p++;
    }

    /* Null-terminate */
    ext_out[ext_len] = '\0';

    /* Must have at least ".X" to be valid */
    return (ext_len >= 2) ? 1 : 0;
}

int is_known_extension(const char *extension, const FileExtensions *known) {
    if (!extension || !known) {
        return 0;
    }

    for (int i = 0; i < known->count; i++) {
        if (strcmp(known->extensions[i], extension) == 0) {
            return 1;
        }
    }

    return 0;
}

int is_plaintext_extension(const char *extension) {
    if (!extension) {
        return 0;
    }

    /* Plaintext file extensions that are not source code */
    static const char *plaintext_extensions[] = {
        ".txt",
        ".md",
        ".markdown",
        ".log",
        ".rst",
        ".adoc",
        ".asciidoc",
        ".org",
        ".pdf",
        ".doc",
        ".docx",
        ".rtf",
        ".odt",
        ".csv",
        ".tsv",
        ".json",     /* Debatable, but often just data files */
        ".xml",      /* Debatable, but often just data files */
        ".yaml",
        ".yml",
        ".toml",
        ".ini",
        ".cfg",
        ".conf",
        ".config",
        ".properties",
        ".env",
        ".gitignore",
        ".gitattributes",
        ".editorconfig",
        ".LICENSE",
        ".INSTALL",
        ".README",
        ".CHANGELOG",
        ".AUTHORS",
        ".CONTRIBUTORS",
        ".COPYING",
        NULL
    };

    for (int i = 0; plaintext_extensions[i] != NULL; i++) {
        if (strcasecmp(extension, plaintext_extensions[i]) == 0) {
            return 1;
        }
    }

    return 0;
}
