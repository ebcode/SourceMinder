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
#include "go_language.h"
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

/* External Go language function from tree-sitter-go */
extern const TSLanguage *tree_sitter_go(void);

/* Global debug flag */
static int g_debug = 0;

/* Forward declarations */
static void visit_node(TSNode node, const char *source_code, const char *directory,
                      const char *filename, ParseResult *result, SymbolFilter *filter);

/* Node handler function pointer type */
typedef void (*NodeHandler)(TSNode node, const char *source_code,
                           const char *directory, const char *filename,
                           ParseResult *result, SymbolFilter *filter,
                           int line);

static void process_children(TSNode node, const char *source_code, const char *directory,
                            const char *filename, ParseResult *result, SymbolFilter *filter) {
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        visit_node(child, source_code, directory, filename, result, filter);
    }
}

/* Type extraction strategy classification
 *
 * Source: tree-sitter-go grammar (as of 2024)
 * Files: ./tree-sitter-go/grammar.js
 *        ./tree-sitter-go/src/node-types.json
 *
 * From grammar.js:
 *   _type: choice(_simple_type, parenthesized_type)
 *   _simple_type: choice(
 *     _type_identifier,    // aliased to type_identifier
 *     generic_type,
 *     qualified_type,
 *     pointer_type,
 *     struct_type,
 *     interface_type,
 *     array_type,
 *     slice_type,
 *     map_type,
 *     channel_type,
 *     function_type,
 *     union_type,          // Go 1.18+ generics
 *     negated_type         // Go 1.18+ generics (~int)
 *   )
 *   _type_identifier: alias(identifier, type_identifier)
 *
 * Additional types found in practice:
 *   - identifier: can appear directly in some contexts
 *   - field_identifier: for struct field types
 *   - implicit_length_array_type: [...] syntax
 *
 * Update this enum when the Go language grammar changes.
 */
typedef enum {
    TYPE_EXTRACT_SIMPLE,      /* Simple text extraction (type_identifier, identifier, field_identifier) */
    TYPE_EXTRACT_QUALIFIED,   /* Qualified type (pkg.Type) - simple text extraction */
    TYPE_EXTRACT_POINTER,     /* Pointer type - needs custom logic to add * prefix */
    TYPE_EXTRACT_COMPLEX,     /* Complex types - full text extraction (struct, map, slice, etc.) */
    TYPE_EXTRACT_RECURSE,     /* Needs recursion/filtering (expression_list, unary_expression, composite_literal) */
    TYPE_EXTRACT_SKIP,        /* Don't extract - return empty (call_expression, punctuation) */
    TYPE_NOT_A_TYPE           /* Not a recognized type node */
} TypeExtractionStrategy;

/* Symbol lookup table - pre-computed at startup */
static struct {
    /* Core identifiers */
    TSSymbol identifier;
    TSSymbol type_identifier;
    TSSymbol field_identifier;

    /* Type nodes */
    TSSymbol qualified_type;
    TSSymbol pointer_type;
    TSSymbol struct_type;
    TSSymbol interface_type;
    TSSymbol array_type;
    TSSymbol slice_type;
    TSSymbol implicit_length_array_type;
    TSSymbol map_type;
    TSSymbol channel_type;
    TSSymbol function_type;
    TSSymbol generic_type;
    TSSymbol union_type;
    TSSymbol negated_type;
    TSSymbol parenthesized_type;

    /* Expression nodes */
    TSSymbol expression_list;
    TSSymbol unary_expression;
    TSSymbol composite_literal;
    TSSymbol call_expression;
    TSSymbol index_expression;
    TSSymbol slice_expression;
    TSSymbol selector_expression;
    TSSymbol binary_expression;
    TSSymbol argument_list;
    TSSymbol literal_value;

    /* Handler dispatch nodes */
    TSSymbol package_clause;
    TSSymbol import_declaration;
    TSSymbol type_declaration;
    TSSymbol type_spec;
    TSSymbol type_alias;
    TSSymbol function_declaration;
    TSSymbol func_literal;
    TSSymbol method_declaration;
    TSSymbol method_spec;
    TSSymbol field_declaration;
    TSSymbol var_declaration;
    TSSymbol const_declaration;
    TSSymbol parameter_declaration;
    TSSymbol short_var_declaration;
    TSSymbol range_clause;
    TSSymbol defer_statement;
    TSSymbol go_statement;
    TSSymbol communication_case;
    TSSymbol send_statement;
    TSSymbol comment;
    TSSymbol interpreted_string_literal;

    /* Import-related nodes */
    TSSymbol import_spec;
    TSSymbol import_spec_list;

    /* Package/module nodes */
    TSSymbol package_identifier;

    /* Function parameter nodes */
    TSSymbol parameter_list;

    /* Structural nodes */
    TSSymbol block;

    /* Keywords (anonymous nodes) */
    TSSymbol keyword_func;
} go_symbols;

/* Initialize symbol lookup table - call once at startup */
static void init_go_symbols(const TSLanguage *language) {
    static int initialized = 0;
    if (initialized) return;
    initialized = 1;

    /* Core identifiers */
    go_symbols.identifier = ts_language_symbol_for_name(language, "identifier", 10, true);
    go_symbols.type_identifier = ts_language_symbol_for_name(language, "type_identifier", 15, true);
    go_symbols.field_identifier = ts_language_symbol_for_name(language, "field_identifier", 16, true);

    /* Type nodes */
    go_symbols.qualified_type = ts_language_symbol_for_name(language, "qualified_type", 14, true);
    go_symbols.pointer_type = ts_language_symbol_for_name(language, "pointer_type", 12, true);
    go_symbols.struct_type = ts_language_symbol_for_name(language, "struct_type", 11, true);
    go_symbols.interface_type = ts_language_symbol_for_name(language, "interface_type", 14, true);
    go_symbols.array_type = ts_language_symbol_for_name(language, "array_type", 10, true);
    go_symbols.slice_type = ts_language_symbol_for_name(language, "slice_type", 10, true);
    go_symbols.implicit_length_array_type = ts_language_symbol_for_name(language, "implicit_length_array_type", 26, true);
    go_symbols.map_type = ts_language_symbol_for_name(language, "map_type", 8, true);
    go_symbols.channel_type = ts_language_symbol_for_name(language, "channel_type", 12, true);
    go_symbols.function_type = ts_language_symbol_for_name(language, "function_type", 13, true);
    go_symbols.generic_type = ts_language_symbol_for_name(language, "generic_type", 12, true);
    go_symbols.union_type = ts_language_symbol_for_name(language, "union_type", 10, true);
    go_symbols.negated_type = ts_language_symbol_for_name(language, "negated_type", 12, true);
    go_symbols.parenthesized_type = ts_language_symbol_for_name(language, "parenthesized_type", 18, true);

    /* Expression nodes */
    go_symbols.expression_list = ts_language_symbol_for_name(language, "expression_list", 15, true);
    go_symbols.unary_expression = ts_language_symbol_for_name(language, "unary_expression", 16, true);
    go_symbols.composite_literal = ts_language_symbol_for_name(language, "composite_literal", 17, true);
    go_symbols.call_expression = ts_language_symbol_for_name(language, "call_expression", 15, true);
    go_symbols.index_expression = ts_language_symbol_for_name(language, "index_expression", 16, true);
    go_symbols.slice_expression = ts_language_symbol_for_name(language, "slice_expression", 16, true);
    go_symbols.selector_expression = ts_language_symbol_for_name(language, "selector_expression", 19, true);
    go_symbols.binary_expression = ts_language_symbol_for_name(language, "binary_expression", 17, true);
    go_symbols.argument_list = ts_language_symbol_for_name(language, "argument_list", 13, true);
    go_symbols.literal_value = ts_language_symbol_for_name(language, "literal_value", 13, true);

    /* Handler dispatch nodes */
    go_symbols.package_clause = ts_language_symbol_for_name(language, "package_clause", 14, true);
    go_symbols.import_declaration = ts_language_symbol_for_name(language, "import_declaration", 18, true);
    go_symbols.type_declaration = ts_language_symbol_for_name(language, "type_declaration", 16, true);
    go_symbols.type_spec = ts_language_symbol_for_name(language, "type_spec", 9, true);
    go_symbols.type_alias = ts_language_symbol_for_name(language, "type_alias", 10, true);
    go_symbols.function_declaration = ts_language_symbol_for_name(language, "function_declaration", 20, true);
    go_symbols.func_literal = ts_language_symbol_for_name(language, "func_literal", 12, true);
    go_symbols.method_declaration = ts_language_symbol_for_name(language, "method_declaration", 18, true);
    go_symbols.method_spec = ts_language_symbol_for_name(language, "method_spec", 11, true);
    go_symbols.field_declaration = ts_language_symbol_for_name(language, "field_declaration", 17, true);
    go_symbols.var_declaration = ts_language_symbol_for_name(language, "var_declaration", 15, true);
    go_symbols.const_declaration = ts_language_symbol_for_name(language, "const_declaration", 17, true);
    go_symbols.parameter_declaration = ts_language_symbol_for_name(language, "parameter_declaration", 21, true);
    go_symbols.short_var_declaration = ts_language_symbol_for_name(language, "short_var_declaration", 21, true);
    go_symbols.range_clause = ts_language_symbol_for_name(language, "range_clause", 12, true);
    go_symbols.defer_statement = ts_language_symbol_for_name(language, "defer_statement", 15, true);
    go_symbols.go_statement = ts_language_symbol_for_name(language, "go_statement", 12, true);
    go_symbols.communication_case = ts_language_symbol_for_name(language, "communication_case", 18, true);
    go_symbols.send_statement = ts_language_symbol_for_name(language, "send_statement", 14, true);
    go_symbols.comment = ts_language_symbol_for_name(language, "comment", 7, true);
    go_symbols.interpreted_string_literal = ts_language_symbol_for_name(language, "interpreted_string_literal", 26, true);

    /* Import-related nodes */
    go_symbols.import_spec = ts_language_symbol_for_name(language, "import_spec", 11, true);
    go_symbols.import_spec_list = ts_language_symbol_for_name(language, "import_spec_list", 16, true);

    /* Package/module nodes */
    go_symbols.package_identifier = ts_language_symbol_for_name(language, "package_identifier", 18, true);

    /* Function parameter nodes */
    go_symbols.parameter_list = ts_language_symbol_for_name(language, "parameter_list", 14, true);

    /* Structural nodes */
    go_symbols.block = ts_language_symbol_for_name(language, "block", 5, true);

    /* Keywords (anonymous nodes) */
    go_symbols.keyword_func = ts_language_symbol_for_name(language, "func", 4, false);
}

