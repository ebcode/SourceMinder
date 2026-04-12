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
#include "../shared/comment_utils.h"
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
    TSSymbol package_name;
    TSSymbol require_expression;
    TSSymbol autoquoted_bareword;
    TSSymbol quoted_word_list;

    /* Method and function calls */
    TSSymbol method_call_expression;
    TSSymbol method;
    TSSymbol function_call_expression;
    TSSymbol ambiguous_function_call_expression;
    TSSymbol function;

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
    perl_symbols.use_statement        = ts_language_symbol_for_name(language, "use_statement",        13, true);
    perl_symbols.package_name         = ts_language_symbol_for_name(language, "package",               7, true);
    perl_symbols.require_expression   = ts_language_symbol_for_name(language, "require_expression",   18, true);
    perl_symbols.autoquoted_bareword  = ts_language_symbol_for_name(language, "autoquoted_bareword",  19, true);
    perl_symbols.quoted_word_list     = ts_language_symbol_for_name(language, "quoted_word_list",     16, true);

    /* Method calls */
    perl_symbols.method_call_expression          = ts_language_symbol_for_name(language, "method_call_expression",          22, true);
    perl_symbols.method                          = ts_language_symbol_for_name(language, "method",                            6, true);
    perl_symbols.function_call_expression        = ts_language_symbol_for_name(language, "function_call_expression",        24, true);
    perl_symbols.ambiguous_function_call_expression = ts_language_symbol_for_name(language, "ambiguous_function_call_expression", 34, true);
    perl_symbols.function                        = ts_language_symbol_for_name(language, "function",                          8, true);

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

/* Index a package_statement: the second 'package' child is the module name */
static void handle_package_statement(TSNode node, const char *source_code,
                                     const char *directory, const char *filename,
                                     ParseResult *result, SymbolFilter *filter,
                                     int line) {
    /* The keyword "package" is anonymous; perl_symbols.package_name (named=true)
     * only matches the module name node — so the first match is what we want. */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_symbol(child) == perl_symbols.package_name) {
            char name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, name, sizeof(name), filename);
            if (filter_should_index(filter, name)) {
                add_entry(result, name, line, CONTEXT_NAMESPACE,
                          directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
            }
            break;
        }
    }
}

/* Index a subroutine_declaration_statement: bareword child is the sub name */
static void handle_subroutine_declaration(TSNode node, const char *source_code,
                                          const char *directory, const char *filename,
                                          ParseResult *result, SymbolFilter *filter,
                                          int line) {
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_symbol(child) == perl_symbols.bareword) {
            char name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, name, sizeof(name), filename);
            if (filter_should_index(filter, name)) {
                add_entry(result, name, line, CONTEXT_FUNCTION,
                          directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
            }
            break;
        }
    }
    /* Recurse into the block body to index variables, comments, etc. */
    process_children(node, source_code, directory, filename, result, filter);
}

/* Index a method_call_expression: the method name as CONTEXT_CALL */
static void handle_method_call(TSNode node, const char *source_code,
                               const char *directory, const char *filename,
                               ParseResult *result, SymbolFilter *filter,
                               int line) {
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_symbol(child) == perl_symbols.method) {
            char name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, name, sizeof(name), filename);
            if (filter_should_index(filter, name)) {
                add_entry(result, name, line, CONTEXT_CALL,
                          directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
            }
            break;
        }
    }
    /* Recurse to catch any nested method calls in arguments */
    process_children(node, source_code, directory, filename, result, filter);
}

/* Index a function_call_expression or ambiguous_function_call_expression:
 * the function name as CONTEXT_CALL */
static void handle_function_call(TSNode node, const char *source_code,
                                 const char *directory, const char *filename,
                                 ParseResult *result, SymbolFilter *filter,
                                 int line) {
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_symbol(child) == perl_symbols.function) {
            char name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, name, sizeof(name), filename);
            if (filter_should_index(filter, name)) {
                add_entry(result, name, line, CONTEXT_CALL,
                          directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
            }
            break;
        }
    }
    process_children(node, source_code, directory, filename, result, filter);
}

/* Index a use_statement: module name + optional qw() imported names */
static void handle_use_statement(TSNode node, const char *source_code,
                                 const char *directory, const char *filename,
                                 ParseResult *result, SymbolFilter *filter,
                                 int line) {
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == perl_symbols.package_name) {
            char module_name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, module_name, sizeof(module_name), filename);
            if (filter_should_index(filter, module_name)) {
                add_entry(result, module_name, line, CONTEXT_IMPORT,
                          directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
            }
        } else if (child_sym == perl_symbols.quoted_word_list) {
            /* Index each imported name from qw(...) */
            uint32_t qw_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < qw_count; j++) {
                TSNode qw_child = ts_node_child(child, j);
                if (ts_node_symbol(qw_child) != perl_symbols.string_content) continue;

                char content[COMMENT_TEXT_BUFFER];
                safe_extract_node_text(source_code, qw_child, content, sizeof(content), filename);

                char word[SYMBOL_MAX_LENGTH];
                char *word_start = content;
                for (char *p = content; ; p++) {
                    if (*p == '\0' || isspace(*p)) {
                        if (p > word_start) {
                            size_t word_len = (size_t)(p - word_start);
                            if (word_len < sizeof(word)) {
                                snprintf(word, sizeof(word), "%.*s", (int)word_len, word_start);
                                if (word[0] && filter_should_index(filter, word)) {
                                    add_entry(result, word, line, CONTEXT_IMPORT,
                                              directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
                                }
                            }
                        }
                        word_start = p + 1;
                        if (*p == '\0') break;
                    }
                }
            }
        }
    }
}

