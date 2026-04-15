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
#include "paths.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
#include <io.h>
#include <windows.h>
#define access _access
#define F_OK 0

/* Get directory containing the current executable */
static int get_executable_dir(char *out_path, size_t out_size) {
    char exe_path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return -1;
    }
    /* Find last backslash or forward slash and truncate */
    char *last_sep = strrchr(exe_path, '\\');
    char *last_fwd = strrchr(exe_path, '/');
    if (last_fwd && (!last_sep || last_fwd > last_sep)) {
        last_sep = last_fwd;
    }
    if (last_sep) {
        *last_sep = '\0';
    }
    if (strlen(exe_path) >= out_size) {
        return -1;
    }
    strcpy(out_path, exe_path);
    return 0;
}
#else
#include <unistd.h>
#endif

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
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
    /* Windows: First check relative to executable (works outside MSYS2) */
    char exe_dir[PATH_MAX_LENGTH];
    if (get_executable_dir(exe_dir, sizeof(exe_dir)) == 0) {
        /* Check exe_dir/../<lang>/data (typical layout) */
        snprintf(candidate, sizeof(candidate), "%s/../%s/data", exe_dir, lang);
        if (dir_exists(candidate)) {
            snprintf(out_path, out_size, "%s", candidate);
            return 0;
        }
        /* Check exe_dir/<lang>/data */
        snprintf(candidate, sizeof(candidate), "%s/%s/data", exe_dir, lang);
        if (dir_exists(candidate)) {
            snprintf(out_path, out_size, "%s", candidate);
            return 0;
        }
    }
    /* MSYS2: Check MSYS2 virtual paths (only work inside MSYS2 shell) */
    snprintf(candidate, sizeof(candidate), "/ucrt64/share/sourceminder/%s/data", lang);
    if (dir_exists(candidate)) {
        snprintf(out_path, out_size, "%s", candidate);
        return 0;
    }
    snprintf(candidate, sizeof(candidate), "/mingw64/share/sourceminder/%s/data", lang);
    if (dir_exists(candidate)) {
        snprintf(out_path, out_size, "%s", candidate);
        return 0;
    }
#elif defined(__APPLE__)
    /* macOS: /usr/local/share/sourceminder/<lang>/data */
    snprintf(candidate, sizeof(candidate), "/usr/local/share/sourceminder/%s/data", lang);
    if (dir_exists(candidate)) {
        snprintf(out_path, out_size, "%s", candidate);
        return 0;
    }
#else
    /* Linux: /usr/share/sourceminder/<lang>/data */
    snprintf(candidate, sizeof(candidate), "/usr/share/sourceminder/%s/data", lang);
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
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
    /* Windows: First check relative to executable (works outside MSYS2) */
    char exe_dir[PATH_MAX_LENGTH];
    if (get_executable_dir(exe_dir, sizeof(exe_dir)) == 0) {
        /* Check exe_dir/../shared/data (typical layout) */
        snprintf(candidate, sizeof(candidate), "%s/../shared/data", exe_dir);
        if (dir_exists(candidate)) {
            snprintf(out_path, out_size, "%s", candidate);
            return 0;
        }
        /* Check exe_dir/shared/data */
        snprintf(candidate, sizeof(candidate), "%s/shared/data", exe_dir);
        if (dir_exists(candidate)) {
            snprintf(out_path, out_size, "%s", candidate);
            return 0;
        }
    }
    /* MSYS2: Check MSYS2 virtual paths (only work inside MSYS2 shell) */
    snprintf(candidate, sizeof(candidate), "/ucrt64/share/sourceminder/shared/data");
    if (dir_exists(candidate)) {
        snprintf(out_path, out_size, "%s", candidate);
        return 0;
    }
    snprintf(candidate, sizeof(candidate), "/mingw64/share/sourceminder/shared/data");
    if (dir_exists(candidate)) {
        snprintf(out_path, out_size, "%s", candidate);
        return 0;
    }
#elif defined(__APPLE__)
    /* macOS: /usr/local/share/sourceminder/shared/data */
    snprintf(candidate, sizeof(candidate), "/usr/local/share/sourceminder/shared/data");
    if (dir_exists(candidate)) {
        snprintf(out_path, out_size, "%s", candidate);
        return 0;
    }
#else
    /* Linux: /usr/share/sourceminder/shared/data */
    snprintf(candidate, sizeof(candidate), "/usr/share/sourceminder/shared/data");
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
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
    /* Windows: First check relative to executable (works outside MSYS2) */
    char exe_dir[PATH_MAX_LENGTH];
    if (get_executable_dir(exe_dir, sizeof(exe_dir)) == 0) {
        /* Check exe_dir/../<relative_path> (typical layout) */
        snprintf(candidate, sizeof(candidate), "%s/../%s", exe_dir, relative_path);
        if (file_exists(candidate)) {
            snprintf(out_path, out_size, "%s", candidate);
            return 0;
        }
        /* Check exe_dir/<relative_path> */
        snprintf(candidate, sizeof(candidate), "%s/%s", exe_dir, relative_path);
        if (file_exists(candidate)) {
            snprintf(out_path, out_size, "%s", candidate);
            return 0;
        }
    }
    /* MSYS2: Check MSYS2 virtual paths (only work inside MSYS2 shell) */
    snprintf(candidate, sizeof(candidate), "/ucrt64/share/sourceminder/%s", relative_path);
    if (file_exists(candidate)) {
        snprintf(out_path, out_size, "%s", candidate);
        return 0;
    }
    snprintf(candidate, sizeof(candidate), "/mingw64/share/sourceminder/%s", relative_path);
    if (file_exists(candidate)) {
        snprintf(out_path, out_size, "%s", candidate);
        return 0;
    }
#elif defined(__APPLE__)
    /* macOS: /usr/local/share/sourceminder/<relative_path> */
    snprintf(candidate, sizeof(candidate), "/usr/local/share/sourceminder/%s", relative_path);
    if (file_exists(candidate)) {
        snprintf(out_path, out_size, "%s", candidate);
        return 0;
    }
#else
    /* Linux: /usr/share/sourceminder/<relative_path> */
    snprintf(candidate, sizeof(candidate), "/usr/share/sourceminder/%s", relative_path);
    if (file_exists(candidate)) {
        snprintf(out_path, out_size, "%s", candidate);
        return 0;
    }
#endif

    /* File not found in any location */
    return -1;
}