static TypeExtractionStrategy classify_type_node(TSNode node) {
    TSSymbol node_sym = ts_node_symbol(node);

    /* Simple types: just extract node text */
    if (node_sym == go_symbols.type_identifier ||
        node_sym == go_symbols.field_identifier ||
        node_sym == go_symbols.identifier) {
        return TYPE_EXTRACT_SIMPLE;
    }

    /* Qualified types: pkg.Type - extract full text */
    if (node_sym == go_symbols.qualified_type) {
        return TYPE_EXTRACT_QUALIFIED;
    }

    /* Pointer types: need to add * prefix */
    if (node_sym == go_symbols.pointer_type) {
        return TYPE_EXTRACT_POINTER;
    }

    /* Complex types: extract full text including nested structure */
    if (node_sym == go_symbols.struct_type ||
        node_sym == go_symbols.interface_type ||
        node_sym == go_symbols.array_type ||
        node_sym == go_symbols.slice_type ||
        node_sym == go_symbols.implicit_length_array_type ||
        node_sym == go_symbols.map_type ||
        node_sym == go_symbols.channel_type ||
        node_sym == go_symbols.function_type ||
        node_sym == go_symbols.generic_type ||
        node_sym == go_symbols.union_type ||
        node_sym == go_symbols.negated_type ||
        node_sym == go_symbols.parenthesized_type) {
        return TYPE_EXTRACT_COMPLEX;
    }

    /* Expression lists: recurse to skip commas */
    /* Unary expressions: handle & operator for pointer types */
    /* Composite literals: skip {...} body, extract type only */
    if (node_sym == go_symbols.expression_list ||
        node_sym == go_symbols.unary_expression ||
        node_sym == go_symbols.composite_literal) {
        return TYPE_EXTRACT_RECURSE;
    }

    /* Skip these entirely - not extractable as types */
    /* Call expressions: would need return type analysis */
    if (node_sym == go_symbols.call_expression) {
        return TYPE_EXTRACT_SKIP;
    }

    /* Punctuation tokens: keep as strcmp for single-character comparisons */
    const char *node_type = ts_node_type(node);
    if (strcmp(node_type, ",") == 0 ||
        strcmp(node_type, "(") == 0 ||
        strcmp(node_type, ")") == 0 ||
        strcmp(node_type, "[") == 0 ||
        strcmp(node_type, "]") == 0 ||
        strcmp(node_type, "{") == 0 ||
        strcmp(node_type, "}") == 0 ||
        strcmp(node_type, "*") == 0 ||
        strcmp(node_type, "&") == 0) {
        return TYPE_EXTRACT_SKIP;
    }

    return TYPE_NOT_A_TYPE;
}

/* Helper: Check if a node is a valid Go type node */
static bool is_go_type_node(TSNode node) {
    TypeExtractionStrategy strategy = classify_type_node(node);
    return strategy != TYPE_NOT_A_TYPE && strategy != TYPE_EXTRACT_SKIP;
}

/* Helper: Extract type from type node using classification strategy */
static void extract_type_from_node(TSNode type_node, const char *source_code, char *type_buffer, size_t type_size, const char *filename) {
    if (ts_node_is_null(type_node)) {
        type_buffer[0] = '\0';
        return;
    }

    TypeExtractionStrategy strategy = classify_type_node(type_node);
    TSSymbol node_sym = ts_node_symbol(type_node);

    switch (strategy) {
        case TYPE_EXTRACT_SKIP:
            /* Punctuation, call_expression, etc. - return empty */
            type_buffer[0] = '\0';
            return;

        case TYPE_EXTRACT_SIMPLE:
            /* type_identifier, field_identifier, identifier - just extract text */
            safe_extract_node_text(source_code, type_node, type_buffer, type_size, filename);
            return;

        case TYPE_EXTRACT_QUALIFIED:
            /* qualified_type (pkg.Type) - extract full text */
            safe_extract_node_text(source_code, type_node, type_buffer, type_size, filename);
            return;

        case TYPE_EXTRACT_POINTER: {
            /* pointer_type - extract with * prefix */
            uint32_t child_count = ts_node_child_count(type_node);
            for (uint32_t i = 0; i < child_count; i++) {
                TSNode child = ts_node_child(type_node, i);
                const char *child_type = ts_node_type(child);
                if (strcmp(child_type, "*") != 0) {
                    char inner_type[SYMBOL_MAX_LENGTH];
                    extract_type_from_node(child, source_code, inner_type, sizeof(inner_type), filename);
                    /* Ensure we have room for * prefix */
                    if (strlen(inner_type) < type_size - 1) {
                        snprintf(type_buffer, type_size, "*%s", inner_type);
                    } else {
                        strncpy(type_buffer, inner_type, type_size - 1);
                        type_buffer[type_size - 1] = '\0';
                    }
                    return;
                }
            }
            type_buffer[0] = '\0';
            return;
        }

        case TYPE_EXTRACT_COMPLEX: {
            /* For inline struct/interface types, use simple placeholder */
            if (node_sym == go_symbols.struct_type) {
                snprintf(type_buffer, type_size, "struct");
                return;
            }
            if (node_sym == go_symbols.interface_type) {
                snprintf(type_buffer, type_size, "interface");
                return;
            }

            /* For slice/array/map types containing inline structs, simplify them */
            if (node_sym == go_symbols.slice_type ||
                node_sym == go_symbols.array_type ||
                node_sym == go_symbols.implicit_length_array_type ||
                node_sym == go_symbols.map_type) {

                /* Check if this contains an inline struct/interface */
                uint32_t child_count = ts_node_child_count(type_node);
                for (uint32_t i = 0; i < child_count; i++) {
                    TSNode child = ts_node_child(type_node, i);
                    TSSymbol child_sym = ts_node_symbol(child);

                    if (child_sym == go_symbols.struct_type) {
                        if (node_sym == go_symbols.slice_type) {
                            snprintf(type_buffer, type_size, "[]struct");
                        } else if (node_sym == go_symbols.array_type ||
                                   node_sym == go_symbols.implicit_length_array_type) {
                            snprintf(type_buffer, type_size, "[]struct");
                        } else {
                            snprintf(type_buffer, type_size, "map");
                        }
                        return;
                    }

                    if (child_sym == go_symbols.interface_type) {
                        if (node_sym == go_symbols.slice_type) {
                            snprintf(type_buffer, type_size, "[]interface");
                        } else if (node_sym == go_symbols.array_type ||
                                   node_sym == go_symbols.implicit_length_array_type) {
                            snprintf(type_buffer, type_size, "[]interface");
                        } else {
                            snprintf(type_buffer, type_size, "map");
                        }
                        return;
                    }
                }
            }

            /* All other complex types - extract normally */
            uint32_t node_size = ts_node_end_byte(type_node) - ts_node_start_byte(type_node);
            if (node_size >= type_size) {
                /* Fallback for unexpectedly large types */
                snprintf(type_buffer, type_size, "<%s>", ts_node_type(type_node));
                return;
            }

            safe_extract_node_text(source_code, type_node, type_buffer, type_size, filename);
            return;
        }

        case TYPE_EXTRACT_RECURSE:
            /* expression_list, unary_expression, composite_literal - needs custom recursion */
            if (node_sym == go_symbols.expression_list) {
                /* Find first child expression, skip commas */
                uint32_t child_count = ts_node_child_count(type_node);
                for (uint32_t i = 0; i < child_count; i++) {
                    TSNode child = ts_node_child(type_node, i);
                    const char *child_type = ts_node_type(child);
                    if (strcmp(child_type, ",") != 0) {
                        extract_type_from_node(child, source_code, type_buffer, type_size, filename);
                        return;
                    }
                }
                type_buffer[0] = '\0';
                return;
            }

            if (node_sym == go_symbols.unary_expression) {
                /* Handle &Type or *Type */
                bool is_address_of = false;
                TSNode operand = {0};

                uint32_t child_count = ts_node_child_count(type_node);
                for (uint32_t i = 0; i < child_count; i++) {
                    TSNode child = ts_node_child(type_node, i);
                    const char *child_type = ts_node_type(child);

                    if (strcmp(child_type, "&") == 0) {
                        is_address_of = true;
                    } else if (strcmp(child_type, "&") != 0 && strcmp(child_type, "*") != 0) {
                        operand = child;
                    }
                }

                if (!ts_node_is_null(operand)) {
                    char inner_type[SYMBOL_MAX_LENGTH];
                    extract_type_from_node(operand, source_code, inner_type, sizeof(inner_type), filename);

                    /* If it's &, prepend * to make it a pointer type */
                    if (is_address_of && inner_type[0]) {
                        if (strlen(inner_type) < type_size - 1) {
                            snprintf(type_buffer, type_size, "*%s", inner_type);
                        } else {
                            strncpy(type_buffer, inner_type, type_size - 1);
                            type_buffer[type_size - 1] = '\0';
                        }
                    } else {
                        snprintf(type_buffer, type_size, "%s", inner_type);
                    }
                    return;
                }
                type_buffer[0] = '\0';
                return;
            }

            if (node_sym == go_symbols.composite_literal) {
                /* Extract just the type, skip {...} literal_value body */
                uint32_t child_count = ts_node_child_count(type_node);
                for (uint32_t i = 0; i < child_count; i++) {
                    TSNode child = ts_node_child(type_node, i);
                    TSSymbol child_sym = ts_node_symbol(child);
                    /* Skip literal_value - that's the {...} body */
                    if (child_sym == go_symbols.literal_value) {
                        continue;
                    }
                    /* Extract any type node we find */
                    if (is_go_type_node(child)) {
                        extract_type_from_node(child, source_code, type_buffer, type_size, filename);
                        return;
                    }
                }
                type_buffer[0] = '\0';
                return;
            }

            /* Fallthrough for unhandled TYPE_EXTRACT_RECURSE types */
            type_buffer[0] = '\0';
            return;

        case TYPE_NOT_A_TYPE:
            /* Unknown node type - this is a bug that should be fixed */
            {
                TSPoint start_point = ts_node_start_point(type_node);
                fprintf(stderr, "\n========== UNHANDLED NODE TYPE IN extract_type_from_node() ==========\n");
                fprintf(stderr, "File: %s\n", filename ? filename : "<unknown>");
                fprintf(stderr, "Location: line %u, column %u\n", start_point.row + 1, start_point.column + 1);
                fprintf(stderr, "Node type: %s\n", ts_node_type(type_node));
                fprintf(stderr, "Classification: TYPE_NOT_A_TYPE\n");
                fprintf(stderr, "\nThis node type is not classified in classify_type_node().\n");
                fprintf(stderr, "Please add it to the appropriate TypeExtractionStrategy category.\n");
                fprintf(stderr, "=======================================================================\n");
                exit(1);
            }
    }

    /* Should never reach here if classify_type_node() is complete */
    type_buffer[0] = '\0';
}

/* Helper: Check if a function name is a Go built-in */
static bool is_builtin_function(const char *name) {
    static const char *builtins[] = {
        "append", "cap", "close", "complex", "copy", "delete",
        "imag", "len", "make", "new", "panic", "print",
        "println", "real", "recover", NULL
    };

    for (int i = 0; builtins[i] != NULL; i++) {
        if (strcmp(name, builtins[i]) == 0) {
            return true;
        }
    }
    return false;
}

/* Helper: Determine scope based on first character (public/private used in lieu of exported/unexported to save space) */
static const char *get_scope_from_name(const char *name) {
    if (name[0] >= 'A' && name[0] <= 'Z') {
        return "public";
    } else if (name[0] >= 'a' && name[0] <= 'z') {
        return "private";
    }
    return NULL;  /* For non-alphabetic names like operators */
}

/* Helper: Extract package name by walking up the AST tree
 * In Go, package_clause is a top-level child of source_file
 */
static void extract_package_name(TSNode node, const char *source_code, char *package_buf, size_t buf_size, const char *filename) {
    package_buf[0] = '\0';  /* Default: empty package */

    TSNode current = node;

    /* Walk up to the root (source_file) */
    while (!ts_node_is_null(current)) {
        TSNode parent = ts_node_parent(current);

        /* When we reach source_file, look for package_clause among its children */
        if (ts_node_is_null(parent)) {
            /* We're at the root (source_file) */
            uint32_t child_count = ts_node_child_count(current);
            for (uint32_t i = 0; i < child_count; i++) {
                TSNode child = ts_node_child(current, i);
                const char *child_type = ts_node_type(child);

                if (strcmp(child_type, "package_clause") == 0) {
                    /* Find package_identifier within package_clause */
                    uint32_t pkg_child_count = ts_node_child_count(child);
                    for (uint32_t j = 0; j < pkg_child_count; j++) {
                        TSNode pkg_child = ts_node_child(child, j);
                        const char *pkg_child_type = ts_node_type(pkg_child);

                        if (strcmp(pkg_child_type, "package_identifier") == 0) {
                            safe_extract_node_text(source_code, pkg_child, package_buf, buf_size, filename);
                            return;
                        }
                    }
                }
            }
            return;  /* No package found */
        }

        current = parent;
    }
}