/* Index a require_expression: the module name being loaded at runtime */
static void handle_require_expression(TSNode node, const char *source_code,
                                      const char *directory, const char *filename,
                                      ParseResult *result, SymbolFilter *filter,
                                      int line) {
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == perl_symbols.autoquoted_bareword ||
            child_sym == perl_symbols.bareword           ||
            child_sym == perl_symbols.string_literal) {
            char module_name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, module_name, sizeof(module_name), filename);
            if (filter_should_index(filter, module_name)) {
                add_entry(result, module_name, line, CONTEXT_IMPORT,
                          directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
            }
            break;
        }
    }
}

/* Index words from a Perl # comment */
static void handle_comment(TSNode node, const char *source_code,
                           const char *directory, const char *filename,
                           ParseResult *result, SymbolFilter *filter,
                           int line) {
    char comment_text[COMMENT_TEXT_BUFFER];
    safe_extract_node_text(source_code, node, comment_text, sizeof(comment_text), filename);

    /* Skip shebang line */
    if (comment_text[0] == '#' && comment_text[1] == '!') return;

    char *text_start = strip_comment_delimiters(comment_text);

    char word[CLEANED_WORD_BUFFER];
    char cleaned[CLEANED_WORD_BUFFER];
    char *word_start = text_start;

    for (char *p = text_start; ; p++) {
        if (*p == '\0' || isspace(*p)) {
            if (p > word_start) {
                size_t word_len = (size_t)(p - word_start);
                if (word_len < sizeof(word)) {
                    snprintf(word, sizeof(word), "%.*s", (int)word_len, word_start);
                    filter_clean_string_symbol(word, cleaned, sizeof(cleaned));
                    if (cleaned[0] && filter_should_index(filter, cleaned)) {
                        add_entry(result, cleaned, line, CONTEXT_COMMENT,
                                  directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
                    }
                }
            }
            word_start = p + 1;
            if (*p == '\0') break;
        }
    }
}

/* Index words from a POD documentation block, skipping directive lines (=head1, =cut, etc.) */
static void handle_pod(TSNode node, const char *source_code,
                       const char *directory, const char *filename,
                       ParseResult *result, SymbolFilter *filter,
                       int line) {
    char pod_text[COMMENT_TEXT_BUFFER];
    safe_extract_node_text(source_code, node, pod_text, sizeof(pod_text), filename);

    char word[CLEANED_WORD_BUFFER];
    char cleaned[CLEANED_WORD_BUFFER];
    char *p = pod_text;
    int current_line = line;

    while (*p) {
        /* Directive lines start with '=': skip the directive keyword (=head1, =cut, etc.)
         * but index any remaining words on the line (e.g. "NAME" from "=head1 NAME") */
        if (*p == '=') {
            while (*p && !isspace(*p)) p++;  /* skip directive keyword */
        }

        /* Collect a line's words */
        char *word_start = p;
        while (*p && *p != '\n') {
            if (isspace(*p)) {
                if (p > word_start) {
                    size_t word_len = (size_t)(p - word_start);
                    if (word_len < sizeof(word)) {
                        snprintf(word, sizeof(word), "%.*s", (int)word_len, word_start);
                        filter_clean_string_symbol(word, cleaned, sizeof(cleaned));
                        if (cleaned[0] && filter_should_index(filter, cleaned)) {
                            add_entry(result, cleaned, current_line, CONTEXT_COMMENT,
                                      directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
                        }
                    }
                }
                word_start = p + 1;
            }
            p++;
        }
        /* Flush last word on line */
        if (p > word_start) {
            size_t word_len = (size_t)(p - word_start);
            if (word_len < sizeof(word)) {
                snprintf(word, sizeof(word), "%.*s", (int)word_len, word_start);
                filter_clean_string_symbol(word, cleaned, sizeof(cleaned));
                if (cleaned[0] && filter_should_index(filter, cleaned)) {
                    add_entry(result, cleaned, current_line, CONTEXT_COMMENT,
                              directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
                }
            }
        }
        if (*p == '\n') { p++; current_line++; }
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

    if (node_sym == perl_symbols.subroutine_declaration_statement) {
        handle_subroutine_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == perl_symbols.package_statement) {
        handle_package_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == perl_symbols.method_call_expression) {
        handle_method_call(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == perl_symbols.function_call_expression ||
        node_sym == perl_symbols.ambiguous_function_call_expression) {
        handle_function_call(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == perl_symbols.use_statement) {
        handle_use_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == perl_symbols.require_expression) {
        handle_require_expression(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == perl_symbols.comment) {
        handle_comment(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == perl_symbols.pod) {
        handle_pod(node, source_code, directory, filename, result, filter, line);
        return;
    }
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
