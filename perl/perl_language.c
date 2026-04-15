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
#include <stdbool.h>
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
    TSSymbol anonymous_subroutine_expression;
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
    TSSymbol coderef_call_expression;
    TSSymbol func1op_call_expression;
    TSSymbol anonymous_hash_expression;

    /* Expressions and statements */
    TSSymbol expression_statement;
    TSSymbol assignment_expression;
    TSSymbol return_expression;
    TSSymbol block;

    /* Literals and identifiers */
    TSSymbol string_literal;
    TSSymbol interpolated_string_literal;
    TSSymbol string_content;
    TSSymbol bareword;

    /* Comments and documentation */
    TSSymbol comment;
    TSSymbol pod;

    /* Goto and labels */
    TSSymbol statement_label;
    TSSymbol goto_expression;
    TSSymbol loopex_expression;
    TSSymbol identifier;
    TSSymbol label;

    /* Special blocks */
    TSSymbol phaser_statement;

    /* Container variables (direct hash/array element access: $hash{key}, $arr[idx]) */
    TSSymbol container_variable;
    TSSymbol hash_element_expression;

    /* Subroutine signature parameters */
    TSSymbol mandatory_parameter;
    TSSymbol optional_parameter;
    TSSymbol slurpy_parameter;
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
    perl_symbols.anonymous_subroutine_expression  = ts_language_symbol_for_name(language, "anonymous_subroutine_expression",  31, true);
    perl_symbols.package_statement                = ts_language_symbol_for_name(language, "package_statement",                17, true);

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
    perl_symbols.coderef_call_expression         = ts_language_symbol_for_name(language, "coderef_call_expression",           23, true);
    perl_symbols.func1op_call_expression         = ts_language_symbol_for_name(language, "func1op_call_expression",           23, true);
    perl_symbols.anonymous_hash_expression       = ts_language_symbol_for_name(language, "anonymous_hash_expression",         25, true);

    /* Expressions and statements */
    perl_symbols.expression_statement = ts_language_symbol_for_name(language, "expression_statement", 20, true);
    perl_symbols.assignment_expression = ts_language_symbol_for_name(language, "assignment_expression", 21, true);
    perl_symbols.return_expression = ts_language_symbol_for_name(language, "return_expression", 17, true);
    perl_symbols.block = ts_language_symbol_for_name(language, "block", 5, true);

    /* Literals and identifiers */
    perl_symbols.string_literal              = ts_language_symbol_for_name(language, "string_literal",              14, true);
    perl_symbols.interpolated_string_literal = ts_language_symbol_for_name(language, "interpolated_string_literal", 27, true);
    perl_symbols.string_content              = ts_language_symbol_for_name(language, "string_content",              14, true);
    perl_symbols.bareword                    = ts_language_symbol_for_name(language, "bareword",                     8, true);

    /* Comments and documentation */
    perl_symbols.comment = ts_language_symbol_for_name(language, "comment", 7, true);
    perl_symbols.pod = ts_language_symbol_for_name(language, "pod", 3, true);

    /* Goto and labels */
    perl_symbols.statement_label  = ts_language_symbol_for_name(language, "statement_label",  15, true);
    perl_symbols.goto_expression  = ts_language_symbol_for_name(language, "goto_expression",  15, true);
    perl_symbols.loopex_expression = ts_language_symbol_for_name(language, "loopex_expression", 17, true);
    perl_symbols.identifier       = ts_language_symbol_for_name(language, "identifier",       10, true);
    perl_symbols.label            = ts_language_symbol_for_name(language, "label",             5, true);

    /* Special blocks */
    perl_symbols.phaser_statement = ts_language_symbol_for_name(language, "phaser_statement", 16, true);

    /* Container variables */
    perl_symbols.container_variable      = ts_language_symbol_for_name(language, "container_variable",      18, true);
    perl_symbols.hash_element_expression = ts_language_symbol_for_name(language, "hash_element_expression", 23, true);

    /* Subroutine signature parameters */
    perl_symbols.mandatory_parameter = ts_language_symbol_for_name(language, "mandatory_parameter", 19, true);
    perl_symbols.optional_parameter  = ts_language_symbol_for_name(language, "optional_parameter",  18, true);
    perl_symbols.slurpy_parameter    = ts_language_symbol_for_name(language, "slurpy_parameter",    16, true);
}

