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
#ifndef PATHS_H
#define PATHS_H

#include <stddef.h>
#include "constants.h"

/**
 * Resolve a language-specific data directory path.
 *
 * Search order:
 * 1. $INDEXER_DATA_DIR/<lang>/data (if env var set)
 * 2. ./<lang>/data (current directory, development mode)
 * 3. /usr/local/share/indexer-c/<lang>/data (macOS)
 * 4. /usr/share/indexer-c/<lang>/data (Linux)
 *
 * @param lang Language name (e.g., "c", "typescript", "php", "go")
 * @param out_path Output buffer for resolved path
 * @param out_size Size of output buffer
 * @return 0 on success, -1 if no valid directory found
 */
int resolve_lang_data_dir(const char *lang, char *out_path, size_t out_size);

/**
 * Resolve the shared data directory path.
 *
 * Search order:
 * 1. $INDEXER_DATA_DIR/shared/data (if env var set)
 * 2. ./shared/data (current directory, development mode)
 * 3. /usr/local/share/indexer-c/shared/data (macOS)
 * 4. /usr/share/indexer-c/shared/data (Linux)
 *
 * @param out_path Output buffer for resolved path
 * @param out_size Size of output buffer
 * @return 0 on success, -1 if no valid directory found
 */
int resolve_shared_data_dir(char *out_path, size_t out_size);

/**
 * Resolve a specific data file path.
 *
 * This is a convenience function that tries multiple search locations
 * for a relative path like "shared/data/stopwords.txt" or "c/data/keywords.txt".
 *
 * Search order:
 * 1. $INDEXER_DATA_DIR/<relative_path> (if env var set)
 * 2. ./<relative_path> (current directory, development mode)
 * 3. /usr/local/share/indexer-c/<relative_path> (macOS)
 * 4. /usr/share/indexer-c/<relative_path> (Linux)
 *
 * @param relative_path Relative path to file (e.g., "shared/data/stopwords.txt")
 * @param out_path Output buffer for resolved absolute path
 * @param out_size Size of output buffer
 * @return 0 on success, -1 if file not found in any location
 */
int resolve_data_file(const char *relative_path, char *out_path, size_t out_size);

#endif /* PATHS_H */
