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
#include "php_language.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/stat.h>
#include <tree_sitter/api.h>
#include "../shared/comment_utils.h"
#include "../shared/file_opener.h"
#include "../shared/string_utils.h"
#include "../shared/parse_result.h"
#include "../shared/file_utils.h"

/* External PHP language function from tree-sitter-php */
extern const TSLanguage *tree_sitter_php(void);

/* Global debug flag */
static int g_debug = 0;

/* Symbol lookup table for fast node type comparisons */
static struct {
    /* Core identifiers */
    TSSymbol name;
    TSSymbol variable_name;
    TSSymbol namespace_name;
    TSSymbol qualified_name;

    /* Expressions */
    TSSymbol scoped_call_expression;
    TSSymbol scoped_property_access_expression;
    TSSymbol member_call_expression;
    TSSymbol member_access_expression;
    TSSymbol function_call_expression;
    TSSymbol class_constant_access_expression;
    TSSymbol subscript_expression;
    TSSymbol assignment_expression;
    TSSymbol binary_expression;
    TSSymbol parenthesized_expression;

    /* Structural nodes */
    TSSymbol arguments;
    TSSymbol argument;
    TSSymbol compound_statement;
    TSSymbol program;

    /* Type-related */
    TSSymbol union_type;
    TSSymbol intersection_type;

    /* String nodes */
    TSSymbol string_value;
    TSSymbol string_content;
    TSSymbol heredoc_body;
    TSSymbol nowdoc_body;
    TSSymbol nowdoc_string;

    /* Declaration elements */
    TSSymbol property_element;
    TSSymbol const_element;
    TSSymbol class_interface_clause;

    /* Parameters */
    TSSymbol simple_parameter;
    TSSymbol property_promotion_parameter;

    /* Modifiers */
    TSSymbol visibility_modifier;
    TSSymbol static_modifier;
    TSSymbol readonly_modifier;
    TSSymbol final_modifier;
    TSSymbol abstract_modifier;
    TSSymbol relative_scope;

    /* Dispatch handler symbols (26 handlers) */
    TSSymbol namespace_definition;
    TSSymbol class_declaration;
    TSSymbol interface_declaration;
    TSSymbol trait_declaration;
    TSSymbol method_declaration;
    TSSymbol function_definition;
    TSSymbol anonymous_function;
    TSSymbol arrow_function;
    TSSymbol enum_declaration;
    TSSymbol enum_case;
    TSSymbol formal_parameters;
    TSSymbol property_declaration;
    TSSymbol const_declaration;
    TSSymbol comment;
    TSSymbol string;
    TSSymbol encapsed_string;
    TSSymbol heredoc;
    TSSymbol nowdoc;
} php_symbols;

/* Forward declaration for helper function used in extract_symbols_from_expression */
static void process_string_content(const char *string_text, int line, const char *directory,
                                   const char *filename, ParseResult *result, SymbolFilter *filter,
                                   const char *namespace_buf, const char *context_clue);

/* Initialize symbol lookup table - called once at startup */
static void init_php_symbols(const TSLanguage *language) {
    static int initialized = 0;
    if (initialized) return;
    initialized = 1;

    /* Core identifiers */
    php_symbols.name = ts_language_symbol_for_name(language, "name", 4, true);
    php_symbols.variable_name = ts_language_symbol_for_name(language, "variable_name", 13, true);
    php_symbols.namespace_name = ts_language_symbol_for_name(language, "namespace_name", 14, true);
    php_symbols.qualified_name = ts_language_symbol_for_name(language, "qualified_name", 14, true);

    /* Expressions */
    php_symbols.scoped_call_expression = ts_language_symbol_for_name(language, "scoped_call_expression", 22, true);
    php_symbols.scoped_property_access_expression = ts_language_symbol_for_name(language, "scoped_property_access_expression", 33, true);
    php_symbols.member_call_expression = ts_language_symbol_for_name(language, "member_call_expression", 22, true);
    php_symbols.member_access_expression = ts_language_symbol_for_name(language, "member_access_expression", 24, true);
    php_symbols.function_call_expression = ts_language_symbol_for_name(language, "function_call_expression", 24, true);
    php_symbols.class_constant_access_expression = ts_language_symbol_for_name(language, "class_constant_access_expression", 32, true);
    php_symbols.subscript_expression = ts_language_symbol_for_name(language, "subscript_expression", 20, true);
    php_symbols.assignment_expression = ts_language_symbol_for_name(language, "assignment_expression", 21, true);
    php_symbols.binary_expression = ts_language_symbol_for_name(language, "binary_expression", 17, true);
    php_symbols.parenthesized_expression = ts_language_symbol_for_name(language, "parenthesized_expression", 24, true);

    /* Structural nodes */
    php_symbols.arguments = ts_language_symbol_for_name(language, "arguments", 9, true);
    php_symbols.argument = ts_language_symbol_for_name(language, "argument", 8, true);
    php_symbols.compound_statement = ts_language_symbol_for_name(language, "compound_statement", 18, true);
    php_symbols.program = ts_language_symbol_for_name(language, "program", 7, true);

    /* Type-related */
    php_symbols.union_type = ts_language_symbol_for_name(language, "union_type", 10, true);
    php_symbols.intersection_type = ts_language_symbol_for_name(language, "intersection_type", 17, true);

    /* String nodes */
    php_symbols.string_value = ts_language_symbol_for_name(language, "string_value", 12, true);
    php_symbols.string_content = ts_language_symbol_for_name(language, "string_content", 14, true);
    php_symbols.heredoc_body = ts_language_symbol_for_name(language, "heredoc_body", 12, true);
    php_symbols.nowdoc_body = ts_language_symbol_for_name(language, "nowdoc_body", 11, true);
    php_symbols.nowdoc_string = ts_language_symbol_for_name(language, "nowdoc_string", 13, true);

    /* Declaration elements */
    php_symbols.property_element = ts_language_symbol_for_name(language, "property_element", 16, true);
    php_symbols.const_element = ts_language_symbol_for_name(language, "const_element", 13, true);
    php_symbols.class_interface_clause = ts_language_symbol_for_name(language, "class_interface_clause", 22, true);

    /* Parameters */
    php_symbols.simple_parameter = ts_language_symbol_for_name(language, "simple_parameter", 16, true);
    php_symbols.property_promotion_parameter = ts_language_symbol_for_name(language, "property_promotion_parameter", 28, true);

    /* Modifiers */
    php_symbols.visibility_modifier = ts_language_symbol_for_name(language, "visibility_modifier", 19, true);
    php_symbols.static_modifier = ts_language_symbol_for_name(language, "static_modifier", 15, true);
    php_symbols.readonly_modifier = ts_language_symbol_for_name(language, "readonly_modifier", 17, true);
    php_symbols.final_modifier = ts_language_symbol_for_name(language, "final_modifier", 14, true);
    php_symbols.abstract_modifier = ts_language_symbol_for_name(language, "abstract_modifier", 17, true);
    php_symbols.relative_scope = ts_language_symbol_for_name(language, "relative_scope", 14, true);

    /* Dispatch handler symbols (26 handlers) */
    php_symbols.namespace_definition = ts_language_symbol_for_name(language, "namespace_definition", 20, true);
    php_symbols.class_declaration = ts_language_symbol_for_name(language, "class_declaration", 17, true);
    php_symbols.interface_declaration = ts_language_symbol_for_name(language, "interface_declaration", 21, true);
    php_symbols.trait_declaration = ts_language_symbol_for_name(language, "trait_declaration", 17, true);
    php_symbols.method_declaration = ts_language_symbol_for_name(language, "method_declaration", 18, true);
    php_symbols.function_definition = ts_language_symbol_for_name(language, "function_definition", 19, true);
    php_symbols.anonymous_function = ts_language_symbol_for_name(language, "anonymous_function", 18, true);
    php_symbols.arrow_function = ts_language_symbol_for_name(language, "arrow_function", 14, true);
    php_symbols.enum_declaration = ts_language_symbol_for_name(language, "enum_declaration", 16, true);
    php_symbols.enum_case = ts_language_symbol_for_name(language, "enum_case", 9, true);
    php_symbols.formal_parameters = ts_language_symbol_for_name(language, "formal_parameters", 17, true);
    php_symbols.property_declaration = ts_language_symbol_for_name(language, "property_declaration", 20, true);
    php_symbols.const_declaration = ts_language_symbol_for_name(language, "const_declaration", 17, true);
    php_symbols.comment = ts_language_symbol_for_name(language, "comment", 7, true);
    php_symbols.string = ts_language_symbol_for_name(language, "string", 6, true);
    php_symbols.encapsed_string = ts_language_symbol_for_name(language, "encapsed_string", 15, true);
    php_symbols.heredoc = ts_language_symbol_for_name(language, "heredoc", 7, true);
    php_symbols.nowdoc = ts_language_symbol_for_name(language, "nowdoc", 6, true);
}

/* Use safe_extract_node_text from shared/string_utils.h instead of local truncating version */

/**
 * Extract parent class/interface/trait name by walking up the AST tree
 * For methods/properties, this returns the containing class/interface/trait name
 * For function parameters, this returns the containing function/method name
 */