/* Extract variable name from a scalar/array/hash node and index it */
static void index_sigil_node(TSNode node, const char *source_code,
                             const char *directory, const char *filename,
                             ParseResult *result, SymbolFilter *filter,
                             const char *modifier, const char *definition,
                             ContextType ctx) {
    /* Find the varname child */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_symbol(child) != perl_symbols.varname) {
            continue;
        }

        /* Complex dereference: @{$ref}, %{$href} — varname contains a nested sigil node.
         * Recurse into the inner scalar/array/hash rather than extracting the raw text span. */
        uint32_t vnc = ts_node_child_count(child);
        if (vnc > 0) {
            for (uint32_t j = 0; j < vnc; j++) {
                TSNode inner = ts_node_child(child, j);
                TSSymbol isym = ts_node_symbol(inner);
                if (isym == perl_symbols.scalar  || isym == perl_symbols.array ||
                    isym == perl_symbols.hash     || isym == perl_symbols.container_variable) {
                    index_sigil_node(inner, source_code, directory, filename,
                                     result, filter, modifier, definition, ctx);
                }
            }
            break;
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

        /* Derive sigil type from node kind */
        TSSymbol node_sym = ts_node_symbol(node);
        const char *sigil_type = PERL_TYPE_SCALAR;
        if (node_sym == perl_symbols.array) sigil_type = PERL_TYPE_ARRAY;
        else if (node_sym == perl_symbols.hash) sigil_type = PERL_TYPE_HASH;
        /* scalar and container_variable both yield PERL_TYPE_SCALAR */

        int line = (int)ts_node_start_point(node).row + 1;
        add_entry(result, varname, line, ctx,
                  directory, filename, NULL,
                  &(ExtColumns){ .modifier = modifier, .definition = definition,
                                 .type = sigil_type });
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

    /* Detect parameter extraction patterns — index as CONTEXT_ARGUMENT:
     *   Case 1: my ($self, $name) = @_
     *   Case 2: my $self = shift  (implicit @_, no explicit array target) */
    ContextType ctx = CONTEXT_VARIABLE;
    TSNode parent = ts_node_parent(node);
    if (!ts_node_is_null(parent) &&
        ts_node_symbol(parent) == perl_symbols.assignment_expression) {
        uint32_t parent_count = ts_node_child_count(parent);
        for (uint32_t i = 0; i < parent_count; i++) {
            TSNode sibling = ts_node_child(parent, i);
            TSSymbol sib_sym = ts_node_symbol(sibling);

            /* Case 1: RHS is @_ array */
            if (sib_sym == perl_symbols.array) {
                uint32_t ac = ts_node_child_count(sibling);
                for (uint32_t j = 0; j < ac; j++) {
                    TSNode ac_child = ts_node_child(sibling, j);
                    if (ts_node_symbol(ac_child) != perl_symbols.varname) continue;
                    char name[8];
                    safe_extract_node_text(source_code, ac_child, name, sizeof(name), filename);
                    if (strcmp(name, "_") == 0) ctx = CONTEXT_ARGUMENT;
                    break;
                }
            }

            /* Case 2: RHS is shift with no explicit non-@_ array target */
            if (sib_sym == perl_symbols.func1op_call_expression) {
                TSNode fn = ts_node_child_by_field_name(sibling, "function", 8);
                if (!ts_node_is_null(fn)) {
                    char fn_text[16];
                    safe_extract_node_text(source_code, fn, fn_text, sizeof(fn_text), filename);
                    if (strcmp(fn_text, "shift") == 0) {
                        /* Check whether an explicit non-@_ array was passed */
                        bool has_explicit_target = false;
                        uint32_t sc = ts_node_child_count(sibling);
                        for (uint32_t j = 0; j < sc; j++) {
                            TSNode sc_child = ts_node_child(sibling, j);
                            if (ts_node_symbol(sc_child) != perl_symbols.array) continue;
                            /* Has array child — is it @_? */
                            uint32_t vc = ts_node_child_count(sc_child);
                            bool is_at_under = false;
                            for (uint32_t k = 0; k < vc; k++) {
                                TSNode vn = ts_node_child(sc_child, k);
                                if (ts_node_symbol(vn) != perl_symbols.varname) continue;
                                char name[8];
                                safe_extract_node_text(source_code, vn, name, sizeof(name), filename);
                                if (strcmp(name, "_") == 0) is_at_under = true;
                                break;
                            }
                            if (!is_at_under) has_explicit_target = true;
                            break;
                        }
                        if (!has_explicit_target) ctx = CONTEXT_ARGUMENT;
                    }
                }
            }

            if (ctx == CONTEXT_ARGUMENT) break;
        }
    }

    /* Detect export lists: our @EXPORT = qw(...) / our @EXPORT_OK = qw(...)
     * Index each exported name as CONTEXT_EXPORT. */
    if (strcmp(modifier, "our") == 0 &&
        !ts_node_is_null(parent) &&
        ts_node_symbol(parent) == perl_symbols.assignment_expression) {
        /* Find the array varname inside this declaration */
        char export_varname[32] = {0};
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            if (ts_node_symbol(child) != perl_symbols.array) continue;
            uint32_t ac = ts_node_child_count(child);
            for (uint32_t j = 0; j < ac; j++) {
                TSNode vn = ts_node_child(child, j);
                if (ts_node_symbol(vn) != perl_symbols.varname) continue;
                safe_extract_node_text(source_code, vn, export_varname, sizeof(export_varname), filename);
                break;
            }
            break;
        }
        if (strcmp(export_varname, "EXPORT") == 0 ||
            strcmp(export_varname, "EXPORT_OK") == 0) {
            /* Find the quoted_word_list on the RHS */
            uint32_t pc = ts_node_child_count(parent);
            for (uint32_t i = 0; i < pc; i++) {
                TSNode sib = ts_node_child(parent, i);
                if (ts_node_symbol(sib) != perl_symbols.quoted_word_list) continue;
                uint32_t qc = ts_node_child_count(sib);
                for (uint32_t j = 0; j < qc; j++) {
                    TSNode qchild = ts_node_child(sib, j);
                    if (ts_node_symbol(qchild) != perl_symbols.string_content) continue;
                    char content[COMMENT_TEXT_BUFFER];
                    safe_extract_node_text(source_code, qchild, content, sizeof(content), filename);
                    int exp_line = (int)ts_node_start_point(qchild).row + 1;
                    char word[SYMBOL_MAX_LENGTH];
                    char *word_start = content;
                    for (char *p = content; ; p++) {
                        if (*p == '\0' || isspace((unsigned char)*p)) {
                            if (p > word_start) {
                                size_t wlen = (size_t)(p - word_start);
                                if (wlen < sizeof(word)) {
                                    snprintf(word, sizeof(word), "%.*s", (int)wlen, word_start);
                                    if (word[0] && filter_should_index(filter, word)) {
                                        add_entry(result, word, exp_line, CONTEXT_EXPORT,
                                                  directory, filename, NULL,
                                                  &(ExtColumns){.definition = "1"});
                                    }
                                }
                                exp_line++;
                            }
                            word_start = p + 1;
                            if (*p == '\0') break;
                        }
                    }
                    break;
                }
                break;
            }
        }
    }

    /* Walk all children looking for scalar/array/hash nodes */
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol sym = ts_node_symbol(child);

        if (sym == perl_symbols.scalar ||
            sym == perl_symbols.array  ||
            sym == perl_symbols.hash) {
            index_sigil_node(child, source_code, directory, filename,
                             result, filter, modifier, "1", ctx);
        }
    }
}

