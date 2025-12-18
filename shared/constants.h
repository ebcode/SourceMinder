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
#ifndef CONSTANTS_H
#define CONSTANTS_H

/*
 * Multi-Language Code Indexer - Constants
 *
 * This file contains all size limits, buffer sizes, and capacity constants
 * used throughout the indexer. Centralizing these values makes it easy to:
 * - Understand project limits at a glance
 * - Adjust limits consistently across the codebase
 * - Add defensive compile-time assertions (future work)
 */

/* ============================================================================
 * Version Information
 * ============================================================================
 */

/* Version string for all indexer binaries and query tool */
#define VERSION "0.1.0"

/* ============================================================================
 * Symbol & Identifier Limits
 * ============================================================================
 * These control the maximum length of symbols, variable names, function names,
 * and other code identifiers extracted from source files.
 */

/* Maximum length for symbol names (variable, function, class names, etc.) */
#define SYMBOL_MAX_LENGTH 512

/* Maximum length for context type name strings when converted to uppercase */
#define CONTEXT_TYPE_MAX_LENGTH 64

/* Maximum length for individual words in filter word lists (stopwords, keywords) */
#define WORD_MAX_LENGTH 64

/* Buffer size for cleaned/processed word extraction from comments and strings */
#define CLEANED_WORD_BUFFER 8192


/* ============================================================================
 * Path & Filename Limits
 * ============================================================================
 * These control the maximum length of filesystem paths, directory names,
 * and filenames throughout the indexer.
 */

/* Maximum length for full filesystem paths (absolute or relative) */
#define PATH_MAX_LENGTH 1024

/* Maximum length for storing full file paths (includes directory + filename) */
#define DIRECTORY_MAX_LENGTH 1024

/* Maximum length for filenames (without directory path) */
#define FILENAME_MAX_LENGTH 256

/* Maximum length for source location strings (e.g., "10:5-12:20") */
#define SOURCE_LOCATION_MAX_LENGTH 32

/* Maximum length for access modifiers (e.g., "protected", "private") */
#define MODIFIER_MAX_LENGTH 16

/* Maximum length for scope identifiers (e.g., "instance", "static") */
#define SCOPE_MAX_LENGTH 16

/* Maximum length for clue/context hint strings */
#define CLUE_MAX_LENGTH 64

/* Maximum length for file extension strings (e.g., ".ts", ".tsx") */
#define FILE_EXTENSION_MAX_LENGTH 16

/* Configuration filename for all indexer tools (placed in $HOME directory) */
#define CONFIG_FILENAME ".smconfig"

/* Configuration directory paths */
#define CONFIG_DIR "config"
#define SHARED_CONFIG_DIR "shared/config"

/* Configuration filenames (language-specific, in <language>/config/) */
#define FILE_EXTENSIONS_FILENAME "file_extensions.txt"
#define IGNORE_FILES_FILENAME "ignore_files.txt"
#define KEYWORDS_FILENAME "keywords.txt"

/* Shared configuration filenames (in shared/config/) */
#define STOPWORDS_FILENAME "stopwords.txt"
#define REGEX_PATTERNS_FILENAME "regex-patterns.txt"


/* ============================================================================
 * Array Capacity Limits
 * ============================================================================
 * These define the maximum number of elements in various collections.
 * Exceeding these limits will cause items to be silently dropped or ignored.
 *
 * NOTE: MAX_PARSE_ENTRIES and MAX_FILES are now legacy - both ParseResult and
 * FileList use dynamic allocation with no hard limits. Constants kept for
 * reference only.
 */

/* Legacy: Maximum number of index entries from parsing a single file
 * ParseResult now uses dynamic allocation starting at 1000 entries and
 * growing by 2x as needed. No hard limit. */
#define MAX_PARSE_ENTRIES 100000

/* Legacy: Maximum number of files in directory traversal
 * FileList now uses dynamic allocation starting at 32 files and
 * growing by 2x as needed. No hard limit. */
#define MAX_FILES 100000

/* Maximum number of words in a filter word list (stopwords or keywords) */
#define MAX_FILTER_WORDS 1024

/* Maximum number of regex patterns in filter pattern list */
#define MAX_REGEX_PATTERNS 128

/* Maximum number of file extensions to track per language */
#define MAX_FILE_EXTENSIONS 16

/* Maximum number of directories that can be excluded via --exclude-dir */
#define MAX_EXCLUDE_DIRS 32

/* Maximum number of target paths (files or directories) on command line */
#define MAX_TARGETS 256