/* Helper: Extract package and store in provided buffer
 * Returns the package buffer pointer for convenience
 */
static char* get_package(TSNode node, const char *source_code, char *buf, size_t size, const char *filename) {
    extract_package_name(node, source_code, buf, size, filename);
    return buf;
}

/* Helper: Infer type from RHS expression in short variable declarations */
static void infer_type_from_expression(TSNode expr_node, const char *source_code, char *type_buffer, size_t type_size, const char *filename) {
    if (ts_node_is_null(expr_node)) {
        type_buffer[0] = '\0';
        return;
    }

    TSSymbol node_sym = ts_node_symbol(expr_node);

    /* Unary expression: handle &Type{...} */
    if (node_sym == go_symbols.unary_expression) {
        /* Look for composite_literal child and recurse */
        uint32_t child_count = ts_node_child_count(expr_node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(expr_node, i);
            TSSymbol child_sym = ts_node_symbol(child);
            if (child_sym == go_symbols.composite_literal) {
                infer_type_from_expression(child, source_code, type_buffer, type_size, filename);
                return;
            }
        }
        /* Not a composite literal unary expression */
        type_buffer[0] = '\0';
        return;
    }

    /* Composite literal: extract explicit type */
    if (node_sym == go_symbols.composite_literal) {
        uint32_t child_count = ts_node_child_count(expr_node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(expr_node, i);
            TSSymbol child_sym = ts_node_symbol(child);

            /* Skip literal_value - that's the {...} body, not the type */
            if (child_sym == go_symbols.literal_value) {
                continue;
            }

            /* Look for type nodes (qualified_type, pointer_type come BEFORE struct_type) */
            if (child_sym == go_symbols.qualified_type ||
                child_sym == go_symbols.pointer_type ||
                child_sym == go_symbols.type_identifier ||
                child_sym == go_symbols.slice_type ||
                child_sym == go_symbols.map_type ||
                child_sym == go_symbols.array_type ||
                child_sym == go_symbols.struct_type) {
                extract_type_from_node(child, source_code, type_buffer, type_size, filename);
                return;
            }
        }
        /* No explicit type found in composite literal */
        type_buffer[0] = '\0';
        return;
    }

    /* Cannot infer type from these expression types */
    if (node_sym == go_symbols.identifier ||
        node_sym == go_symbols.index_expression ||
        node_sym == go_symbols.slice_expression ||
        node_sym == go_symbols.selector_expression ||
        node_sym == go_symbols.call_expression ||
        node_sym == go_symbols.binary_expression) {
        type_buffer[0] = '\0';
        return;
    }

    /* Default: cannot infer */
    type_buffer[0] = '\0';
}

/* Handler: package_clause */
static void handle_package_clause(TSNode node, const char *source_code, const char *directory,
                                  const char *filename, ParseResult *result, SymbolFilter *filter,
                                  int line) {
    /* Find package_identifier child */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "package_identifier") == 0) {
            char pkg_name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, pkg_name, sizeof(pkg_name), filename);

            if (pkg_name[0] && filter_should_index(filter, pkg_name)) {
                add_entry(result, pkg_name, line, CONTEXT_NAMESPACE,
                         directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
            }
            break;
        }
    }
}

/* Helper: Process individual import_spec node */
static void handle_import_spec(TSNode node, const char *source_code, const char *directory,
                               const char *filename, ParseResult *result, SymbolFilter *filter,
                               int line) {
    (void)line; /* Unused - we extract line from node instead */

    TSNode alias_node = {0};
    TSNode path_node = {0};
    bool is_blank = false;
    bool is_dot = false;
    bool has_alias = false;

    /* Get actual line number from this import_spec node */
    TSPoint start = ts_node_start_point(node);
    int actual_line = (int)start.row + 1;

    /* Scan children to identify import type */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "package_identifier") == 0) {
            alias_node = child;
            has_alias = true;
        } else if (strcmp(child_type, "blank_identifier") == 0) {
            is_blank = true;
        } else if (strcmp(child_type, "dot") == 0) {
            is_dot = true;
        } else if (strcmp(child_type, "interpreted_string_literal") == 0) {
            path_node = child;
        }
    }

    /* Extract import path */
    if (ts_node_is_null(path_node)) {
        return;
    }

    char import_path[SYMBOL_MAX_LENGTH];
    safe_extract_node_text(source_code, path_node, import_path, sizeof(import_path), filename);

    /* Remove quotes */
    if (import_path[0] != '"' || strlen(import_path) <= 2) {
        return;
    }
    import_path[strlen(import_path) - 1] = '\0';
    char *path = import_path + 1;

    if (!path[0]) {
        return;
    }

    /* Determine symbol and metadata */
    char symbol[SYMBOL_MAX_LENGTH];
    const char *clue = NULL;
    const char *type_info = NULL;

    if (has_alias) {
        /* Aliased import: symbol=alias, type=original_path, clue=alias */
        safe_extract_node_text(source_code, alias_node, symbol, sizeof(symbol), filename);
        type_info = path;
        clue = "alias";
    } else if (is_blank) {
        /* Blank import: symbol=path, clue=blank */
        snprintf(symbol, sizeof(symbol), "%s", path);
        clue = "blank";
    } else if (is_dot) {
        /* Dot import: symbol=path, clue=dot */
        snprintf(symbol, sizeof(symbol), "%s", path);
        clue = "dot";
    } else {
        /* Regular import: symbol=path */
        snprintf(symbol, sizeof(symbol), "%s", path);
    }

    /* Index the import */
    if (filter_should_index(filter, symbol)) {
        char package_buf[SYMBOL_MAX_LENGTH];
        get_package(node, source_code, package_buf, sizeof(package_buf), filename);

        ExtColumns ext = {
            .parent = NULL,
            .scope = NULL,
            .modifier = NULL,
            .clue = clue,
            .namespace = package_buf[0] ? package_buf : NULL,
            .type = type_info
        };
        add_entry(result, symbol, actual_line, CONTEXT_IMPORT,
                 directory, filename, NULL, &ext);
    }
}

/* Handler: import_declaration */
static void handle_import_declaration(TSNode node, const char *source_code, const char *directory,
                                      const char *filename, ParseResult *result, SymbolFilter *filter,
                                      int line) {
    /* Visit children to find import_spec nodes (directly or inside import_spec_list) */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == go_symbols.import_spec) {
            /* Direct import_spec (single import without parentheses) */
            handle_import_spec(child, source_code, directory, filename, result, filter, line);
        } else if (child_sym == go_symbols.import_spec_list) {
            /* Process all import_spec children inside the list */
            uint32_t spec_list_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < spec_list_count; j++) {
                TSNode spec_child = ts_node_child(child, j);
                TSSymbol spec_sym = ts_node_symbol(spec_child);
                if (spec_sym == go_symbols.import_spec) {
                    handle_import_spec(spec_child, source_code, directory, filename, result, filter, line);
                }
            }
        }
    }
}

/* Handler: function_declaration */
static void handle_function_declaration(TSNode node, const char *source_code, const char *directory,
                                        const char *filename, ParseResult *result, SymbolFilter *filter,
                                        int line) {
    /* Functions have: name, parameters, optional return type */
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    TSNode params_node = {0};
    TSNode return_node = {0};

    /* Find parameters and return type */
    uint32_t child_count = ts_node_child_count(node);
    int param_list_count = 0;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == go_symbols.parameter_list) {
            param_list_count++;
            if (param_list_count == 1) {
                params_node = child; /* First parameter_list is parameters */
            } else if (param_list_count == 2) {
                return_node = child; /* Second is return types */
            }
        } else if (!ts_node_is_null(params_node) && ts_node_is_null(return_node) &&
                   child_sym != go_symbols.keyword_func && child_sym != go_symbols.block) {
            /* Single return type (not wrapped in parameter_list) */
            return_node = child;
        }
    }

    if (!ts_node_is_null(name_node)) {
        char func_name[SYMBOL_MAX_LENGTH];
        char return_type[SYMBOL_MAX_LENGTH] = "";
        char package_buf[SYMBOL_MAX_LENGTH];

        safe_extract_node_text(source_code, name_node, func_name, sizeof(func_name), filename);
        get_package(node, source_code, package_buf, sizeof(package_buf), filename);

        /* Extract return type */
        if (!ts_node_is_null(return_node)) {
            TSSymbol return_sym = ts_node_symbol(return_node);
            if (return_sym == go_symbols.parameter_list) {
                /* Multiple return types */
                char temp_types[SYMBOL_MAX_LENGTH * 2] = "(";
                bool first = true;
                uint32_t return_children = ts_node_child_count(return_node);
                for (uint32_t i = 0; i < return_children; i++) {
                    TSNode return_child = ts_node_child(return_node, i);
                    TSSymbol return_child_sym = ts_node_symbol(return_child);
                    if (return_child_sym == go_symbols.parameter_declaration) {
                        char single_type[SYMBOL_MAX_LENGTH];
                        /* Extract type from parameter_declaration */
                        uint32_t decl_children = ts_node_child_count(return_child);
                        for (uint32_t j = 0; j < decl_children; j++) {
                            TSNode type_child = ts_node_child(return_child, j);
                            extract_type_from_node(type_child, source_code, single_type, sizeof(single_type), filename);
                            if (single_type[0]) {
                                if (!first) strncat(temp_types, ", ", sizeof(temp_types) - strlen(temp_types) - 1);
                                strncat(temp_types, single_type, sizeof(temp_types) - strlen(temp_types) - 1);
                                first = false;
                                break;
                            }
                        }
                    }
                }
                strncat(temp_types, ")", sizeof(temp_types) - strlen(temp_types) - 1);
                snprintf(return_type, sizeof(return_type), "%s", temp_types);
            } else {
                /* Single return type */
                extract_type_from_node(return_node, source_code, return_type, sizeof(return_type), filename);
            }
        }

        if (func_name[0] && filter_should_index(filter, func_name)) {
            /* Extract source location for full function definition */
            char location[128];
            format_source_location(node, location, sizeof(location));

            ExtColumns ext = {
                .parent = NULL,
                .scope = get_scope_from_name(func_name),
                .modifier = NULL,
                .clue = NULL,
                .namespace = package_buf[0] ? package_buf : NULL,
                .type = return_type[0] ? return_type : NULL,
                .definition = "1"
            };
            add_entry(result, func_name, line, CONTEXT_FUNCTION,
                     directory, filename, location, &ext);
        }

        /* Process parameters */
        if (!ts_node_is_null(params_node)) {
            process_children(params_node, source_code, directory, filename, result, filter);
        }
    }

    /* Process body */
    TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body_node)) {
        process_children(body_node, source_code, directory, filename, result, filter);
    }
}