/* Handle signature parameters (mandatory, optional, slurpy): index sigil child as ARG,
 * recurse into everything else so default values in optional parameters are indexed. */
static void handle_parameter(TSNode node, const char *source_code,
                             const char *directory, const char *filename,
                             ParseResult *result, SymbolFilter *filter,
                             int line) {
    (void)line;
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol sym = ts_node_symbol(child);
        if (sym == perl_symbols.scalar ||
            sym == perl_symbols.array  ||
            sym == perl_symbols.hash) {
            index_sigil_node(child, source_code, directory, filename,
                             result, filter, "", "1", CONTEXT_ARGUMENT);
        } else {
            visit_node(child, source_code, directory, filename, result, filter);
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
                          directory, filename, NULL, &(ExtColumns){.definition = "1"});
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
    char location[128];
    format_source_location(node, location, sizeof(location));

    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_symbol(child) == perl_symbols.bareword) {
            char name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, name, sizeof(name), filename);
            if (filter_should_index(filter, name)) {
                add_entry(result, name, line, CONTEXT_FUNCTION,
                          directory, filename, location, &(ExtColumns){.definition = "1"});
            }
            break;
        }
    }
    /* Recurse into the block body to index variables, comments, etc. */
    process_children(node, source_code, directory, filename, result, filter);
}

