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
#ifndef RUST_LANGUAGE_H
#define RUST_LANGUAGE_H

#include "../shared/parse_result.h"
#include "../shared/filter.h"
#include <tree_sitter/api.h>

typedef struct {
    TSParser *parser;
    SymbolFilter *filter;
    int debug;
} RustParser;

int parser_init(RustParser *parser, SymbolFilter *filter);
void parser_set_debug(RustParser *parser, int debug);
int parser_parse_file(RustParser *parser, const char *filepath, const char *project_root, ParseResult *result);
void parser_free(RustParser *parser);

#endif /* RUST_LANGUAGE_H */
