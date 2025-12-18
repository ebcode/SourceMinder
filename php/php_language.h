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
#ifndef PHP_LANGUAGE_H
#define PHP_LANGUAGE_H

#include "../shared/database.h"
#include "../shared/filter.h"
#include "../shared/constants.h"
#include "../shared/parse_result.h"

typedef struct {
    SymbolFilter *filter;
    int debug;
} PHPParser;

/* Initialize parser with a filter */
int parser_init(PHPParser *parser, SymbolFilter *filter);

/* Parse a PHP file and extract symbols */
int parser_parse_file(PHPParser *parser, const char *filepath, const char *project_root, ParseResult *result);

/* Set debug mode */
void parser_set_debug(PHPParser *parser, int debug);

/* Free parser resources */
void parser_free(PHPParser *parser);

#endif