static void handle_anonymous_sub(TSNode node, const char *source_code,
                                 const char *directory, const char *filename,
                                 ParseResult *result, SymbolFilter *filter,
                                 int line) {
    char location[128];
    format_source_location(node, location, sizeof(location));

    add_entry(result, "<lambda>", line, CONTEXT_LAMBDA,
              directory, filename, location, &(ExtColumns){.definition = "1"});

    /* Recurse into the block body */
    process_children(node, source_code, directory, filename, result, filter);
}

/* Handle BEGIN/END/INIT/CHECK/UNITCHECK phaser blocks */
static void handle_phaser_statement(TSNode node, const char *source_code,
                                    const char *directory, const char *filename,
                                    ParseResult *result, SymbolFilter *filter,
                                    int line) {
    /* First child is the keyword node (BEGIN, END, etc.) — its type IS the name */
    TSNode keyword = ts_node_child(node, 0);
    if (ts_node_is_null(keyword)) return;

    const char *phaser_name = ts_node_type(keyword);

    char location[128];
    format_source_location(node, location, sizeof(location));

    add_entry(result, phaser_name, line, CONTEXT_FUNCTION,
              directory, filename, location, &(ExtColumns){.definition = "1"});

    /* Recurse into the block body */
    process_children(node, source_code, directory, filename, result, filter);
}

/* Index a method_call_expression: the method name as CONTEXT_CALL */
static void handle_method_call(TSNode node, const char *source_code,
                               const char *directory, const char *filename,
                               ParseResult *result, SymbolFilter *filter,
                               int line) {
    char invocant[SYMBOL_MAX_LENGTH] = "";
    char method_name[SYMBOL_MAX_LENGTH] = "";
    bool found_invocant = false;

    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (!found_invocant) {
            if (child_sym == perl_symbols.scalar) {
                /* $obj->method() — extract varname from scalar child */
                uint32_t sc_count = ts_node_child_count(child);
                for (uint32_t j = 0; j < sc_count; j++) {
                    TSNode sc = ts_node_child(child, j);
                    if (ts_node_symbol(sc) == perl_symbols.varname) {
                        safe_extract_node_text(source_code, sc, invocant, sizeof(invocant), filename);
                        break;
                    }
                }
                found_invocant = true;
                continue;
            } else if (child_sym == perl_symbols.bareword) {
                /* Dog->new() — class name as bareword */
                safe_extract_node_text(source_code, child, invocant, sizeof(invocant), filename);
                found_invocant = true;
                continue;
            }
        }

        if (child_sym == perl_symbols.method) {
            /* Static method: method node contains plain text */
            /* Dynamic method: $self->$orig() — method node contains a nested scalar */
            uint32_t mc = ts_node_child_count(child);
            bool found = false;
            for (uint32_t j = 0; j < mc; j++) {
                TSNode m = ts_node_child(child, j);
                if (ts_node_symbol(m) == perl_symbols.scalar) {
                    /* Dynamic: dig into scalar's varname */
                    uint32_t sc = ts_node_child_count(m);
                    for (uint32_t k = 0; k < sc; k++) {
                        TSNode vn = ts_node_child(m, k);
                        if (ts_node_symbol(vn) == perl_symbols.varname) {
                            safe_extract_node_text(source_code, vn, method_name, sizeof(method_name), filename);
                            found = true;
                            break;
                        }
                    }
                    break;
                }
            }
            if (!found) {
                safe_extract_node_text(source_code, child, method_name, sizeof(method_name), filename);
            }
        }
    }

    if (method_name[0] && filter_should_index(filter, method_name)) {
        add_entry(result, method_name, line, CONTEXT_CALL,
                  directory, filename, NULL,
                  &(ExtColumns){
                      .parent = invocant[0] ? invocant : NULL,
                      .definition = "0"
                  });
    }

    /* Recurse to catch nested calls in arguments */
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
                          directory, filename, NULL, &(ExtColumns){.definition = "0"});
            }
            break;
        }
    }
    process_children(node, source_code, directory, filename, result, filter);
}

