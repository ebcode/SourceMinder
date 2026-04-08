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
#include "perl_language.h"
#include "../shared/constants.h"
#include "../shared/string_utils.h"
#include "../shared/file_opener.h"
#include "../shared/file_utils.h"
#include "../shared/filter.h"
#include "../shared/parse_result.h"
#include "../shared/debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

/* Declare the tree-sitter Perl language function */
const TSLanguage *tree_sitter_perl(void);

/* Global debug flag */
static int g_debug = 0;

/* Pre-looked-up node type IDs for fast dispatch */
static struct {
    /* Variables */
    TSSymbol variable_declaration;
    TSSymbol scalar;
    TSSymbol array;
    TSSymbol hash;
    TSSymbol varname;

    /* Declarations and definitions */
    TSSymbol subroutine_declaration_statement;
    TSSymbol package_statement;

    /* Imports */
    TSSymbol use_statement;

    /* Expressions and statements */
    TSSymbol expression_statement;
    TSSymbol assignment_expression;
    TSSymbol return_expression;
    TSSymbol block;

    /* Literals and identifiers */
    TSSymbol string_literal;
    TSSymbol string_content;
    TSSymbol bareword;

    /* Comments and documentation */
    TSSymbol comment;
    TSSymbol pod;
} perl_symbols;

/* Forward declarations */
static void process_children(TSNode node, const char *source_code,
                             const char *directory, const char *filename,
                             ParseResult *result, SymbolFilter *filter);
static void visit_node(TSNode node, const char *source_code,
                       const char *directory, const char *filename,
                       ParseResult *result, SymbolFilter *filter);

/* Initialize symbol lookup table (called once) */
static void init_perl_symbols(const TSLanguage *language) {
    static int initialized = 0;
    if (initialized) return;
    initialized = 1;

    /* Variables */
    perl_symbols.variable_declaration = ts_language_symbol_for_name(language, "variable_declaration", 20, true);
    perl_symbols.scalar  = ts_language_symbol_for_name(language, "scalar",  6, true);
    perl_symbols.array   = ts_language_symbol_for_name(language, "array",   5, true);
    perl_symbols.hash    = ts_language_symbol_for_name(language, "hash",    4, true);
    perl_symbols.varname = ts_language_symbol_for_name(language, "varname", 7, true);

    /* Declarations and definitions */
    perl_symbols.subroutine_declaration_statement = ts_language_symbol_for_name(language, "subroutine_declaration_statement", 32, true);
    perl_symbols.package_statement = ts_language_symbol_for_name(language, "package_statement", 17, true);

    /* Imports */
    perl_symbols.use_statement = ts_language_symbol_for_name(language, "use_statement", 13, true);

    /* Expressions and statements */
    perl_symbols.expression_statement = ts_language_symbol_for_name(language, "expression_statement", 20, true);
    perl_symbols.assignment_expression = ts_language_symbol_for_name(language, "assignment_expression", 21, true);
    perl_symbols.return_expression = ts_language_symbol_for_name(language, "return_expression", 17, true);
    perl_symbols.block = ts_language_symbol_for_name(language, "block", 5, true);

    /* Literals and identifiers */
    perl_symbols.string_literal = ts_language_symbol_for_name(language, "string_literal", 14, true);
    perl_symbols.string_content = ts_language_symbol_for_name(language, "string_content", 14, true);
    perl_symbols.bareword = ts_language_symbol_for_name(language, "bareword", 8, true);

    /* Comments and documentation */
    perl_symbols.comment = ts_language_symbol_for_name(language, "comment", 7, true);
    perl_symbols.pod = ts_language_symbol_for_name(language, "pod", 3, true);
}

/* Extract variable name from a scalar/array/hash node and index it */
static void index_sigil_node(TSNode node, const char *source_code,
                             const char *directory, const char *filename,
                             ParseResult *result, SymbolFilter *filter,
                             const char *modifier, const char *definition) {
    /* Find the varname child */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_symbol(child) != perl_symbols.varname) {
            continue;
        }

        char varname[SYMBOL_MAX_LENGTH];
        safe_extract_node_text(source_code, child, varname, sizeof(varname), filename);

        /* Skip single-char or empty names that are Perl internals (@_, $_, etc.) */
        if (strlen(varname) <= 1) {
            continue;
        }

        if (!filter_should_index(filter, varname)) {
            continue;
        }

        int line = (int)ts_node_start_point(node).row + 1;
        add_entry(result, varname, line, CONTEXT_VARIABLE,
                  directory, filename, NULL,
                  &(ExtColumns){ .modifier = modifier, .definition = definition });
        break;
    }
}

/* Handle variable_declaration: my/our/local $x, @arr, %hash, or list form */
static void handle_variable_declaration(TSNode node, const char *source_code,
                                        const char *directory, const char *filename,
                                        ParseResult *result, SymbolFilter *filter) {
    /* Determine the storage modifier (my, our, local) from the first child */
    const char *modifier = "";
    uint32_t child_count = ts_node_child_count(node);

    if (child_count > 0) {
        TSNode first = ts_node_child(node, 0);
        const char *kw = ts_node_type(first);
        if (strcmp(kw, "my") == 0)    modifier = "my";
        else if (strcmp(kw, "our") == 0)   modifier = "our";
        else if (strcmp(kw, "local") == 0) modifier = "local";
    }

    /* Walk all children looking for scalar/array/hash nodes */
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol sym = ts_node_symbol(child);

        if (sym == perl_symbols.scalar ||
            sym == perl_symbols.array  ||
            sym == perl_symbols.hash) {
            index_sigil_node(child, source_code, directory, filename,
                             result, filter, modifier, "1");
        }
    }
}

