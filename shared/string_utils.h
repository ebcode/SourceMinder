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
#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <stddef.h>
#include <ctype.h>
#include <tree_sitter/api.h>

/* Safe bounded string length - scans at most n bytes for null terminator */
size_t strnlength(const char *s, size_t n);

/* Safe case conversion - handles unsigned char conversion and narrowing */
#define TOUPPERCASE(c) ((char)toupper((unsigned char)(c)))
#define TOLOWERCASE(c) ((char)tolower((unsigned char)(c)))

/* String case conversion functions */
void to_upper(char *str);
void to_lower(char *str);
void to_lowercase_copy(const char *src, char *dst, size_t size);

/* Safe tree-sitter node text extraction
 * Extracts text from a tree-sitter node into buffer.
 * EXITS with error if text exceeds buffer_size.
 * Parameters:
 *   source_code  - Original source code
 *   node         - Tree-sitter node to extract text from
 *   buffer       - Destination buffer
 *   buffer_size  - Size of destination buffer
 *   filename     - Source filename for error reporting
 */
void safe_extract_node_text(const char *source_code, TSNode node, char *buffer,
                            size_t buffer_size, const char *filename);

/* Format source location from tree-sitter node
 * Formats node's start and end positions as "startRow:startCol - endRow:endCol"
 * Example: "15:0 - 17:1" means lines 15-17, starting at column 0, ending at column 1
 * Parameters:
 *   node         - Tree-sitter node
 *   buffer       - Destination buffer
 *   buffer_size  - Size of destination buffer
 */
void format_source_location(TSNode node, char *buffer, size_t buffer_size);

/* Safe strdup with contextual error reporting
 * Duplicates string with malloc error checking.
 * EXITS on allocation failure with error message to stderr.
 * Use this when OOM is unrecoverable (typical for language parsers).
 * Parameters:
 *   str     - String to duplicate (must not be NULL)
 *   err_msg - Complete error message to display on failure
 *             (e.g., "Failed to allocate memory for config argument")
 * Returns: Duplicated string (never NULL)
 */
char *safe_strdup_ctx(const char *str, const char *err_msg);

/* Safe strdup with error reporting (non-exiting version)
 * Duplicates string with malloc error checking.
 * Prints error to stderr but RETURNS NULL on failure.
 * Use this in main() where cleanup/recovery is possible.
 * Parameters:
 *   str     - String to duplicate (must not be NULL)
 *   err_msg - Complete error message to display on failure
 * Returns: Duplicated string on success, NULL on allocation failure
 */
char *try_strdup_ctx(const char *str, const char *err_msg);

/* Skip leading character if present
 * Returns pointer past the first character if it matches 'ch',
 * otherwise returns the original pointer.
 * Useful for stripping prefixes like '$' from PHP variables.
 * Parameters:
 *   str - String to check (must not be NULL)
 *   ch  - Character to skip if present at start
 * Returns: Pointer past first char if it matches ch, otherwise str
 */
const char *skip_leading_char(const char *str, char ch);

/* Fast single-character string comparison
 * Compare a string to a single character followed by null terminator.
 * Faster than strcmp for single-character strings.
 * Parameters:
 *   s  - String to check
 *   c  - Character to compare against
 * Returns: true if s equals {c, '\0'}, false otherwise
 * Example: chrcmp1(str, '.') returns true if str is "."
 */
static inline int chrcmp1(const char *s, char c) {
    return s[0] == c && s[1] == '\0';
}

/* Fast two-character string comparison
 * Compare a string to two characters followed by null terminator.
 * Faster than strcmp for two-character strings.
 * Parameters:
 *   s   - String to check
 *   c1  - First character to compare against
 *   c2  - Second character to compare against
 * Returns: true if s equals {c1, c2, '\0'}, false otherwise
 * Example: chrcmp2(str, '=', '=') returns true if str is "=="
 */
static inline int chrcmp2(const char *s, char c1, char c2) {
    return s[0] == c1 && s[1] == c2 && s[2] == '\0';
}

#endif /* STRING_UTILS_H */
