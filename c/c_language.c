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
#include "c_language.h"
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
#include "../shared/debug.h"

/* External C language function from tree-sitter-c */
extern const TSLanguage *tree_sitter_c(void);

/* Global debug flag */
static int g_debug = 0;

/* Symbol lookup table for fast node type comparisons */
static struct {
    TSSymbol identifier;
    TSSymbol type_identifier;
    TSSymbol pointer_declarator;
    TSSymbol compound_statement;
    TSSymbol parenthesized_expression;
    TSSymbol field_expression;
    TSSymbol while_statement;
    TSSymbol return_statement;
    TSSymbol if_statement;
    TSSymbol else_clause;
    TSSymbol function_declarator;
    TSSymbol for_statement;
    TSSymbol field_identifier;
    TSSymbol expression_statement;
    TSSymbol union_specifier;
    TSSymbol subscript_expression;
    TSSymbol struct_specifier;
    TSSymbol enum_specifier;
    TSSymbol call_expression;
    TSSymbol update_expression;
    TSSymbol string_literal;
    TSSymbol primitive_type;
    TSSymbol init_declarator;
    TSSymbol binary_expression;
    TSSymbol assignment_expression;
    TSSymbol array_declarator;
    TSSymbol unary_expression;
    TSSymbol sizeof_expression;
    TSSymbol conditional_expression;
    TSSymbol cast_expression;
    TSSymbol compound_literal_expression;
    TSSymbol concatenated_string;
    TSSymbol declaration;
    TSSymbol enumerator;
    TSSymbol enumerator_list;
    TSSymbol field_declaration;
    TSSymbol field_declaration_list;
    TSSymbol field_designator;
    TSSymbol initializer_list;
    TSSymbol initializer_pair;
    TSSymbol parameter_declaration;
    TSSymbol parameter_list;
    TSSymbol pointer_expression;
    TSSymbol sized_type_specifier;
    TSSymbol storage_class_specifier;
    TSSymbol string_content;
    TSSymbol type_qualifier;
    TSSymbol argument_list;
    TSSymbol system_lib_string;
    TSSymbol function_definition;
    TSSymbol type_definition;
    TSSymbol comment;
    TSSymbol preproc_def;
    TSSymbol preproc_function_def;
    TSSymbol preproc_include;
    TSSymbol preproc_params;
    TSSymbol preproc_ifdef;
    TSSymbol preproc_if;
    TSSymbol preproc_elif;
    TSSymbol preproc_else;
    TSSymbol do_statement;
    TSSymbol switch_statement;
    TSSymbol case_statement;
    TSSymbol goto_statement;
    TSSymbol labeled_statement;
    TSSymbol statement_identifier;
    /* Keywords (anonymous nodes) */
    TSSymbol keyword_struct;
    TSSymbol keyword_union;
    TSSymbol keyword_enum;
} c_symbols;

/* Use safe_extract_node_text from shared/string_utils.h instead of local truncating version */

/* Forward declarations */
static void visit_node(TSNode node, const char *source_code, const char *directory,
                      const char *filename, ParseResult *result, SymbolFilter *filter);

static void visit_expression(
    TSNode node,
    const char *source_code,
    const char *directory,
    const char *filename,
    ParseResult *result,
    SymbolFilter *filter
);

static void extract_type_from_declaration(TSNode declaration_node, const char *source_code,
                                          TSNode declarator_node, char *type_buffer, size_t type_size,
                                          char *modifier_buffer, size_t modifier_size, const char *filename);

/* Node handler function pointer type */
typedef void (*NodeHandler)(TSNode node, const char *source_code,
                           const char *directory, const char *filename,
                           ParseResult *result, SymbolFilter *filter,
                           int line);

/* Initialize symbol lookup table - called once at startup */
static void init_c_symbols(const TSLanguage *language) {
    static int initialized = 0;
    if (initialized) return;
    initialized = 1;

    /* Core node types */
    c_symbols.identifier = ts_language_symbol_for_name(language, "identifier", 10, true);
    c_symbols.type_identifier = ts_language_symbol_for_name(language, "type_identifier", 15, true);
    c_symbols.field_identifier = ts_language_symbol_for_name(language, "field_identifier", 16, true);

    /* Declarators */
    c_symbols.pointer_declarator = ts_language_symbol_for_name(language, "pointer_declarator", 18, true);
    c_symbols.function_declarator = ts_language_symbol_for_name(language, "function_declarator", 19, true);
    c_symbols.array_declarator = ts_language_symbol_for_name(language, "array_declarator", 16, true);
    c_symbols.init_declarator = ts_language_symbol_for_name(language, "init_declarator", 15, true);

    /* Statements */
    c_symbols.compound_statement = ts_language_symbol_for_name(language, "compound_statement", 18, true);
    c_symbols.expression_statement = ts_language_symbol_for_name(language, "expression_statement", 20, true);
    c_symbols.return_statement = ts_language_symbol_for_name(language, "return_statement", 16, true);
    c_symbols.if_statement = ts_language_symbol_for_name(language, "if_statement", 12, true);
    c_symbols.else_clause = ts_language_symbol_for_name(language, "else_clause", 11, true);
    c_symbols.for_statement = ts_language_symbol_for_name(language, "for_statement", 13, true);
    c_symbols.while_statement = ts_language_symbol_for_name(language, "while_statement", 15, true);

    /* Expressions */
    c_symbols.parenthesized_expression = ts_language_symbol_for_name(language, "parenthesized_expression", 24, true);
    c_symbols.field_expression = ts_language_symbol_for_name(language, "field_expression", 16, true);
    c_symbols.subscript_expression = ts_language_symbol_for_name(language, "subscript_expression", 20, true);
    c_symbols.call_expression = ts_language_symbol_for_name(language, "call_expression", 15, true);
    c_symbols.update_expression = ts_language_symbol_for_name(language, "update_expression", 17, true);
    c_symbols.binary_expression = ts_language_symbol_for_name(language, "binary_expression", 17, true);
    c_symbols.unary_expression = ts_language_symbol_for_name(language, "unary_expression", 16, true);
    c_symbols.assignment_expression = ts_language_symbol_for_name(language, "assignment_expression", 21, true);
    c_symbols.sizeof_expression = ts_language_symbol_for_name(language, "sizeof_expression", 17, true);
    c_symbols.conditional_expression = ts_language_symbol_for_name(language, "conditional_expression", 22, true);
    c_symbols.cast_expression = ts_language_symbol_for_name(language, "cast_expression", 15, true);
    c_symbols.compound_literal_expression = ts_language_symbol_for_name(language, "compound_literal_expression", 27, true);
    c_symbols.pointer_expression = ts_language_symbol_for_name(language, "pointer_expression", 18, true);

    /* Type specifiers */
    c_symbols.struct_specifier = ts_language_symbol_for_name(language, "struct_specifier", 16, true);
    c_symbols.union_specifier = ts_language_symbol_for_name(language, "union_specifier", 15, true);
    c_symbols.enum_specifier = ts_language_symbol_for_name(language, "enum_specifier", 14, true);
    c_symbols.primitive_type = ts_language_symbol_for_name(language, "primitive_type", 14, true);
    c_symbols.sized_type_specifier = ts_language_symbol_for_name(language, "sized_type_specifier", 20, true);

    /* Strings and literals */
    c_symbols.string_literal = ts_language_symbol_for_name(language, "string_literal", 14, true);
    c_symbols.string_content = ts_language_symbol_for_name(language, "string_content", 14, true);
    c_symbols.concatenated_string = ts_language_symbol_for_name(language, "concatenated_string", 19, true);

    /* Declarations and definitions */
    c_symbols.declaration = ts_language_symbol_for_name(language, "declaration", 11, true);
    c_symbols.field_declaration = ts_language_symbol_for_name(language, "field_declaration", 17, true);
    c_symbols.field_declaration_list = ts_language_symbol_for_name(language, "field_declaration_list", 22, true);
    c_symbols.parameter_declaration = ts_language_symbol_for_name(language, "parameter_declaration", 21, true);
    c_symbols.parameter_list = ts_language_symbol_for_name(language, "parameter_list", 14, true);

    /* Initializers and enumerators */
    c_symbols.initializer_list = ts_language_symbol_for_name(language, "initializer_list", 16, true);
    c_symbols.initializer_pair = ts_language_symbol_for_name(language, "initializer_pair", 16, true);
    c_symbols.enumerator = ts_language_symbol_for_name(language, "enumerator", 10, true);
    c_symbols.enumerator_list = ts_language_symbol_for_name(language, "enumerator_list", 15, true);
    c_symbols.field_designator = ts_language_symbol_for_name(language, "field_designator", 16, true);

    /* Modifiers and qualifiers */
    c_symbols.storage_class_specifier = ts_language_symbol_for_name(language, "storage_class_specifier", 23, true);
    c_symbols.type_qualifier = ts_language_symbol_for_name(language, "type_qualifier", 14, true);

    /* Other */
    c_symbols.argument_list = ts_language_symbol_for_name(language, "argument_list", 13, true);
    c_symbols.system_lib_string = ts_language_symbol_for_name(language, "system_lib_string", 17, true);

    /* Top-level handlers */
    c_symbols.function_definition = ts_language_symbol_for_name(language, "function_definition", 19, true);
    c_symbols.type_definition = ts_language_symbol_for_name(language, "type_definition", 15, true);
    c_symbols.comment = ts_language_symbol_for_name(language, "comment", 7, true);
    c_symbols.preproc_def = ts_language_symbol_for_name(language, "preproc_def", 11, true);
    c_symbols.preproc_function_def = ts_language_symbol_for_name(language, "preproc_function_def", 20, true);
    c_symbols.preproc_include = ts_language_symbol_for_name(language, "preproc_include", 15, true);
    c_symbols.preproc_params = ts_language_symbol_for_name(language, "preproc_params", 14, true);
    c_symbols.preproc_ifdef = ts_language_symbol_for_name(language, "preproc_ifdef", 13, true);
    c_symbols.preproc_if = ts_language_symbol_for_name(language, "preproc_if", 10, true);
    c_symbols.preproc_elif = ts_language_symbol_for_name(language, "preproc_elif", 12, true);
    c_symbols.preproc_else = ts_language_symbol_for_name(language, "preproc_else", 12, true);
    c_symbols.do_statement = ts_language_symbol_for_name(language, "do_statement", 12, true);
    c_symbols.switch_statement = ts_language_symbol_for_name(language, "switch_statement", 16, true);
    c_symbols.case_statement = ts_language_symbol_for_name(language, "case_statement", 14, true);
    c_symbols.goto_statement = ts_language_symbol_for_name(language, "goto_statement", 14, true);
    c_symbols.labeled_statement = ts_language_symbol_for_name(language, "labeled_statement", 17, true);
    c_symbols.statement_identifier = ts_language_symbol_for_name(language, "statement_identifier", 20, true);

    /* Keywords (anonymous nodes - note the 'false' parameter) */
    c_symbols.keyword_struct = ts_language_symbol_for_name(language, "struct", 6, false);
    c_symbols.keyword_union = ts_language_symbol_for_name(language, "union", 5, false);
    c_symbols.keyword_enum = ts_language_symbol_for_name(language, "enum", 4, false);
}

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