static void extract_parent_name(TSNode node, const char *source_code, char *parent_buf, size_t buf_size,
                                 const char *target_node_type, const char *filename) {
    parent_buf[0] = '\0';  /* Default: empty parent */

    TSNode current = ts_node_parent(node);

    while (!ts_node_is_null(current)) {
        const char *node_type = ts_node_type(current);

        /* Found target node type - extract its name */
        if (strcmp(node_type, target_node_type) == 0) {
            uint32_t child_count = ts_node_child_count(current);
            for (uint32_t i = 0; i < child_count; i++) {
                TSNode child = ts_node_child(current, i);
                TSSymbol child_sym = ts_node_symbol(child);
                if (child_sym == php_symbols.name) {
                    safe_extract_node_text(source_code, child, parent_buf, buf_size, filename);
                    return;
                }
            }
            return;
        }

        current = ts_node_parent(current);
    }
}

/**
 * Helper: Extract parent name by trying multiple node types in order
 * Returns true if parent was found, false otherwise
 */
static bool extract_parent_any(TSNode node, const char *source_code, char *parent_buf, size_t buf_size, const char *filename) {
    const char *types[] = {"class_declaration", "interface_declaration", "trait_declaration", "enum_declaration", NULL};
    for (int i = 0; types[i]; i++) {
        extract_parent_name(node, source_code, parent_buf, buf_size, types[i], filename);
        if (parent_buf[0] != '\0') return true;
    }
    return false;
}

/**
 * Extract namespace path by walking up the AST tree
 * In PHP, namespace_definition is a sibling of class_declaration under program,
 * so we need to search siblings when we reach the program node.
 */
static void extract_namespace_path(TSNode node, const char *source_code, char *namespace_buf, size_t buf_size, const char *filename) {
    namespace_buf[0] = '\0';  /* Default: empty namespace */

    TSNode current = ts_node_parent(node);

    while (!ts_node_is_null(current)) {
        const char *node_type = ts_node_type(current);

        /* Found a namespace definition! Extract the namespace name */
        if (strcmp(node_type, "namespace_definition") == 0) {
            /* Find the namespace_name child node */
            uint32_t child_count = ts_node_child_count(current);
            for (uint32_t i = 0; i < child_count; i++) {
                TSNode child = ts_node_child(current, i);
                if (strcmp(ts_node_type(child), "namespace_name") == 0) {
                    safe_extract_node_text(source_code, child, namespace_buf, buf_size, filename);
                    return;
                }
            }
            return;
        }

        /* Hit the program root - check siblings for namespace_definition */
        if (strcmp(node_type, "program") == 0) {
            /* Get the byte position of our original node */
            uint32_t target_start = ts_node_start_byte(node);

            /* Search children of program for namespace_definition that appears before our node */
            uint32_t child_count = ts_node_child_count(current);
            for (uint32_t i = 0; i < child_count; i++) {
                TSNode child = ts_node_child(current, i);
                const char *child_type = ts_node_type(child);

                /* Stop when we reach or pass our target node */
                if (ts_node_start_byte(child) >= target_start) {
                    break;
                }

                /* Found a namespace_definition before our node */
                if (strcmp(child_type, "namespace_definition") == 0) {
                    uint32_t ns_child_count = ts_node_child_count(child);
                    for (uint32_t j = 0; j < ns_child_count; j++) {
                        TSNode ns_child = ts_node_child(child, j);
                        if (strcmp(ts_node_type(ns_child), "namespace_name") == 0) {
                            safe_extract_node_text(source_code, ns_child, namespace_buf, buf_size, filename);
                            return;
                        }
                    }
                }
            }
            return;  /* No namespace found */
        }

        current = ts_node_parent(current);
    }
}

/* Forward declarations */
static void visit_node(TSNode node, const char *source_code, const char *directory,
                      const char *filename, ParseResult *result, SymbolFilter *filter);

/* Node handler function pointer type */
typedef void (*NodeHandler)(TSNode node, const char *source_code,
                           const char *directory, const char *filename,
                           ParseResult *result, SymbolFilter *filter,
                           int line);

/* Dispatch table entry */
typedef struct {
    const char *node_type;
    NodeHandler handler;
} NodeHandlerEntry;

static void process_children(TSNode node, const char *source_code, const char *directory,
                            const char *filename, ParseResult *result, SymbolFilter *filter) {
    TSTreeCursor cursor = ts_tree_cursor_new(node);

    if (ts_tree_cursor_goto_first_child(&cursor)) {
        do {
            TSNode child = ts_tree_cursor_current_node(&cursor);
            visit_node(child, source_code, directory, filename, result, filter);
        } while (ts_tree_cursor_goto_next_sibling(&cursor));
    }

    ts_tree_cursor_delete(&cursor);
}

/**
 * Recursively extract the final symbol name from a scoped expression
 * For scoped_call_expression: extracts the method name (e.g., "ho" from "$h::hey()::ho()")
 * For scoped_property_access_expression: extracts the property name
 * For other nodes: extracts the full node text
 */
static void extract_scoped_symbol_name(TSNode node, const char *source_code, char *symbol, size_t size, const char *filename) {
    TSSymbol node_sym = ts_node_symbol(node);

    /* For nested scoped expressions, recursively extract the rightmost symbol */
    if (node_sym == php_symbols.scoped_call_expression ||
        node_sym == php_symbols.scoped_property_access_expression) {

        uint32_t child_count = ts_node_child_count(node);
        bool found_scope_op = false;

        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            TSSymbol child_sym = ts_node_symbol(child);

            if (strcmp(ts_node_type(child), "::") == 0) {
                found_scope_op = true;
            }
            /* After ::, find the symbol name */
            else if (found_scope_op) {
                if (child_sym == php_symbols.name) {
                    /* Found the method/constant name */
                    safe_extract_node_text(source_code, child, symbol, size, filename);
                    return;
                } else if (child_sym == php_symbols.variable_name) {
                    /* Found the property name */
                    safe_extract_node_text(source_code, child, symbol, size, filename);
                    return;
                } else if (child_sym == php_symbols.scoped_call_expression ||
                          child_sym == php_symbols.scoped_property_access_expression) {
                    /* Nested scoped expression - recurse */
                    extract_scoped_symbol_name(child, source_code, symbol, size, filename);
                    return;
                }
            }
        }
    }

    /* For non-scoped nodes, extract full text */
    safe_extract_node_text(source_code, node, symbol, size, filename);
}

/**
 * Extract the atomic symbol name from an expression node
 * Used for constant access on expressions: getClass()::CONSTANT
 * Returns just the symbol (e.g., "getClass", "getInstance", "factory")
 * without parentheses or full expression text
 */
static void extract_expression_symbol(TSNode expr_node, const char *source_code, char *symbol, size_t size, const char *filename) {
    TSSymbol node_sym = ts_node_symbol(expr_node);

    /* For scoped call expressions, use existing recursive extractor */
    if (node_sym == php_symbols.scoped_call_expression) {
        extract_scoped_symbol_name(expr_node, source_code, symbol, size, filename);
        return;
    }

    /* For function_call_expression: extract function name */
    if (node_sym == php_symbols.function_call_expression) {
        uint32_t child_count = ts_node_child_count(expr_node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(expr_node, i);
            if (ts_node_symbol(child) == php_symbols.name) {
                safe_extract_node_text(source_code, child, symbol, size, filename);
                return;
            }
        }
    }

    /* For member_call_expression: extract method name (after ->) */
    if (node_sym == php_symbols.member_call_expression) {
        uint32_t child_count = ts_node_child_count(expr_node);
        bool found_arrow = false;
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(expr_node, i);
            const char *child_type = ts_node_type(child);
            TSSymbol child_sym = ts_node_symbol(child);

            if (strcmp(child_type, "->") == 0) {
                found_arrow = true;
            } else if (found_arrow && child_sym == php_symbols.name) {
                safe_extract_node_text(source_code, child, symbol, size, filename);
                return;
            }
        }
    }

    /* For member_access_expression: extract property name (after ->) */
    if (node_sym == php_symbols.member_access_expression) {
        uint32_t child_count = ts_node_child_count(expr_node);
        bool found_arrow = false;
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(expr_node, i);
            const char *child_type = ts_node_type(child);
            TSSymbol child_sym = ts_node_symbol(child);

            if (strcmp(child_type, "->") == 0) {
                found_arrow = true;
            } else if (found_arrow && child_sym == php_symbols.name) {
                safe_extract_node_text(source_code, child, symbol, size, filename);
                return;
            }
        }
    }

    /* For parenthesized_expression: recursively extract inner expression */
    if (node_sym == php_symbols.parenthesized_expression) {
        uint32_t child_count = ts_node_child_count(expr_node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(expr_node, i);
            const char *child_type = ts_node_type(child);
            TSSymbol child_sym = ts_node_symbol(child);

            /* Skip parentheses, process inner expression */
            if (!chrcmp1(child_type, '(') && !chrcmp1(child_type, ')')) {
                /* For variable_name, extract with $ prefix */
                if (child_sym == php_symbols.variable_name) {
                    safe_extract_node_text(source_code, child, symbol, size, filename);
                    return;
                }
                /* For other expressions, recurse */
                extract_expression_symbol(child, source_code, symbol, size, filename);
                return;
            }
        }
    }

    /* For variable_name: extract full text (includes $) */
    if (node_sym == php_symbols.variable_name) {
        safe_extract_node_text(source_code, expr_node, symbol, size, filename);
        return;
    }

    /* Fallback: extract full node text */
    safe_extract_node_text(source_code, expr_node, symbol, size, filename);
}

