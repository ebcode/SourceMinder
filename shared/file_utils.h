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
#ifndef FILE_UTILS_H
#define FILE_UTILS_H

/* Convert absolute filepath to relative path with directory/filename split
 * Parameters:
 *   filepath     - Absolute or relative file path
 *   project_root - Project root directory for relative path calculation
 *   directory    - Output: relative directory path (with trailing /)
 *   filename     - Output: filename without directory
 */
void get_relative_path(const char *filepath, const char *project_root,
                      char *directory, char *filename);

/* Parse source_location string into line and column numbers
 * Parameters:
 *   source_location - Location string in format "startRow:startCol - endRow:endCol"
 *   start_line      - Output: starting line number
 *   start_column    - Output: starting column number
 *   end_line        - Output: ending line number
 *   end_column      - Output: ending column number
 * Returns: 0 on success, -1 on parse error
 */
int parse_source_location(const char *source_location, int *start_line, int *start_column,
                         int *end_line, int *end_column);

/* Extract and display lines from file within specified range
 * Parameters:
 *   filepath     - Path to source file
 *   start_line   - Starting line number (1-indexed)
 *   end_line     - Ending line number (1-indexed)
 *   start_column - Starting column (0-indexed)
 *   end_column   - Ending column (0-indexed)
 * Returns: 0 on success, -1 on error
 */
int print_lines_range(const char *filepath, int start_line, int end_line,
                     int start_column, int end_column);

#endif /* FILE_UTILS_H */