/* Helper: Extract identifier from potentially nested declarator */
static TSNode find_identifier_in_declarator(TSNode declarator_node) {
    TSSymbol node_sym = ts_node_symbol(declarator_node);

    /* Direct identifier */
    if (node_sym == c_symbols.identifier) {
        return declarator_node;
    }

    /* Pointer declarator wraps the identifier */
    if (node_sym == c_symbols.pointer_declarator) {
        uint32_t child_count = ts_node_child_count(declarator_node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(declarator_node, i);
            TSSymbol child_sym = ts_node_symbol(child);
            if (child_sym == c_symbols.identifier ||
                child_sym == c_symbols.pointer_declarator) {
                return find_identifier_in_declarator(child);
            }
        }
    }

    /* Array declarator (for arrays) */
    if (node_sym == c_symbols.array_declarator) {
        uint32_t child_count = ts_node_child_count(declarator_node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(declarator_node, i);
            TSSymbol child_sym = ts_node_symbol(child);
            if (child_sym == c_symbols.identifier) {
                return child;
            }
        }
    }

    /* Function declarator (for function pointers in typedefs) */
    if (node_sym == c_symbols.function_declarator) {
        uint32_t child_count = ts_node_child_count(declarator_node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(declarator_node, i);
            TSSymbol child_sym = ts_node_symbol(child);
            if (child_sym == c_symbols.identifier ||
                child_sym == c_symbols.pointer_declarator ||
                child_sym == c_symbols.function_declarator) {
                return find_identifier_in_declarator(child);
            }
        }
    }

    TSNode null_node = {0};
    return null_node;
}

static void extract_parameters(TSNode param_list_node, const char *source_code, const char *directory,
                              const char *filename, ParseResult *result, SymbolFilter *filter, int is_definition) {
    if (ts_node_is_null(param_list_node)) return;

    uint32_t child_count = ts_node_child_count(param_list_node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(param_list_node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == c_symbols.parameter_declaration) {
            TSPoint start_point = ts_node_start_point(child);
            int line = (int)(start_point.row + 1);

            /* Iterate through parameter_declaration children */
            uint32_t param_child_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < param_child_count; j++) {
                TSNode param_child = ts_node_child(child, j);
                TSSymbol param_child_sym = ts_node_symbol(param_child);

                /* Extract type_identifier (custom types like Point, CodeIndexDatabase) */
                if (param_child_sym == c_symbols.type_identifier) {
                    char type_symbol[SYMBOL_MAX_LENGTH];
                    safe_extract_node_text(source_code, param_child, type_symbol, sizeof(type_symbol), filename);

                    if (filter_should_index(filter, type_symbol)) {
                        add_entry(result, type_symbol, line, CONTEXT_TYPE,
                                directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
                    }
                }
                /* Extract struct_specifier (e.g., struct Node *ptr) */
                else if (param_child_sym == c_symbols.struct_specifier ||
                         param_child_sym == c_symbols.union_specifier ||
                         param_child_sym == c_symbols.enum_specifier) {
                    /* Find type_identifier within struct/union/enum */
                    uint32_t spec_child_count = ts_node_child_count(param_child);
                    for (uint32_t k = 0; k < spec_child_count; k++) {
                        TSNode spec_child = ts_node_child(param_child, k);
                        if (ts_node_symbol(spec_child) == c_symbols.type_identifier) {
                            char type_symbol[SYMBOL_MAX_LENGTH];
                            safe_extract_node_text(source_code, spec_child, type_symbol, sizeof(type_symbol), filename);

                            if (filter_should_index(filter, type_symbol)) {
                                add_entry(result, type_symbol, line, CONTEXT_TYPE,
                                        directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
                            }
                            break;
                        }
                    }
                }
            }

            /* Extract parameter identifier (variable name) and type */
            for (uint32_t j = 0; j < param_child_count; j++) {
                TSNode param_child = ts_node_child(child, j);
                TSNode id_node = find_identifier_in_declarator(param_child);

                if (!ts_node_is_null(id_node)) {
                    char symbol[SYMBOL_MAX_LENGTH];
                    safe_extract_node_text(source_code, id_node, symbol, sizeof(symbol), filename);

                    /* Extract parameter type using byte positions */
                    char param_type[SYMBOL_MAX_LENGTH] = "";
                    uint32_t param_start = ts_node_start_byte(child);
                    uint32_t id_start = ts_node_start_byte(id_node);

                    if (id_start > param_start && (id_start - param_start) < sizeof(param_type)) {
                        uint32_t type_len = id_start - param_start;
                        strncpy(param_type, source_code + param_start, type_len);
                        param_type[type_len] = '\0';

                        /* Trim trailing whitespace */
                        while (type_len > 0 && isspace((unsigned char)param_type[type_len - 1])) {
                            param_type[--type_len] = '\0';
                        }
                    }

                    if (filter_should_index(filter, symbol)) {
                        ExtColumns ext = {
                            .parent = NULL,
                            .modifier = NULL,
                            .clue = NULL,
                            .type = param_type[0] != '\0' ? param_type : NULL,
                            .definition = is_definition ? "1" : "0"
                        };
                        add_entry(result, symbol, line, CONTEXT_ARGUMENT,
                                directory, filename, NULL, &ext);
                    }
                    break;
                }
            }
        }
    }
}

static void handle_function_definition(TSNode node, const char *source_code, const char *directory,
                                        const char *filename, ParseResult *result, SymbolFilter *filter,
                                        int line) {
    /* Find function_declarator child (may be wrapped in pointer_declarator for pointer return types) */
    uint32_t child_count = ts_node_child_count(node);
    TSNode function_declarator_node = {0};

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == c_symbols.function_declarator) {
            function_declarator_node = child;
            break;
        } else if (child_sym == c_symbols.pointer_declarator) {
            /* For pointer return types (char *, int *, etc.), look inside pointer_declarator */
            uint32_t ptr_child_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < ptr_child_count; j++) {
                TSNode ptr_child = ts_node_child(child, j);
                if (ts_node_symbol(ptr_child) == c_symbols.function_declarator) {
                    function_declarator_node = ptr_child;
                    break;
                }
            }
            if (!ts_node_is_null(function_declarator_node)) {
                break;
            }
        }
    }

    if (!ts_node_is_null(function_declarator_node)) {
        TSNode child = function_declarator_node;
        /* Function declarator has identifier and parameter_list */
        uint32_t decl_child_count = ts_node_child_count(child);
        TSNode func_name_node = {0};
        TSNode param_list_node = {0};

        for (uint32_t j = 0; j < decl_child_count; j++) {
            TSNode decl_child = ts_node_child(child, j);
            TSSymbol decl_child_sym = ts_node_symbol(decl_child);

            if (decl_child_sym == c_symbols.identifier) {
                func_name_node = decl_child;
            } else if (decl_child_sym == c_symbols.parameter_list) {
                param_list_node = decl_child;
            }
        }

        /* Extract function name and return type */
        if (!ts_node_is_null(func_name_node)) {
            char symbol[SYMBOL_MAX_LENGTH];
            char type_str[SYMBOL_MAX_LENGTH];
            char modifier_str[SYMBOL_MAX_LENGTH];
            char location[128];
            safe_extract_node_text(source_code, func_name_node, symbol, sizeof(symbol), filename);

            /* Extract return type from function_definition node (same structure as declaration) */
            TSNode null_declarator = {0}; /* Functions don't use pointer_declarator for return type */
            extract_type_from_declaration(node, source_code, null_declarator,
                                          type_str, sizeof(type_str),
                                          modifier_str, sizeof(modifier_str), filename);

            if (filter_should_index(filter, symbol)) {
                /* Extract source location for full function definition */
                format_source_location(node, location, sizeof(location));

                add_entry(result, symbol, line, CONTEXT_FUNCTION,
                        directory, filename, location, &(ExtColumns){
                            .type = type_str,
                            .modifier = modifier_str,
                            .definition = "1"
                        });
            }
        }

        /* Extract parameters */
        if (!ts_node_is_null(param_list_node)) {
            extract_parameters(param_list_node, source_code, directory, filename, result, filter, 1);
        }
    }

    /* Process function body (compound_statement) to index local variables, strings, calls, etc. */
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);

        if (ts_node_symbol(child) == c_symbols.compound_statement) {
            process_children(child, source_code, directory, filename, result, filter);
            break;
        }
    }
}