/* Recursively process all children of a node */
static void process_children(TSNode node, const char *source_code,
                             const char *directory, const char *filename,
                             ParseResult *result, SymbolFilter *filter) {
    TSTreeCursor cursor = ts_tree_cursor_new(node);

    if (ts_tree_cursor_goto_first_child(&cursor)) {
        do {
            TSNode child = ts_tree_cursor_current_node(&cursor);
            visit_node(child, source_code, directory, filename, result, filter);
        } while (ts_tree_cursor_goto_next_sibling(&cursor));
    }

    ts_tree_cursor_delete(&cursor);
}

/* Visit a single node: dispatch to a handler or recurse into children */
static void visit_node(TSNode node, const char *source_code,
                       const char *directory, const char *filename,
                       ParseResult *result, SymbolFilter *filter) {
    TSSymbol node_sym = ts_node_symbol(node);
    TSPoint start_point = ts_node_start_point(node);
    int line = (int)(start_point.row + 1);

    if (g_debug) {
        debug("[visit_node] Line %d: node_type='%s'", line, ts_node_type(node));
    }

    /* Fast symbol-based dispatch */
    if (node_sym == perl_symbols.variable_declaration) {
        handle_variable_declaration(node, source_code, directory, filename, result, filter);
        return;
    }

    /* Standalone scalar/array/hash outside a declaration — index as reference */
    if (node_sym == perl_symbols.scalar ||
        node_sym == perl_symbols.array  ||
        node_sym == perl_symbols.hash) {
        index_sigil_node(node, source_code, directory, filename,
                         result, filter, "", "0");
        return;
    }

    /* TODO: subroutine_declaration_statement */
    /* TODO: package_statement */
    /* TODO: use_statement */
    /* TODO: comment */
    /* TODO: expression_statement */

    /* No handler — recurse into children */
    if (g_debug) {
        debug("[visit_node] Line %d: no handler for '%s'", line, ts_node_type(node));
    }
    process_children(node, source_code, directory, filename, result, filter);
}

/* ── Public interface ─────────────────────────────────────────────────── */

int parser_init(PerlParser *parser, SymbolFilter *filter) {
    parser->filter = filter;
    parser->debug  = 0;

    parser->parser = ts_parser_new();
    if (!parser->parser) {
        fprintf(stderr, "index-perl: failed to create tree-sitter parser\n");
        return -1;
    }

    const TSLanguage *lang = tree_sitter_perl();
    if (!ts_parser_set_language(parser->parser, lang)) {
        fprintf(stderr, "index-perl: failed to set Perl language (ABI mismatch?)\n");
        ts_parser_delete(parser->parser);
        parser->parser = NULL;
        return -1;
    }

    init_perl_symbols(lang);

    return 0;
}

void parser_set_debug(PerlParser *parser, int debug) {
    parser->debug = debug;
    g_debug = debug;
}

int parser_parse_file(PerlParser *parser, const char *filepath,
                      const char *project_root, ParseResult *result) {
    /* Read source file */
    FILE *fp = safe_fopen(filepath, "rb", 0);
    if (!fp) {
        fprintf(stderr, "index-perl: cannot open %s\n", filepath);
        return -1;
    }

    int fd = fileno(fp);
    struct stat st;
    if (fstat(fd, &st) != 0) {
        fclose(fp);
        return -1;
    }
    size_t file_size = (size_t)st.st_size;

    char *source_code = malloc(file_size + 1);
    if (!source_code) {
        fclose(fp);
        return -1;
    }

    size_t bytes_read = fread(source_code, 1, file_size, fp);
    if (bytes_read != file_size) {
        fprintf(stderr, "index-perl: error reading %s\n", filepath);
        free(source_code);
        fclose(fp);
        return -1;
    }
    source_code[bytes_read] = '\0';
    fclose(fp);

    result->count = 0;

    /* Parse with tree-sitter */
    TSTree *tree = ts_parser_parse_string(parser->parser, NULL,
                                          source_code, (uint32_t)bytes_read);
    if (!tree) {
        fprintf(stderr, "index-perl: failed to parse %s\n", filepath);
        free(source_code);
        return -1;
    }

    /* Resolve directory and filename relative to project root */
    char directory[DIRECTORY_MAX_LENGTH];
    char filename[FILENAME_MAX_LENGTH];
    get_relative_path(filepath, project_root, directory, filename);

    /* Index filename without extension */
    char filename_no_ext[FILENAME_MAX_LENGTH];
    snprintf(filename_no_ext, sizeof(filename_no_ext), "%s", filename);
    char *dot = strrchr(filename_no_ext, '.');
    if (dot) *dot = '\0';

    if (filter_should_index(parser->filter, filename_no_ext)) {
        add_entry(result, filename_no_ext, 1, CONTEXT_FILENAME,
                  directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
    }

    /* Walk the AST */
    TSNode root = ts_tree_root_node(tree);
    visit_node(root, source_code, directory, filename, result, parser->filter);

    ts_tree_delete(tree);
    free(source_code);
    return 0;
}

void parser_free(PerlParser *parser) {
    if (parser->parser) {
        ts_parser_delete(parser->parser);
        parser->parser = NULL;
    }
}