/**
 * Recursively extract and index symbols from expressions (used in subscripts, function arguments, etc.)
 * Handles: variable_name, binary_expression, function_call_expression, subscript_expression
 */
static void extract_symbols_from_expression(TSNode expr_node, const char *source_code, const char *directory,
                                            const char *filename, ParseResult *result, SymbolFilter *filter,
                                            const char *namespace_buf) {
    const char *node_type = ts_node_type(expr_node);
    TSPoint start_point = ts_node_start_point(expr_node);
    int line = (int)(start_point.row + 1);

    /* Handle variable_name nodes - extract and index */
    if (strcmp(node_type, "variable_name") == 0) {
        char var_name[SYMBOL_MAX_LENGTH];
        safe_extract_node_text(source_code, expr_node, var_name, sizeof(var_name), filename);

        if (filter_should_index(filter, var_name)) {
            add_entry(result, var_name, line, CONTEXT_VARIABLE,
                    directory, filename, NULL, &(ExtColumns){.namespace = namespace_buf});
        }
        return;
    }

    /* Handle string literals in subscript expressions - index as property-like access */
    if (strcmp(node_type, "string") == 0 || strcmp(node_type, "encapsed_string") == 0) {
        uint32_t child_count = ts_node_child_count(expr_node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(expr_node, i);
            TSSymbol child_sym = ts_node_symbol(child);
            if (child_sym == php_symbols.string_value || child_sym == php_symbols.string_content) {
                char string_text[SYMBOL_MAX_LENGTH];
                safe_extract_node_text(source_code, child, string_text, sizeof(string_text), filename);
                process_string_content(string_text, line, directory, filename, result, filter, namespace_buf, "");
            }
        }
        return;
    }

    /* Handle function_call_expression - extract function name and recurse on arguments */
    if (strcmp(node_type, "function_call_expression") == 0) {
        char func_name[SYMBOL_MAX_LENGTH] = "";
        uint32_t child_count = ts_node_child_count(expr_node);

        /* First pass: extract function name */
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(expr_node, i);
            TSSymbol child_sym = ts_node_symbol(child);

            if (child_sym == php_symbols.name || child_sym == php_symbols.variable_name) {
                safe_extract_node_text(source_code, child, func_name, sizeof(func_name), filename);

                TSPoint func_start = ts_node_start_point(child);
                int func_line = (int)(func_start.row + 1);

                if (filter_should_index(filter, func_name)) {
                    add_entry(result, func_name, func_line, CONTEXT_CALL,
                            directory, filename, NULL, &(ExtColumns){.namespace = namespace_buf});
                }
                break;
            }
        }

        /* Second pass: process arguments with function name as clue */
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(expr_node, i);
            TSSymbol child_sym = ts_node_symbol(child);

            if (child_sym == php_symbols.arguments) {
                uint32_t arg_count = ts_node_child_count(child);
                for (uint32_t j = 0; j < arg_count; j++) {
                    TSNode arg = ts_node_child(child, j);
                    TSSymbol arg_sym = ts_node_symbol(arg);

                    /* PHP wraps arguments in 'argument' nodes */
                    if (arg_sym == php_symbols.argument) {
                        uint32_t arg_child_count = ts_node_child_count(arg);
                        for (uint32_t k = 0; k < arg_child_count; k++) {
                            TSNode arg_child = ts_node_child(arg, k);
                            TSSymbol arg_child_sym = ts_node_symbol(arg_child);

                            /* Index variable_name arguments with function name as clue */
                            if (arg_child_sym == php_symbols.variable_name && func_name[0] != '\0') {
                                char arg_symbol[SYMBOL_MAX_LENGTH];
                                safe_extract_node_text(source_code, arg_child, arg_symbol, sizeof(arg_symbol), filename);

                                TSPoint arg_start = ts_node_start_point(arg_child);
                                int arg_line = (int)(arg_start.row + 1);

                                /* Design decision: Don't filter keywords when used as actual identifiers */
                                // if (filter_should_index(filter, arg_symbol)) {
                                    add_entry(result, arg_symbol, arg_line, CONTEXT_ARGUMENT,
                                            directory, filename, NULL, &(ExtColumns){.clue = func_name, .namespace = namespace_buf});
                                // }
                            }
                        }

                        /* Recurse on all arguments for nested expressions */
                        extract_symbols_from_expression(arg, source_code, directory, filename, result, filter, namespace_buf);
                    }
                }
                break;
            }
        }
        return;
    }

    /* Handle binary_expression - recurse on left and right operands */
    if (strcmp(node_type, "binary_expression") == 0) {
        uint32_t child_count = ts_node_child_count(expr_node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(expr_node, i);
            const char *child_type = ts_node_type(child);

            /* Skip operators, recurse on operands */
            if (!chrcmp1(child_type, '+') && !chrcmp1(child_type, '-') &&
                !chrcmp1(child_type, '*') && !chrcmp1(child_type, '/') &&
                !chrcmp1(child_type, '.') && !chrcmp1(child_type, '%')) {
                extract_symbols_from_expression(child, source_code, directory, filename, result, filter, namespace_buf);
            }
        }
        return;
    }

    /* Handle nested subscript_expression - recurse into it */
    if (strcmp(node_type, "subscript_expression") == 0) {
        uint32_t child_count = ts_node_child_count(expr_node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(expr_node, i);
            const char *child_type = ts_node_type(child);

            /* Skip brackets, recurse on everything else */
            if (!chrcmp1(child_type, '[') && !chrcmp1(child_type, ']')) {
                extract_symbols_from_expression(child, source_code, directory, filename, result, filter, namespace_buf);
            }
        }
        return;
    }

    /* For other node types, recurse on children */
    uint32_t child_count = ts_node_child_count(expr_node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(expr_node, i);
        extract_symbols_from_expression(child, source_code, directory, filename, result, filter, namespace_buf);
    }
}

/**
 * Helper: Extract namespace and store in provided buffer
 * Returns the namespace buffer pointer for convenience
 */
static char* get_namespace(TSNode node, const char *source_code, char *buf, size_t size, const char *filename) {
    extract_namespace_path(node, source_code, buf, size, filename);
    return buf;
}

/**
 * Helper: Handle simple declarations (interface, function, enum, trait)
 * These all follow the pattern: extract namespace, find "name" child, index with context type
 */
static void handle_simple_declaration(TSNode node, const char *source_code, const char *directory,
                                       const char *filename, ParseResult *result, SymbolFilter *filter,
                                       int line, ContextType context) {
    char namespace_buf[SYMBOL_MAX_LENGTH];
    char location[128];
    get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (strcmp(ts_node_type(child), "name") == 0) {
            char symbol[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, symbol, sizeof(symbol), filename);
            if (filter_should_index(filter, symbol)) {
                /* Extract source location for full declaration */
                format_source_location(node, location, sizeof(location));

                add_entry(result, symbol, line, context,
                        directory, filename, location, &(ExtColumns){.namespace = namespace_buf, .definition = "1"});
            }
            break;
        }
    }

    /* Process declaration body (methods, properties, etc.) */
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) {
        process_children(body, source_code, directory, filename, result, filter);
    }
}

/**
 * Extract type information from union_type or intersection_type node
 * PHP uses union_type for all type annotations (single types, nullable, unions)
 * Examples: int, ?string, int|float, string|array|null
 * PHP 8.1+ uses intersection_type for intersection types
 * Examples: Countable&Traversable
 */
static void extract_type_from_union(TSNode parent_node, const char *source_code, char *type_buffer, size_t type_size, const char *filename) {
    type_buffer[0] = '\0';

    uint32_t child_count = ts_node_child_count(parent_node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(parent_node, i);
        const char *child_type = ts_node_type(child);

        /* Found union_type or intersection_type node - extract full text */
        if (strcmp(child_type, "union_type") == 0 || strcmp(child_type, "intersection_type") == 0) {
            safe_extract_node_text(source_code, child, type_buffer, type_size, filename);
            return;
        }
    }
}

/**
 * Extract visibility modifier from PHP node
 * Returns "public", "private", "protected", or empty string
 */
static void extract_visibility(TSNode node, const char *source_code, char *visibility, size_t size, const char *filename) {
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "visibility_modifier") == 0) {
            safe_extract_node_text(source_code, child, visibility, size, filename);
            return;
        }
    }
    visibility[0] = '\0';
}

/**
 * Extract property name from property_element node
 * PHP properties have structure: property_element -> variable_name (includes $)
 */
static void extract_property_name(TSNode property_element, const char *source_code, char *name, size_t size, const char *filename) {
    uint32_t child_count = ts_node_child_count(property_element);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(property_element, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "variable_name") == 0) {
            /* Extract full variable_name node (including $) */
            safe_extract_node_text(source_code, child, name, size, filename);
            return;
        }
    }
    name[0] = '\0';
}