/* Extract type and modifier information from a declaration node
 * Handles: primitive_type, sized_type_specifier, struct_specifier, type_identifier
 * Modifiers: type_qualifier (const, volatile, etc.)
 * Also detects pointer declarators and appends '*' to type
 */
static void extract_type_from_declaration(TSNode declaration_node, const char *source_code,
                                          TSNode declarator_node, char *type_buffer, size_t type_size,
                                          char *modifier_buffer, size_t modifier_size, const char *filename) {
    type_buffer[0] = '\0';
    modifier_buffer[0] = '\0';
    char temp[SYMBOL_MAX_LENGTH] = {0};
    bool has_type_content = false;
    bool has_modifier_content = false;
    bool is_pointer = false;

    /* Check if declarator is a pointer */
    if (!ts_node_is_null(declarator_node)) {
        if (ts_node_symbol(declarator_node) == c_symbols.pointer_declarator) {
            is_pointer = true;
        }
    }

    /* Iterate through declaration children to build type and modifier strings */
    uint32_t child_count = ts_node_child_count(declaration_node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(declaration_node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        /* Storage class specifier (static, inline, extern, etc.) - goes to modifier column */
        if (child_sym == c_symbols.storage_class_specifier) {
            safe_extract_node_text(source_code, child, temp, sizeof(temp), filename);
            if (has_modifier_content) strncat(modifier_buffer, " ", modifier_size - strlen(modifier_buffer) - 1);
            strncat(modifier_buffer, temp, modifier_size - strlen(modifier_buffer) - 1);
            has_modifier_content = true;
        }
        /* Type qualifier (const, volatile, etc.) - goes to modifier column */
        else if (child_sym == c_symbols.type_qualifier) {
            safe_extract_node_text(source_code, child, temp, sizeof(temp), filename);
            if (has_modifier_content) strncat(modifier_buffer, " ", modifier_size - strlen(modifier_buffer) - 1);
            strncat(modifier_buffer, temp, modifier_size - strlen(modifier_buffer) - 1);
            has_modifier_content = true;
        }
        /* Primitive type (int, char, double, void, etc.) */
        else if (child_sym == c_symbols.primitive_type) {
            safe_extract_node_text(source_code, child, temp, sizeof(temp), filename);
            if (has_type_content) strncat(type_buffer, " ", type_size - strlen(type_buffer) - 1);
            strncat(type_buffer, temp, type_size - strlen(type_buffer) - 1);
            has_type_content = true;
        }
        /* Sized type (unsigned long, signed int, etc.) */
        else if (child_sym == c_symbols.sized_type_specifier) {
            safe_extract_node_text(source_code, child, temp, sizeof(temp), filename);
            if (has_type_content) strncat(type_buffer, " ", type_size - strlen(type_buffer) - 1);
            strncat(type_buffer, temp, type_size - strlen(type_buffer) - 1);
            has_type_content = true;
        }
        /* Struct/union/enum specifier (struct Point, etc.) */
        else if (child_sym == c_symbols.struct_specifier ||
                 child_sym == c_symbols.union_specifier ||
                 child_sym == c_symbols.enum_specifier) {
            /* Extract just "struct TypeName" part, not the full definition */
            uint32_t struct_child_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < struct_child_count; j++) {
                TSNode struct_child = ts_node_child(child, j);
                TSSymbol struct_child_sym = ts_node_symbol(struct_child);

                /* Get "struct", "union", or "enum" keyword */
                if (struct_child_sym == c_symbols.keyword_struct ||
                    struct_child_sym == c_symbols.keyword_union ||
                    struct_child_sym == c_symbols.keyword_enum) {
                    safe_extract_node_text(source_code, struct_child, temp, sizeof(temp), filename);
                    if (has_type_content) strncat(type_buffer, " ", type_size - strlen(type_buffer) - 1);
                    strncat(type_buffer, temp, type_size - strlen(type_buffer) - 1);
                    has_type_content = true;
                }
                /* Get type name */
                else if (struct_child_sym == c_symbols.type_identifier) {
                    safe_extract_node_text(source_code, struct_child, temp, sizeof(temp), filename);
                    strncat(type_buffer, " ", type_size - strlen(type_buffer) - 1);
                    strncat(type_buffer, temp, type_size - strlen(type_buffer) - 1);
                    break; /* Stop after getting the name, ignore field_declaration_list */
                }
            }
        }
        /* Type identifier (typedef'd types like Rectangle, size_t, etc.) */
        else if (child_sym == c_symbols.type_identifier) {
            safe_extract_node_text(source_code, child, temp, sizeof(temp), filename);
            if (has_type_content) strncat(type_buffer, " ", type_size - strlen(type_buffer) - 1);
            strncat(type_buffer, temp, type_size - strlen(type_buffer) - 1);
            has_type_content = true;
        }
        /* Stop when we hit the declarator (identifier, init_declarator, pointer_declarator) */
        else if (child_sym == c_symbols.identifier ||
                 child_sym == c_symbols.init_declarator ||
                 child_sym == c_symbols.pointer_declarator) {
            break;
        }
    }

    /* Append '*' if it's a pointer */
    if (is_pointer && has_type_content) {
        strncat(type_buffer, "*", type_size - strlen(type_buffer) - 1);
    }
}

static void handle_declaration(TSNode node, const char *source_code, const char *directory,
                                const char *filename, ParseResult *result, SymbolFilter *filter,
                                int line) {
    /* Find init_declarator children */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == c_symbols.init_declarator) {
            /* init_declarator contains identifier or pointer_declarator */
            uint32_t init_child_count = ts_node_child_count(child);
            TSNode declarator_node = {0};

            for (uint32_t j = 0; j < init_child_count; j++) {
                TSNode init_child = ts_node_child(child, j);
                TSNode id_node = find_identifier_in_declarator(init_child);

                if (!ts_node_is_null(id_node)) {
                    char symbol[SYMBOL_MAX_LENGTH];
                    char type_str[SYMBOL_MAX_LENGTH];
                    char modifier_str[SYMBOL_MAX_LENGTH];
                    char location[128];
                    safe_extract_node_text(source_code, id_node, symbol, sizeof(symbol), filename);

                    /* Get the declarator node (identifier or pointer_declarator) for type extraction */
                    declarator_node = init_child;

                    /* Extract type and modifier information */
                    extract_type_from_declaration(node, source_code, declarator_node,
                                                  type_str, sizeof(type_str),
                                                  modifier_str, sizeof(modifier_str), filename);

                    if (filter_should_index(filter, symbol)) {
                        /* Extract source location for full variable declaration */
                        format_source_location(node, location, sizeof(location));

                        add_entry(result, symbol, line, CONTEXT_VARIABLE,
                                directory, filename, location, &(ExtColumns){
                                    .type = type_str,
                                    .modifier = modifier_str,
                                    .definition = "1"
                                });
                    }
                    break;
                }
            }

            /* Visit initializer expression if present */
            for (uint32_t j = 0; j < init_child_count; j++) {
                TSNode init_child = ts_node_child(child, j);
                const char *init_child_type = ts_node_type(init_child);
                if (strcmp(init_child_type, "=") == 0) {
                    /* Next child is the initializer expression */
                    if (j + 1 < init_child_count) {
                        TSNode init_expr = ts_node_child(child, j + 1);
                        visit_expression(init_expr, source_code, directory, filename, result, filter);
                    }
                    break;
                }
            }
        } else if (child_sym == c_symbols.identifier ||
                   child_sym == c_symbols.pointer_declarator) {
            /* Direct declarator without init_declarator wrapper */
            TSNode id_node = find_identifier_in_declarator(child);

            if (!ts_node_is_null(id_node)) {
                char symbol[SYMBOL_MAX_LENGTH];
                char type_str[SYMBOL_MAX_LENGTH];
                char modifier_str[SYMBOL_MAX_LENGTH];
                char location[128];
                safe_extract_node_text(source_code, id_node, symbol, sizeof(symbol), filename);

                /* Extract type and modifier information */
                extract_type_from_declaration(node, source_code, child,
                                              type_str, sizeof(type_str),
                                              modifier_str, sizeof(modifier_str), filename);

                if (filter_should_index(filter, symbol)) {
                    /* Extract source location for full variable declaration */
                    format_source_location(node, location, sizeof(location));

                    add_entry(result, symbol, line, CONTEXT_VARIABLE,
                            directory, filename, location, &(ExtColumns){
                                .type = type_str,
                                .modifier = modifier_str,
                                .definition = "1"
                            });
                }
            }
        } else if (child_sym == c_symbols.array_declarator) {
            /* Array declaration: type name[size] */
            uint32_t array_child_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < array_child_count; j++) {
                TSNode array_child = ts_node_child(child, j);
                TSSymbol array_child_sym = ts_node_symbol(array_child);

                if (array_child_sym == c_symbols.identifier && j == 0) {
                    /* This is the array variable name (first child) */
                    char symbol[SYMBOL_MAX_LENGTH];
                    char type_str[SYMBOL_MAX_LENGTH];
                    char modifier_str[SYMBOL_MAX_LENGTH];
                    char location[128];
                    safe_extract_node_text(source_code, array_child, symbol, sizeof(symbol), filename);

                    /* Extract type and modifier information */
                    extract_type_from_declaration(node, source_code, child,
                                                  type_str, sizeof(type_str),
                                                  modifier_str, sizeof(modifier_str), filename);

                    if (filter_should_index(filter, symbol)) {
                        /* Extract source location for full variable declaration */
                        format_source_location(node, location, sizeof(location));

                        add_entry(result, symbol, line, CONTEXT_VARIABLE,
                                directory, filename, location, &(ExtColumns){
                                    .type = type_str,
                                    .modifier = modifier_str,
                                    .definition = "1"
                                });
                    }
                } else if (array_child_sym == c_symbols.identifier) {
                    /* This is the array size (e.g., SQL_QUERY_BUFFER) */
                    char array_size_symbol[SYMBOL_MAX_LENGTH];
                    safe_extract_node_text(source_code, array_child, array_size_symbol, sizeof(array_size_symbol), filename);

                    if (filter_should_index(filter, array_size_symbol)) {
                        add_entry(result, array_size_symbol, line, CONTEXT_VARIABLE,
                                directory, filename, NULL, NULL);
                    }
                }
            }
        }
    }
}

