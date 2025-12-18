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
#include "file_walker.h"
#include "string_utils.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fnmatch.h>

/* FileList growth constants */
#define FILE_LIST_INITIAL_CAPACITY 32
#define FILE_LIST_GROWTH_FACTOR 2

/* Initialize FileList with dynamic allocation */
void init_file_list(FileList *list) {
    list->files = malloc(FILE_LIST_INITIAL_CAPACITY * sizeof(char*));
    if (!list->files) {
        fprintf(stderr, "FATAL: Failed to allocate FileList\n");
        exit(EXIT_FAILURE);
    }
    list->count = 0;
    list->capacity = FILE_LIST_INITIAL_CAPACITY;
}

/* Free FileList and all allocated strings */
void free_file_list(FileList *list) {
    if (!list) return;

    /* Free each individual path string */
    for (int i = 0; i < list->count; i++) {
        free(list->files[i]);
    }

    /* Free the array of pointers */
    free(list->files);
    list->files = NULL;
    list->count = 0;
    list->capacity = 0;
}

/* Ensure FileList has capacity for one more entry (grows if needed) */
static int ensure_file_list_capacity(FileList *list) {
    if (list->count < list->capacity) {
        return 0;  /* Still have space */
    }

    /* Need to grow the pointer array */
    int new_capacity = list->capacity * FILE_LIST_GROWTH_FACTOR;
    if (new_capacity <= list->capacity) {
        /* Overflow protection */
        return -1;
    }

    char **new_files = realloc(list->files, new_capacity * sizeof(char*));
    if (!new_files) {
        /* Allocation failure - fatal error */
        return -1;
    }

    list->files = new_files;
    list->capacity = new_capacity;

    fprintf(stderr, "[FileList] Growing capacity: %d → %d files\n",
            list->capacity / FILE_LIST_GROWTH_FACTOR, new_capacity);

    return 0;
}

/* Add a file path to the FileList (grows capacity as needed) */
void add_file_to_list(FileList *list, const char *path) {
    /* Ensure we have capacity for one more pointer */
    if (ensure_file_list_capacity(list) != 0) {
        fprintf(stderr, "FATAL: Failed to grow FileList at %d files\n", list->count);
        fprintf(stderr, "       Cannot add file: %s\n", path);
        exit(EXIT_FAILURE);
    }

    /* Allocate exact size for this specific path (not DIRECTORY_MAX_LENGTH!) */
    list->files[list->count] = strdup(path);
    if (!list->files[list->count]) {
        fprintf(stderr, "FATAL: Failed to allocate path string: %s\n", path);
        exit(EXIT_FAILURE);
    }

    list->count++;
}

/* Helper function to detect glob characters in pattern */
static int has_glob_chars(const char *pattern) {
    return (strchr(pattern, '*') != NULL ||
            strchr(pattern, '?') != NULL ||
            strchr(pattern, '[') != NULL);
}

/* Pattern matching with auto-detection of glob patterns */
static int pattern_matches(const char *pattern, const char *str, int use_pathname_flag) {
    if (has_glob_chars(pattern)) {
        /* Use fnmatch for glob patterns */
        int flags = use_pathname_flag ? FNM_PATHNAME : 0;
        return fnmatch(pattern, str, flags) == 0;
    } else {
        /* Exact match for backward compatibility */
        return strcmp(pattern, str) == 0;
    }
}