static void handle_coderef_call(TSNode node, const char *source_code,
                                const char *directory, const char *filename,
                                ParseResult *result, SymbolFilter *filter,
                                int line) {
    uint32_t child_count = ts_node_child_count(node);
    bool found_coderef = false;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);

        if (!found_coderef && ts_node_symbol(child) == perl_symbols.scalar) {
            /* Index the coderef variable as CALL rather than letting the
             * standalone scalar handler index it as VAR */
            uint32_t sc_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < sc_count; j++) {
                TSNode sc = ts_node_child(child, j);
                if (ts_node_symbol(sc) == perl_symbols.varname) {
                    char name[SYMBOL_MAX_LENGTH];
                    safe_extract_node_text(source_code, sc, name, sizeof(name), filename);
                    if (strlen(name) > 1 && filter_should_index(filter, name)) {
                        add_entry(result, name, line, CONTEXT_CALL,
                                  directory, filename, NULL, &(ExtColumns){.definition = "0"});
                    }
                    break;
                }
            }
            found_coderef = true;
            continue; /* skip — already indexed as CALL above */
        }

        /* Recurse into arguments */
        visit_node(child, source_code, directory, filename, result, filter);
    }
}

/* Index a statement_label: OUTER: while (...) { ... } → "OUTER" as CONTEXT_LABEL */
static void handle_statement_label(TSNode node, const char *source_code,
                                   const char *directory, const char *filename,
                                   ParseResult *result, SymbolFilter *filter,
                                   int line) {
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_symbol(child) == perl_symbols.identifier) {
            char name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, name, sizeof(name), filename);
            if (filter_should_index(filter, name)) {
                add_entry(result, name, line, CONTEXT_LABEL,
                          directory, filename, NULL, &(ExtColumns){.definition = "1"});
            }
            break;
        }
    }
    /* Recurse into the labeled statement */
    process_children(node, source_code, directory, filename, result, filter);
}

/* Index a goto_expression:
 *   goto LABEL   → label name as CONTEXT_GOTO
 *   goto &func   → function name as CONTEXT_GOTO (tail call)
 *   goto $expr   → skip (computed target) */
static void handle_goto_expression(TSNode node, const char *source_code,
                                   const char *directory, const char *filename,
                                   ParseResult *result, SymbolFilter *filter,
                                   int line) {
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == perl_symbols.label) {
            /* goto LABEL */
            char name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, name, sizeof(name), filename);
            if (filter_should_index(filter, name)) {
                add_entry(result, name, line, CONTEXT_GOTO,
                          directory, filename, NULL, &(ExtColumns){.definition = "0"});
            }
            return;
        }
        if (child_sym == perl_symbols.function_call_expression) {
            /* goto &func — extract varname from inside the function field */
            TSNode fn = ts_node_child_by_field_name(child, "function", 8);
            if (!ts_node_is_null(fn)) {
                uint32_t fn_count = ts_node_child_count(fn);
                for (uint32_t j = 0; j < fn_count; j++) {
                    TSNode fn_child = ts_node_child(fn, j);
                    if (ts_node_symbol(fn_child) == perl_symbols.varname) {
                        char name[SYMBOL_MAX_LENGTH];
                        safe_extract_node_text(source_code, fn_child, name, sizeof(name), filename);
                        if (filter_should_index(filter, name)) {
                            add_entry(result, name, line, CONTEXT_GOTO,
                                      directory, filename, NULL, &(ExtColumns){.definition = "0"});
                        }
                        return;
                    }
                }
            }
            return;
        }
    }
}