static void handle_struct_specifier(TSNode node, const char *source_code, const char *directory,
                                     const char *filename, ParseResult *result, SymbolFilter *filter,
                                     int line) {
    /* Find struct name (type_identifier) and fields */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == c_symbols.type_identifier) {
            /* Extract struct name */
            char symbol[SYMBOL_MAX_LENGTH];
            char location[128];
            safe_extract_node_text(source_code, child, symbol, sizeof(symbol), filename);

            if (filter_should_index(filter, symbol)) {
                /* Extract source location for full struct definition */
                format_source_location(node, location, sizeof(location));

                add_entry(result, symbol, line, CONTEXT_TYPE,
                  directory, filename, location, &(ExtColumns){.definition = "1"});
            }
        } else if (child_sym == c_symbols.field_declaration_list) {
            /* Extract field names */
            uint32_t field_list_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < field_list_count; j++) {
                TSNode field = ts_node_child(child, j);
                TSSymbol field_sym = ts_node_symbol(field);

                if (field_sym == c_symbols.field_declaration) {
                    /* Extract type and field identifier from field_declaration */
                    char type_str[SYMBOL_MAX_LENGTH] = "";
                    char modifier_str[SYMBOL_MAX_LENGTH] = "";
                    TSNode declarator_node = {0};

                    uint32_t field_child_count = ts_node_child_count(field);
                    TSPoint field_start = ts_node_start_point(field);
                    int field_line = (int)(field_start.row + 1);

                    for (uint32_t k = 0; k < field_child_count; k++) {
                        TSNode field_child = ts_node_child(field, k);
                        TSSymbol field_child_sym = ts_node_symbol(field_child);

                        /* Index type identifier (e.g., ContextType in "ContextType types[10]") */
                        if (field_child_sym == c_symbols.type_identifier ||
                            field_child_sym == c_symbols.primitive_type) {
                            char type_symbol[SYMBOL_MAX_LENGTH];
                            safe_extract_node_text(source_code, field_child, type_symbol, sizeof(type_symbol), filename);

                            if (filter_should_index(filter, type_symbol)) {
                                add_entry(result, type_symbol, field_line, CONTEXT_TYPE,
                                        directory, filename, NULL, NULL);
                            }
                        }

                        if (field_child_sym == c_symbols.field_identifier) {
                            char field_symbol[SYMBOL_MAX_LENGTH];
                            safe_extract_node_text(source_code, field_child, field_symbol, sizeof(field_symbol), filename);

                            /* Extract type information */
                            extract_type_from_declaration(field, source_code, declarator_node,
                                                          type_str, sizeof(type_str),
                                                          modifier_str, sizeof(modifier_str), filename);

                            if (filter_should_index(filter, field_symbol)) {
                                add_entry(result, field_symbol, field_line, CONTEXT_PROPERTY,
                                        directory, filename, NULL, &(ExtColumns){
                                            .definition = "1",
                                            .type = type_str[0] ? type_str : NULL
                                        });
                            }
                        } else if (field_child_sym == c_symbols.pointer_declarator) {
                            /* Field is a pointer, extract from pointer_declarator */
                            declarator_node = field_child;  /* Save for type extraction */
                            uint32_t ptr_child_count = ts_node_child_count(field_child);
                            for (uint32_t m = 0; m < ptr_child_count; m++) {
                                TSNode ptr_child = ts_node_child(field_child, m);
                                if (ts_node_symbol(ptr_child) == c_symbols.field_identifier) {
                                    char field_symbol[SYMBOL_MAX_LENGTH];
                                    safe_extract_node_text(source_code, ptr_child, field_symbol, sizeof(field_symbol), filename);

                                    /* Extract type information */
                                    extract_type_from_declaration(field, source_code, declarator_node,
                                                                  type_str, sizeof(type_str),
                                                                  modifier_str, sizeof(modifier_str), filename);

                                    if (filter_should_index(filter, field_symbol)) {
                                        add_entry(result, field_symbol, field_line, CONTEXT_PROPERTY,
                                                directory, filename, NULL, &(ExtColumns){
                                                    .definition = "1",
                                                    .type = type_str[0] ? type_str : NULL
                                                });
                                    }
                                    break;
                                }
                            }
                        } else if (field_child_sym == c_symbols.array_declarator) {
                            /* Field is an array, extract field name and array size */
                            declarator_node = field_child;  /* Save for type extraction */
                            uint32_t array_child_count = ts_node_child_count(field_child);
                            for (uint32_t m = 0; m < array_child_count; m++) {
                                TSNode array_child = ts_node_child(field_child, m);
                                TSSymbol array_child_sym = ts_node_symbol(array_child);

                                if (array_child_sym == c_symbols.field_identifier) {
                                    /* This is the array field name */
                                    char field_symbol[SYMBOL_MAX_LENGTH];
                                    safe_extract_node_text(source_code, array_child, field_symbol, sizeof(field_symbol), filename);

                                    /* Extract type information */
                                    extract_type_from_declaration(field, source_code, declarator_node,
                                                                  type_str, sizeof(type_str),
                                                                  modifier_str, sizeof(modifier_str), filename);

                                    if (filter_should_index(filter, field_symbol)) {
                                        add_entry(result, field_symbol, field_line, CONTEXT_PROPERTY,
                                                directory, filename, NULL, &(ExtColumns){
                                                    .definition = "1",
                                                    .type = type_str[0] ? type_str : NULL
                                                });
                                    }
                                } else if (array_child_sym == c_symbols.identifier) {
                                    /* This is the array size (e.g., MAX_CONTEXT_TYPES) */
                                    char array_size_symbol[SYMBOL_MAX_LENGTH];
                                    safe_extract_node_text(source_code, array_child, array_size_symbol, sizeof(array_size_symbol), filename);

                                    if (filter_should_index(filter, array_size_symbol)) {
                                        add_entry(result, array_size_symbol, field_line, CONTEXT_VARIABLE,
                                                directory, filename, NULL, NULL);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

static void handle_type_definition(TSNode node, const char *source_code, const char *directory,
                                    const char *filename, ParseResult *result, SymbolFilter *filter,
                                    int line) {
    /* typedef can wrap struct/enum/union - extract the typedef name (type_identifier) */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == c_symbols.type_identifier) {
            /* This is the typedef name */
            char symbol[SYMBOL_MAX_LENGTH];
            char location[128];
            safe_extract_node_text(source_code, child, symbol, sizeof(symbol), filename);

            if (filter_should_index(filter, symbol)) {
                /* Extract source location for full typedef definition */
                format_source_location(node, location, sizeof(location));

                add_entry(result, symbol, line, CONTEXT_TYPE,
					directory, filename, location, &(ExtColumns){.definition = "1"});
            }
        } else if (child_sym == c_symbols.struct_specifier ||
                   child_sym == c_symbols.enum_specifier ||
                   child_sym == c_symbols.union_specifier) {
            /* Process struct/enum/union fields within typedef */
            visit_node(child, source_code, directory, filename, result, filter);
        }
    }
}

static void handle_enum_specifier(TSNode node, const char *source_code, const char *directory,
                                   const char *filename, ParseResult *result, SymbolFilter *filter,
                                   int line) {
    /* Find enum name (type_identifier) and enumerators */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == c_symbols.type_identifier) {
            /* Extract enum name */
            char symbol[SYMBOL_MAX_LENGTH];
            char location[128];
            safe_extract_node_text(source_code, child, symbol, sizeof(symbol), filename);

            if (filter_should_index(filter, symbol)) {
                /* Extract source location for full enum definition */
                format_source_location(node, location, sizeof(location));

                add_entry(result, symbol, line, CONTEXT_ENUM,
					directory, filename, location, &(ExtColumns){.definition = "1"});
            }
        } else if (child_sym == c_symbols.enumerator_list) {
            /* Extract enumerator names */
            uint32_t enum_list_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < enum_list_count; j++) {
                TSNode enumerator = ts_node_child(child, j);
                TSSymbol enum_sym = ts_node_symbol(enumerator);

                if (enum_sym == c_symbols.enumerator) {
                    /* Find identifier in enumerator */
                    uint32_t enum_child_count = ts_node_child_count(enumerator);
                    for (uint32_t k = 0; k < enum_child_count; k++) {
                        TSNode enum_child = ts_node_child(enumerator, k);
                        if (ts_node_symbol(enum_child) == c_symbols.identifier) {
                            char enum_symbol[SYMBOL_MAX_LENGTH];
                            safe_extract_node_text(source_code, enum_child, enum_symbol, sizeof(enum_symbol), filename);

                            TSPoint enum_start = ts_node_start_point(enumerator);
                            int enum_line = (int)(enum_start.row + 1);

                            if (filter_should_index(filter, enum_symbol)) {
                                add_entry(result, enum_symbol, enum_line, CONTEXT_ENUM_CASE,
                                        directory, filename, NULL, &(ExtColumns){.definition = "1"});
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
}

static void handle_comment(TSNode node, const char *source_code, const char *directory,
                           const char *filename, ParseResult *result, SymbolFilter *filter,
                           int line) {
    char comment_text[COMMENT_TEXT_BUFFER];
    safe_extract_node_text(source_code, node, comment_text, sizeof(comment_text), filename);

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

                    /* Clean the word (preserve path characters for comments) */
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

static void handle_string_literal(TSNode node, const char *source_code, const char *directory,
                                  const char *filename, ParseResult *result, SymbolFilter *filter,
                                  int line) {
    /* Find string_content child */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);
        if (child_sym == c_symbols.string_content) {
            char string_text[COMMENT_TEXT_BUFFER];
            safe_extract_node_text(source_code, child, string_text, sizeof(string_text), filename);

            /* Extract words from string - split on whitespace */
            char word[CLEANED_WORD_BUFFER];
            char cleaned[CLEANED_WORD_BUFFER];
            char *word_start = string_text;

            for (char *p = string_text; ; p++) {
                if (*p == '\0' || isspace(*p)) {
                    if (p > word_start) {
                        size_t word_len = (size_t)(p - word_start);
                        if (word_len < sizeof(word)) {
                            snprintf(word, sizeof(word), "%.*s", (int)word_len, word_start);

                            /* Clean the word (preserve path characters for strings) */
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
    }
}

/**
 * Helper: Extract the immediate parent identifier from the left side of a field expression.
 * For entry->context, returns "entry"
 * For a->b->c, returns "b" (the immediate parent of c)
 */
static void extract_field_parent(const char *source_code, TSNode field_expr_node, char *parent_buf, size_t buf_size, const char *filename) {
    /* Get the left side of the field expression */
    TSNode left = ts_node_child_by_field_name(field_expr_node, "argument", 8);

    if (ts_node_is_null(left)) {
        parent_buf[0] = '\0';
        return;
    }

    TSSymbol left_sym = ts_node_symbol(left);

    /* If left is an identifier, that's our parent */
    if (left_sym == c_symbols.identifier) {
        safe_extract_node_text(source_code, left, parent_buf, buf_size, filename);
        return;
    }

    /* If left is another field_expression (a->b->c), extract the rightmost field (b) */
    if (left_sym == c_symbols.field_expression) {
        TSNode field = ts_node_child_by_field_name(left, "field", 5);
        if (!ts_node_is_null(field)) {
            safe_extract_node_text(source_code, field, parent_buf, buf_size, filename);
            return;
        }
    }

    /* If left is subscript_expression (items[0].field), extract the array name */
    if (left_sym == c_symbols.subscript_expression) {
        TSNode array = ts_node_child_by_field_name(left, "argument", 8);
        TSSymbol array_sym = ts_node_symbol(array);
        if (!ts_node_is_null(array) && array_sym == c_symbols.identifier) {
            safe_extract_node_text(source_code, array, parent_buf, buf_size, filename);
            return;
        }
    }

    /* If left is concatenated_string (parser error), extract the last identifier child */
    if (left_sym == c_symbols.concatenated_string) {
        /* Walk backwards through children to find last identifier */
        uint32_t child_count = ts_node_child_count(left);
        for (int32_t i = (int32_t)child_count - 1; i >= 0; i--) {
            TSNode child = ts_node_child(left, (uint32_t)i);
            TSSymbol child_sym = ts_node_symbol(child);
            if (child_sym == c_symbols.identifier) {
                safe_extract_node_text(source_code, child, parent_buf, buf_size, filename);
                return;
            }
        }
    }

    /* Default: extract full text of left side */
    safe_extract_node_text(source_code, left, parent_buf, buf_size, filename);
}

/**
 * Recursively visit expressions and extract symbol references.
 * Handles function calls, field accesses, variable references, and more.
 */
static void visit_expression(
    TSNode node,
    const char *source_code,
    const char *directory,
    const char *filename,
    ParseResult *result,
    SymbolFilter *filter
) {
    if (ts_node_is_null(node)) {
        return;
    }

    TSSymbol node_sym = ts_node_symbol(node);
    const char *node_type = ts_node_type(node);  /* Only for debug output */
    TSPoint start_point = ts_node_start_point(node);
    int line = (int)(start_point.row + 1);

    if (g_debug) {
        debug("[visit_expression] Line %d: node_type='%s'", line, node_type);
    }

    /* Handle different expression types */
    if (node_sym == c_symbols.call_expression) {
        if (g_debug) {
            debug("[visit_expression] Line %d: Processing call_expression", line);
        }
        /* Function call: index the function name as CALL_EXPRESSION */
        char func_name[SYMBOL_MAX_LENGTH] = "";
        TSNode function = ts_node_child_by_field_name(node, "function", 8);
        if (!ts_node_is_null(function)) {
            const char *func_type = ts_node_type(function);
            if (g_debug) {
                debug("[visit_expression] Line %d: function node type='%s'", line, func_type);
            }

            /* Direct function call: func(args) */
            if (ts_node_symbol(function) == c_symbols.identifier) {
                safe_extract_node_text(source_code, function, func_name, sizeof(func_name), filename);
                if (g_debug) {
                    debug("[visit_expression] Line %d: Function name='%s'", line, func_name);
                }

                if (filter_should_index(filter, func_name)) {
                    if (g_debug) {
                        debug("[visit_expression] Line %d: Indexing call to '%s'", line, func_name);
                    }
                    add_entry(result, func_name, line, CONTEXT_CALL,
                            directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
                } else if (g_debug) {
                    debug("[visit_expression] Line %d: Filter rejected '%s'", line, func_name);
                }
            }
            /* Field expression function call: obj->func(args) */
            else if (ts_node_symbol(function) == c_symbols.field_expression) {
                /* First, visit the field expression to index obj->func with parent */
                visit_expression(function, source_code, directory, filename, result, filter);

                /* Extract the method name for argument clues */
                TSNode field = ts_node_child_by_field_name(function, "field", 5);
                if (!ts_node_is_null(field)) {
                    safe_extract_node_text(source_code, field, func_name, sizeof(func_name), filename);
                }
            }
            /* Other complex function expressions */
            else {
                visit_expression(function, source_code, directory, filename, result, filter);
            }
        }

        /* Find and process argument_list by iterating through children */
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);

            if (ts_node_symbol(child) == c_symbols.argument_list) {
                /* Iterate through argument_list children */
                uint32_t arg_count = ts_node_child_count(child);
                for (uint32_t j = 0; j < arg_count; j++) {
                    TSNode arg = ts_node_child(child, j);

                    /* Index identifier arguments with function name as clue */
                    if (ts_node_symbol(arg) == c_symbols.identifier && func_name[0] != '\0') {
                        char arg_symbol[SYMBOL_MAX_LENGTH];
                        safe_extract_node_text(source_code, arg, arg_symbol, sizeof(arg_symbol), filename);

                        /* Design decision: Don't filter keywords when used as actual identifiers
                         * (variable names, property names, function names, etc.).
                         * Keywords should only be filtered when used in their keyword context,
                         * not when repurposed as identifiers in code. Strings and comments
                         * are filtered separately by context type. */
                        // if (filter_should_index(filter, arg_symbol)) {
                            add_entry(result, arg_symbol, line, CONTEXT_ARGUMENT,
                                    directory, filename, NULL, &(ExtColumns){.clue = func_name});
                        // }
                    }
                    /* Visit non-identifier arguments to index complex expressions */
                    else if (ts_node_symbol(arg) != c_symbols.identifier) {
                        visit_expression(arg, source_code, directory, filename, result, filter);
                    }
                    /* Skip visiting simple identifiers - already indexed as ARG above */
                }
                break;  /* Found argument_list, no need to continue */
            }
        }
    }
    else if (node_sym == c_symbols.field_expression) {
        /* Field access: obj->field or obj.field */
        char parent[SYMBOL_MAX_LENGTH];
        extract_field_parent(source_code, node, parent, sizeof(parent), filename);

        TSNode field = ts_node_child_by_field_name(node, "field", 5);
        if (!ts_node_is_null(field)) {
            char field_name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, field, field_name, sizeof(field_name), filename);

            if (filter_should_index(filter, field_name)) {
                /* Use the field's line number, not the field_expression's line number */
                TSPoint field_point = ts_node_start_point(field);
                int field_line = (int)(field_point.row + 1);
                add_entry(result, field_name, field_line, CONTEXT_PROPERTY,
                        directory, filename, NULL, &(ExtColumns){.parent = parent});
            }
        }

        /* Also visit the left side (argument) to catch nested field accesses */
        TSNode argument = ts_node_child_by_field_name(node, "argument", 8);
        if (!ts_node_is_null(argument)) {
            visit_expression(argument, source_code, directory, filename, result, filter);
        }
    }
    else if (node_sym == c_symbols.identifier) {
        /* Variable or constant reference */
        char symbol[SYMBOL_MAX_LENGTH];
        safe_extract_node_text(source_code, node, symbol, sizeof(symbol), filename);

        if (filter_should_index(filter, symbol)) {
            add_entry(result, symbol, line, CONTEXT_VARIABLE,
                    directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
        }
    }
    else if (node_sym == c_symbols.subscript_expression) {
        /* Array access: array[index] - visit both array and index */
        TSNode argument = ts_node_child_by_field_name(node, "argument", 8);
        TSNode index = ts_node_child_by_field_name(node, "index", 5);

        if (!ts_node_is_null(argument)) {
            visit_expression(argument, source_code, directory, filename, result, filter);
        }
        if (!ts_node_is_null(index)) {
            visit_expression(index, source_code, directory, filename, result, filter);
        }
    }
    else if (node_sym == c_symbols.binary_expression) {
        /* Binary operation: a + b, a | b, etc. - visit left and right */
        /* Iterate through children instead of using field names */
        uint32_t child_count = ts_node_child_count(node);
        if (g_debug) {
            debug("[visit_expression] Line %d: binary_expression has %d children", line, child_count);
        }
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            const char *child_type = ts_node_type(child);
            if (g_debug) {
                debug("[visit_expression] Line %d: binary child[%d]='%s'", line, i, child_type);
            }
            /* Skip operators */
            if (strlen(child_type) > 2 || (child_type[0] != '+' && child_type[0] != '-' &&
                child_type[0] != '*' && child_type[0] != '/' && child_type[0] != '%' &&
                child_type[0] != '&' && child_type[0] != '|' && child_type[0] != '^' &&
                child_type[0] != '<' && child_type[0] != '>' && child_type[0] != '=' &&
                child_type[0] != '!')) {
                if (g_debug) {
                    debug("[visit_expression] Line %d: Visiting binary child '%s'", line, child_type);
                }
                visit_expression(child, source_code, directory, filename, result, filter);
            } else {
                if (g_debug) {
                    debug("[visit_expression] Line %d: SKIPPING operator '%s'", line, child_type);
                }
            }
        }
    }
    else if (node_sym == c_symbols.assignment_expression) {
        /* Assignment: a = b, a += b, etc. - visit left and right */
        TSNode left = ts_node_child_by_field_name(node, "left", 4);
        TSNode right = ts_node_child_by_field_name(node, "right", 5);

        if (!ts_node_is_null(left)) {
            visit_expression(left, source_code, directory, filename, result, filter);
        }
        if (!ts_node_is_null(right)) {
            visit_expression(right, source_code, directory, filename, result, filter);
        }
    }
    else if (node_sym == c_symbols.conditional_expression) {
        /* Ternary: a ? b : c - visit condition, consequence, and alternative */
        TSNode condition = ts_node_child_by_field_name(node, "condition", 9);
        TSNode consequence = ts_node_child_by_field_name(node, "consequence", 11);
        TSNode alternative = ts_node_child_by_field_name(node, "alternative", 11);

        if (!ts_node_is_null(condition)) {
            visit_expression(condition, source_code, directory, filename, result, filter);
        }
        if (!ts_node_is_null(consequence)) {
            visit_expression(consequence, source_code, directory, filename, result, filter);
        }
        if (!ts_node_is_null(alternative)) {
            visit_expression(alternative, source_code, directory, filename, result, filter);
        }
    }
    else if (node_sym == c_symbols.pointer_expression) {
        /* Pointer operations: *ptr (dereference) or &var (address-of) */
        /* Get the operator to determine if it's deref (*) or addr_of (&) */
        TSNode operator_node = ts_node_child(node, 0);
        const char *op_text = ts_node_type(operator_node);

        /* Get the operand */
        TSNode operand = ts_node_child_by_field_name(node, "argument", 8);
        if (!ts_node_is_null(operand)) {
            /* If operand is a simple identifier, index it with the clue */
            if (ts_node_symbol(operand) == c_symbols.identifier) {
                char symbol[SYMBOL_MAX_LENGTH];
                safe_extract_node_text(source_code, operand, symbol, sizeof(symbol), filename);

                /* Determine clue based on operator */
                const char *clue = NULL;
                if (strcmp(op_text, "*") == 0) {
                    clue = "deref";
                } else if (strcmp(op_text, "&") == 0) {
                    clue = "address";
                }

                if (clue && filter_should_index(filter, symbol)) {
                    TSPoint point = ts_node_start_point(operand);
                    int line = (int)(point.row + 1);
                    add_entry(result, symbol, line, CONTEXT_VARIABLE,
                            directory, filename, NULL, &(ExtColumns){.clue = clue});
                }
            } else {
                /* For complex operands (e.g., arr[0] in &arr[0]), recurse */
                visit_expression(operand, source_code, directory, filename, result, filter);
            }
        }
    }
    else if (node_sym == c_symbols.unary_expression) {
        /* Other unary: ++i, --i, !x - visit the operand */
        TSNode operand = ts_node_child_by_field_name(node, "argument", 8);
        if (!ts_node_is_null(operand)) {
            visit_expression(operand, source_code, directory, filename, result, filter);
        }
    }
    else if (node_sym == c_symbols.parenthesized_expression) {
        /* Parentheses: (expr) - visit contents */
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            visit_expression(child, source_code, directory, filename, result, filter);
        }
    }
    else if (node_sym == c_symbols.cast_expression) {
        /* Cast: (int*)expr - visit the value expression, ignore the type */
        TSNode value = ts_node_child_by_field_name(node, "value", 5);
        if (!ts_node_is_null(value)) {
            visit_expression(value, source_code, directory, filename, result, filter);
        }
    }
    else if (node_sym == c_symbols.sizeof_expression) {
        /* sizeof(expr) or sizeof(Type) - if it's an expression, visit it */
        TSNode value = ts_node_child_by_field_name(node, "value", 5);
        if (!ts_node_is_null(value)) {
            TSSymbol value_sym = ts_node_symbol(value);
            /* Only visit if it's an expression, not a type */
            if (value_sym == c_symbols.identifier ||
                value_sym == c_symbols.field_expression ||
                value_sym == c_symbols.subscript_expression ||
                value_sym == c_symbols.call_expression) {
                visit_expression(value, source_code, directory, filename, result, filter);
            }
        }
    }
    else if (node_sym == c_symbols.initializer_list || node_sym == c_symbols.compound_literal_expression) {
        /* Initializer list: {a, b, c} or compound literal: (Point){.x=1, .y=2} */
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            visit_expression(child, source_code, directory, filename, result, filter);
        }
    }
    else if (node_sym == c_symbols.initializer_pair) {
        /* Designated initializer: .field = value */
        /* Extract field name from field_designator */
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            TSSymbol child_sym = ts_node_symbol(child);

            if (child_sym == c_symbols.field_designator) {
                /* Find field_identifier within field_designator */
                uint32_t designator_child_count = ts_node_child_count(child);
                for (uint32_t j = 0; j < designator_child_count; j++) {
                    TSNode designator_child = ts_node_child(child, j);
                    TSSymbol designator_child_sym = ts_node_symbol(designator_child);
                    if (designator_child_sym == c_symbols.field_identifier) {
                        char field_name[SYMBOL_MAX_LENGTH];
                        safe_extract_node_text(source_code, designator_child, field_name, sizeof(field_name), filename);

                        if (filter_should_index(filter, field_name)) {
                            add_entry(result, field_name, line, CONTEXT_PROPERTY,
                                    directory, filename, NULL, NULL);
                        }
                        break;
                    }
                }
            }
        }

        /* Process the value expression */
        TSNode value = ts_node_child_by_field_name(node, "value", 5);
        if (!ts_node_is_null(value)) {
            visit_expression(value, source_code, directory, filename, result, filter);
        }
    }
    else if (node_sym == c_symbols.update_expression) {
        /* Pre/post increment/decrement: i++ or ++i */
        TSNode argument = ts_node_child_by_field_name(node, "argument", 8);
        if (!ts_node_is_null(argument)) {
            visit_expression(argument, source_code, directory, filename, result, filter);
        }
    }
    else if (node_sym == c_symbols.string_literal) {
        /* String literal in expression context (e.g., function arguments) */
        handle_string_literal(node, source_code, directory, filename, result, filter, line);
    }
    else {
        /* Default case: recursively visit all children for unknown expression types */
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            /* Skip non-expression children like punctuation */
            const char *child_type = ts_node_type(child);
            if (child_type[0] != '(' && child_type[0] != ')' &&
                strcmp(child_type, ",") != 0 && strcmp(child_type, ";") != 0) {
                visit_expression(child, source_code, directory, filename, result, filter);
            }
        }
    }
}

/**
 * Statement handlers - visit expressions within statements
 */
static void handle_expression_statement(TSNode node, const char *source_code, const char *directory,
                                        const char *filename, ParseResult *result, SymbolFilter *filter,
                                        int line) {
    (void)line; /* Line number comes from the expression itself */

    if (g_debug) {
        debug("[handle_expression_statement] Line %d: Processing expression_statement", line);
    }

    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);
        /* Skip semicolons */
        if (strcmp(child_type, ";") != 0) {
            if (g_debug) {
                debug("[handle_expression_statement] Line %d: Visiting child '%s'", line, child_type);
            }
            visit_expression(child, source_code, directory, filename, result, filter);
        }
    }
}

static void handle_return_statement(TSNode node, const char *source_code, const char *directory,
                                     const char *filename, ParseResult *result, SymbolFilter *filter,
                                     int line) {
    (void)line; /* Line number comes from the expression itself */

    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);
        /* Skip 'return' keyword and semicolon */
        if (strcmp(child_type, "return") != 0 && strcmp(child_type, ";") != 0) {
            visit_expression(child, source_code, directory, filename, result, filter);
        }
    }
}

static void handle_if_statement(TSNode node, const char *source_code, const char *directory,
                                 const char *filename, ParseResult *result, SymbolFilter *filter,
                                 int line) {
    (void)line; /* Not used */

    if (g_debug) {
        debug("[handle_if_statement] Line %d: Processing if_statement", line);
    }

    /* Visit all children: condition, consequence, and optional alternative (else clause) */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == c_symbols.parenthesized_expression) {
            /* Visit the condition expression */
            if (g_debug) {
                debug("[handle_if_statement] Line %d: Found condition, visiting", line);
            }
            visit_expression(child, source_code, directory, filename, result, filter);
        } else if (child_sym == c_symbols.compound_statement ||
                   child_sym == c_symbols.expression_statement ||
                   child_sym == c_symbols.return_statement ||
                   child_sym == c_symbols.if_statement ||
                   child_sym == c_symbols.while_statement ||
                   child_sym == c_symbols.for_statement) {
            /* Visit consequence or alternative body - must explicitly visit children */
            if (g_debug) {
                debug("[handle_if_statement] Line %d: Found body (%s), visiting", line, ts_node_type(child));
            }
            visit_node(child, source_code, directory, filename, result, filter);
        } else if (child_sym == c_symbols.else_clause) {
            /* Visit else clause - it contains either another if_statement (else-if) or a statement body */
            if (g_debug) {
                debug("[handle_if_statement] Line %d: Found else_clause, visiting", line);
            }
            visit_node(child, source_code, directory, filename, result, filter);
        }
        /* Skip "if" and "else" keywords */
    }
}

static void handle_while_statement(TSNode node, const char *source_code, const char *directory,
                                    const char *filename, ParseResult *result, SymbolFilter *filter,
                                    int line) {
    (void)line; /* Not used */

    if (g_debug) {
        debug("[handle_while_statement] Line %d: Processing while_statement", line);
    }

    /* Visit all children: condition and body */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == c_symbols.parenthesized_expression) {
            /* Visit the condition expression */
            if (g_debug) {
                debug("[handle_while_statement] Line %d: Found condition, visiting", line);
            }
            visit_expression(child, source_code, directory, filename, result, filter);
        } else if (child_sym == c_symbols.compound_statement ||
                   child_sym == c_symbols.expression_statement ||
                   child_sym == c_symbols.return_statement ||
                   child_sym == c_symbols.if_statement ||
                   child_sym == c_symbols.while_statement ||
                   child_sym == c_symbols.for_statement) {
            /* Visit body - must explicitly visit children */
            if (g_debug) {
                debug("[handle_while_statement] Line %d: Found body (%s), visiting", line, ts_node_type(child));
            }
            visit_node(child, source_code, directory, filename, result, filter);
        }
        /* Skip "while" keyword */
    }
}