static void handle_class_declaration(TSNode node, const char *source_code, const char *directory,
                                     const char *filename, ParseResult *result, SymbolFilter *filter,
                                     int line) {
    char namespace_buf[SYMBOL_MAX_LENGTH];
    get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

    /* Check for modifiers: abstract, final, readonly */
    char modifier[64] = "";
    bool has_abstract = false;
    bool has_final = false;
    bool has_readonly = false;

    /* Find class name, check for modifiers, and index implemented interfaces */
    uint32_t child_count = ts_node_child_count(node);
    bool found_class_name = false;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "abstract_modifier") == 0) {
            has_abstract = true;
        } else if (strcmp(child_type, "final_modifier") == 0) {
            has_final = true;
        } else if (strcmp(child_type, "readonly_modifier") == 0) {
            has_readonly = true;
        } else if (strcmp(child_type, "name") == 0 && !found_class_name) {
            char symbol[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, symbol, sizeof(symbol), filename);

            /* Build modifier string */
            if (has_abstract) {
                snprintf(modifier, sizeof(modifier), "abstract");
            } else if (has_final) {
                snprintf(modifier, sizeof(modifier), "final");
            } else if (has_readonly) {
                snprintf(modifier, sizeof(modifier), "readonly");
            }

            if (filter_should_index(filter, symbol)) {
                add_entry(result, symbol, line, CONTEXT_CLASS,
                        directory, filename, NULL, &(ExtColumns){.namespace = namespace_buf, .modifier = modifier, .definition = "1"});
            }
            found_class_name = true;
        } else if (strcmp(child_type, "class_interface_clause") == 0) {
            /* Index interfaces from implements clause */
            uint32_t interface_child_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < interface_child_count; j++) {
                TSNode interface_child = ts_node_child(child, j);
                const char *interface_child_type = ts_node_type(interface_child);

                if (strcmp(interface_child_type, "name") == 0) {
                    char interface_name[SYMBOL_MAX_LENGTH];
                    safe_extract_node_text(source_code, interface_child, interface_name, sizeof(interface_name), filename);

                    TSPoint interface_start = ts_node_start_point(interface_child);
                    int interface_line = (int)(interface_start.row + 1);

                    if (filter_should_index(filter, interface_name)) {
                        add_entry(result, interface_name, interface_line, CONTEXT_TYPE,
                                directory, filename, NULL, &(ExtColumns){.namespace = namespace_buf, .clue = "implements"});
                    }
                }
            }
        }
    }

    /* Process class body (methods, properties, constants, etc.) */
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) {
        process_children(body, source_code, directory, filename, result, filter);
    }
}

static void handle_interface_declaration(TSNode node, const char *source_code, const char *directory,
                                         const char *filename, ParseResult *result, SymbolFilter *filter,
                                         int line) {
    handle_simple_declaration(node, source_code, directory, filename, result, filter, line, CONTEXT_INTERFACE);
}

static void handle_formal_parameters(TSNode node, const char *source_code, const char *directory,
                                      const char *filename, ParseResult *result, SymbolFilter *filter,
                                      int line) {
    (void)line;
    char namespace_buf[SYMBOL_MAX_LENGTH];
    get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

    /* Determine if this is a function/method definition by checking parent node type */
    int is_definition = 0;
    TSNode parent = ts_node_parent(node);
    if (!ts_node_is_null(parent)) {
        const char *parent_type = ts_node_type(parent);
        if (strcmp(parent_type, "function_definition") == 0 ||
            strcmp(parent_type, "method_declaration") == 0) {
            is_definition = 1;
        }
    }

    /* Extract parent class/interface/trait name for promoted properties */
    char parent_name[SYMBOL_MAX_LENGTH];
    extract_parent_any(node, source_code, parent_name, sizeof(parent_name), filename);

    /* Iterate through child nodes looking for simple_parameter and property_promotion_parameter */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "simple_parameter") == 0) {
            /* Extract type from union_type node */
            char param_type[SYMBOL_MAX_LENGTH];
            extract_type_from_union(child, source_code, param_type, sizeof(param_type), filename);

            /* Find variable_name within the parameter */
            uint32_t param_child_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < param_child_count; j++) {
                TSNode param_child = ts_node_child(child, j);
                const char *param_child_type = ts_node_type(param_child);

                if (strcmp(param_child_type, "variable_name") == 0) {
                    /* Extract full variable_name node (including $) */
                    char param_name[SYMBOL_MAX_LENGTH];
                    safe_extract_node_text(source_code, param_child, param_name, sizeof(param_name), filename);

                    TSPoint param_start = ts_node_start_point(param_child);
                    int param_line = (int)(param_start.row + 1);

                    if (filter_should_index(filter, param_name)) {
                        add_entry(result, param_name, param_line, CONTEXT_ARGUMENT,
                                directory, filename, NULL, &(ExtColumns){.namespace = namespace_buf, .type = param_type, .definition = is_definition ? "1" : "0"});
                    }
                    break;
                }
            }
        }
        /* Handle property promotion parameters (PHP 8.0+) */
        else if (strcmp(child_type, "property_promotion_parameter") == 0) {
            char visibility[32] = "";
            char modifier[64] = "";
            char property_name[SYMBOL_MAX_LENGTH] = "";
            char param_type[SYMBOL_MAX_LENGTH];
            bool has_readonly = false;

            /* Extract type from union_type node */
            extract_type_from_union(child, source_code, param_type, sizeof(param_type), filename);

            /* Extract visibility and readonly modifier */
            uint32_t param_child_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < param_child_count; j++) {
                TSNode param_child = ts_node_child(child, j);
                const char *param_child_type = ts_node_type(param_child);

                if (strcmp(param_child_type, "visibility_modifier") == 0) {
                    safe_extract_node_text(source_code, param_child, visibility, sizeof(visibility), filename);
                } else if (strcmp(param_child_type, "readonly_modifier") == 0) {
                    has_readonly = true;
                } else if (strcmp(param_child_type, "variable_name") == 0) {
                    safe_extract_node_text(source_code, param_child, property_name, sizeof(property_name), filename);
                }
            }

            /* Build modifier string */
            if (has_readonly) {
                snprintf(modifier, sizeof(modifier), "readonly");
            }

            if (property_name[0]) {
                TSPoint property_start = ts_node_start_point(child);
                int property_line = (int)(property_start.row + 1);

                if (filter_should_index(filter, property_name)) {
                    /* Promoted properties are indexed as PROPERTY_NAME with parent class */
                    add_entry(result, property_name, property_line, CONTEXT_PROPERTY,
                            directory, filename, NULL, &(ExtColumns){.parent = parent_name, .scope = visibility, .namespace = namespace_buf, .modifier = modifier, .type = param_type});
                }
            }
        }
    }
}

static void handle_method_declaration(TSNode node, const char *source_code, const char *directory,
                                      const char *filename, ParseResult *result, SymbolFilter *filter,
                                      int line) {
    char visibility[32];
    char modifier[64];
    char method_name[SYMBOL_MAX_LENGTH];
    char parent_name[SYMBOL_MAX_LENGTH];
    char type_str[SYMBOL_MAX_LENGTH];
    bool has_name = false;
    bool is_static = false;
    bool is_abstract = false;
    bool is_final = false;

    char namespace_buf[SYMBOL_MAX_LENGTH];
    get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

    /* Extract parent class/interface/enum/trait name */
    extract_parent_any(node, source_code, parent_name, sizeof(parent_name), filename);

    /* Extract visibility modifier */
    extract_visibility(node, source_code, visibility, sizeof(visibility), filename);

    /* Extract return type from union_type node (appears after : token) */
    extract_type_from_union(node, source_code, type_str, sizeof(type_str), filename);

    /* Check for modifiers and find method name */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "static_modifier") == 0) {
            is_static = true;
        } else if (strcmp(child_type, "abstract_modifier") == 0) {
            is_abstract = true;
        } else if (strcmp(child_type, "final_modifier") == 0) {
            is_final = true;
        } else if (strcmp(child_type, "name") == 0) {
            safe_extract_node_text(source_code, child, method_name, sizeof(method_name), filename);
            has_name = true;
        }
    }

    /* Build modifier string (prioritize abstract/final over static) */
    if (is_abstract) {
        snprintf(modifier, sizeof(modifier), "abstract");
    } else if (is_final) {
        snprintf(modifier, sizeof(modifier), "final");
    } else if (is_static) {
        snprintf(modifier, sizeof(modifier), "static");
    } else {
        modifier[0] = '\0';
    }

    if (has_name && filter_should_index(filter, method_name)) {
        /* Extract source location for full method definition */
        char location[128];
        format_source_location(node, location, sizeof(location));

        add_entry(result, method_name, line, CONTEXT_FUNCTION,
                directory, filename, location, &(ExtColumns){.parent = parent_name, .scope = visibility, .namespace = namespace_buf, .modifier = modifier, .type = type_str, .definition = "1"});
    }

    /* Process method body to index local variables, strings, calls, etc. */
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "compound_statement") == 0) {
            process_children(child, source_code, directory, filename, result, filter);
            break;
        }
    }
}

