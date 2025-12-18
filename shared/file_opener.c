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
#include "file_opener.h"
#include <sys/stat.h>
#include <stdio.h>

FILE *safe_fopen(const char *filepath, const char *mode, int silent) {
    if (!filepath || !mode) {
        return NULL;
    }

    /* Use lstat to avoid following symlinks */
    struct stat st;
    if (lstat(filepath, &st) != 0) {
        /* File doesn't exist or can't be accessed - this is normal, return NULL silently */
        return NULL;
    }

    /* Reject symlinks, device files, FIFOs, etc. - only allow regular files */
    if (!S_ISREG(st.st_mode)) {
        if (!silent) {
            fprintf(stderr, "Warning: '%s' is not a regular file, skipping\n", filepath);
        }
        return NULL;
    }

    /* File is verified as regular, safe to open */
    return fopen(filepath, mode);
}