static void handle_do_statement(TSNode node, const char *source_code, const char *directory,
                                 const char *filename, ParseResult *result, SymbolFilter *filter,
                                 int line) {
    (void)line; /* Not used */

    if (g_debug) {
        debug("[handle_do_statement] Line %d: Processing do_statement", line);
    }

    /* Visit all children: body and condition */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == c_symbols.parenthesized_expression) {
            /* Visit the condition expression */
            if (g_debug) {
                debug("[handle_do_statement] Line %d: Found condition, visiting", line);
            }
            visit_expression(child, source_code, directory, filename, result, filter);
        } else if (child_sym == c_symbols.compound_statement ||
                   child_sym == c_symbols.expression_statement ||
                   child_sym == c_symbols.return_statement ||
                   child_sym == c_symbols.if_statement ||
                   child_sym == c_symbols.while_statement ||
                   child_sym == c_symbols.for_statement) {
            /* Visit body - must explicitly visit children */
            if (g_debug) {
                debug("[handle_do_statement] Line %d: Found body (%s), visiting", line, ts_node_type(child));
            }
            visit_node(child, source_code, directory, filename, result, filter);
        }
        /* Skip "do" and "while" keywords */
    }
}

static void handle_for_statement(TSNode node, const char *source_code, const char *directory,
                                  const char *filename, ParseResult *result, SymbolFilter *filter,
                                  int line) {
    (void)line; /* Not used */

    if (g_debug) {
        debug("[handle_for_statement] Line %d: Processing for_statement", line);
    }

    /* Visit all children: initializer, condition, update, and body */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        /* For loops have a special structure with parenthesized parts */
        /* We need to visit declaration, expressions, and the body */
        if (child_sym == c_symbols.declaration) {
            /* Initializer declaration - visit it */
            if (g_debug) {
                debug("[handle_for_statement] Line %d: Found initializer declaration, visiting", line);
            }
            visit_node(child, source_code, directory, filename, result, filter);
        } else if (child_sym == c_symbols.assignment_expression ||
                   child_sym == c_symbols.update_expression ||
                   child_sym == c_symbols.binary_expression ||
                   child_sym == c_symbols.call_expression ||
                   child_sym == c_symbols.identifier) {
            /* Initializer, condition, or update expressions */
            if (g_debug) {
                debug("[handle_for_statement] Line %d: Found expression (%s), visiting", line, ts_node_type(child));
            }
            visit_expression(child, source_code, directory, filename, result, filter);
        } else if (child_sym == c_symbols.compound_statement ||
                   child_sym == c_symbols.expression_statement ||
                   child_sym == c_symbols.return_statement ||
                   child_sym == c_symbols.if_statement ||
                   child_sym == c_symbols.while_statement ||
                   child_sym == c_symbols.for_statement) {
            /* Visit body - must explicitly visit children */
            if (g_debug) {
                debug("[handle_for_statement] Line %d: Found body (%s), visiting", line, ts_node_type(child));
            }
            visit_node(child, source_code, directory, filename, result, filter);
        }
        /* Skip "for", "(", ")", ";", and other punctuation */
    }
}