/* Maximum number of context types in query include/exclude filters */
#define MAX_CONTEXT_TYPES 16

/* Maximum number of search patterns in a single query */
#define MAX_PATTERNS 512


/* ============================================================================
 * Dynamic Array Initial Capacities
 * ============================================================================
 * Initial sizes for dynamically-growing arrays. These are starting points;
 * arrays will grow as needed (typically doubling in size).
 */

/* Initial capacity for TOC entry arrays (per file) */
#define TOC_INITIAL_CAPACITY 16

/* Initial capacity for file descriptor arrays in file watcher
 * Set high (1024) since file watchers typically monitor many files */
#define FILE_WATCHER_INITIAL_CAPACITY 1024


/* ============================================================================
 * Text Processing Buffers
 * ============================================================================
 * Temporary buffers for reading, parsing, and processing text data.
 */

/* Buffer for extracting text from comments and string literals */
#define COMMENT_TEXT_BUFFER 8192

/* Buffer for constructing SQL queries */
#define SQL_QUERY_BUFFER 4096

/* Buffer for error messages and regex error descriptions */
#define ERROR_MESSAGE_BUFFER 256

/* Buffer for executable paths (used in debug/backtrace functionality) */
#define EXECUTABLE_PATH_BUFFER 1024

/* Small line buffer for reading short configuration lines */
#define LINE_BUFFER_SMALL 32

/* Standard line buffer for most text file reading */
#define LINE_BUFFER_MEDIUM 256

/* Large line buffer for regex patterns and long lines */
#define LINE_BUFFER_LARGE 512


/* ============================================================================
 * Validation & Processing Constants
 * ============================================================================
 * Constants used for validation and filtering logic.
 */

/* Minimum length for a symbol to be indexed (filters out single characters) */
#define MIN_SYMBOL_LENGTH 2

/* Default number of context lines to show when -A/-B/-C flags used without argument */
#define DEFAULT_CONTEXT_RANGE 3

/* Maximum number of context lines to show before or after a match in query results */
#define MAXIMUM_CONTEXT_RANGE 100


/* ============================================================================
 * Compile-Time Validation
 * ============================================================================
 * These static assertions validate relationships between constants at compile
 * time, catching configuration errors before they become runtime bugs.
 */

/* Path and buffer size relationships */
_Static_assert(PATH_MAX_LENGTH >= DIRECTORY_MAX_LENGTH,
    "PATH_MAX_LENGTH must be >= DIRECTORY_MAX_LENGTH for temporary path buffers");

_Static_assert(PATH_MAX_LENGTH >= FILENAME_MAX_LENGTH,
    "PATH_MAX_LENGTH must be >= FILENAME_MAX_LENGTH");

_Static_assert(DIRECTORY_MAX_LENGTH + FILENAME_MAX_LENGTH + 256 <= SQL_QUERY_BUFFER,
    "SQL_QUERY_BUFFER must have room for paths plus SQL statement overhead");

/* Symbol and word processing relationships */
_Static_assert(SYMBOL_MAX_LENGTH > MIN_SYMBOL_LENGTH,
    "SYMBOL_MAX_LENGTH must be greater than MIN_SYMBOL_LENGTH");

_Static_assert(WORD_MAX_LENGTH >= MIN_SYMBOL_LENGTH,
    "WORD_MAX_LENGTH must be >= MIN_SYMBOL_LENGTH for word filters");

_Static_assert(LINE_BUFFER_LARGE >= SYMBOL_MAX_LENGTH,
    "LINE_BUFFER_LARGE must fit at least one symbol");

_Static_assert(LINE_BUFFER_LARGE >= FILE_EXTENSION_MAX_LENGTH,
    "LINE_BUFFER_LARGE must fit file extension lines");

/* Array capacity sanity checks */
_Static_assert(MAX_PARSE_ENTRIES > 0, "MAX_PARSE_ENTRIES must be positive");
_Static_assert(MAX_FILES > 0, "MAX_FILES must be positive");
_Static_assert(MAX_FILTER_WORDS > 0, "MAX_FILTER_WORDS must be positive");
_Static_assert(MAX_FILE_EXTENSIONS > 0 && MAX_FILE_EXTENSIONS <= 32,
    "MAX_FILE_EXTENSIONS must be positive and reasonable (1-32)");
_Static_assert(MAX_EXCLUDE_DIRS > 0, "MAX_EXCLUDE_DIRS must be positive");
_Static_assert(MAX_TARGETS > 0, "MAX_TARGETS must be positive");

#endif /* CONSTANTS_H */
/* trigger reindex */
/* test quiet-init */