/* Handler: call_expression */
static void handle_call_expression(TSNode node, const char *source_code, const char *directory,
                                   const char *filename, ParseResult *result, SymbolFilter *filter,
                                   int line) {
    TSNode function_node = ts_node_child_by_field_name(node, "function", 8);
    if (ts_node_is_null(function_node)) {
        /* Process arguments anyway */
        TSNode args_node = ts_node_child_by_field_name(node, "arguments", 9);
        if (!ts_node_is_null(args_node)) {
            process_children(args_node, source_code, directory, filename, result, filter);
        }
        return;
    }

    const char *func_type = ts_node_type(function_node);

    if (strcmp(func_type, "selector_expression") == 0) {
        /* Handle method call: obj.method() */
        TSNode field_node = ts_node_child_by_field_name(function_node, "field", 5);
        TSNode operand_node = ts_node_child_by_field_name(function_node, "operand", 7);

        if (!ts_node_is_null(field_node) && !ts_node_is_null(operand_node)) {
            char method_name[SYMBOL_MAX_LENGTH];
            char parent_name[SYMBOL_MAX_LENGTH] = "";

            safe_extract_node_text(source_code, field_node, method_name, sizeof(method_name), filename);

            /* Only extract parent if it's a simple expression (identifier or selector)
             * Skip complex expressions like call_expression which can be huge */
            const char *operand_type = ts_node_type(operand_node);
            if (strcmp(operand_type, "identifier") == 0 ||
                strcmp(operand_type, "selector_expression") == 0) {
                /* Check size before extracting */
                uint32_t operand_size = ts_node_end_byte(operand_node) - ts_node_start_byte(operand_node);
                if (operand_size < sizeof(parent_name)) {
                    safe_extract_node_text(source_code, operand_node, parent_name, sizeof(parent_name), filename);
                }
            }

            if (method_name[0] && filter_should_index(filter, method_name)) {
                ExtColumns ext = {
                    .parent = parent_name[0] ? parent_name : NULL,
                    .scope = NULL,
                    .modifier = NULL,
                    .clue = NULL,
                    .namespace = NULL,
                    .type = NULL
                };
                add_entry(result, method_name, line, CONTEXT_CALL,
                         directory, filename, NULL, &ext);
            }
        }
    } else if (strcmp(func_type, "identifier") == 0) {
        /* Handle function calls, both user-defined and built-in: foo(), fmt.Println */
        char func_name[SYMBOL_MAX_LENGTH];
        safe_extract_node_text(source_code, function_node, func_name, sizeof(func_name), filename);

        if (func_name[0] && filter_should_index(filter, func_name)) {
            if (is_builtin_function(func_name)) {
                ExtColumns ext = {
                    .parent = NULL,
                    .scope = NULL,
                    .modifier = NULL,
                    .clue = "built-in",
                    .namespace = NULL,
                    .type = NULL
                };
                add_entry(result, func_name, line, CONTEXT_CALL,
                         directory, filename, NULL, &ext);
            } else {
                add_entry(result, func_name, line, CONTEXT_CALL,
                         directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
            }
        }
    }

    /* Process arguments */
    TSNode args_node = ts_node_child_by_field_name(node, "arguments", 9);
    if (!ts_node_is_null(args_node)) {
        process_children(args_node, source_code, directory, filename, result, filter);
    }
}

/* Handler: comment */
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

                    /* Clean the word */
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

/* Handler: interpreted_string_literal */
static void handle_string_literal(TSNode node, const char *source_code, const char *directory,
                                  const char *filename, ParseResult *result, SymbolFilter *filter,
                                  int line) {
    char string_text[COMMENT_TEXT_BUFFER];
    safe_extract_node_text(source_code, node, string_text, sizeof(string_text), filename);

    /* Remove quotes */
    size_t len = strlen(string_text);
    if (len < 2 || string_text[0] != '"') return;

    char content[COMMENT_TEXT_BUFFER];
    strncpy(content, string_text + 1, sizeof(content) - 1);
    content[sizeof(content) - 1] = '\0';
    if (strlen(content) > 0) {
        content[strlen(content) - 1] = '\0'; /* Remove closing quote */
    }

    /* Extract words from string content */
    char word[CLEANED_WORD_BUFFER];
    char cleaned[CLEANED_WORD_BUFFER];
    char *word_start = content;

    for (char *p = content; ; p++) {
        if (*p == '\0' || isspace(*p)) {
            if (p > word_start) {
                size_t word_len = (size_t)(p - word_start);
                if (word_len < sizeof(word)) {
                    snprintf(word, sizeof(word), "%.*s", (int)word_len, word_start);

                    /* Clean the word */
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

/* Handler: type_declaration */
static void handle_type_declaration(TSNode node, const char *source_code, const char *directory,
                                     const char *filename, ParseResult *result, SymbolFilter *filter,
                                     int line) {
    (void)line;  /* Unused - child handlers will get their own line numbers */
    /* Process all children - let dispatch handle type_spec, type_alias, etc. */
    process_children(node, source_code, directory, filename, result, filter);
}

/* Handler: type_spec */
static void handle_type_spec(TSNode node, const char *source_code, const char *directory,
                              const char *filename, ParseResult *result, SymbolFilter *filter,
                              int line) {
    /* Get type name (first type_identifier in type_spec) */
    TSNode name_node = ts_node_child(node, 0);
    if (!ts_node_is_null(name_node) && strcmp(ts_node_type(name_node), "type_identifier") == 0) {
        char type_name[SYMBOL_MAX_LENGTH];
        char package_buf[SYMBOL_MAX_LENGTH];

        safe_extract_node_text(source_code, name_node, type_name, sizeof(type_name), filename);
        get_package(node, source_code, package_buf, sizeof(package_buf), filename);

        if (type_name[0] && filter_should_index(filter, type_name)) {
            ExtColumns ext = {
                .parent = NULL,
                .scope = get_scope_from_name(type_name),
                .modifier = NULL,
                .clue = NULL,
                .namespace = package_buf[0] ? package_buf : NULL,
                .type = NULL,
                .definition = "1"
            };
            add_entry(result, type_name, line, CONTEXT_TYPE,
                     directory, filename, NULL, &ext);
        }

        /* Process the type definition (struct_type, interface_type, etc.) */
        TSNode type_def = ts_node_child(node, 1);
        if (!ts_node_is_null(type_def)) {
            const char *type_def_type = ts_node_type(type_def);
            if (strcmp(type_def_type, "struct_type") == 0) {
                /* Process struct fields */
                process_children(type_def, source_code, directory, filename, result, filter);
            } else if (strcmp(type_def_type, "interface_type") == 0) {
                /* Process interface methods */
                process_children(type_def, source_code, directory, filename, result, filter);
            }
        }
    }
}

/* Handler: type_alias */
static void handle_type_alias(TSNode node, const char *source_code, const char *directory,
                               const char *filename, ParseResult *result, SymbolFilter *filter,
                               int line) {
    /* Type alias: type MyString = string
     * Structure: type_alias
     *   type_identifier (alias name)
     *   =
     *   type expression (underlying type)
     */
    TSNode name_node = {0};
    TSNode type_node = {0};

    uint32_t child_count = ts_node_child_count(node);
    bool found_equals = false;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "type_identifier") == 0 && ts_node_is_null(name_node)) {
            name_node = child;  /* First type_identifier is the alias name */
        } else if (strcmp(child_type, "=") == 0) {
            found_equals = true;
        } else if (found_equals && ts_node_is_null(type_node)) {
            /* First node after = is the underlying type */
            type_node = child;
        }
    }

    if (!ts_node_is_null(name_node)) {
        char alias_name[SYMBOL_MAX_LENGTH];
        char underlying_type[SYMBOL_MAX_LENGTH] = "";
        char package_buf[SYMBOL_MAX_LENGTH];

        safe_extract_node_text(source_code, name_node, alias_name, sizeof(alias_name), filename);
        get_package(node, source_code, package_buf, sizeof(package_buf), filename);

        if (!ts_node_is_null(type_node)) {
            extract_type_from_node(type_node, source_code, underlying_type, sizeof(underlying_type), filename);
        }

        if (alias_name[0] && filter_should_index(filter, alias_name)) {
            ExtColumns ext = {
                .parent = NULL,
                .scope = get_scope_from_name(alias_name),
                .modifier = NULL,
                .clue = "alias",
                .namespace = package_buf[0] ? package_buf : NULL,
                .type = underlying_type[0] ? underlying_type : NULL,
                .definition = "1"
            };
            add_entry(result, alias_name, line, CONTEXT_TYPE,
                     directory, filename, NULL, &ext);
        }
    }
}

/* Handler: field_declaration */
static void handle_field_declaration(TSNode node, const char *source_code, const char *directory,
                                      const char *filename, ParseResult *result, SymbolFilter *filter,
                                      int line) {
    /* Extract field name and type
     * Regular field: field_identifier + type
     * Embedded field: NO field_identifier, type comes first
     */
    TSNode name_node = {0};
    TSNode type_node = {0};
    bool is_embedded = false;

    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == go_symbols.field_identifier) {
            name_node = child;
        } else if (ts_node_is_null(name_node) == false && ts_node_is_null(type_node)) {
            /* First type node after name is the type - use helper to verify it's a type */
            if (is_go_type_node(child)) {
                type_node = child;
            }
        } else if (ts_node_is_null(name_node) && ts_node_is_null(type_node)) {
            /* No field_identifier yet - check if this is a type (embedded field) */
            /* Keep strcmp for "*" punctuation (single-character) */
            if (is_go_type_node(child) || strcmp(ts_node_type(child), "*") == 0) {
                /* This is an embedded field - type comes first */
                type_node = child;
                is_embedded = true;
            }
        }
    }

    char package_buf[SYMBOL_MAX_LENGTH];
    get_package(node, source_code, package_buf, sizeof(package_buf), filename);

    if (!ts_node_is_null(name_node)) {
        /* Regular field with explicit name */
        char field_name[SYMBOL_MAX_LENGTH];
        char field_type[SYMBOL_MAX_LENGTH] = "";

        safe_extract_node_text(source_code, name_node, field_name, sizeof(field_name), filename);

        if (!ts_node_is_null(type_node)) {
            const char *type_node_type = ts_node_type(type_node);

            /* For inline struct/interface types, use a simple placeholder
             * and recursively index the nested fields */
            if (strcmp(type_node_type, "struct_type") == 0) {
                snprintf(field_type, sizeof(field_type), "struct");
                /* Recursively process inline struct fields */
                process_children(type_node, source_code, directory, filename, result, filter);
            } else if (strcmp(type_node_type, "interface_type") == 0) {
                snprintf(field_type, sizeof(field_type), "interface");
                /* Recursively process inline interface methods */
                process_children(type_node, source_code, directory, filename, result, filter);
            } else {
                /* For all other types, extract normally */
                extract_type_from_node(type_node, source_code, field_type, sizeof(field_type), filename);
            }
        }

        if (field_name[0] && filter_should_index(filter, field_name)) {
            ExtColumns ext = {
                .parent = NULL,
                .scope = get_scope_from_name(field_name),
                .modifier = NULL,
                .clue = NULL,
                .namespace = package_buf[0] ? package_buf : NULL,
                .type = field_type[0] ? field_type : NULL
            };
            add_entry(result, field_name, line, CONTEXT_PROPERTY,
                     directory, filename, NULL, &ext);
        }
    } else if (is_embedded && !ts_node_is_null(type_node)) {
        /* Embedded field - extract type name to use as field name */
        char embedded_type[SYMBOL_MAX_LENGTH];
        char embedded_name[SYMBOL_MAX_LENGTH] = "";

        extract_type_from_node(type_node, source_code, embedded_type, sizeof(embedded_type), filename);

        /* Extract just the type name (last part after . if qualified) */
        const char *last_dot = strrchr(embedded_type, '.');
        if (last_dot) {
            snprintf(embedded_name, sizeof(embedded_name), "%s", last_dot + 1);
        } else {
            /* Remove pointer prefix if present */
            const char *type_start = embedded_type;
            if (embedded_type[0] == '*') {
                type_start = embedded_type + 1;
            }
            snprintf(embedded_name, sizeof(embedded_name), "%s", type_start);
        }

        if (embedded_name[0] && filter_should_index(filter, embedded_name)) {
            ExtColumns ext = {
                .parent = NULL,
                .scope = get_scope_from_name(embedded_name),
                .modifier = NULL,
                .clue = "embedded",
                .namespace = package_buf[0] ? package_buf : NULL,
                .type = embedded_type
            };
            add_entry(result, embedded_name, line, CONTEXT_PROPERTY,
                     directory, filename, NULL, &ext);
        }
    }
}

/* Handler: method_declaration */
static void handle_method_declaration(TSNode node, const char *source_code, const char *directory,
                                       const char *filename, ParseResult *result, SymbolFilter *filter,
                                       int line) {
    /* Method has: receiver (parameter_list), name (field_identifier), parameters, optional return */
    TSNode receiver_node = {0};
    TSNode name_node = {0};
    TSNode params_node = {0};
    TSNode return_node = {0};

    uint32_t child_count = ts_node_child_count(node);
    int param_list_count = 0;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "parameter_list") == 0) {
            param_list_count++;
            if (param_list_count == 1) {
                receiver_node = child; /* First parameter_list is receiver */
            } else if (param_list_count == 2) {
                params_node = child; /* Second is actual parameters */
            } else if (param_list_count == 3) {
                return_node = child; /* Third is return types */
            }
        } else if (strcmp(child_type, "field_identifier") == 0) {
            name_node = child;
        } else if (!ts_node_is_null(params_node) && ts_node_is_null(return_node) &&
                   strcmp(child_type, "func") != 0 && strcmp(child_type, "block") != 0) {
            /* Single return type (not wrapped in parameter_list) */
            return_node = child;
        }
    }

    /* Extract method name */
    if (!ts_node_is_null(name_node)) {
        char method_name[SYMBOL_MAX_LENGTH];
        char receiver_type[SYMBOL_MAX_LENGTH] = "";
        char return_type[SYMBOL_MAX_LENGTH] = "";
        char package_buf[SYMBOL_MAX_LENGTH];

        safe_extract_node_text(source_code, name_node, method_name, sizeof(method_name), filename);
        get_package(node, source_code, package_buf, sizeof(package_buf), filename);

        /* Extract receiver type */
        if (!ts_node_is_null(receiver_node)) {
            uint32_t receiver_children = ts_node_child_count(receiver_node);
            for (uint32_t i = 0; i < receiver_children; i++) {
                TSNode param_decl = ts_node_child(receiver_node, i);
                if (strcmp(ts_node_type(param_decl), "parameter_declaration") == 0) {
                    /* Find the type in parameter_declaration */
                    uint32_t decl_children = ts_node_child_count(param_decl);
                    for (uint32_t j = 0; j < decl_children; j++) {
                        TSNode type_child = ts_node_child(param_decl, j);
                        const char *type_child_type = ts_node_type(type_child);
                        if (strcmp(type_child_type, "identifier") != 0) {
                            extract_type_from_node(type_child, source_code, receiver_type, sizeof(receiver_type), filename);
                            break;
                        }
                    }
                    break;
                }
            }
        }

        /* Extract return type */
        if (!ts_node_is_null(return_node)) {
            TSSymbol return_sym = ts_node_symbol(return_node);
            if (return_sym == go_symbols.parameter_list) {
                /* Multiple return types */
                char temp_types[SYMBOL_MAX_LENGTH * 2] = "(";
                bool first = true;
                uint32_t return_children = ts_node_child_count(return_node);
                for (uint32_t i = 0; i < return_children; i++) {
                    TSNode return_child = ts_node_child(return_node, i);
                    TSSymbol return_child_sym = ts_node_symbol(return_child);
                    if (return_child_sym == go_symbols.parameter_declaration) {
                        char single_type[SYMBOL_MAX_LENGTH];
                        /* Extract type from parameter_declaration */
                        uint32_t decl_children = ts_node_child_count(return_child);
                        for (uint32_t j = 0; j < decl_children; j++) {
                            TSNode type_child = ts_node_child(return_child, j);
                            extract_type_from_node(type_child, source_code, single_type, sizeof(single_type), filename);
                            if (single_type[0]) {
                                if (!first) strncat(temp_types, ", ", sizeof(temp_types) - strlen(temp_types) - 1);
                                strncat(temp_types, single_type, sizeof(temp_types) - strlen(temp_types) - 1);
                                first = false;
                                break;
                            }
                        }
                    }
                }
                strncat(temp_types, ")", sizeof(temp_types) - strlen(temp_types) - 1);
                snprintf(return_type, sizeof(return_type), "%s", temp_types);
            } else {
                /* Single return type */
                extract_type_from_node(return_node, source_code, return_type, sizeof(return_type), filename);
            }
        }

        if (method_name[0] && filter_should_index(filter, method_name)) {
            /* Extract source location for full method definition */
            char location[128];
            format_source_location(node, location, sizeof(location));

            ExtColumns ext = {
                .parent = receiver_type[0] ? receiver_type : NULL,
                .scope = get_scope_from_name(method_name),
                .modifier = NULL,
                .clue = NULL,
                .namespace = package_buf[0] ? package_buf : NULL,
                .type = return_type[0] ? return_type : NULL,
                .definition = "1"
            };
            add_entry(result, method_name, line, CONTEXT_FUNCTION,
                     directory, filename, location, &ext);
        }

        /* Process parameters */
        if (!ts_node_is_null(params_node)) {
            process_children(params_node, source_code, directory, filename, result, filter);
        }
    }

    /* Process method body */
    TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body_node)) {
        process_children(body_node, source_code, directory, filename, result, filter);
    }
}