static void handle_switch_statement(TSNode node, const char *source_code, const char *directory,
                                     const char *filename, ParseResult *result, SymbolFilter *filter,
                                     int line) {
    (void)line; /* Not used */

    if (g_debug) {
        debug("[handle_switch_statement] Line %d: Processing switch_statement", line);
    }

    /* Visit all children: value expression and body */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == c_symbols.parenthesized_expression ||
            child_sym == c_symbols.identifier ||
            child_sym == c_symbols.field_expression) {
            /* Visit the switch value expression */
            if (g_debug) {
                debug("[handle_switch_statement] Line %d: Found value expression, visiting", line);
            }
            visit_expression(child, source_code, directory, filename, result, filter);
        } else if (child_sym == c_symbols.compound_statement) {
            /* Visit body (contains case statements) - must explicitly visit children */
            if (g_debug) {
                debug("[handle_switch_statement] Line %d: Found body, visiting", line);
            }
            visit_node(child, source_code, directory, filename, result, filter);
        }
        /* Skip "switch" keyword */
    }
}

static void handle_case_statement(TSNode node, const char *source_code, const char *directory,
                                   const char *filename, ParseResult *result, SymbolFilter *filter,
                                   int line) {
    (void)line; /* Not used */

    /* Visit the case value expression */
    TSNode value = ts_node_child_by_field_name(node, "value", 5);
    if (!ts_node_is_null(value)) {
        visit_expression(value, source_code, directory, filename, result, filter);
    }
}

