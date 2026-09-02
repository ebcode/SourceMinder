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
#include "../config.h"
#include "parse_result.h"
#include "string_utils.h"
#include "constants.h"
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* Initial capacity for ParseResult dynamic array */
#define PARSE_RESULT_INITIAL_CAPACITY 1000
/* Growth factor when capacity is exceeded */
#define PARSE_RESULT_GROWTH_FACTOR 2

int init_parse_result(ParseResult *result) {
    result->entries = malloc(PARSE_RESULT_INITIAL_CAPACITY * sizeof(IndexEntry));
    if (!result->entries) {
        result->count = 0;
        result->capacity = 0;
        return -1;
    }
    result->count = 0;
    result->capacity = PARSE_RESULT_INITIAL_CAPACITY;
    result->filter = NULL;
    return 0;
}

void free_parse_result(ParseResult *result) {
    if (result->entries) {
        free(result->entries);
        result->entries = NULL;
    }
    result->count = 0;
    result->capacity = 0;
}

/* Ensure ParseResult has capacity for at least one more entry
 * Returns: 0 on success, -1 on allocation failure (caller should exit)
 */
static int ensure_capacity(ParseResult *result) {
    if (result->count < result->capacity) {
        return 0;  /* Still have space */
    }

    /* Need to grow */
    int new_capacity = result->capacity * PARSE_RESULT_GROWTH_FACTOR;
    if (new_capacity <= result->capacity) {
        /* Overflow protection - this is a fatal error */
        return -1;
    }

    IndexEntry *new_entries = realloc(result->entries, (size_t)new_capacity * sizeof(IndexEntry));
    if (!new_entries) {
        /* Memory allocation failure - caller will handle fatal error */
        return -1;
    }

    result->entries = new_entries;
    result->capacity = new_capacity;
    return 0;
}

void add_entry(ParseResult *result, const char *symbol, int line,
              ContextType context, const char *directory,
              const char *filename, const char *source_location,
              const ExtColumns *ext) {
    /* Never index an empty symbol (e.g. a node safe_extract_node_text skipped). */
    if (!symbol || symbol[0] == '\0') return;

    /* A symbol at or beyond SYMBOL_MAX_LENGTH won't fit the storage fields without
     * truncation. Past this length it's data, not a code symbol, so skip it and
     * warn rather than silently store a truncated fragment. (String/comment words
     * arrive here at full length; over-long identifiers are already dropped to ""
     * by safe_extract_node_text and short-circuited above.) */
    size_t symbol_len = strlen(symbol);
    if (symbol_len >= SYMBOL_MAX_LENGTH) {
        warn_oversized_symbol(context_to_string(context, 1), symbol_len,
                              symbol, (unsigned int)line, filename);
        return;
    }

    /* Ensure we have capacity for one more entry - fail fast if allocation fails */
    if (ensure_capacity(result) != 0) {
        fprintf(stderr, "FATAL: Failed to allocate memory for parse result at entry %d\n", result->count);
        fprintf(stderr, "       Symbol '%s' at %s:%d cannot be indexed\n", symbol, filename, line);
        exit(EXIT_FAILURE);
    }

    IndexEntry *entry = &result->entries[result->count];

    /* Store full_symbol first (preserves original symbol with any prefixes like $) */
    snprintf(entry->full_symbol, sizeof(entry->full_symbol), "%s", symbol);

    /* For PHP variables/properties, strip leading $ before creating search symbol */
    const char *symbol_for_search = symbol;
    if (context == CONTEXT_VARIABLE || context == CONTEXT_PROPERTY) {
        symbol_for_search = skip_leading_char(symbol, '$');
    }

    /* Create lowercase symbol, stripping trailing punctuation for strings/comments */
    to_lowercase_copy(symbol_for_search, entry->symbol, sizeof(entry->symbol));
    if (context == CONTEXT_COMMENT || context == CONTEXT_STRING) {
        size_t len = strlen(entry->symbol);
        while (len > 0 && (entry->symbol[len-1] == '.' || entry->symbol[len-1] == ',' ||
                           entry->symbol[len-1] == '!' || entry->symbol[len-1] == '?' ||
                           entry->symbol[len-1] == ':' || entry->symbol[len-1] == ';')) {
            len--;
            entry->symbol[len] = '\0';
        }
        /* An all-punctuation word ("...", "::") strips to nothing. The empty
         * check at the top ran before this, so re-check. */
        if (len == 0) return;
    }

    /* English stopwords are a prose filter: drop them from STRING/COMMENT words
     * only. Code symbols are never stopword-filtered (filter_should_index no
     * longer does it). result->filter may be NULL for callers that don't set it.
     * The slot at result->entries[count] is not committed until count++ below,
     * so returning here simply leaves it to be reused. */
    if ((context == CONTEXT_COMMENT || context == CONTEXT_STRING) &&
        result->filter && filter_is_stopword(result->filter, entry->symbol)) {
        return;
    }

    snprintf(entry->directory, sizeof(entry->directory), "%s", directory);
    snprintf(entry->filename, sizeof(entry->filename), "%s", filename);
    entry->line = line;
    entry->context = context;
    snprintf(entry->source_location, sizeof(entry->source_location), "%s", source_location ? source_location : "");

    /* Extract extensible columns from struct (default to empty string if NULL) */
    snprintf(entry->parent_symbol, sizeof(entry->parent_symbol), "%s", ext && ext->parent ? ext->parent : "");
#if ENABLED(TYPESCRIPT) || ENABLED(PHP) || ENABLED(GO) || ENABLED(PYTHON)
    snprintf(entry->scope, sizeof(entry->scope), "%s", ext && ext->scope ? ext->scope : "");
    snprintf(entry->namespace, sizeof(entry->namespace), "%s", ext && ext->namespace ? ext->namespace : "");
#endif
    snprintf(entry->modifier, sizeof(entry->modifier), "%s", ext && ext->modifier ? ext->modifier : "");
    snprintf(entry->clue, sizeof(entry->clue), "%s", ext && ext->clue ? ext->clue : "");
    snprintf(entry->type, sizeof(entry->type), "%s", ext && ext->type ? ext->type : "");
    /* INTEGER columns: parse string to int */
    if (ext && ext->definition) {
        char *endptr;
        errno = 0;
        long val = strtol(ext->definition, &endptr, 10);
        entry->is_definition = (errno == 0 && *endptr == '\0') ? (int)val : 0;
    } else {
        entry->is_definition = 0;
    }

    result->count++;
}