/* Handler: method_spec (interface methods) */
static void handle_method_spec(TSNode node, const char *source_code, const char *directory,
                                const char *filename, ParseResult *result, SymbolFilter *filter,
                                int line) {
    /* Interface method: Read(p []byte) (n int, err error)
     * Structure: method_spec
     *   field_identifier (method name)
     *   parameter_list (parameters)
     *   parameter_list OR type (return types)
     */
    TSNode name_node = {0};
    TSNode params_node = {0};
    TSNode return_node = {0};

    uint32_t child_count = ts_node_child_count(node);
    int param_list_count = 0;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "field_identifier") == 0) {
            name_node = child;
        } else if (strcmp(child_type, "parameter_list") == 0) {
            param_list_count++;
            if (param_list_count == 1) {
                params_node = child;  /* First parameter_list is parameters */
            } else if (param_list_count == 2) {
                return_node = child;  /* Second is return types */
            }
        } else if (!ts_node_is_null(params_node) && ts_node_is_null(return_node)) {
            /* Single return type (not wrapped in parameter_list) */
            return_node = child;
        }
    }

    if (!ts_node_is_null(name_node)) {
        char method_name[SYMBOL_MAX_LENGTH];
        char return_type[SYMBOL_MAX_LENGTH] = "";
        char package_buf[SYMBOL_MAX_LENGTH];

        safe_extract_node_text(source_code, name_node, method_name, sizeof(method_name), filename);
        get_package(node, source_code, package_buf, sizeof(package_buf), filename);

        /* Extract return type (similar to function_declaration logic) */
        if (!ts_node_is_null(return_node)) {
            const char *return_node_type = ts_node_type(return_node);
            if (strcmp(return_node_type, "parameter_list") == 0) {
                /* Multiple return types */
                char temp_types[SYMBOL_MAX_LENGTH * 2] = "(";
                bool first = true;
                uint32_t return_children = ts_node_child_count(return_node);
                for (uint32_t i = 0; i < return_children; i++) {
                    TSNode return_child = ts_node_child(return_node, i);
                    if (strcmp(ts_node_type(return_child), "parameter_declaration") == 0) {
                        char single_type[SYMBOL_MAX_LENGTH];
                        /* Extract type from parameter_declaration */
                        uint32_t decl_children = ts_node_child_count(return_child);
                        for (uint32_t j = 0; j < decl_children; j++) {
                            TSNode type_child = ts_node_child(return_child, j);
                            extract_type_from_node(type_child, source_code, single_type, sizeof(single_type), filename);
                            if (single_type[0]) {
                                if (!first) strncat(temp_types, ", ", sizeof(temp_types) - strlen(temp_types) - 1);
                                strncat(temp_types, single_type, sizeof(temp_types) - strlen(temp_types) - 1);
                                first = false;
                                break;
                            }
                        }
                    }
                }
                strncat(temp_types, ")", sizeof(temp_types) - strlen(temp_types) - 1);
                snprintf(return_type, sizeof(return_type), "%s", temp_types);
            } else {
                /* Single return type */
                extract_type_from_node(return_node, source_code, return_type, sizeof(return_type), filename);
            }
        }

        if (method_name[0] && filter_should_index(filter, method_name)) {
            ExtColumns ext = {
                .parent = NULL,  /* TODO: Could extract parent interface name if needed */
                .scope = get_scope_from_name(method_name),
                .modifier = NULL,
                .clue = "interface",
                .namespace = package_buf[0] ? package_buf : NULL,
                .type = return_type[0] ? return_type : NULL,
                .definition = "1"
            };
            add_entry(result, method_name, line, CONTEXT_FUNCTION,
                     directory, filename, NULL, &ext);
        }
    }
}

/* Handler: var_declaration */
static void handle_var_declaration(TSNode node, const char *source_code, const char *directory,
                                    const char *filename, ParseResult *result, SymbolFilter *filter,
                                    int line) {
    /* Find var_spec children */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "var_spec") == 0) {
            /* Extract variable name and type */
            TSNode name_node = {0};
            TSNode type_node = {0};

            uint32_t spec_children = ts_node_child_count(child);
            for (uint32_t j = 0; j < spec_children; j++) {
                TSNode spec_child = ts_node_child(child, j);
                const char *spec_child_type = ts_node_type(spec_child);

                if (strcmp(spec_child_type, "identifier") == 0 && ts_node_is_null(name_node)) {
                    name_node = spec_child;
                } else if (!ts_node_is_null(name_node) && ts_node_is_null(type_node) &&
                           strcmp(spec_child_type, "=") != 0) {
                    /* Type comes after name, before = */
                    type_node = spec_child;
                }
            }

            if (!ts_node_is_null(name_node)) {
                char var_name[SYMBOL_MAX_LENGTH];
                char var_type[SYMBOL_MAX_LENGTH] = "";
                char package_buf[SYMBOL_MAX_LENGTH];

                safe_extract_node_text(source_code, name_node, var_name, sizeof(var_name), filename);
                get_package(node, source_code, package_buf, sizeof(package_buf), filename);

                if (!ts_node_is_null(type_node)) {
                    const char *type_node_type = ts_node_type(type_node);
                    /* Only extract actual type nodes, not computed expressions */
                    if (strcmp(type_node_type, "binary_expression") != 0 &&
                        strcmp(type_node_type, "expression_list") != 0) {
                        extract_type_from_node(type_node, source_code, var_type, sizeof(var_type), filename);
                    }
                }

                if (var_name[0] && filter_should_index(filter, var_name)) {
                    /* Extract source location for full variable declaration */
                    char location[128];
                    format_source_location(node, location, sizeof(location));

                    ExtColumns ext = {
                        .parent = NULL,
                        .scope = get_scope_from_name(var_name),
                        .modifier = "var",
                        .clue = NULL,
                        .namespace = package_buf[0] ? package_buf : NULL,
                        .type = var_type[0] ? var_type : NULL,
                        .definition = "1"
                    };
                    add_entry(result, var_name, line, CONTEXT_VARIABLE,
                             directory, filename, location, &ext);
                }
            }

            /* Process children to index symbols in computed expressions */
            process_children(child, source_code, directory, filename, result, filter);
        }
    }
}

/* Helper: Check if iota is used in a node's subtree */
static int contains_iota(TSNode node, const char *source_code) {
    const char *node_type = ts_node_type(node);

    /* Check if this node type is "iota" (tree-sitter keyword node) */
    if (strcmp(node_type, "iota") == 0) {
        return 1;
    }

    /* Recursively check children */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        if (contains_iota(ts_node_child(node, i), source_code)) {
            return 1;
        }
    }

    return 0;
}