static void handle_preproc_def(TSNode node, const char *source_code, const char *directory,
                                const char *filename, ParseResult *result, SymbolFilter *filter,
                                int line) {
    /* Extract the macro name from #define directives */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == c_symbols.identifier) {
            char symbol[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, symbol, sizeof(symbol), filename);

            if (filter_should_index(filter, symbol)) {
                add_entry(result, symbol, line, CONTEXT_VARIABLE,
                        directory, filename, NULL, &(ExtColumns){.clue = "macro", .definition = "1"});
            }
            break;  /* Only index the macro name, not the value */
        }
    }
}

static void handle_preproc_function_def(TSNode node, const char *source_code, const char *directory,
                                         const char *filename, ParseResult *result, SymbolFilter *filter,
                                         int line) {
    /* Extract the macro name and parameters from function-like #define directives */
    char macro_name[SYMBOL_MAX_LENGTH] = "";
    uint32_t child_count = ts_node_child_count(node);

    /* First pass: find and index the macro name */
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == c_symbols.identifier) {
            safe_extract_node_text(source_code, child, macro_name, sizeof(macro_name), filename);

            if (filter_should_index(filter, macro_name)) {
                add_entry(result, macro_name, line, CONTEXT_FUNCTION,
                        directory, filename, NULL, &(ExtColumns){.clue = "macro", .definition = "1"});
            }
            break;  /* Found macro name, exit first pass */
        }
    }

    /* Second pass: find and index macro parameters */
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == c_symbols.preproc_params) {
            /* Iterate through parameter list children */
            uint32_t param_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < param_count; j++) {
                TSNode param = ts_node_child(child, j);
                if (ts_node_symbol(param) == c_symbols.identifier) {
                    char param_name[SYMBOL_MAX_LENGTH];
                    safe_extract_node_text(source_code, param, param_name, sizeof(param_name), filename);

                    if (filter_should_index(filter, param_name)) {
                        add_entry(result, param_name, line, CONTEXT_ARGUMENT,
                                directory, filename, NULL, &(ExtColumns){.clue = macro_name});
                    }
                }
            }
            break;  /* Found parameters, done */
        }
    }
}

