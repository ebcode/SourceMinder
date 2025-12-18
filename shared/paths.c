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
#include "paths.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Check if a directory exists */
static int dir_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

/* Check if a file exists */
static int file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

int resolve_lang_data_dir(const char *lang, char *out_path, size_t out_size) {
    if (!lang || !out_path || out_size == 0) {
        return -1;
    }

    char candidate[PATH_MAX_LENGTH];

    /* 1. Check $INDEXER_DATA_DIR environment variable */
    const char *env_data_dir = getenv("INDEXER_DATA_DIR");
    if (env_data_dir) {
        snprintf(candidate, sizeof(candidate), "%s/%s/data", env_data_dir, lang);
        if (dir_exists(candidate)) {
            snprintf(out_path, out_size, "%s", candidate);
            return 0;
        }
    }

    /* 2. Check current directory (development mode) */
    snprintf(candidate, sizeof(candidate), "%s/data", lang);
    if (dir_exists(candidate)) {
        snprintf(out_path, out_size, "%s", candidate);
        return 0;
    }

    /* 3. Check platform-specific installation directories */
#ifdef __APPLE__
    /* macOS: /usr/local/share/indexer-c/<lang>/data */
    snprintf(candidate, sizeof(candidate), "/usr/local/share/indexer-c/%s/data", lang);
    if (dir_exists(candidate)) {
        snprintf(out_path, out_size, "%s", candidate);
        return 0;
    }
#else
    /* Linux: /usr/share/indexer-c/<lang>/data */
    snprintf(candidate, sizeof(candidate), "/usr/share/indexer-c/%s/data", lang);
    if (dir_exists(candidate)) {
        snprintf(out_path, out_size, "%s", candidate);
        return 0;
    }
#endif

    /* No valid directory found */
    return -1;
}

int resolve_shared_data_dir(char *out_path, size_t out_size) {
    if (!out_path || out_size == 0) {
        return -1;
    }

    char candidate[PATH_MAX_LENGTH];

    /* 1. Check $INDEXER_DATA_DIR environment variable */
    const char *env_data_dir = getenv("INDEXER_DATA_DIR");
    if (env_data_dir) {
        snprintf(candidate, sizeof(candidate), "%s/shared/data", env_data_dir);
        if (dir_exists(candidate)) {
            snprintf(out_path, out_size, "%s", candidate);
            return 0;
        }
    }

    /* 2. Check current directory (development mode) */
    snprintf(candidate, sizeof(candidate), "shared/data");
    if (dir_exists(candidate)) {
        snprintf(out_path, out_size, "%s", candidate);
        return 0;
    }

    /* 3. Check platform-specific installation directories */
#ifdef __APPLE__
    /* macOS: /usr/local/share/indexer-c/shared/data */
    snprintf(candidate, sizeof(candidate), "/usr/local/share/indexer-c/shared/data");
    if (dir_exists(candidate)) {
        snprintf(out_path, out_size, "%s", candidate);
        return 0;
    }
#else
    /* Linux: /usr/share/indexer-c/shared/data */
    snprintf(candidate, sizeof(candidate), "/usr/share/indexer-c/shared/data");
    if (dir_exists(candidate)) {
        snprintf(out_path, out_size, "%s", candidate);
        return 0;
    }
#endif

    /* No valid directory found */
    return -1;
}

int resolve_data_file(const char *relative_path, char *out_path, size_t out_size) {
    if (!relative_path || !out_path || out_size == 0) {
        return -1;
    }

    char candidate[PATH_MAX_LENGTH];

    /* 1. Check $INDEXER_DATA_DIR environment variable */
    const char *env_data_dir = getenv("INDEXER_DATA_DIR");
    if (env_data_dir) {
        snprintf(candidate, sizeof(candidate), "%s/%s", env_data_dir, relative_path);
        if (file_exists(candidate)) {
            snprintf(out_path, out_size, "%s", candidate);
            return 0;
        }
    }

    /* 2. Check current directory (development mode) */
    snprintf(candidate, sizeof(candidate), "%s", relative_path);
    if (file_exists(candidate)) {
        snprintf(out_path, out_size, "%s", candidate);
        return 0;
    }

    /* 3. Check platform-specific installation directories */
#ifdef __APPLE__
    /* macOS: /usr/local/share/indexer-c/<relative_path> */
    snprintf(candidate, sizeof(candidate), "/usr/local/share/indexer-c/%s", relative_path);
    if (file_exists(candidate)) {
        snprintf(out_path, out_size, "%s", candidate);
        return 0;
    }
#else
    /* Linux: /usr/share/indexer-c/<relative_path> */
    snprintf(candidate, sizeof(candidate), "/usr/share/indexer-c/%s", relative_path);
    if (file_exists(candidate)) {
        snprintf(out_path, out_size, "%s", candidate);
        return 0;
    }
#endif

    /* File not found in any location */
    return -1;
}