static void handle_function_definition(TSNode node, const char *source_code, const char *directory,
                                       const char *filename, ParseResult *result, SymbolFilter *filter,
                                       int line) {
    char namespace_buf[SYMBOL_MAX_LENGTH];
    char type_str[SYMBOL_MAX_LENGTH];
    get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

    /* Extract return type from union_type node (appears after : token) */
    extract_type_from_union(node, source_code, type_str, sizeof(type_str), filename);

    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (strcmp(ts_node_type(child), "name") == 0) {
            char symbol[SYMBOL_MAX_LENGTH];
            char location[128];
            safe_extract_node_text(source_code, child, symbol, sizeof(symbol), filename);
            if (filter_should_index(filter, symbol)) {
                /* Extract source location for full function definition */
                format_source_location(node, location, sizeof(location));

                add_entry(result, symbol, line, CONTEXT_FUNCTION,
                        directory, filename, location, &(ExtColumns){.namespace = namespace_buf, .type = type_str, .definition = "1"});
            }
            break;
        }
    }

    /* Process function body to index local variables, strings, calls, etc. */
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "compound_statement") == 0) {
            process_children(child, source_code, directory, filename, result, filter);
            break;
        }
    }
}

static void handle_enum_declaration(TSNode node, const char *source_code, const char *directory,
                                    const char *filename, ParseResult *result, SymbolFilter *filter,
                                    int line) {
    handle_simple_declaration(node, source_code, directory, filename, result, filter, line, CONTEXT_ENUM);
}

static void handle_enum_case(TSNode node, const char *source_code, const char *directory,
                             const char *filename, ParseResult *result, SymbolFilter *filter,
                             int line) {
    char case_name[SYMBOL_MAX_LENGTH];
    char parent_enum[SYMBOL_MAX_LENGTH];
    bool has_name = false;

    char namespace_buf[SYMBOL_MAX_LENGTH];
    get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

    /* Extract parent enum name */
    extract_parent_name(node, source_code, parent_enum, sizeof(parent_enum), "enum_declaration", filename);

    /* Find case name */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "name") == 0) {
            safe_extract_node_text(source_code, child, case_name, sizeof(case_name), filename);
            has_name = true;
            break;
        }
    }

    if (has_name && filter_should_index(filter, case_name)) {
        /* Enum cases belong to parent enum, no visibility modifier */
        add_entry(result, case_name, line, CONTEXT_ENUM_CASE,
                directory, filename, NULL, &(ExtColumns){.parent = parent_enum, .namespace = namespace_buf, .definition = "1"});
    }
}

static void handle_trait_declaration(TSNode node, const char *source_code, const char *directory,
                                     const char *filename, ParseResult *result, SymbolFilter *filter,
                                     int line) {
    handle_simple_declaration(node, source_code, directory, filename, result, filter, line, CONTEXT_TRAIT);
}

static void handle_property_declaration(TSNode node, const char *source_code, const char *directory,
                                        const char *filename, ParseResult *result, SymbolFilter *filter,
                                        int line) {
    char visibility[32];
    char modifier[64];
    char parent_name[SYMBOL_MAX_LENGTH];
    char type_str[SYMBOL_MAX_LENGTH];
    char location[128];
    bool is_static = false;

    char namespace_buf[SYMBOL_MAX_LENGTH];
    get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

    /* Extract parent class/trait name */
    extract_parent_any(node, source_code, parent_name, sizeof(parent_name), filename);

    /* Extract visibility modifier */
    extract_visibility(node, source_code, visibility, sizeof(visibility), filename);

    /* Extract type from union_type node */
    extract_type_from_union(node, source_code, type_str, sizeof(type_str), filename);

    /* Check for static modifier */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "static_modifier") == 0) {
            is_static = true;
            break;
        }
    }

    /* Build modifier string */
    if (is_static) {
        snprintf(modifier, sizeof(modifier), "static");
    } else {
        modifier[0] = '\0';
    }

    /* Find property_element nodes */
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "property_element") == 0) {
            char property_name[SYMBOL_MAX_LENGTH];
            extract_property_name(child, source_code, property_name, sizeof(property_name), filename);

            if (property_name[0] && filter_should_index(filter, property_name)) {
                /* Extract source location for full property declaration */
                format_source_location(node, location, sizeof(location));

                add_entry(result, property_name, line, CONTEXT_PROPERTY,
                        directory, filename, location, &(ExtColumns){.parent = parent_name, .scope = visibility, .namespace = namespace_buf, .modifier = modifier, .type = type_str, .definition = "1"});
            }
        }
    }
}

static void handle_const_declaration(TSNode node, const char *source_code, const char *directory,
                                      const char *filename, ParseResult *result, SymbolFilter *filter,
                                      int line) {
    char visibility[32] = "";
    char parent_name[SYMBOL_MAX_LENGTH] = "";
    char location[128];

    char namespace_buf[SYMBOL_MAX_LENGTH];
    get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

    /* Extract parent class/interface/trait/enum name */
    extract_parent_any(node, source_code, parent_name, sizeof(parent_name), filename);

    /* Extract visibility modifier */
    extract_visibility(node, source_code, visibility, sizeof(visibility), filename);

    /* Find const_element nodes */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "const_element") == 0) {
            /* Find the name node within const_element */
            uint32_t element_child_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < element_child_count; j++) {
                TSNode element_child = ts_node_child(child, j);
                const char *element_child_type = ts_node_type(element_child);

                if (strcmp(element_child_type, "name") == 0) {
                    char const_name[SYMBOL_MAX_LENGTH];
                    safe_extract_node_text(source_code, element_child, const_name, sizeof(const_name), filename);

                    if (const_name[0] && filter_should_index(filter, const_name)) {
                        /* Extract source location for full const declaration */
                        format_source_location(node, location, sizeof(location));

                        add_entry(result, const_name, line, CONTEXT_VARIABLE,
                                directory, filename, location, &(ExtColumns){.parent = parent_name, .scope = visibility, .namespace = namespace_buf, .modifier = "const", .definition = "1"});
                    }
                    break;
                }
            }
        }
    }
}

static void handle_class_constant_access_expression(TSNode node, const char *source_code, const char *directory,
                                                      const char *filename, ParseResult *result, SymbolFilter *filter,
                                                      int line) {
    (void)line;
    char namespace_buf[SYMBOL_MAX_LENGTH];
    get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

    char scope[SYMBOL_MAX_LENGTH] = "";
    char constant_name[SYMBOL_MAX_LENGTH] = "";
    bool found_scope = false;

    /* Find the scope and constant name */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        /* Extract scope from relative_scope (self/static/parent) */
        if (strcmp(child_type, "relative_scope") == 0) {
            safe_extract_node_text(source_code, child, scope, sizeof(scope), filename);
            found_scope = true;
        }

        /* Extract scope from expression types (function calls, method calls, property access, etc.) */
        if (!found_scope && (strcmp(child_type, "function_call_expression") == 0 ||
                             strcmp(child_type, "member_call_expression") == 0 ||
                             strcmp(child_type, "scoped_call_expression") == 0 ||
                             strcmp(child_type, "member_access_expression") == 0 ||
                             strcmp(child_type, "parenthesized_expression") == 0)) {
            extract_expression_symbol(child, source_code, scope, sizeof(scope), filename);
            found_scope = true;
        }

        /* First 'name' node is the class name (scope) */
        if (strcmp(child_type, "name") == 0 && !found_scope) {
            safe_extract_node_text(source_code, child, scope, sizeof(scope), filename);
            found_scope = true;
        }
        /* Second 'name' node is the constant name */
        else if (strcmp(child_type, "name") == 0 && found_scope && constant_name[0] == '\0') {
            safe_extract_node_text(source_code, child, constant_name, sizeof(constant_name), filename);

            TSPoint constant_start = ts_node_start_point(child);
            int constant_line = (int)(constant_start.row + 1);

            if (filter_should_index(filter, constant_name)) {
                add_entry(result, constant_name, constant_line, CONTEXT_VARIABLE,
                        directory, filename, NULL, &(ExtColumns){.parent = scope, .namespace = namespace_buf, .modifier = "const"});
            }
        }
    }
}

/**
 * Helper: Extract and index words from string content
 */
static void process_string_content(const char *string_text, int line, const char *directory,
                                   const char *filename, ParseResult *result, SymbolFilter *filter,
                                   const char *namespace_buf, const char *context_clue) {
    char word[CLEANED_WORD_BUFFER];
    char cleaned[CLEANED_WORD_BUFFER];
    const char *word_start = string_text;

    for (const char *p = string_text; ; p++) {
        if (*p == '\0' || isspace(*p)) {
            if (p > word_start) {
                size_t word_len = (size_t)(p - word_start);
                if (word_len < sizeof(word)) {
                    snprintf(word, sizeof(word), "%.*s", (int)word_len, word_start);

                    /* Clean the word (preserve path characters for strings) */
                    filter_clean_string_symbol(word, cleaned, sizeof(cleaned));

                    if (cleaned[0] && filter_should_index(filter, cleaned)) {
                        add_entry(result, cleaned, line, CONTEXT_STRING, directory, filename, NULL, &(ExtColumns){.clue = context_clue, .namespace = namespace_buf});
                    }
                }
            }
            word_start = p + 1;
            if (*p == '\0') break;
        }
    }
}

