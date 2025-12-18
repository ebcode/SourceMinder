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
#ifndef FILE_OPENER_H
#define FILE_OPENER_H

#include <stdio.h>

/*
 * Secure file opening utilities
 *
 * Provides defense-in-depth file access that validates file types before opening.
 * Protects against symlink attacks, device file attacks, and other file-based threats.
 */

/* Open a file securely with validation
 *
 * Returns: FILE pointer on success, NULL on failure
 *
 * Security checks performed:
 * - Verifies path points to a regular file (not symlink, device, FIFO, etc.)
 * - Only follows the provided path, doesn't follow symlinks
 * - Silent failure for missing files (returns NULL)
 * - Warning message for non-regular files
 *
 * Parameters:
 *   filepath - Path to file to open
 *   mode - fopen mode string (e.g., "r", "w", "a")
 *   silent - If non-zero, suppress warning messages for non-regular files
 */
FILE *safe_fopen(const char *filepath, const char *mode, int silent);

#endif /* FILE_OPENER_H */
