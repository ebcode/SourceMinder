/* SourceMinder
 * Copyright 2025 Eli Bird
 *
 * This file is part of SourceMinder.
 *
 * SourceMinder is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at
 * your option) any later version.
 *
 * SourceMinder is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with SourceMinder. If not, see <https://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../shared/indexer_main.h"
#include "../shared/constants.h"
#include "../shared/version.h"
#include "rust_language.h"
#include "grammar_version.h"

static void* parser_init_wrapper(SymbolFilter *filter) {
    RustParser *parser = malloc(sizeof(RustParser));
    if (!parser) {
        return NULL;
    }
    if (parser_init(parser, filter) != 0) {
        free(parser);
        return NULL;
    }
    return parser;
}

static int parser_parse_wrapper(void *parser, const char *filepath, const char *project_root, ParseResult *result) {
    return parser_parse_file((RustParser*)parser, filepath, project_root, result);
}

static void parser_free_wrapper(void *parser) {
    if (parser) {
        parser_free((RustParser*)parser);
        free(parser);
    }
}

static void parser_set_debug_wrapper(void *parser, int debug) {
    parser_set_debug((RustParser*)parser, debug);
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            print_version_with_grammar(GRAMMAR_NAME, GRAMMAR_VERSION);
            return 0;
        }
    }

    IndexerConfig config = {
        .name = "index-rust",
        .data_dir = "rust/" CONFIG_DIR,
        .parser_init = parser_init_wrapper,
        .parser_parse = parser_parse_wrapper,
        .parser_free = parser_free_wrapper,
        .parser_set_debug = parser_set_debug_wrapper
    };

    return indexer_main(argc, argv, &config);
}