/* Handler: const_declaration */
static void handle_const_declaration(TSNode node, const char *source_code, const char *directory,
                                      const char *filename, ParseResult *result, SymbolFilter *filter,
                                      int line) {
    /* Find const_spec children */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "const_spec") == 0) {
            /* Extract constant name and type */
            TSNode name_node = {0};
            TSNode type_node = {0};

            uint32_t spec_children = ts_node_child_count(child);
            for (uint32_t j = 0; j < spec_children; j++) {
                TSNode spec_child = ts_node_child(child, j);
                const char *spec_child_type = ts_node_type(spec_child);

                if (strcmp(spec_child_type, "identifier") == 0 && ts_node_is_null(name_node)) {
                    name_node = spec_child;
                } else if (!ts_node_is_null(name_node) && ts_node_is_null(type_node) &&
                           strcmp(spec_child_type, "=") != 0) {
                    /* Type comes after name, before = */
                    type_node = spec_child;
                }
            }

            if (!ts_node_is_null(name_node)) {
                char const_name[SYMBOL_MAX_LENGTH];
                char const_type[SYMBOL_MAX_LENGTH] = "";
                char package_buf[SYMBOL_MAX_LENGTH];

                safe_extract_node_text(source_code, name_node, const_name, sizeof(const_name), filename);
                get_package(node, source_code, package_buf, sizeof(package_buf), filename);

                if (!ts_node_is_null(type_node)) {
                    const char *type_node_type = ts_node_type(type_node);
                    /* Only extract actual type nodes, not computed expressions */
                    if (strcmp(type_node_type, "binary_expression") != 0 &&
                        strcmp(type_node_type, "expression_list") != 0) {
                        extract_type_from_node(type_node, source_code, const_type, sizeof(const_type), filename);
                    }
                }

                if (const_name[0] && filter_should_index(filter, const_name)) {
                    /* Check if iota is used in this const_spec */
                    const char *clue = NULL;
                    if (contains_iota(child, source_code)) {
                        clue = "iota";
                    }

                    /* Extract source location for full const declaration */
                    char location[128];
                    format_source_location(node, location, sizeof(location));

                    ExtColumns ext = {
                        .parent = NULL,
                        .scope = get_scope_from_name(const_name),
                        .modifier = "const",
                        .clue = clue,
                        .namespace = package_buf[0] ? package_buf : NULL,
                        .type = const_type[0] ? const_type : NULL,
                        .definition = "1"
                    };
                    add_entry(result, const_name, line, CONTEXT_VARIABLE,
                             directory, filename, location, &ext);
                }
            }

            /* Process children to index symbols in computed expressions */
            process_children(child, source_code, directory, filename, result, filter);
        }
    }
}

/* Handler: parameter_declaration */
static void handle_parameter_declaration(TSNode node, const char *source_code, const char *directory,
                                          const char *filename, ParseResult *result, SymbolFilter *filter,
                                          int line) {
    /* Determine if this is a function declaration parameter by checking parent chain */
    int is_definition = 0;
    TSNode current = node;
    while (!ts_node_is_null(current)) {
        TSNode parent = ts_node_parent(current);
        if (!ts_node_is_null(parent)) {
            const char *parent_type = ts_node_type(parent);
            if (strcmp(parent_type, "function_declaration") == 0 ||
                strcmp(parent_type, "method_declaration") == 0) {
                is_definition = 1;
                break;
            }
        }
        current = parent;
    }

    /* Go allows multiple parameters to share a type: username, email string
     * Collect all identifiers and find the type (last non-identifier, non-comma child) */
    TSNode identifiers[10];  /* Support up to 10 params with shared type */
    int identifier_count = 0;
    TSNode type_node = {0};

    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "identifier") == 0) {
            if (identifier_count < 10) {
                identifiers[identifier_count++] = child;
            }
        } else if (strcmp(child_type, ",") != 0) {
            /* Non-comma, non-identifier = type node */
            type_node = child;
        }
    }

    /* Extract the shared type */
    char param_type[SYMBOL_MAX_LENGTH] = "";
    if (!ts_node_is_null(type_node)) {
        extract_type_from_node(type_node, source_code, param_type, sizeof(param_type), filename);
    }

    /* Add entry for each parameter with the shared type */
    for (int i = 0; i < identifier_count; i++) {
        char param_name[SYMBOL_MAX_LENGTH];
        safe_extract_node_text(source_code, identifiers[i], param_name, sizeof(param_name), filename);

        if (param_name[0] && filter_should_index(filter, param_name)) {
            ExtColumns ext = {
                .parent = NULL,
                .scope = NULL,
                .modifier = NULL,
                .clue = NULL,
                .namespace = NULL,
                .type = param_type[0] ? param_type : NULL,
                .definition = is_definition ? "1" : "0"
            };
            add_entry(result, param_name, line, CONTEXT_ARGUMENT,
                     directory, filename, NULL, &ext);
        }
    }
}

/* Handler: short_var_declaration */
static void handle_short_var_declaration(TSNode node, const char *source_code, const char *directory,
                                          const char *filename, ParseResult *result, SymbolFilter *filter,
                                          int line) {
    /* Short variable declaration: name := value
     * Structure: short_var_declaration
     *   expression_list (LHS - variable names)
     *   :=
     *   expression_list (RHS - values)
     */

    TSNode lhs_list = {0};
    TSNode rhs_list = {0};

    /* Find LHS and RHS expression_lists */
    uint32_t child_count = ts_node_child_count(node);
    bool found_operator = false;

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "expression_list") == 0) {
            if (!found_operator) {
                lhs_list = child;  /* LHS before := */
            } else {
                rhs_list = child;  /* RHS after := */
            }
        } else if (strcmp(child_type, ":=") == 0) {
            found_operator = true;
        }
    }

    if (ts_node_is_null(lhs_list)) {
        return;  /* No variables to declare */
    }

    /* Extract all identifiers from LHS */
    TSNode identifiers[10];  /* Support up to 10 variables */
    int identifier_count = 0;

    uint32_t lhs_count = ts_node_child_count(lhs_list);
    for (uint32_t i = 0; i < lhs_count; i++) {
        TSNode child = ts_node_child(lhs_list, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "identifier") == 0) {
            if (identifier_count < 10) {
                identifiers[identifier_count++] = child;
            }
        }
    }

    /* Try to infer type from RHS if there's a single variable and single expression */
    char inferred_type[SYMBOL_MAX_LENGTH] = "";

    if (identifier_count == 1 && !ts_node_is_null(rhs_list)) {
        /* Single variable: try to infer type from RHS expression */
        uint32_t rhs_count = ts_node_child_count(rhs_list);
        for (uint32_t i = 0; i < rhs_count; i++) {
            TSNode child = ts_node_child(rhs_list, i);
            const char *child_type = ts_node_type(child);

            /* Skip commas, get first non-comma expression */
            if (strcmp(child_type, ",") != 0) {
                infer_type_from_expression(child, source_code, inferred_type, sizeof(inferred_type), filename);
                break;
            }
        }
    }

    /* Add entry for each variable */
    char package_buf[SYMBOL_MAX_LENGTH];
    get_package(node, source_code, package_buf, sizeof(package_buf), filename);

    for (int i = 0; i < identifier_count; i++) {
        char var_name[SYMBOL_MAX_LENGTH];
        safe_extract_node_text(source_code, identifiers[i], var_name, sizeof(var_name), filename);

        /* Skip blank identifier */
        if (strcmp(var_name, "_") == 0) {
            continue;
        }

        if (var_name[0] && filter_should_index(filter, var_name)) {
            /* Extract source location for full short variable declaration */
            char location[128];
            format_source_location(node, location, sizeof(location));

            ExtColumns ext = {
                .parent = NULL,
                .scope = get_scope_from_name(var_name),
                .modifier = NULL,
                .clue = NULL,
                .namespace = package_buf[0] ? package_buf : NULL,
                .type = inferred_type[0] ? inferred_type : NULL,
                .definition = "1"
            };
            add_entry(result, var_name, line, CONTEXT_VARIABLE,
                     directory, filename, location, &ext);
        }
    }

    /* Continue processing children (for nested expressions) */
    if (!ts_node_is_null(rhs_list)) {
        process_children(rhs_list, source_code, directory, filename, result, filter);
    }
}

/* Handler: range_clause */
static void handle_range_clause(TSNode node, const char *source_code, const char *directory,
                                 const char *filename, ParseResult *result, SymbolFilter *filter,
                                 int line) {
    /* Range clause: for i, name := range items
     * Structure: range_clause
     *   expression_list (loop variables)
     *   :=
     *   range
     *   identifier/expression (what we're ranging over)
     */

    TSNode var_list = {0};

    /* Find the expression_list (loop variables) */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "expression_list") == 0) {
            var_list = child;
            break;
        }
    }

    if (ts_node_is_null(var_list)) {
        return;  /* No loop variables */
    }

    /* Extract all identifiers from loop variable list */
    char package_buf[SYMBOL_MAX_LENGTH];
    get_package(node, source_code, package_buf, sizeof(package_buf), filename);

    uint32_t var_count = ts_node_child_count(var_list);
    for (uint32_t i = 0; i < var_count; i++) {
        TSNode child = ts_node_child(var_list, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "identifier") == 0) {
            char var_name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, var_name, sizeof(var_name), filename);

            /* Skip blank identifier */
            if (strcmp(var_name, "_") == 0) {
                continue;
            }

            if (var_name[0] && filter_should_index(filter, var_name)) {
                ExtColumns ext = {
                    .parent = NULL,
                    .scope = get_scope_from_name(var_name),
                    .modifier = NULL,
                    .clue = "range",
                    .namespace = package_buf[0] ? package_buf : NULL,
                    .type = NULL  /* Cannot reliably infer type */
                };
                add_entry(result, var_name, line, CONTEXT_VARIABLE,
                         directory, filename, NULL, &ext);
            }
        }
    }
}

/* Handler: defer_statement */
static void handle_defer_statement(TSNode node, const char *source_code, const char *directory,
                                    const char *filename, ParseResult *result, SymbolFilter *filter,
                                    int line) {
    /* Defer statement: defer funcName() or defer func() { ... }()
     * Structure: defer_statement
     *   defer (keyword)
     *   call_expression (the function being deferred)
     */

    TSNode call_node = {0};

    /* Find the call_expression child */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "call_expression") == 0) {
            call_node = child;
            break;
        }
    }

    if (ts_node_is_null(call_node)) {
        return;  /* No call expression found */
    }

    /* Extract function name from call_expression */
    TSNode function_node = ts_node_child_by_field_name(call_node, "function", 8);
    if (ts_node_is_null(function_node)) {
        /* Process the call_expression children anyway to index nested symbols */
        process_children(call_node, source_code, directory, filename, result, filter);
        return;
    }

    const char *func_type = ts_node_type(function_node);
    char defer_symbol[SYMBOL_MAX_LENGTH] = "";
    char *parent_name = NULL;

    if (strcmp(func_type, "identifier") == 0) {
        /* Named function: defer releaseResource(resource) */
        safe_extract_node_text(source_code, function_node, defer_symbol, sizeof(defer_symbol), filename);
    } else if (strcmp(func_type, "func_literal") == 0) {
        /* Anonymous function: defer func() { ... }() */
        snprintf(defer_symbol, sizeof(defer_symbol), "func");
    } else if (strcmp(func_type, "selector_expression") == 0) {
        /* Method call: defer fmt.Println() */
        TSNode field_node = ts_node_child_by_field_name(function_node, "field", 5);
        TSNode operand_node = ts_node_child_by_field_name(function_node, "operand", 7);

        if (!ts_node_is_null(field_node)) {
            safe_extract_node_text(source_code, field_node, defer_symbol, sizeof(defer_symbol), filename);

            /* Extract parent (object/package name) */
            if (!ts_node_is_null(operand_node)) {
                const char *operand_type = ts_node_type(operand_node);
                if (strcmp(operand_type, "identifier") == 0) {
                    static char parent_buf[SYMBOL_MAX_LENGTH];
                    safe_extract_node_text(source_code, operand_node, parent_buf, sizeof(parent_buf), filename);
                    parent_name = parent_buf;
                }
            }
        }
    }

    /* Index the deferred call
     * Note: We bypass filter_should_index here because we're extracting a meaningful
     * symbol based on its semantic role (deferred call), not generic identifier usage.
     */
    if (defer_symbol[0]) {
        char package_buf[SYMBOL_MAX_LENGTH];
        get_package(node, source_code, package_buf, sizeof(package_buf), filename);

        ExtColumns ext = {
            .parent = parent_name,
            .scope = NULL,
            .modifier = NULL,
            .clue = "defer",
            .namespace = package_buf[0] ? package_buf : NULL,
            .type = NULL
        };
        add_entry(result, defer_symbol, line, CONTEXT_CALL,
                 directory, filename, NULL, &ext);
    }

    /* Process children to index arguments and nested expressions */
    process_children(call_node, source_code, directory, filename, result, filter);
}