/* Index a loopex_expression: last/next/redo LABEL → label name as CONTEXT_GOTO */
static void handle_loopex_expression(TSNode node, const char *source_code,
                                     const char *directory, const char *filename,
                                     ParseResult *result, SymbolFilter *filter,
                                     int line) {
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_symbol(child) == perl_symbols.label) {
            char name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, name, sizeof(name), filename);
            if (filter_should_index(filter, name)) {
                add_entry(result, name, line, CONTEXT_GOTO,
                          directory, filename, NULL, &(ExtColumns){.definition = "0"});
            }
            return;
        }
    }
}

/* Recursively index autoquoted_bareword nodes as constants (use constant PI => ...) */
/* Extract varname text from a scalar/array/hash/container_variable node without indexing it.
 * Returns true if a usable name was found (length > 1, skipping single-char Perl internals). */
static bool extract_sigil_varname(TSNode node, const char *source_code,
                                  const char *filename,
                                  char *buf, size_t bufsize) {
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_symbol(child) == perl_symbols.varname) {
            safe_extract_node_text(source_code, child, buf, bufsize, filename);
            return strlen(buf) > 1;
        }
    }
    return false;
}

/* Handle $obj->{key} and $hash{key}: index the container as VAR and the key as PROP with parent */
static void handle_hash_element_expression(TSNode node, const char *source_code,
                                           const char *directory, const char *filename,
                                           ParseResult *result, SymbolFilter *filter,
                                           int line) {
    char parent_name[SYMBOL_MAX_LENGTH] = "";
    bool has_parent = false;

    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol sym = ts_node_symbol(child);

        if (sym == perl_symbols.scalar || sym == perl_symbols.container_variable) {
            /* Extract container name for use as parent, and index it as VAR */
            if (extract_sigil_varname(child, source_code, filename, parent_name, sizeof(parent_name))) {
                has_parent = true;
                if (filter_should_index(filter, parent_name)) {
                    add_entry(result, parent_name, line, CONTEXT_VARIABLE,
                              directory, filename, NULL, &(ExtColumns){.definition = "0"});
                }
            }
        } else if (sym == perl_symbols.autoquoted_bareword) {
            /* Static key — index as PROP with parent */
            char key[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, key, sizeof(key), filename);
            if (filter_should_index(filter, key)) {
                add_entry(result, key, line, CONTEXT_PROPERTY,
                          directory, filename, NULL,
                          &(ExtColumns){
                              .definition = "0",
                              .parent = has_parent ? parent_name : NULL
                          });
            }
        } else {
            /* Dynamic key or nested expression — recurse normally */
            visit_node(child, source_code, directory, filename, result, filter);
        }
    }
}

/* Handle autoquoted barewords used as hash keys: $hash{KEY}, bless { key => val }, method(key => val) */
static void handle_autoquoted_bareword(TSNode node, const char *source_code,
                                       const char *directory, const char *filename,
                                       ParseResult *result, SymbolFilter *filter,
                                       int line) {
    char name[SYMBOL_MAX_LENGTH];
    safe_extract_node_text(source_code, node, name, sizeof(name), filename);
    if (filter_should_index(filter, name)) {
        add_entry(result, name, line, CONTEXT_PROPERTY,
                  directory, filename, NULL, &(ExtColumns){.definition = "0"});
    }
}

static void index_constant_names(TSNode node, const char *source_code,
                                 const char *directory, const char *filename,
                                 ParseResult *result, SymbolFilter *filter,
                                 int line) {
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_symbol(child) == perl_symbols.autoquoted_bareword) {
            char name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, name, sizeof(name), filename);
            if (filter_should_index(filter, name)) {
                add_entry(result, name, line, CONTEXT_VARIABLE,
                          directory, filename, NULL, &(ExtColumns){.modifier = "const", .definition = "1"});
            }
        } else {
            index_constant_names(child, source_code, directory, filename, result, filter, line);
        }
    }
}

