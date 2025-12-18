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
#ifndef EXTENSIONS_H
#define EXTENSIONS_H

#include "filter.h"

/**
 * Load all file extensions from discovered language directories.
 *
 * Scans for <lang>/data/file-extensions.txt files in the current directory
 * and merges all extensions into a single FileExtensions structure.
 *
 * @param all_exts Output structure to store all discovered extensions
 * @return 0 on success, -1 on error
 */
int load_all_language_extensions(FileExtensions *all_exts);

/**
 * Extract file extension from a SQL LIKE pattern.
 *
 * Examples:
 *   "%.js" -> ".js"
 *   "test%.c" -> ".c"
 *   "%.h" -> ".h"
 *   "file.txt" -> ".txt"
 *   "noext" -> NULL
 *
 * @param pattern SQL LIKE pattern (may contain %, _, etc.)
 * @param ext_out Buffer to store extracted extension (caller-allocated)
 * @param ext_size Size of ext_out buffer
 * @return 1 if extension found, 0 if no extension in pattern
 */
int extract_extension_from_pattern(const char *pattern, char *ext_out, size_t ext_size);

/**
 * Check if an extension is in the list of known extensions.
 *
 * @param extension Extension to check (must start with '.')
 * @param known List of known extensions
 * @return 1 if known, 0 if unknown
 */
int is_known_extension(const char *extension, const FileExtensions *known);

/**
 * Check if an extension is a plaintext file (not source code).
 *
 * This tool is designed for indexing source code, not plaintext files.
 * Returns 1 for extensions like .txt, .md, .log, etc.
 *
 * @param extension Extension to check (must start with '.')
 * @return 1 if plaintext, 0 if not
 */
int is_plaintext_extension(const char *extension);

#endif /* EXTENSIONS_H */