/* Handler: go_statement */
static void handle_go_statement(TSNode node, const char *source_code, const char *directory,
                                 const char *filename, ParseResult *result, SymbolFilter *filter,
                                 int line) {
    /* Go statement: go funcName() or go func() { ... }()
     * Structure: go_statement
     *   go (keyword)
     *   call_expression (the function being called)
     */

    TSNode call_node = {0};

    /* Find the call_expression child */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "call_expression") == 0) {
            call_node = child;
            break;
        }
    }

    if (ts_node_is_null(call_node)) {
        return;  /* No call expression found */
    }

    /* Extract function name from call_expression */
    TSNode function_node = ts_node_child_by_field_name(call_node, "function", 8);
    if (ts_node_is_null(function_node)) {
        /* Process the call_expression children anyway to index nested symbols */
        process_children(call_node, source_code, directory, filename, result, filter);
        return;
    }

    const char *func_type = ts_node_type(function_node);
    char goroutine_symbol[SYMBOL_MAX_LENGTH] = "";
    char *parent_name = NULL;

    if (strcmp(func_type, "identifier") == 0) {
        /* Named function: go worker(1, nil, nil) */
        safe_extract_node_text(source_code, function_node, goroutine_symbol, sizeof(goroutine_symbol), filename);
    } else if (strcmp(func_type, "func_literal") == 0) {
        /* Anonymous function: go func() { ... }() */
        snprintf(goroutine_symbol, sizeof(goroutine_symbol), "func");
    } else if (strcmp(func_type, "selector_expression") == 0) {
        /* Method call: go obj.method() */
        TSNode field_node = ts_node_child_by_field_name(function_node, "field", 5);
        TSNode operand_node = ts_node_child_by_field_name(function_node, "operand", 7);

        if (!ts_node_is_null(field_node)) {
            safe_extract_node_text(source_code, field_node, goroutine_symbol, sizeof(goroutine_symbol), filename);

            /* Extract parent (object/package name) */
            if (!ts_node_is_null(operand_node)) {
                const char *operand_type = ts_node_type(operand_node);
                if (strcmp(operand_type, "identifier") == 0) {
                    static char parent_buf[SYMBOL_MAX_LENGTH];
                    safe_extract_node_text(source_code, operand_node, parent_buf, sizeof(parent_buf), filename);
                    parent_name = parent_buf;
                }
            }
        }
    }

    /* Index the goroutine call
     * Note: We bypass filter_should_index here because we're extracting a meaningful
     * symbol based on its semantic role (goroutine launch), not generic identifier usage.
     * Keywords like "func" are acceptable here because modifier disambiguates them.
     */
    if (goroutine_symbol[0]) {
        char package_buf[SYMBOL_MAX_LENGTH];
        get_package(node, source_code, package_buf, sizeof(package_buf), filename);

        ExtColumns ext = {
            .parent = parent_name,
            .scope = NULL,
            .modifier = NULL,
            .clue = "go",
            .namespace = package_buf[0] ? package_buf : NULL,
            .type = NULL
        };
        add_entry(result, goroutine_symbol, line, CONTEXT_CALL,
                 directory, filename, NULL, &ext);
    }

    /* Process children to index arguments and nested expressions */
    process_children(call_node, source_code, directory, filename, result, filter);
}

/* Handler: communication_case */
static void handle_communication_case(TSNode node, const char *source_code, const char *directory,
                                      const char *filename, ParseResult *result, SymbolFilter *filter,
                                      int line) {
    /* Communication case: case msg := <-ch or case ch <- value
     * Structure: communication_case
     *   case (keyword)
     *   receive_statement or send_statement
     *   : (colon)
     *   statement(s) (body)
     */

    TSNode statement_node = {0};
    bool is_receive = false;

    /* Find the receive_statement or send_statement child */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "receive_statement") == 0) {
            statement_node = child;
            is_receive = true;
            break;
        } else if (strcmp(child_type, "send_statement") == 0) {
            statement_node = child;
            break;
        }
    }

    if (ts_node_is_null(statement_node)) {
        /* No receive/send statement, just process children normally */
        process_children(node, source_code, directory, filename, result, filter);
        return;
    }

    /* Handle receive_statement: msg := <-ch */
    if (is_receive) {
        /* Look for expression_list (LHS - the variable being assigned) */
        TSNode expr_list = {0};
        uint32_t stmt_child_count = ts_node_child_count(statement_node);

        for (uint32_t i = 0; i < stmt_child_count; i++) {
            TSNode child = ts_node_child(statement_node, i);
            const char *child_type = ts_node_type(child);

            if (strcmp(child_type, "expression_list") == 0) {
                expr_list = child;
                break;
            }
        }

        /* Extract variable name(s) from expression_list */
        if (!ts_node_is_null(expr_list)) {
            uint32_t expr_child_count = ts_node_child_count(expr_list);
            for (uint32_t i = 0; i < expr_child_count; i++) {
                TSNode identifier_node = ts_node_child(expr_list, i);
                const char *identifier_type = ts_node_type(identifier_node);

                if (strcmp(identifier_type, "identifier") == 0) {
                    char var_name[SYMBOL_MAX_LENGTH];
                    safe_extract_node_text(source_code, identifier_node, var_name, sizeof(var_name), filename);

                    /* Index the variable
                     * Note: We bypass filter_should_index here because we're extracting a meaningful
                     * symbol based on its semantic role (select case variable), not generic identifier usage.
                     */
                    if (var_name[0]) {
                        char package_buf[SYMBOL_MAX_LENGTH];
                        get_package(node, source_code, package_buf, sizeof(package_buf), filename);

                        ExtColumns ext = {
                            .parent = NULL,
                            .scope = get_scope_from_name(var_name),
                            .modifier = NULL,
                            .clue = "select",
                            .namespace = package_buf[0] ? package_buf : NULL,
                            .type = NULL  /* Type inference for channels is complex */
                        };
                        add_entry(result, var_name, line, CONTEXT_VARIABLE,
                                 directory, NULL, filename, &ext);
                    }
                }
            }
        }
    }

    /* For send_statement, we don't extract new variables, just index the channel
     * The channel identifier will be picked up by the normal identifier handler */

    /* Process children to index nested expressions and statements in the case body */
    process_children(node, source_code, directory, filename, result, filter);
}

/* Handler: send_statement */
static void handle_send_statement(TSNode node, const char *source_code, const char *directory,
                                   const char *filename, ParseResult *result, SymbolFilter *filter,
                                   int line) {
    /* Send statement: ch <- value
     * Structure: send_statement
     *   identifier (channel)
     *   <- (operator)
     *   expression (value being sent)
     */

    TSNode channel_node = {0};

    /* Find the channel identifier (first child) */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "identifier") == 0) {
            channel_node = child;
            break;
        }
    }

    if (ts_node_is_null(channel_node)) {
        /* No channel identifier found, just process children */
        process_children(node, source_code, directory, filename, result, filter);
        return;
    }

    /* Extract channel name */
    char channel_name[SYMBOL_MAX_LENGTH];
    safe_extract_node_text(source_code, channel_node, channel_name, sizeof(channel_name), filename);

    /* Index the send operation
     * Note: We bypass filter_should_index here because we're extracting a meaningful
     * symbol based on its semantic role (channel send), not generic identifier usage.
     */
    if (channel_name[0]) {
        char package_buf[SYMBOL_MAX_LENGTH];
        get_package(node, source_code, package_buf, sizeof(package_buf), filename);

        ExtColumns ext = {
            .parent = NULL,
            .scope = get_scope_from_name(channel_name),
            .modifier = NULL,
            .clue = "send",
            .namespace = package_buf[0] ? package_buf : NULL,
            .type = NULL
        };
        add_entry(result, channel_name, line, CONTEXT_VARIABLE, directory, filename, NULL, &ext);
    }

    /* Process children to index the value expression, but skip the channel identifier
     * to avoid duplication (we already indexed it with clue=send) */
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (!ts_node_eq(child, channel_node)) {
            visit_node(child, source_code, directory, filename, result, filter);
        }
    }
}

/* Handler: unary_expression - for channel receive operations */
static void handle_unary_expression(TSNode node, const char *source_code, const char *directory,
                                     const char *filename, ParseResult *result, SymbolFilter *filter,
                                     int line) {
    /* Unary expression can be many things, but we're interested in channel receives: <-ch
     * Structure: unary_expression
     *   <- (operator)
     *   identifier (channel)
     */

    /* Check if this is a channel receive operation by looking for <- operator */
    bool is_receive = false;
    TSNode channel_node = {0};

    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        /* Check for <- operator */
        if (strcmp(child_type, "<-") == 0) {
            is_receive = true;
        }
        /* Find the channel identifier */
        else if (strcmp(child_type, "identifier") == 0) {
            channel_node = child;
        }
    }

    if (!is_receive || ts_node_is_null(channel_node)) {
        /* Not a channel receive, just process children normally */
        process_children(node, source_code, directory, filename, result, filter);
        return;
    }

    /* Extract channel name */
    char channel_name[SYMBOL_MAX_LENGTH];
    safe_extract_node_text(source_code, channel_node, channel_name, sizeof(channel_name), filename);

    /* Index the receive operation
     * Note: We bypass filter_should_index here because we're extracting a meaningful
     * symbol based on its semantic role (channel receive), not generic identifier usage.
     */
    if (channel_name[0]) {
        char package_buf[SYMBOL_MAX_LENGTH];
        get_package(node, source_code, package_buf, sizeof(package_buf), filename);

        ExtColumns ext = {
            .parent = NULL,
            .scope = get_scope_from_name(channel_name),
            .modifier = NULL,
            .clue = "receive",
            .namespace = package_buf[0] ? package_buf : NULL,
            .type = NULL
        };
        add_entry(result, channel_name, line, CONTEXT_VARIABLE, directory, filename, NULL, &ext);
    }

    /* Process children but skip the channel identifier to avoid duplication
     * (we already indexed it with clue=receive) */
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (!ts_node_eq(child, channel_node)) {
            visit_node(child, source_code, directory, filename, result, filter);
        }
    }
}

/* Handler: func_literal - for anonymous functions (lambdas) */
static void handle_func_literal(TSNode node, const char *source_code, const char *directory,
                                 const char *filename, ParseResult *result, SymbolFilter *filter,
                                 int line) {
    /* func_literal structure:
     * - func keyword
     * - parameter_list (parameters)
     * - optional return type
     * - block (body)
     *
     * We index the lambda itself as "<lambda>" symbol with CONTEXT_LAMBDA
     */

    char package_buf[SYMBOL_MAX_LENGTH];
    get_package(node, source_code, package_buf, sizeof(package_buf), filename);

    /* Extract source location (line range) for the full lambda expression */
    char location[128];
    format_source_location(node, location, sizeof(location));

    /* Index the lambda expression itself */
    ExtColumns ext = {
        .parent = NULL,
        .scope = NULL,
        .modifier = NULL,
        .clue = NULL,
        .namespace = package_buf[0] ? package_buf : NULL,
        .type = NULL,
        .definition = "1"
    };
    add_entry(result, "<lambda>", line, CONTEXT_LAMBDA, directory, filename, location, &ext);

    /* Process children to index parameters and body */
    process_children(node, source_code, directory, filename, result, filter);
}