/* Index a use_statement: module name + optional qw() imported names */
static void handle_use_statement(TSNode node, const char *source_code,
                                 const char *directory, const char *filename,
                                 ParseResult *result, SymbolFilter *filter,
                                 int line) {
    char module_name[SYMBOL_MAX_LENGTH] = {0};
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == perl_symbols.package_name) {
            safe_extract_node_text(source_code, child, module_name, sizeof(module_name), filename);
            if (filter_should_index(filter, module_name)) {
                add_entry(result, module_name, line, CONTEXT_IMPORT,
                          directory, filename, NULL, &(ExtColumns){.definition = "0"});
            }
        } else if (strcmp(module_name, "constant") == 0) {
            /* use constant NAME => val  or  use constant { NAME => val, ... }
             * Recurse into any non-package child to collect autoquoted_bareword keys */
            index_constant_names(child, source_code, directory, filename, result, filter, line);
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
                                              directory, filename, NULL, &(ExtColumns){.definition = "0"});
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
                          directory, filename, NULL, &(ExtColumns){.definition = "0"});
            }
            break;
        }
    }
}

/* Index words from string literals as CONTEXT_STRING.
 * Handles both 'single-quoted' and "double-quoted/interpolated" strings.
 * For interpolated strings, process_children recurses into embedded
 * variable/expression nodes so they are also indexed (as VAR/CALL). */
static void handle_string(TSNode node, const char *source_code,
                          const char *directory, const char *filename,
                          ParseResult *result, SymbolFilter *filter,
                          int line) {
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_symbol(child) != perl_symbols.string_content) continue;

        char text[CLEANED_WORD_BUFFER];
        safe_extract_node_text(source_code, child, text, sizeof(text), filename);

        char word[CLEANED_WORD_BUFFER];
        char cleaned[CLEANED_WORD_BUFFER];
        char *word_start = text;

        for (char *p = text; ; p++) {
            if (*p == '\0' || isspace(*p)) {
                if (p > word_start) {
                    size_t word_len = (size_t)(p - word_start);
                    if (word_len < sizeof(word)) {
                        snprintf(word, sizeof(word), "%.*s", (int)word_len, word_start);
                        filter_clean_string_symbol(word, cleaned, sizeof(cleaned));
                        if (cleaned[0] && filter_should_index(filter, cleaned)) {
                            add_entry(result, cleaned, line, CONTEXT_STRING,
                                      directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
                        }
                    }
                }
                word_start = p + 1;
                if (*p == '\0') break;
            }
        }
    }
    /* Recurse to index interpolated variable/expression references */
    process_children(node, source_code, directory, filename, result, filter);
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

    if (node_sym == perl_symbols.hash_element_expression) {
        handle_hash_element_expression(node, source_code, directory, filename, result, filter, line);
        return;
    }

    /* Standalone scalar/array/hash/container_variable outside a declaration — index as reference */
    if (node_sym == perl_symbols.scalar         ||
        node_sym == perl_symbols.array          ||
        node_sym == perl_symbols.hash           ||
        node_sym == perl_symbols.container_variable) {
        index_sigil_node(node, source_code, directory, filename,
                         result, filter, "", "0", CONTEXT_VARIABLE);
        return;
    }

    if (node_sym == perl_symbols.mandatory_parameter ||
        node_sym == perl_symbols.optional_parameter  ||
        node_sym == perl_symbols.slurpy_parameter) {
        handle_parameter(node, source_code, directory, filename, result, filter, line);
        return;
    }

    if (node_sym == perl_symbols.subroutine_declaration_statement) {
        handle_subroutine_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == perl_symbols.anonymous_subroutine_expression) {
        handle_anonymous_sub(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == perl_symbols.phaser_statement) {
        handle_phaser_statement(node, source_code, directory, filename, result, filter, line);
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
    if (node_sym == perl_symbols.coderef_call_expression) {
        handle_coderef_call(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == perl_symbols.string_literal ||
        node_sym == perl_symbols.interpolated_string_literal) {
        handle_string(node, source_code, directory, filename, result, filter, line);
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
    if (node_sym == perl_symbols.statement_label) {
        handle_statement_label(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == perl_symbols.goto_expression) {
        handle_goto_expression(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == perl_symbols.loopex_expression) {
        handle_loopex_expression(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == perl_symbols.autoquoted_bareword) {
        handle_autoquoted_bareword(node, source_code, directory, filename, result, filter, line);
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
