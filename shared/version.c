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
#include "version.h"
#include "constants.h"
#include <stdio.h>
#include <sqlite3.h>
#include <tree_sitter/api.h>

void print_version(void) {
    printf("%s\n", VERSION);
    printf("SQLite: %s\n", sqlite3_libversion());
    printf("Tree-sitter: (language version %d)\n", TREE_SITTER_LANGUAGE_VERSION);
}

void print_version_with_grammar(const char *grammar_name, const char *grammar_version) {
    printf("%s\n", VERSION);
    printf("SQLite: %s\n", sqlite3_libversion());
    printf("Tree-sitter: (language version %d)\n", TREE_SITTER_LANGUAGE_VERSION);
    printf("Grammar: tree-sitter-%s %s\n", grammar_name, grammar_version);
}