static void handle_comment(TSNode node, const char *source_code, const char *directory,
                           const char *filename, ParseResult *result, SymbolFilter *filter,
                           int line) {
    char comment_text[COMMENT_TEXT_BUFFER];
    safe_extract_node_text(source_code, node, comment_text, sizeof(comment_text), filename);

    char namespace_buf[SYMBOL_MAX_LENGTH];
    get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

    /* Strip comment delimiters before processing words */
    char *text_start = strip_comment_delimiters(comment_text);

    /* Extract words from comment - split on whitespace */
    char word[CLEANED_WORD_BUFFER];
    char cleaned[CLEANED_WORD_BUFFER];
    char *word_start = text_start;

    for (char *p = text_start; ; p++) {
        if (*p == '\0' || isspace(*p)) {
            if (p > word_start) {
                size_t word_len = (size_t)(p - word_start);
                if (word_len < sizeof(word)) {
                    snprintf(word, sizeof(word), "%.*s", (int)word_len, word_start);

                    /* Clean the word */
                    filter_clean_string_symbol(word, cleaned, sizeof(cleaned));

                    if (cleaned[0] && filter_should_index(filter, cleaned)) {
                        add_entry(result, cleaned, line, CONTEXT_COMMENT, directory, filename, NULL, &(ExtColumns){.namespace = namespace_buf});
                    }
                }
            }
            word_start = p + 1;
            if (*p == '\0') break;
        }
    }
}

/**
 * Handle strings: string → string_value, encapsed_string → string_value
 * Both single-quoted and double-quoted strings use the same structure
 */
static void handle_string(TSNode node, const char *source_code, const char *directory,
                         const char *filename, ParseResult *result, SymbolFilter *filter,
                         int line) {
    char namespace_buf[SYMBOL_MAX_LENGTH];
    get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (strcmp(ts_node_type(child), "string_value") == 0) {
            char string_text[COMMENT_TEXT_BUFFER];
            safe_extract_node_text(source_code, child, string_text, sizeof(string_text), filename);
            process_string_content(string_text, line, directory, filename, result, filter, namespace_buf, "");
        }
    }
}

/**
 * Handle heredoc: heredoc → heredoc_body → string_value (multiple)
 * Context clue: "heredoc"
 */
static void handle_heredoc(TSNode node, const char *source_code, const char *directory,
                          const char *filename, ParseResult *result, SymbolFilter *filter,
                          int line) {
    char namespace_buf[SYMBOL_MAX_LENGTH];
    get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

    /* Find heredoc_body */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (strcmp(ts_node_type(child), "heredoc_body") == 0) {
            /* Extract all string_value children */
            uint32_t body_child_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < body_child_count; j++) {
                TSNode body_child = ts_node_child(child, j);
                if (strcmp(ts_node_type(body_child), "string_value") == 0) {
                    char string_text[COMMENT_TEXT_BUFFER];
                    safe_extract_node_text(source_code, body_child, string_text, sizeof(string_text), filename);
                    process_string_content(string_text, line, directory, filename, result, filter, namespace_buf, "heredoc");
                }
            }
            break;
        }
    }
}

/**
 * Handle nowdoc: nowdoc → nowdoc_body → nowdoc_string (multiple)
 * Note: nowdoc uses nowdoc_string, not string_value
 * Context clue: "nowdoc"
 */
static void handle_nowdoc(TSNode node, const char *source_code, const char *directory,
                         const char *filename, ParseResult *result, SymbolFilter *filter,
                         int line) {
    char namespace_buf[SYMBOL_MAX_LENGTH];
    get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

    /* Find nowdoc_body */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (strcmp(ts_node_type(child), "nowdoc_body") == 0) {
            /* Extract all nowdoc_string children */
            uint32_t body_child_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < body_child_count; j++) {
                TSNode body_child = ts_node_child(child, j);
                if (strcmp(ts_node_type(body_child), "nowdoc_string") == 0) {
                    char string_text[COMMENT_TEXT_BUFFER];
                    safe_extract_node_text(source_code, body_child, string_text, sizeof(string_text), filename);
                    process_string_content(string_text, line, directory, filename, result, filter, namespace_buf, "nowdoc");
                }
            }
            break;
        }
    }
}

static void handle_namespace_definition(TSNode node, const char *source_code, const char *directory,
                                        const char *filename, ParseResult *result, SymbolFilter *filter,
                                        int line) {
    /* Find namespace_name node */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "namespace_name") == 0) {
            char namespace_name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, namespace_name, sizeof(namespace_name), filename);

            if (filter_should_index(filter, namespace_name)) {
                add_entry(result, namespace_name, line, CONTEXT_NAMESPACE,
                        directory, filename, NULL, &(ExtColumns){.definition = "1"});
            }
            break;
        }
    }
}

static void handle_assignment_expression(TSNode node, const char *source_code, const char *directory,
                                          const char *filename, ParseResult *result, SymbolFilter *filter,
                                          int line) {
    (void)line;
    char namespace_buf[SYMBOL_MAX_LENGTH];
    get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

    /* Find the left-hand side variable_name */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "variable_name") == 0) {
            /* Extract full variable_name node (including $) */
            char var_name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, var_name, sizeof(var_name), filename);

            TSPoint var_start = ts_node_start_point(child);
            int var_line = (int)(var_start.row + 1);

            if (filter_should_index(filter, var_name)) {
                add_entry(result, var_name, var_line, CONTEXT_VARIABLE,
                        directory, filename, NULL, &(ExtColumns){.namespace = namespace_buf});
            }
            break;  /* Only index the LHS variable */
        }
    }

    /* Process children to index RHS (e.g., anonymous functions, expressions) */
    process_children(node, source_code, directory, filename, result, filter);
}

static void handle_member_access_expression(TSNode node, const char *source_code, const char *directory,
                                             const char *filename, ParseResult *result, SymbolFilter *filter,
                                             int line) {
    (void)line;
    char namespace_buf[SYMBOL_MAX_LENGTH];
    get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

    char parent_obj[SYMBOL_MAX_LENGTH] = "";
    char property_name[SYMBOL_MAX_LENGTH] = "";

    /* Find the object and property name */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        /* Extract parent object from variable_name (including $) */
        if (strcmp(child_type, "variable_name") == 0) {
            safe_extract_node_text(source_code, child, parent_obj, sizeof(parent_obj), filename);
        }

        /* Extract property name */
        if (strcmp(child_type, "name") == 0 && property_name[0] == '\0') {
            safe_extract_node_text(source_code, child, property_name, sizeof(property_name), filename);

            TSPoint property_start = ts_node_start_point(child);
            int property_line = (int)(property_start.row + 1);

            if (filter_should_index(filter, property_name)) {
                add_entry(result, property_name, property_line, CONTEXT_PROPERTY,
                        directory, filename, NULL, &(ExtColumns){.parent = parent_obj, .namespace = namespace_buf});
            }
        }
    }
}

static void handle_scoped_property_access_expression(TSNode node, const char *source_code, const char *directory,
                                                       const char *filename, ParseResult *result, SymbolFilter *filter,
                                                       int line) {
    (void)line;
    char namespace_buf[SYMBOL_MAX_LENGTH];
    get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

    char scope[SYMBOL_MAX_LENGTH] = "";
    char property_name[SYMBOL_MAX_LENGTH] = "";
    bool found_scope = false;

    /* Find the scope (self/static/parent, class name, variable, or nested expression) and property name */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        /* Extract scope from relative_scope (self/static/parent) */
        if (strcmp(child_type, "relative_scope") == 0) {
            safe_extract_node_text(source_code, child, scope, sizeof(scope), filename);
            found_scope = true;
        }

        /* Extract scope from first 'name' node (class name) if not relative_scope */
        if (strcmp(child_type, "name") == 0 && !found_scope) {
            safe_extract_node_text(source_code, child, scope, sizeof(scope), filename);
            found_scope = true;
        }

        /* Extract scope from nested scoped expression (chained access) */
        if ((strcmp(child_type, "scoped_call_expression") == 0 ||
             strcmp(child_type, "scoped_property_access_expression") == 0) && !found_scope) {
            extract_scoped_symbol_name(child, source_code, scope, sizeof(scope), filename);
            found_scope = true;
        }

        /* First variable_name is scope ($var::), second is property (::$prop) */
        if (strcmp(child_type, "variable_name") == 0 && !found_scope) {
            safe_extract_node_text(source_code, child, scope, sizeof(scope), filename);
            found_scope = true;
        }
        else if (strcmp(child_type, "variable_name") == 0 && found_scope) {
            safe_extract_node_text(source_code, child, property_name, sizeof(property_name), filename);

            TSPoint property_start = ts_node_start_point(child);
            int property_line = (int)(property_start.row + 1);

            if (filter_should_index(filter, property_name)) {
                add_entry(result, property_name, property_line, CONTEXT_PROPERTY,
                        directory, filename, NULL, &(ExtColumns){.parent = scope, .namespace = namespace_buf, .modifier = "static"});
            }
        }
    }
}