/* Handler: identifier - for references in expressions */
static void handle_identifier(TSNode node, const char *source_code, const char *directory,
                              const char *filename, ParseResult *result, SymbolFilter *filter,
                              int line) {
    char identifier[SYMBOL_MAX_LENGTH];
    safe_extract_node_text(source_code, node, identifier, sizeof(identifier), filename);

    /* Only index non-keyword identifiers that aren't operators/punctuation */
    if (identifier[0] && filter_should_index(filter, identifier)) {
        char package_buf[SYMBOL_MAX_LENGTH];
        get_package(node, source_code, package_buf, sizeof(package_buf), filename);

        /* Check if this identifier is an argument in a function call */
        TSNode parent = ts_node_parent(node);
        const char *parent_type = ts_node_type(parent);

        TSNode arg_list_node = {0};

        /* Direct child of argument_list: foo(bar) */
        if (strcmp(parent_type, "argument_list") == 0) {
            arg_list_node = parent;
        }
        /* Inside selector_expression within argument_list: foo(bar.baz) */
        else if (strcmp(parent_type, "selector_expression") == 0) {
            TSNode grandparent = ts_node_parent(parent);
            const char *grandparent_type = ts_node_type(grandparent);
            if (strcmp(grandparent_type, "argument_list") == 0) {
                arg_list_node = grandparent;
            }
        }

        if (!ts_node_is_null(arg_list_node)) {
            /* This identifier is a function argument - get the function name as clue */
            TSNode call_node = ts_node_parent(arg_list_node);
            const char *call_type = ts_node_type(call_node);

            if (strcmp(call_type, "call_expression") == 0) {
                TSNode function_node = ts_node_child_by_field_name(call_node, "function", 8);
                if (!ts_node_is_null(function_node)) {
                    char func_name[SYMBOL_MAX_LENGTH] = "";
                    const char *func_type = ts_node_type(function_node);

                    if (strcmp(func_type, "selector_expression") == 0) {
                        /* Method call: extract field name */
                        TSNode field_node = ts_node_child_by_field_name(function_node, "field", 5);
                        if (!ts_node_is_null(field_node)) {
                            safe_extract_node_text(source_code, field_node, func_name, sizeof(func_name), filename);
                        }
                    } else if (strcmp(func_type, "identifier") == 0) {
                        /* Function call: extract identifier */
                        safe_extract_node_text(source_code, function_node, func_name, sizeof(func_name), filename);
                    }

                    if (func_name[0] != '\0') {
                        /* Index as function argument with clue */
                        ExtColumns ext = {
                            .parent = NULL,
                            .scope = NULL,
                            .modifier = NULL,
                            .clue = func_name,
                            .namespace = package_buf[0] ? package_buf : NULL,
                            .type = NULL
                        };
                        add_entry(result, identifier, line, CONTEXT_ARGUMENT, directory, filename, NULL, &ext);
                        return;
                    }
                }
            }
        }

        /* Default: index as variable */
        ExtColumns ext = {
            .parent = NULL,
            .scope = NULL,
            .modifier = NULL,
            .clue = NULL,
            .namespace = package_buf[0] ? package_buf : NULL,
            .type = NULL
        };
        add_entry(result, identifier, line, CONTEXT_VARIABLE, directory, filename, NULL, &ext);
    }
}

/* Handler: field_identifier - for field access in selector expressions */
static void handle_field_identifier(TSNode node, const char *source_code, const char *directory,
                                    const char *filename, ParseResult *result, SymbolFilter *filter,
                                    int line) {
    char field_name[SYMBOL_MAX_LENGTH];
    safe_extract_node_text(source_code, node, field_name, sizeof(field_name), filename);

    if (field_name[0] && filter_should_index(filter, field_name)) {
        char package_buf[SYMBOL_MAX_LENGTH];
        get_package(node, source_code, package_buf, sizeof(package_buf), filename);

        /* field_identifier is always a child of selector_expression */
        TSNode selector_node = ts_node_parent(node);
        const char *selector_type = ts_node_type(selector_node);

        if (strcmp(selector_type, "selector_expression") == 0) {
            /* Extract the operand (left side of the dot) as parent */
            TSNode operand_node = ts_node_child_by_field_name(selector_node, "operand", 7);
            char parent_name[SYMBOL_MAX_LENGTH] = "";
            if (!ts_node_is_null(operand_node)) {
                safe_extract_node_text(source_code, operand_node, parent_name, sizeof(parent_name), filename);
            }

            /* Check if selector_expression is inside an argument_list */
            TSNode grandparent = ts_node_parent(selector_node);
            const char *grandparent_type = ts_node_type(grandparent);

            if (strcmp(grandparent_type, "argument_list") == 0) {
                /* This field is part of a function argument - get function name as clue */
                TSNode call_node = ts_node_parent(grandparent);
                const char *call_type = ts_node_type(call_node);

                if (strcmp(call_type, "call_expression") == 0) {
                    TSNode function_node = ts_node_child_by_field_name(call_node, "function", 8);
                    if (!ts_node_is_null(function_node)) {
                        char func_name[SYMBOL_MAX_LENGTH] = "";
                        const char *func_type = ts_node_type(function_node);

                        if (strcmp(func_type, "selector_expression") == 0) {
                            TSNode field_node = ts_node_child_by_field_name(function_node, "field", 5);
                            if (!ts_node_is_null(field_node)) {
                                safe_extract_node_text(source_code, field_node, func_name, sizeof(func_name), filename);
                            }
                        } else if (strcmp(func_type, "identifier") == 0) {
                            safe_extract_node_text(source_code, function_node, func_name, sizeof(func_name), filename);
                        }

                        if (func_name[0] != '\0') {
                            /* Index as function argument with clue and parent */
                            ExtColumns ext = {
                                .parent = parent_name[0] ? parent_name : NULL,
                                .scope = NULL,
                                .modifier = NULL,
                                .clue = func_name,
                                .namespace = package_buf[0] ? package_buf : NULL,
                                .type = NULL
                            };
                            add_entry(result, field_name, line, CONTEXT_ARGUMENT, directory, filename, NULL, &ext);
                            return;
                        }
                    }
                }
            }

            /* Default: index as variable with parent */
            ExtColumns ext = {
                .parent = parent_name[0] ? parent_name : NULL,
                .scope = NULL,
                .modifier = NULL,
                .clue = NULL,
                .namespace = package_buf[0] ? package_buf : NULL,
                .type = NULL
            };
            add_entry(result, field_name, line, CONTEXT_VARIABLE, directory, filename, NULL, &ext);
        }
    }
}

/* Visit node function */
static void visit_node(TSNode node, const char *source_code, const char *directory,
                      const char *filename, ParseResult *result, SymbolFilter *filter) {
    TSSymbol node_sym = ts_node_symbol(node);

    /* Get line number */
    TSPoint start = ts_node_start_point(node);
    int line = (int)start.row + 1;

    /* Symbol-based dispatch - O(1) comparisons with early returns */
    if (node_sym == go_symbols.package_clause) {
        handle_package_clause(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.import_declaration) {
        handle_import_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.type_declaration) {
        handle_type_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.type_spec) {
        handle_type_spec(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.type_alias) {
        handle_type_alias(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.function_declaration) {
        handle_function_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.func_literal) {
        handle_func_literal(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.method_declaration) {
        handle_method_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.method_spec) {
        handle_method_spec(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.field_declaration) {
        handle_field_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.var_declaration) {
        handle_var_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.const_declaration) {
        handle_const_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.parameter_declaration) {
        handle_parameter_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.short_var_declaration) {
        handle_short_var_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.range_clause) {
        handle_range_clause(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.defer_statement) {
        handle_defer_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.go_statement) {
        handle_go_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.communication_case) {
        handle_communication_case(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.send_statement) {
        handle_send_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.unary_expression) {
        handle_unary_expression(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.call_expression) {
        handle_call_expression(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.identifier) {
        handle_identifier(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.field_identifier) {
        handle_field_identifier(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.comment) {
        handle_comment(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == go_symbols.interpreted_string_literal) {
        handle_string_literal(node, source_code, directory, filename, result, filter, line);
        return;
    }

    /* No handler found, recursively visit children */
    process_children(node, source_code, directory, filename, result, filter);
}

/* Public interface implementation */
int parser_init(GoParser *parser, SymbolFilter *filter) {
    parser->filter = filter;
    return 0;
}

void parser_set_debug(GoParser *parser, int debug) {
    parser->debug = debug;
    g_debug = debug;
}

void parser_free(GoParser *parser) {
    /* Nothing to free currently */
    (void)parser;
}

int parser_parse_file(GoParser *parser, const char *filepath,
                      const char *project_root, ParseResult *result) {
    /* Open file */
    FILE *file = safe_fopen(filepath, "r", 0);
    if (!file) {
        fprintf(stderr, "Failed to open file: %s\n", filepath);
        return -1;
    }

    /* Get file size using fstat */
    int fd = fileno(file);
    struct stat st;
    if (fstat(fd, &st) != 0) {
        fclose(file);
        return -1;
    }
    size_t file_size = (size_t)st.st_size;

    char *source_code = malloc(file_size + 1);
    if (!source_code) {
        fprintf(stderr, "Failed to allocate memory for file: %s\n", filepath);
        fclose(file);
        return -1;
    }

    size_t bytes_read = fread(source_code, 1, file_size, file);
    if (bytes_read != file_size) {
        fprintf(stderr, "Error reading file: %s (expected %zu bytes, got %zu)\n",
                filepath, file_size, bytes_read);
        free(source_code);
        fclose(file);
        return -1;
    }
    source_code[bytes_read] = '\0';
    fclose(file);

    /* Create parser */
    TSParser *ts_parser = ts_parser_new();
    const TSLanguage *language = tree_sitter_go();
    if (!ts_parser_set_language(ts_parser, language)) {
        fprintf(stderr, "Failed to set Go language\n");
        free(source_code);
        ts_parser_delete(ts_parser);
        return -1;
    }

    /* Initialize symbol lookup table */
    init_go_symbols(language);

    /* Parse */
    TSTree *tree = ts_parser_parse_string(ts_parser, NULL, source_code, (uint32_t)bytes_read);
    if (!tree) {
        fprintf(stderr, "Failed to parse file: %s\n", filepath);
        free(source_code);
        ts_parser_delete(ts_parser);
        return -1;
    }

    /* Get relative path */
    char directory[DIRECTORY_MAX_LENGTH] = "";
    char filename[FILENAME_MAX_LENGTH] = "";
    get_relative_path(filepath, project_root, directory, filename);

    /* Index filename itself (without .go extension) */
    char name_only[FILENAME_MAX_LENGTH];
    strncpy(name_only, filename, sizeof(name_only) - 1);
    name_only[sizeof(name_only) - 1] = '\0';
    char *dot = strrchr(name_only, '.');
    if (dot) *dot = '\0';

    result->count = 0;
    if (name_only[0] && filter_should_index(parser->filter, name_only)) {
        add_entry(result, name_only, 1, CONTEXT_FILENAME, directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
    }

    /* Traverse AST */
    TSNode root = ts_tree_root_node(tree);
    visit_node(root, source_code, directory, filename, result, parser->filter);

    /* Cleanup */
    ts_tree_delete(tree);
    ts_parser_delete(ts_parser);
    free(source_code);

    return 0;
}
