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
#ifndef FILE_WALKER_H
#define FILE_WALKER_H

#include "filter.h"
#include "constants.h"

typedef struct {
    char **files;       /* Dynamic array of string pointers */
    int count;          /* Current number of files */
    int capacity;       /* Allocated capacity */
} FileList;

typedef struct {
    char dirs[MAX_EXCLUDE_DIRS][FILENAME_MAX_LENGTH];
    int count;
} ExcludeDirs;

/* Initialize FileList with dynamic allocation */
void init_file_list(FileList *list);

/* Free FileList and all allocated strings */
void free_file_list(FileList *list);

/* Add a file path to the FileList (grows capacity as needed) */
void add_file_to_list(FileList *list, const char *path);

/* Find all files matching configured extensions in a directory recursively */
int find_files(const char *dir_path, FileList *files, const ExcludeDirs *exclude_dirs, const FileExtensions *extensions, const WordSet *ignore_dirs);

/* Check if a path should be ignored based on ignore_dirs patterns */
int is_path_ignored(const char *full_path, const char *dirname, const WordSet *ignore_dirs);

#endif