static void handle_member_call_expression(TSNode node, const char *source_code, const char *directory,
                                           const char *filename, ParseResult *result, SymbolFilter *filter,
                                           int line) {
    (void)line;
    char namespace_buf[SYMBOL_MAX_LENGTH];
    get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

    char parent_obj[SYMBOL_MAX_LENGTH] = "";
    char method_name[SYMBOL_MAX_LENGTH] = "";

    /* Find the object and method name */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        /* Extract parent object from variable_name (including $) */
        if (strcmp(child_type, "variable_name") == 0) {
            safe_extract_node_text(source_code, child, parent_obj, sizeof(parent_obj), filename);
        }

        /* Extract method name */
        if (strcmp(child_type, "name") == 0 && method_name[0] == '\0') {
            safe_extract_node_text(source_code, child, method_name, sizeof(method_name), filename);

            TSPoint method_start = ts_node_start_point(child);
            int method_line = (int)(method_start.row + 1);

            if (filter_should_index(filter, method_name)) {
                add_entry(result, method_name, method_line, CONTEXT_CALL,
                        directory, filename, NULL, &(ExtColumns){.parent = parent_obj, .namespace = namespace_buf});
            }
        }
    }

    /* Index identifier arguments with method name as clue */
    if (method_name[0] != '\0') {
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            const char *child_type = ts_node_type(child);

            if (strcmp(child_type, "arguments") == 0) {
                /* Iterate through arguments children */
                uint32_t arg_count = ts_node_child_count(child);
                for (uint32_t j = 0; j < arg_count; j++) {
                    TSNode arg = ts_node_child(child, j);
                    const char *arg_type = ts_node_type(arg);

                    /* PHP wraps arguments in 'argument' nodes */
                    if (strcmp(arg_type, "argument") == 0) {
                        uint32_t arg_child_count = ts_node_child_count(arg);
                        for (uint32_t k = 0; k < arg_child_count; k++) {
                            TSNode arg_child = ts_node_child(arg, k);
                            const char *arg_child_type = ts_node_type(arg_child);

                            if (strcmp(arg_child_type, "variable_name") == 0) {
                                char arg_symbol[SYMBOL_MAX_LENGTH];
                                safe_extract_node_text(source_code, arg_child, arg_symbol, sizeof(arg_symbol), filename);

                                TSPoint arg_start = ts_node_start_point(arg_child);
                                int arg_line = (int)(arg_start.row + 1);

                                /* Design decision: Don't filter keywords when used as actual identifiers */
                                // if (filter_should_index(filter, arg_symbol)) {
                                    add_entry(result, arg_symbol, arg_line, CONTEXT_ARGUMENT,
                                            directory, filename, NULL, &(ExtColumns){.clue = method_name, .namespace = namespace_buf});
                                // }
                            }
                        }
                    }
                }
                break;  /* Found arguments, no need to continue */
            }
        }
    }
}

static void handle_scoped_call_expression(TSNode node, const char *source_code, const char *directory,
                                           const char *filename, ParseResult *result, SymbolFilter *filter,
                                           int line) {
    (void)line;
    char namespace_buf[SYMBOL_MAX_LENGTH];
    get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

    char scope[SYMBOL_MAX_LENGTH] = "";
    char method_name[SYMBOL_MAX_LENGTH] = "";
    bool found_scope = false;

    /* Find the scope (self/static/parent, class name, qualified name, variable, or nested expression) and method name */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        /* Extract scope from relative_scope (self/static/parent) */
        if (strcmp(child_type, "relative_scope") == 0) {
            safe_extract_node_text(source_code, child, scope, sizeof(scope), filename);
            found_scope = true;
        }

        /* Extract scope from qualified_name (\App\Models\Class) */
        if (strcmp(child_type, "qualified_name") == 0 && !found_scope) {
            safe_extract_node_text(source_code, child, scope, sizeof(scope), filename);
            found_scope = true;
        }

        /* Extract scope from variable_name ($class::method) */
        if (strcmp(child_type, "variable_name") == 0 && !found_scope) {
            safe_extract_node_text(source_code, child, scope, sizeof(scope), filename);
            found_scope = true;
        }

        /* Extract scope from nested scoped expression (chained calls) */
        if ((strcmp(child_type, "scoped_call_expression") == 0 ||
             strcmp(child_type, "scoped_property_access_expression") == 0) && !found_scope) {
            extract_scoped_symbol_name(child, source_code, scope, sizeof(scope), filename);
            found_scope = true;
        }

        /* For simple class name or first 'name' node, it's the scope */
        if (strcmp(child_type, "name") == 0 && !found_scope) {
            safe_extract_node_text(source_code, child, scope, sizeof(scope), filename);
            found_scope = true;
        }
        /* Second 'name' node (after scope is found) is the method name */
        else if (strcmp(child_type, "name") == 0 && found_scope && method_name[0] == '\0') {
            safe_extract_node_text(source_code, child, method_name, sizeof(method_name), filename);

            TSPoint method_start = ts_node_start_point(child);
            int method_line = (int)(method_start.row + 1);

            if (filter_should_index(filter, method_name)) {
                add_entry(result, method_name, method_line, CONTEXT_CALL,
                        directory, filename, NULL, &(ExtColumns){.parent = scope, .namespace = namespace_buf, .modifier = "static"});
            }
        }
    }

    /* Index identifier arguments with method name as clue */
    if (method_name[0] != '\0') {
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            const char *child_type = ts_node_type(child);

            if (strcmp(child_type, "arguments") == 0) {
                /* Iterate through arguments children */
                uint32_t arg_count = ts_node_child_count(child);
                for (uint32_t j = 0; j < arg_count; j++) {
                    TSNode arg = ts_node_child(child, j);
                    const char *arg_type = ts_node_type(arg);

                    /* PHP wraps arguments in 'argument' nodes */
                    if (strcmp(arg_type, "argument") == 0) {
                        uint32_t arg_child_count = ts_node_child_count(arg);
                        for (uint32_t k = 0; k < arg_child_count; k++) {
                            TSNode arg_child = ts_node_child(arg, k);
                            const char *arg_child_type = ts_node_type(arg_child);

                            if (strcmp(arg_child_type, "variable_name") == 0) {
                                char arg_symbol[SYMBOL_MAX_LENGTH];
                                safe_extract_node_text(source_code, arg_child, arg_symbol, sizeof(arg_symbol), filename);

                                TSPoint arg_start = ts_node_start_point(arg_child);
                                int arg_line = (int)(arg_start.row + 1);

                                /* Design decision: Don't filter keywords when used as actual identifiers */
                                // if (filter_should_index(filter, arg_symbol)) {
                                    add_entry(result, arg_symbol, arg_line, CONTEXT_ARGUMENT,
                                            directory, filename, NULL, &(ExtColumns){.clue = method_name, .namespace = namespace_buf});
                                // }
                            }
                        }
                    }
                }
                break;  /* Found arguments, no need to continue */
            }
        }
    }
}

static void handle_function_call_expression(TSNode node, const char *source_code, const char *directory,
                                             const char *filename, ParseResult *result, SymbolFilter *filter,
                                             int line) {
    (void)line;
    char namespace_buf[SYMBOL_MAX_LENGTH];
    get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

    char func_name[SYMBOL_MAX_LENGTH] = "";
    uint32_t child_count = ts_node_child_count(node);

    /* First pass: extract function name */
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "name") == 0 || strcmp(child_type, "variable_name") == 0) {
            safe_extract_node_text(source_code, child, func_name, sizeof(func_name), filename);

            TSPoint func_start = ts_node_start_point(child);
            int func_line = (int)(func_start.row + 1);

            if (filter_should_index(filter, func_name)) {
                add_entry(result, func_name, func_line, CONTEXT_CALL,
                        directory, filename, NULL, &(ExtColumns){.namespace = namespace_buf});
            }
            break;
        }
    }

    /* Second pass: process arguments with function name as clue */
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "arguments") == 0) {
            uint32_t arg_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < arg_count; j++) {
                TSNode arg = ts_node_child(child, j);
                const char *arg_type = ts_node_type(arg);

                /* PHP wraps arguments in 'argument' nodes, look inside them */
                if (strcmp(arg_type, "argument") == 0) {
                    /* Check if the argument contains a variable_name */
                    uint32_t arg_child_count = ts_node_child_count(arg);
                    for (uint32_t k = 0; k < arg_child_count; k++) {
                        TSNode arg_child = ts_node_child(arg, k);
                        const char *arg_child_type = ts_node_type(arg_child);

                        if (strcmp(arg_child_type, "variable_name") == 0 && func_name[0] != '\0') {
                            char arg_symbol[SYMBOL_MAX_LENGTH];
                            safe_extract_node_text(source_code, arg_child, arg_symbol, sizeof(arg_symbol), filename);

                            TSPoint arg_start = ts_node_start_point(arg_child);
                            int arg_line = (int)(arg_start.row + 1);

                            /* Design decision: Don't filter keywords when used as actual identifiers */
                            // if (filter_should_index(filter, arg_symbol)) {
                                add_entry(result, arg_symbol, arg_line, CONTEXT_ARGUMENT,
                                        directory, filename, NULL, &(ExtColumns){.clue = func_name, .namespace = namespace_buf});
                            // }
                        }
                    }

                    /* Recurse to handle complex expressions */
                    extract_symbols_from_expression(arg, source_code, directory, filename, result, filter, namespace_buf);
                }
            }
            break;
        }
    }
}