static int is_excluded(const char *full_path, const char *dirname, const ExcludeDirs *exclude_dirs) {
    if (!exclude_dirs) return 0;

    for (int i = 0; i < exclude_dirs->count; i++) {
        /* Check if the exclude pattern matches the full path */
        if (strstr(full_path, exclude_dirs->dirs[i]) != NULL) {
            /* Verify it's a proper path component match */
            const char *match = strstr(full_path, exclude_dirs->dirs[i]);
            size_t pattern_len = strnlength(exclude_dirs->dirs[i], FILENAME_MAX_LENGTH);
            /* Ensure the match is either at the start or preceded by '/' */
            /* and is either at the end or followed by '/' or '\0' */
            if ((match == full_path || *(match - 1) == '/') &&
                (match[pattern_len] == '/' || match[pattern_len] == '\0')) {
                return 1;
            }
        }
        /* Also check basename for simple exclude patterns */
        if (strcmp(dirname, exclude_dirs->dirs[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

int is_path_ignored(const char *full_path, const char *dirname, const WordSet *ignore_dirs) {
    if (!ignore_dirs) return 0;

    for (int i = 0; i < ignore_dirs->count; i++) {
        const char *pattern = ignore_dirs->words[i];

        /* Check if pattern contains path separator */
        if (strchr(pattern, '/') != NULL) {
            /* Path-based pattern: match against full path */
            /* Strip trailing slash from pattern for comparison */
            char pattern_copy[FILENAME_MAX_LENGTH];
            snprintf(pattern_copy, sizeof(pattern_copy), "%s", pattern);
            size_t pattern_len = strnlength(pattern_copy, sizeof(pattern_copy));
            if (pattern_len > 0 && pattern_copy[pattern_len - 1] == '/') {
                pattern_copy[pattern_len - 1] = '\0';
                pattern_len--;
            }

            /* Use glob matching if pattern contains wildcards */
            if (has_glob_chars(pattern_copy)) {
                if (pattern_matches(pattern_copy, full_path, 1)) {
                    return 1;
                }
            } else {
                /* Exact substring match for non-glob patterns */
                const char *match = strstr(full_path, pattern_copy);
                if (match != NULL) {
                    /* Verify it's a proper path component match */
                    if ((match == full_path || *(match - 1) == '/') &&
                        (match[pattern_len] == '/' || match[pattern_len] == '\0')) {
                        return 1;
                    }
                }
            }
        } else {
            /* Simple directory name: match against basename */
            if (pattern_matches(pattern, dirname, 0)) {
                return 1;
            }
        }
    }
    return 0;
}

/* Check if a file matches any ignore pattern */
static int is_file_ignored(const char *full_path, const char *filename, const WordSet *ignore_patterns) {
    if (!ignore_patterns) return 0;

    /* Strip leading ./ from path for pattern matching */
    const char *match_path = full_path;
    if (full_path[0] == '.' && full_path[1] == '/') {
        match_path = full_path + 2;
    }

    for (int i = 0; i < ignore_patterns->count; i++) {
        const char *pattern = ignore_patterns->words[i];

        /* Check if pattern contains path separator */
        if (strchr(pattern, '/') != NULL) {
            /* Path-based pattern: match against full path with FNM_PATHNAME */
            if (has_glob_chars(pattern)) {
                if (pattern_matches(pattern, match_path, 1)) {
                    return 1;
                }
            }
            /* Skip non-glob path patterns for files (directory-only) */
        } else {
            /* Simple filename pattern: match against basename */
            if (pattern_matches(pattern, filename, 0)) {
                return 1;
            }
        }
    }
    return 0;
}

static int has_valid_extension(const char *filename, const FileExtensions *extensions) {
    if (!extensions || extensions->count == 0) return 0;

    size_t len = strnlength(filename, FILENAME_MAX_LENGTH);

    for (int i = 0; i < extensions->count; i++) {
        size_t ext_len = strnlength(extensions->extensions[i], FILE_EXTENSION_MAX_LENGTH);
        if (len > ext_len && strcmp(filename + len - ext_len, extensions->extensions[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static void walk_directory(const char *dir_path, FileList *files, const ExcludeDirs *exclude_dirs, const FileExtensions *extensions, const WordSet *ignore_dirs) {
    DIR *dir;
    struct dirent *entry;

    dir = opendir(dir_path);
    if (!dir) {
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        /* Build full path */
        char path[PATH_MAX_LENGTH];
        size_t dir_len = strnlength(dir_path, PATH_MAX_LENGTH);
        int has_trailing_slash = (dir_len > 0 && dir_path[dir_len - 1] == '/');
        snprintf(path, sizeof(path), "%s%s%s", dir_path, has_trailing_slash ? "" : "/", entry->d_name);

        /* Use stat to determine file type */
        struct stat st;
        if (stat(path, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            /* Skip configured ignore directories */
            if (is_path_ignored(path, entry->d_name, ignore_dirs)) {
                continue;
            }
            /* Skip user-specified exclude directories */
            if (is_excluded(path, entry->d_name, exclude_dirs)) {
                continue;
            }
            /* Recursively walk subdirectory */
            walk_directory(path, files, exclude_dirs, extensions, ignore_dirs);
        }
        else if (S_ISREG(st.st_mode)) {
            /* Skip ignored files (e.g., *.o, test_*.tmp, *.c) */
            if (is_file_ignored(path, entry->d_name, ignore_dirs)) {
                continue;
            }
            /* Check if file has valid extension */
            if (has_valid_extension(entry->d_name, extensions)) {
                add_file_to_list(files, path);
            }
        }
    }

    closedir(dir);
}

int find_files(const char *dir_path, FileList *files, const ExcludeDirs *exclude_dirs, const FileExtensions *extensions, const WordSet *ignore_dirs) {
    files->count = 0;
    walk_directory(dir_path, files, exclude_dirs, extensions, ignore_dirs);
    return 0;
}