static void handle_preproc_include(TSNode node, const char *source_code, const char *directory,
                                    const char *filename, ParseResult *result, SymbolFilter *filter,
                                    int line) {
    /* Extract the header name from #include directives */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        /* Handle <stdio.h> style includes */
        if (child_sym == c_symbols.system_lib_string) {
            char symbol[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, symbol, sizeof(symbol), filename);

            if (filter_should_index(filter, symbol)) {
                add_entry(result, symbol, line, CONTEXT_IMPORT,
                        directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
            }
            break;
        }
        /* Handle "myheader.h" style includes */
        else if (child_sym == c_symbols.string_literal) {
            char symbol[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, symbol, sizeof(symbol), filename);

            if (filter_should_index(filter, symbol)) {
                add_entry(result, symbol, line, CONTEXT_IMPORT,
                        directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
            }
            break;
        }
    }
}

static void handle_preproc_ifdef(TSNode node, const char *source_code, const char *directory,
                                   const char *filename, ParseResult *result, SymbolFilter *filter,
                                   int line) {
    /* Extract the condition identifier from #ifdef or #ifndef directives */
    /* Determine if it's #ifdef or #ifndef by checking first child */
    const char *directive_type = "ifdef";  /* default */

    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        /* Check if it's #ifndef */
        if (strcmp(child_type, "#ifndef") == 0) {
            directive_type = "ifndef";
        }

        /* Find the identifier (e.g., DEBUG, PRODUCTION) */
        if (ts_node_symbol(child) == c_symbols.identifier) {
            char symbol[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, symbol, sizeof(symbol), filename);

            if (filter_should_index(filter, symbol)) {
                add_entry(result, symbol, line, CONTEXT_VARIABLE,
                        directory, filename, NULL, &(ExtColumns){.clue = directive_type});
            }
            break;  /* Found the condition identifier, done */
        }
    }
}

static void handle_preproc_if(TSNode node, const char *source_code, const char *directory,
                               const char *filename, ParseResult *result, SymbolFilter *filter,
                               int line) {
    /* Extract identifiers from #if, #elif, or #else directives */
    TSSymbol node_symbol = ts_node_symbol(node);
    const char *directive_type;

    if (node_symbol == c_symbols.preproc_if) {
        directive_type = "if";
    } else if (node_symbol == c_symbols.preproc_elif) {
        directive_type = "elif";
    } else if (node_symbol == c_symbols.preproc_else) {
        directive_type = "else";
        /* #else has no condition, just mark the directive itself */
        return;
    } else {
        return;  /* Unknown node type */
    }

    /* Extract all identifiers from the condition expression */
    /* For #if DEBUG, #if defined(VERBOSE), #if DEBUG > 2, etc. */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_symbol = ts_node_symbol(child);

        /* Skip the directive itself (#if, #elif) */
        const char *child_type = ts_node_type(child);
        if (strcmp(child_type, "#if") == 0 || strcmp(child_type, "#elif") == 0) {
            continue;
        }

        /* Index simple identifier (e.g., DEBUG, PRODUCTION, MAX_SIZE) */
        if (child_symbol == c_symbols.identifier) {
            char symbol[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, symbol, sizeof(symbol), filename);

            if (filter_should_index(filter, symbol)) {
                add_entry(result, symbol, line, CONTEXT_VARIABLE,
                        directory, filename, NULL, &(ExtColumns){.clue = directive_type});
            }
        }
        /* For complex expressions (binary_expression, preproc_defined), recurse to find identifiers */
        else if (strcmp(child_type, "binary_expression") == 0 ||
                 strcmp(child_type, "preproc_defined") == 0) {
            /* Recurse to extract identifiers from nested expressions */
            uint32_t nested_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < nested_count; j++) {
                TSNode nested = ts_node_child(child, j);
                if (ts_node_symbol(nested) == c_symbols.identifier) {
                    char symbol[SYMBOL_MAX_LENGTH];
                    safe_extract_node_text(source_code, nested, symbol, sizeof(symbol), filename);

                    if (filter_should_index(filter, symbol)) {
                        add_entry(result, symbol, line, CONTEXT_VARIABLE,
                                directory, filename, NULL, &(ExtColumns){.clue = directive_type});
                    }
                }
            }
        }
    }
}

static void handle_labeled_statement(TSNode node, const char *source_code, const char *directory,
                                      const char *filename, ParseResult *result, SymbolFilter *filter,
                                      int line) {
    /* Extract label name from labeled_statement (e.g., "cleanup:", "error_handler:") */
    TSNode label_node = ts_node_child_by_field_name(node, "label", 5);
    if (!ts_node_is_null(label_node)) {
        char label_name[SYMBOL_MAX_LENGTH];
        safe_extract_node_text(source_code, label_node, label_name, sizeof(label_name), filename);

        if (filter_should_index(filter, label_name)) {
            /* Index label definition */
            add_entry(result, label_name, line, CONTEXT_LABEL,
                    directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
        }
    }

    /* Process the statement after the label */
    process_children(node, source_code, directory, filename, result, filter);
}

static void handle_goto_statement(TSNode node, const char *source_code, const char *directory,
                                   const char *filename, ParseResult *result, SymbolFilter *filter,
                                   int line) {
    /* Extract target label from goto statement (e.g., "goto cleanup;") */
    TSNode label_node = ts_node_child_by_field_name(node, "label", 5);
    if (!ts_node_is_null(label_node)) {
        char label_name[SYMBOL_MAX_LENGTH];
        safe_extract_node_text(source_code, label_node, label_name, sizeof(label_name), filename);

        if (filter_should_index(filter, label_name)) {
            /* Index goto reference */
            add_entry(result, label_name, line, CONTEXT_GOTO,
                    directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
        }
    }
}

static void visit_node(TSNode node, const char *source_code, const char *directory,
                      const char *filename, ParseResult *result, SymbolFilter *filter) {
    TSSymbol node_sym = ts_node_symbol(node);
    TSPoint start_point = ts_node_start_point(node);
    int line = (int)(start_point.row + 1);

    if (g_debug) {
        debug("[visit_node] Line %d: node_type='%s'", line, ts_node_type(node));
    }

    /* Fast symbol-based dispatch - if-else chain with early returns */
    if (node_sym == c_symbols.function_definition) {
        handle_function_definition(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.declaration) {
        handle_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.struct_specifier || node_sym == c_symbols.union_specifier) {
        handle_struct_specifier(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.type_definition) {
        handle_type_definition(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.enum_specifier) {
        handle_enum_specifier(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.comment) {
        handle_comment(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.string_literal) {
        handle_string_literal(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.preproc_def) {
        handle_preproc_def(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.preproc_function_def) {
        handle_preproc_function_def(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.preproc_include) {
        handle_preproc_include(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.preproc_ifdef) {
        handle_preproc_ifdef(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.preproc_if || node_sym == c_symbols.preproc_elif || node_sym == c_symbols.preproc_else) {
        handle_preproc_if(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.expression_statement) {
        handle_expression_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.return_statement) {
        handle_return_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.if_statement) {
        handle_if_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.while_statement) {
        handle_while_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.do_statement) {
        handle_do_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.for_statement) {
        handle_for_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.switch_statement) {
        handle_switch_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.case_statement) {
        handle_case_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.labeled_statement) {
        handle_labeled_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == c_symbols.goto_statement) {
        handle_goto_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }

    /* No handler found, recursively process children */
    if (g_debug) {
        debug("[visit_node] Line %d: No handler for '%s', processing children", line, ts_node_type(node));
    }
    process_children(node, source_code, directory, filename, result, filter);
}

int parser_init(CParser *parser, SymbolFilter *filter) {
    parser->filter = filter;
    return 0;
}

int parser_parse_file(CParser *parser, const char *filepath, const char *project_root, ParseResult *result) {
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
    const TSLanguage *language = tree_sitter_c();
    if (!ts_parser_set_language(ts_parser, language)) {
        fprintf(stderr, "ERROR: Failed to set C language\n");
        free(source_code);
        ts_parser_delete(ts_parser);
        return -1;
    }

    /* Initialize symbol lookup table */
    init_c_symbols(language);

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

void parser_set_debug(CParser *parser, int debug) {
    parser->debug = debug;
    g_debug = debug;
}

void parser_free(CParser *parser) {
    /* No resources to free yet */
    (void)parser;
}