static void handle_subscript_expression(TSNode node, const char *source_code, const char *directory,
                                         const char *filename, ParseResult *result, SymbolFilter *filter,
                                         int line) {
    (void)line;
    char namespace_buf[SYMBOL_MAX_LENGTH];
    get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

    /* Process both the base expression and the index expression */
    uint32_t child_count = ts_node_child_count(node);
    bool found_open_bracket = false;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (chrcmp1(child_type, '[')) {
            found_open_bracket = true;
        } else if (found_open_bracket && !chrcmp1(child_type, ']')) {
            /* This is the index expression (between [ and ]) - recursively extract symbols from it */
            extract_symbols_from_expression(child, source_code, directory, filename, result, filter, namespace_buf);
        } else if (chrcmp1(child_type, ']')) {
            break;  /* Done with this subscript level */
        } else if (!found_open_bracket) {
            /* This is the base expression (before [) - recursively extract symbols from it */
            extract_symbols_from_expression(child, source_code, directory, filename, result, filter, namespace_buf);
        }
    }
}

/* Handler: anonymous_function - for anonymous functions (closures) */
static void handle_anonymous_function(TSNode node, const char *source_code, const char *directory,
                                       const char *filename, ParseResult *result, SymbolFilter *filter,
                                       int line) {
    /* anonymous_function structure:
     * - function keyword
     * - formal_parameters
     * - optional anonymous_function_use_clause
     * - optional return type
     * - compound_statement (body)
     */

    if (g_debug) fprintf(stderr, "[DEBUG] handle_anonymous_function called at line %d in %s\n", line, filename);

    char namespace_buf[SYMBOL_MAX_LENGTH];
    get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

    /* Extract source location (line range) for the full lambda expression */
    char location[128];
    format_source_location(node, location, sizeof(location));

    if (g_debug) fprintf(stderr, "[DEBUG] anonymous_function: namespace=%s, location=%s\n", namespace_buf, location);

    /* Index the lambda expression itself */
    add_entry(result, "<lambda>", line, CONTEXT_LAMBDA,
            directory, filename, location, &(ExtColumns){.namespace = namespace_buf, .definition = "1"});

    if (g_debug) fprintf(stderr, "[DEBUG] anonymous_function: indexed <lambda> successfully\n");

    /* Process children to index parameters and body */
    process_children(node, source_code, directory, filename, result, filter);
}

/* Handler: arrow_function - for arrow functions (PHP 7.4+) */
static void handle_arrow_function(TSNode node, const char *source_code, const char *directory,
                                   const char *filename, ParseResult *result, SymbolFilter *filter,
                                   int line) {
    /* arrow_function structure:
     * - fn keyword
     * - formal_parameters
     * - => operator
     * - expression (body)
     */

    if (g_debug) fprintf(stderr, "[DEBUG] handle_arrow_function called at line %d in %s\n", line, filename);

    char namespace_buf[SYMBOL_MAX_LENGTH];
    get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

    /* Extract source location (line range) for the full arrow function */
    char location[128];
    format_source_location(node, location, sizeof(location));

    if (g_debug) fprintf(stderr, "[DEBUG] arrow_function: namespace=%s, location=%s\n", namespace_buf, location);

    /* Index the arrow function itself */
    add_entry(result, "<lambda>", line, CONTEXT_LAMBDA,
            directory, filename, location, &(ExtColumns){.namespace = namespace_buf, .clue = "arrow", .definition = "1"});

    if (g_debug) fprintf(stderr, "[DEBUG] arrow_function: indexed <lambda> successfully\n");

    /* Process children to index parameters and body */
    process_children(node, source_code, directory, filename, result, filter);
}

static void visit_node(TSNode node, const char *source_code, const char *directory,
                      const char *filename, ParseResult *result, SymbolFilter *filter) {
    TSSymbol node_sym = ts_node_symbol(node);
    TSPoint start_point = ts_node_start_point(node);
    int line = (int)(start_point.row + 1);

    if (g_debug && (node_sym == php_symbols.anonymous_function || node_sym == php_symbols.arrow_function)) {
        fprintf(stderr, "[DEBUG] visit_node: found %s at line %d\n", ts_node_type(node), line);
    }

    /* Symbol-based dispatch - O(1) comparisons */
    if (node_sym == php_symbols.namespace_definition) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for namespace_definition\n");
        handle_namespace_definition(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.class_declaration) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for class_declaration\n");
        handle_class_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.interface_declaration) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for interface_declaration\n");
        handle_interface_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.trait_declaration) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for trait_declaration\n");
        handle_trait_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.method_declaration) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for method_declaration\n");
        handle_method_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.function_definition) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for function_definition\n");
        handle_function_definition(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.anonymous_function) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for anonymous_function\n");
        handle_anonymous_function(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.arrow_function) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for arrow_function\n");
        handle_arrow_function(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.enum_declaration) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for enum_declaration\n");
        handle_enum_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.enum_case) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for enum_case\n");
        handle_enum_case(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.formal_parameters) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for formal_parameters\n");
        handle_formal_parameters(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.property_declaration) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for property_declaration\n");
        handle_property_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.const_declaration) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for const_declaration\n");
        handle_const_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.assignment_expression) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for assignment_expression\n");
        handle_assignment_expression(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.subscript_expression) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for subscript_expression\n");
        handle_subscript_expression(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.member_access_expression) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for member_access_expression\n");
        handle_member_access_expression(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.scoped_property_access_expression) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for scoped_property_access_expression\n");
        handle_scoped_property_access_expression(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.member_call_expression) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for member_call_expression\n");
        handle_member_call_expression(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.scoped_call_expression) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for scoped_call_expression\n");
        handle_scoped_call_expression(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.function_call_expression) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for function_call_expression\n");
        handle_function_call_expression(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.class_constant_access_expression) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for class_constant_access_expression\n");
        handle_class_constant_access_expression(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.comment) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for comment\n");
        handle_comment(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.string || node_sym == php_symbols.encapsed_string) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for string/encapsed_string\n");
        handle_string(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.heredoc) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for heredoc\n");
        handle_heredoc(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == php_symbols.nowdoc) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_node: calling handler for nowdoc\n");
        handle_nowdoc(node, source_code, directory, filename, result, filter, line);
        return;
    }

    /* Handle binary_expression - extract symbols from operands */
    if (node_sym == php_symbols.binary_expression) {
        char namespace_buf[SYMBOL_MAX_LENGTH];
        get_namespace(node, source_code, namespace_buf, sizeof(namespace_buf), filename);

        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            TSSymbol child_sym = ts_node_symbol(child);

            /* Process operands - skip operator tokens (middle child is typically the operator) */
            if (child_sym == php_symbols.variable_name ||
                child_sym == php_symbols.binary_expression ||
                child_sym == php_symbols.function_call_expression ||
                child_sym == php_symbols.subscript_expression ||
                child_sym == php_symbols.member_access_expression ||
                child_sym == php_symbols.member_call_expression ||
                child_sym == php_symbols.parenthesized_expression) {
                extract_symbols_from_expression(child, source_code, directory, filename, result, filter, namespace_buf);
            }
        }
        return;
    }

    /* No handler found, recursively process children */
    process_children(node, source_code, directory, filename, result, filter);
}

int parser_init(PHPParser *parser, SymbolFilter *filter) {
    parser->filter = filter;
    return 0;
}

int parser_parse_file(PHPParser *parser, const char *filepath, const char *project_root, ParseResult *result) {
    FILE *fp = safe_fopen(filepath, "r", 0);
    if (!fp) {
        fprintf(stderr, "Cannot open file: %s\n", filepath);
        return -1;
    }

    /* Get file size using fstat */
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
        fprintf(stderr, "Error reading file: %s (expected %zu bytes, got %zu)\n",
                filepath, file_size, bytes_read);
        free(source_code);
        fclose(fp);
        return -1;
    }
    source_code[bytes_read] = '\0';
    fclose(fp);

    result->count = 0;

    char directory[DIRECTORY_MAX_LENGTH];
    char filename[FILENAME_MAX_LENGTH];
    get_relative_path(filepath, project_root, directory, filename);

    /* Index filename without extension */
    char filename_no_ext[FILENAME_MAX_LENGTH];
    snprintf(filename_no_ext, sizeof(filename_no_ext), "%s", filename);
    char *dot = strrchr(filename_no_ext, '.');
    if (dot) *dot = '\0';

    if (filter_should_index(parser->filter, filename_no_ext)) {
        add_entry(result, filename_no_ext, 1, CONTEXT_FILENAME, directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
    }

    /* Parse with tree-sitter */
    TSParser *ts_parser = ts_parser_new();
    const TSLanguage *language = tree_sitter_php();
    if (!ts_parser_set_language(ts_parser, language)) {
        fprintf(stderr, "ERROR: Failed to set PHP language\n");
        free(source_code);
        ts_parser_delete(ts_parser);
        return -1;
    }

    /* Initialize symbol lookup table */
    init_php_symbols(language);

    TSTree *tree = ts_parser_parse_string(ts_parser, NULL, source_code, (uint32_t)file_size);
    if (!tree) {
        fprintf(stderr, "ERROR: Failed to parse file: %s\n", filepath);
        free(source_code);
        ts_parser_delete(ts_parser);
        return -1;
    }

    TSNode root_node = ts_tree_root_node(tree);

    /* Visit all nodes */
    visit_node(root_node, source_code, directory, filename, result, parser->filter);

    /* Cleanup */
    ts_tree_delete(tree);
    ts_parser_delete(ts_parser);
    free(source_code);

    return 0;
}

void parser_set_debug(PHPParser *parser, int debug) {
    parser->debug = debug;
    g_debug = debug;
}

void parser_free(PHPParser *parser) {
    /* No resources to free yet */
    (void)parser;
}
