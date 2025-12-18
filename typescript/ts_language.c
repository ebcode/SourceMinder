#include "ts_language.h"
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
#include "../shared/debug.h"
#include "../shared/file_utils.h"

/* External TypeScript language function */
extern const TSLanguage *tree_sitter_typescript(void);

/* Global debug flag */
static int g_debug = 0;

/* Symbol lookup table for fast node type comparisons */
static struct {
    TSSymbol identifier;
    TSSymbol member_expression;
    TSSymbol type_identifier;
    TSSymbol predefined_type;
    TSSymbol arrow_function;
    TSSymbol function_expression;
    TSSymbol required_parameter;
    TSSymbol optional_parameter;
    TSSymbol rest_parameter;
    TSSymbol array_pattern;
    TSSymbol object_pattern;
    TSSymbol non_null_expression;
    TSSymbol type_annotation;
    TSSymbol variable_declarator;
    TSSymbol private_property_identifier;
    TSSymbol this_keyword;
    TSSymbol string_fragment;
    TSSymbol arguments;
    TSSymbol return_keyword;
    TSSymbol semicolon;
    TSSymbol colon;
    /* Type classification symbols */
    TSSymbol generic_type;
    TSSymbol object_type;
    TSSymbol array_type;
    TSSymbol tuple_type;
    TSSymbol literal_type;
    TSSymbol union_type;
    TSSymbol intersection_type;
    TSSymbol nested_type_identifier;
    TSSymbol this_type;
    TSSymbol existential_type;
    TSSymbol parenthesized_type;
    TSSymbol readonly_type;
    TSSymbol function_type;
    TSSymbol constructor_type;
    TSSymbol type_query;
    TSSymbol index_type_query;
    TSSymbol lookup_type;
    TSSymbol conditional_type;
    TSSymbol template_literal_type;
    TSSymbol infer_type;
    TSSymbol type_predicate;
    TSSymbol mapped_type_clause;
    TSSymbol flow_maybe_type;
    TSSymbol optional_type;
    TSSymbol rest_type;
    /* Expression types */
    TSSymbol call_expression;
    TSSymbol new_expression;
    TSSymbol binary_expression;
    TSSymbol update_expression;
    TSSymbol unary_expression;
    TSSymbol parenthesized_expression;
    TSSymbol ternary_expression;
    TSSymbol assignment_expression;
    TSSymbol augmented_assignment_expression;
    TSSymbol subscript_expression;
    TSSymbol object;
    TSSymbol array;
    TSSymbol template_string;
    /* Declaration types */
    TSSymbol accessibility_modifier;
    TSSymbol class_declaration;
    TSSymbol function_declaration;
    TSSymbol interface_declaration;
    TSSymbol type_alias_declaration;
    TSSymbol lexical_declaration;
    /* Import/Export types */
    TSSymbol import_clause;
    TSSymbol named_imports;
    TSSymbol import_specifier;
    TSSymbol export_clause;
    TSSymbol export_specifier;
    TSSymbol type_arguments;
    /* Handler dispatch symbols */
    TSSymbol abstract_class_declaration;
    TSSymbol type_parameter;
    TSSymbol return_statement;
    TSSymbol method_definition;
    TSSymbol variable_declaration;
    TSSymbol property_signature;
    TSSymbol public_field_definition;
    TSSymbol import_statement;
    TSSymbol export_statement;
    TSSymbol string;
    TSSymbol comment;
    TSSymbol shorthand_property_identifier;
    TSSymbol pair;
    TSSymbol template_substitution;
    TSSymbol class_body;
} ts_symbols;

/* Removed: Now using safe_extract_node_text() from shared/string_utils.h */

/* Initialize symbol lookup table - called once at startup */
static void init_ts_symbols(const TSLanguage *language) {
    static int initialized = 0;
    if (initialized) return;
    initialized = 1;

    /* Core node types */
    ts_symbols.identifier = ts_language_symbol_for_name(language, "identifier", 10, true);
    ts_symbols.member_expression = ts_language_symbol_for_name(language, "member_expression", 17, true);
    ts_symbols.type_identifier = ts_language_symbol_for_name(language, "type_identifier", 15, true);
    ts_symbols.predefined_type = ts_language_symbol_for_name(language, "predefined_type", 15, true);
    ts_symbols.arrow_function = ts_language_symbol_for_name(language, "arrow_function", 14, true);
    ts_symbols.function_expression = ts_language_symbol_for_name(language, "function_expression", 19, true);

    /* Parameter types */
    ts_symbols.required_parameter = ts_language_symbol_for_name(language, "required_parameter", 18, true);
    ts_symbols.optional_parameter = ts_language_symbol_for_name(language, "optional_parameter", 18, true);
    ts_symbols.rest_parameter = ts_language_symbol_for_name(language, "rest_parameter", 14, true);
    ts_symbols.array_pattern = ts_language_symbol_for_name(language, "array_pattern", 13, true);
    ts_symbols.object_pattern = ts_language_symbol_for_name(language, "object_pattern", 14, true);
    ts_symbols.non_null_expression = ts_language_symbol_for_name(language, "non_null_expression", 19, true);

    /* Other common types */
    ts_symbols.type_annotation = ts_language_symbol_for_name(language, "type_annotation", 15, true);
    ts_symbols.variable_declarator = ts_language_symbol_for_name(language, "variable_declarator", 19, true);
    ts_symbols.private_property_identifier = ts_language_symbol_for_name(language, "private_property_identifier", 27, true);
    ts_symbols.this_keyword = ts_language_symbol_for_name(language, "this", 4, true);
    ts_symbols.string_fragment = ts_language_symbol_for_name(language, "string_fragment", 15, true);
    ts_symbols.arguments = ts_language_symbol_for_name(language, "arguments", 9, true);

    /* Keywords and punctuation (anonymous nodes) */
    ts_symbols.return_keyword = ts_language_symbol_for_name(language, "return", 6, false);
    ts_symbols.semicolon = ts_language_symbol_for_name(language, ";", 1, false);
    ts_symbols.colon = ts_language_symbol_for_name(language, ":", 1, false);

    /* Type classification symbols */
    ts_symbols.generic_type = ts_language_symbol_for_name(language, "generic_type", 12, true);
    ts_symbols.object_type = ts_language_symbol_for_name(language, "object_type", 11, true);
    ts_symbols.array_type = ts_language_symbol_for_name(language, "array_type", 10, true);
    ts_symbols.tuple_type = ts_language_symbol_for_name(language, "tuple_type", 10, true);
    ts_symbols.literal_type = ts_language_symbol_for_name(language, "literal_type", 12, true);
    ts_symbols.union_type = ts_language_symbol_for_name(language, "union_type", 10, true);
    ts_symbols.intersection_type = ts_language_symbol_for_name(language, "intersection_type", 17, true);
    ts_symbols.nested_type_identifier = ts_language_symbol_for_name(language, "nested_type_identifier", 22, true);
    ts_symbols.this_type = ts_language_symbol_for_name(language, "this_type", 9, true);
    ts_symbols.existential_type = ts_language_symbol_for_name(language, "existential_type", 16, true);
    ts_symbols.parenthesized_type = ts_language_symbol_for_name(language, "parenthesized_type", 18, true);
    ts_symbols.readonly_type = ts_language_symbol_for_name(language, "readonly_type", 13, true);
    ts_symbols.function_type = ts_language_symbol_for_name(language, "function_type", 13, true);
    ts_symbols.constructor_type = ts_language_symbol_for_name(language, "constructor_type", 16, true);
    ts_symbols.type_query = ts_language_symbol_for_name(language, "type_query", 10, true);
    ts_symbols.index_type_query = ts_language_symbol_for_name(language, "index_type_query", 16, true);
    ts_symbols.lookup_type = ts_language_symbol_for_name(language, "lookup_type", 11, true);
    ts_symbols.conditional_type = ts_language_symbol_for_name(language, "conditional_type", 16, true);
    ts_symbols.template_literal_type = ts_language_symbol_for_name(language, "template_literal_type", 21, true);
    ts_symbols.infer_type = ts_language_symbol_for_name(language, "infer_type", 10, true);
    ts_symbols.type_predicate = ts_language_symbol_for_name(language, "type_predicate", 14, true);
    ts_symbols.mapped_type_clause = ts_language_symbol_for_name(language, "mapped_type_clause", 18, true);
    ts_symbols.flow_maybe_type = ts_language_symbol_for_name(language, "flow_maybe_type", 15, true);
    ts_symbols.optional_type = ts_language_symbol_for_name(language, "optional_type", 13, true);
    ts_symbols.rest_type = ts_language_symbol_for_name(language, "rest_type", 9, true);

    /* Expression types */
    ts_symbols.call_expression = ts_language_symbol_for_name(language, "call_expression", 15, true);
    ts_symbols.new_expression = ts_language_symbol_for_name(language, "new_expression", 14, true);
    ts_symbols.binary_expression = ts_language_symbol_for_name(language, "binary_expression", 17, true);
    ts_symbols.update_expression = ts_language_symbol_for_name(language, "update_expression", 17, true);
    ts_symbols.unary_expression = ts_language_symbol_for_name(language, "unary_expression", 16, true);
    ts_symbols.parenthesized_expression = ts_language_symbol_for_name(language, "parenthesized_expression", 24, true);
    ts_symbols.ternary_expression = ts_language_symbol_for_name(language, "ternary_expression", 18, true);
    ts_symbols.assignment_expression = ts_language_symbol_for_name(language, "assignment_expression", 21, true);
    ts_symbols.augmented_assignment_expression = ts_language_symbol_for_name(language, "augmented_assignment_expression", 31, true);
    ts_symbols.subscript_expression = ts_language_symbol_for_name(language, "subscript_expression", 20, true);
    ts_symbols.object = ts_language_symbol_for_name(language, "object", 6, true);
    ts_symbols.array = ts_language_symbol_for_name(language, "array", 5, true);
    ts_symbols.template_string = ts_language_symbol_for_name(language, "template_string", 15, true);

    /* Declaration types */
    ts_symbols.accessibility_modifier = ts_language_symbol_for_name(language, "accessibility_modifier", 22, true);
    ts_symbols.class_declaration = ts_language_symbol_for_name(language, "class_declaration", 17, true);
    ts_symbols.function_declaration = ts_language_symbol_for_name(language, "function_declaration", 20, true);
    ts_symbols.interface_declaration = ts_language_symbol_for_name(language, "interface_declaration", 21, true);
    ts_symbols.type_alias_declaration = ts_language_symbol_for_name(language, "type_alias_declaration", 22, true);
    ts_symbols.lexical_declaration = ts_language_symbol_for_name(language, "lexical_declaration", 19, true);

    /* Import/Export types */
    ts_symbols.import_clause = ts_language_symbol_for_name(language, "import_clause", 13, true);
    ts_symbols.named_imports = ts_language_symbol_for_name(language, "named_imports", 13, true);
    ts_symbols.import_specifier = ts_language_symbol_for_name(language, "import_specifier", 16, true);
    ts_symbols.export_clause = ts_language_symbol_for_name(language, "export_clause", 13, true);
    ts_symbols.export_specifier = ts_language_symbol_for_name(language, "export_specifier", 16, true);
    ts_symbols.type_arguments = ts_language_symbol_for_name(language, "type_arguments", 14, true);

    /* Handler dispatch symbols */
    ts_symbols.abstract_class_declaration = ts_language_symbol_for_name(language, "abstract_class_declaration", 26, true);
    ts_symbols.type_parameter = ts_language_symbol_for_name(language, "type_parameter", 14, true);
    ts_symbols.return_statement = ts_language_symbol_for_name(language, "return_statement", 16, true);
    ts_symbols.method_definition = ts_language_symbol_for_name(language, "method_definition", 17, true);
    ts_symbols.variable_declaration = ts_language_symbol_for_name(language, "variable_declaration", 20, true);
    ts_symbols.property_signature = ts_language_symbol_for_name(language, "property_signature", 18, true);
    ts_symbols.public_field_definition = ts_language_symbol_for_name(language, "public_field_definition", 23, true);
    ts_symbols.import_statement = ts_language_symbol_for_name(language, "import_statement", 16, true);
    ts_symbols.export_statement = ts_language_symbol_for_name(language, "export_statement", 16, true);
    ts_symbols.string = ts_language_symbol_for_name(language, "string", 6, true);
    ts_symbols.comment = ts_language_symbol_for_name(language, "comment", 7, true);
    ts_symbols.shorthand_property_identifier = ts_language_symbol_for_name(language, "shorthand_property_identifier", 29, true);
    ts_symbols.pair = ts_language_symbol_for_name(language, "pair", 4, true);
    ts_symbols.template_substitution = ts_language_symbol_for_name(language, "template_substitution", 21, true);
    ts_symbols.class_body = ts_language_symbol_for_name(language, "class_body", 10, true);
}

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

/* Query infrastructure for complex handlers */
typedef struct {
    TSQuery *query;
    const char *query_string;
} CachedQuery;

/* Forward declaration for type extraction helper */
static void extract_type_from_annotation(TSNode type_annotation_node, const char *source_code,
                                         char *type_buffer, size_t type_size, const char *filename);

/* Global query cache - compiled once, reused for all files */
static CachedQuery import_query = {NULL, NULL};
static CachedQuery export_clause_query = {NULL, NULL};
static CachedQuery export_decl_query = {NULL, NULL};
static CachedQuery export_var_query = {NULL, NULL};
static CachedQuery param_query = {NULL, NULL};

/* Compile and cache a query */
static TSQuery* compile_query(const TSLanguage *language, CachedQuery *cache, const char *query_string) {
    if (cache->query != NULL) {
        return cache->query;
    }

    uint32_t error_offset;
    TSQueryError error_type;
    cache->query = ts_query_new(
        language,
        query_string,
        (uint32_t)strnlength(query_string, SQL_QUERY_BUFFER),
        &error_offset,
        &error_type
    );

    if (cache->query == NULL || error_type != TSQueryErrorNone) {
        fprintf(stderr, "Query compilation failed at offset %u, error type: %d\n", error_offset, error_type);
        fprintf(stderr, "Query: %s\n", query_string);
        return NULL;
    }

    cache->query_string = query_string;
    return cache->query;
}

/* Extract text from a capture node */
static void get_capture_text(const char *source_code, TSNode node, char *buffer, size_t buffer_size, const char *filename) {
    safe_extract_node_text(source_code, node, buffer, buffer_size, filename);
}

/* Free all cached queries */
static void free_queries(void) {
    if (import_query.query) ts_query_delete(import_query.query);
    if (export_clause_query.query) ts_query_delete(export_clause_query.query);
    if (export_decl_query.query) ts_query_delete(export_decl_query.query);
    if (export_var_query.query) ts_query_delete(export_var_query.query);
    if (param_query.query) ts_query_delete(param_query.query);

    import_query.query = NULL;
    export_clause_query.query = NULL;
    export_decl_query.query = NULL;
    export_var_query.query = NULL;
    param_query.query = NULL;
}

static void extract_parameters(TSNode params_node, const char *source_code, const char *directory,
                              const char *filename, ParseResult *result, SymbolFilter *filter, int is_definition) {
    if (ts_node_is_null(params_node)) return;

    uint32_t param_count = ts_node_child_count(params_node);
    for (uint32_t i = 0; i < param_count; i++) {
        TSNode param = ts_node_child(params_node, i);
        TSSymbol param_sym = ts_node_symbol(param);

        /* Handle required_parameter, optional_parameter, rest_parameter */
        if (param_sym == ts_symbols.required_parameter ||
            param_sym == ts_symbols.optional_parameter ||
            param_sym == ts_symbols.rest_parameter) {

            /* Get the pattern (name) field */
            TSNode pattern_node = ts_node_child_by_field_name(param, "pattern", 7);
            if (!ts_node_is_null(pattern_node)) {
                TSSymbol pattern_sym = ts_node_symbol(pattern_node);

                /* Skip destructuring patterns */
                if (pattern_sym == ts_symbols.array_pattern ||
                    pattern_sym == ts_symbols.object_pattern) {
                    continue;
                }

                /* Handle wrapped identifiers (non_null_expression, etc.) */
                TSNode identifier_node = pattern_node;
                TSSymbol identifier_sym = pattern_sym;

                /* Unwrap non_null_expression to get the actual identifier */
                if (identifier_sym == ts_symbols.non_null_expression) {
                    /* non_null_expression wraps an expression with ! operator */
                    /* Get the first child which should be the actual pattern */
                    if (ts_node_child_count(pattern_node) > 0) {
                        identifier_node = ts_node_child(pattern_node, 0);
                        identifier_sym = ts_node_symbol(identifier_node);
                    }
                }

                /* Extract parameter name - prefer identifier but fall back to pattern_node if simple enough */
                char symbol[SYMBOL_MAX_LENGTH];
                if (identifier_sym == ts_symbols.identifier) {
                    safe_extract_node_text(source_code, identifier_node, symbol, sizeof(symbol), filename);
                } else {
                    /* For non-identifier patterns, only extract if they're small enough to be safe */
                    uint32_t text_len = ts_node_end_byte(pattern_node) - ts_node_start_byte(pattern_node);
                    if (text_len < SYMBOL_MAX_LENGTH) {
                        safe_extract_node_text(source_code, pattern_node, symbol, sizeof(symbol), filename);
                    } else {
                        /* Skip patterns that are too large (like the JSX expression we saw earlier) */
                        continue;
                    }
                }

                /* Get line number from the parameter itself */
                TSPoint start_point = ts_node_start_point(param);
                int line = (int)(start_point.row + 1);

                /* Extract type annotation if present */
                char param_type[SYMBOL_MAX_LENGTH] = "";
                TSNode type_annotation = ts_node_child_by_field_name(param, "type", 4);
                if (!ts_node_is_null(type_annotation)) {
                    /* Use the smart type extraction that handles complex/large types */
                    extract_type_from_annotation(type_annotation, source_code, param_type, sizeof(param_type), filename);

                    /* Also index type identifiers as separate entries for backward compatibility */
                    uint32_t type_child_count = ts_node_child_count(type_annotation);
                    for (uint32_t j = 0; j < type_child_count; j++) {
                        TSNode type_child = ts_node_child(type_annotation, j);
                        TSSymbol type_child_sym = ts_node_symbol(type_child);

                        if (type_child_sym == ts_symbols.type_identifier ||
                            type_child_sym == ts_symbols.predefined_type) {
                            char type_name[SYMBOL_MAX_LENGTH];
                            safe_extract_node_text(source_code, type_child, type_name, sizeof(type_name), filename);

                            if (filter_should_index(filter, type_name)) {
                                add_entry(result, type_name, line, CONTEXT_TYPE, directory, filename, NULL, &(ExtColumns){.definition = "1"});
                            }
                            break;
                        }
                    }
                }

                /* Index parameter with its type */
                if (filter_should_index(filter, symbol)) {
                    ExtColumns ext = {
                        .parent = NULL,
                        .scope = NULL,
                        .modifier = NULL,
                        .clue = NULL,
                        .namespace = NULL,
                        .type = param_type[0] != '\0' ? param_type : NULL,
                        .definition = is_definition ? "1" : "0"
                    };
                    add_entry(result, symbol, line, CONTEXT_ARGUMENT, directory, filename, NULL, &ext);
                }
            }
        }
    }
}

static void process_children(TSNode node, const char *source_code, const char *directory,
                            const char *filename, ParseResult *result, SymbolFilter *filter) {
    /* Tree cursor approach */
    TSTreeCursor cursor = ts_tree_cursor_new(node);

    if (ts_tree_cursor_goto_first_child(&cursor)) {
        do {
            TSNode child = ts_tree_cursor_current_node(&cursor);
            visit_node(child, source_code, directory, filename, result, filter);
        } while (ts_tree_cursor_goto_next_sibling(&cursor));
    }

    ts_tree_cursor_delete(&cursor);
}

/* Visit expression nodes and index identifiers found within them */
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
    TSPoint start_point = ts_node_start_point(node);
    int line = (int)(start_point.row + 1);

    if (g_debug) {
        const char *node_type = ts_node_type(node);  /* Only for debug output */
        debug("[visit_expression] Line %d: node_type='%s'", line, node_type);
    }

    /* Handle identifier - the leaf expression that references a variable */
    if (node_sym == ts_symbols.identifier) {
        char symbol[SYMBOL_MAX_LENGTH];
        safe_extract_node_text(source_code, node, symbol, sizeof(symbol), filename);

        if (g_debug) {
            debug("[visit_expression] Line %d: Found identifier='%s'", line, symbol);
        }

        if (filter_should_index(filter, symbol)) {
            /* Check if parent is new_expression to use CALL context */
            TSNode parent_node = ts_node_parent(node);
            ContextType context = CONTEXT_VARIABLE;
            if (!ts_node_is_null(parent_node)) {
                TSSymbol parent_sym = ts_node_symbol(parent_node);
                if (parent_sym == ts_symbols.new_expression) {
                    context = CONTEXT_CALL;
                }
            }

            if (g_debug) {
                debug("[visit_expression] Line %d: Indexing identifier '%s' as %s",
                      line, symbol, context == CONTEXT_CALL ? "CALL" : "VAR");
            }
            add_entry(result, symbol, line, context, directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
        }
    }
    /* Handle member expressions like obj.prop - but don't re-index, let specific handlers handle these */
    else if (node_sym == ts_symbols.member_expression) {
        if (g_debug) {
            debug("[visit_expression] Line %d: Visiting member_expression children", line);
        }
        /* Visit the object part recursively */
        TSNode object = ts_node_child_by_field_name(node, "object", 6);
        if (!ts_node_is_null(object)) {
            visit_expression(object, source_code, directory, filename, result, filter);
        }

        /* Index the property when in argument context */
        TSNode parent_node = ts_node_parent(node);
        if (!ts_node_is_null(parent_node)) {
            TSSymbol parent_sym = ts_node_symbol(parent_node);
            if (parent_sym == ts_symbols.arguments) {
                TSNode property = ts_node_child_by_field_name(node, "property", 8);
                if (!ts_node_is_null(property)) {
                    char property_symbol[SYMBOL_MAX_LENGTH];
                    char parent_symbol[SYMBOL_MAX_LENGTH] = "";
                    safe_extract_node_text(source_code, property, property_symbol, sizeof(property_symbol), filename);

                    /* Extract parent from object */
                    if (!ts_node_is_null(object)) {
                        TSSymbol object_sym = ts_node_symbol(object);
                        if (object_sym == ts_symbols.identifier || object_sym == ts_symbols.this_keyword) {
                            safe_extract_node_text(source_code, object, parent_symbol, sizeof(parent_symbol), filename);
                        } else if (object_sym == ts_symbols.member_expression) {
                            /* Nested member - get property from parent member expression */
                            TSNode parent_prop = ts_node_child_by_field_name(object, "property", 8);
                            if (!ts_node_is_null(parent_prop)) {
                                safe_extract_node_text(source_code, parent_prop, parent_symbol, sizeof(parent_symbol), filename);
                            }
                        }
                    }

                    if (filter_should_index(filter, property_symbol)) {
                        if (g_debug) {
                            debug("[visit_expression] Line %d: Indexing property '%s' as ARG with parent='%s'",
                                  line, property_symbol, parent_symbol[0] ? parent_symbol : "(none)");
                        }
                        add_entry(result, property_symbol, line, CONTEXT_ARGUMENT, directory, filename, NULL,
                                &(ExtColumns){.parent = parent_symbol[0] ? parent_symbol : NULL, .definition = "0"});
                    }
                }
            }
        }
    }
    /* Handle call expressions - delegate to visit_node so handler gets called */
    else if (node_sym == ts_symbols.call_expression) {
        if (g_debug) {
            debug("[visit_expression] Line %d: Delegating call_expression to visit_node", line);
        }
        /* Call visit_node to trigger handle_call_expression via dispatch table */
        visit_node(node, source_code, directory, filename, result, filter);
    }
    /* Handle binary expressions like a + b, a && b */
    else if (node_sym == ts_symbols.binary_expression) {
        if (g_debug) {
            debug("[visit_expression] Line %d: Visiting binary_expression children", line);
        }
        TSNode left = ts_node_child_by_field_name(node, "left", 4);
        TSNode right = ts_node_child_by_field_name(node, "right", 5);

        if (!ts_node_is_null(left)) {
            visit_expression(left, source_code, directory, filename, result, filter);
        }
        if (!ts_node_is_null(right)) {
            visit_expression(right, source_code, directory, filename, result, filter);
        }
    }
    /* Handle unary expressions like !x, -y, ++i */
    else if (node_sym == ts_symbols.unary_expression || node_sym == ts_symbols.update_expression) {
        if (g_debug) {
            debug("[visit_expression] Line %d: Visiting unary/update expression", line);
        }
        TSNode argument = ts_node_child_by_field_name(node, "argument", 8);
        if (!ts_node_is_null(argument)) {
            visit_expression(argument, source_code, directory, filename, result, filter);
        }
    }
    /* Handle parenthesized expressions like (x + y) */
    else if (node_sym == ts_symbols.parenthesized_expression) {
        if (g_debug) {
            debug("[visit_expression] Line %d: Visiting parenthesized_expression children", line);
        }
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            visit_expression(child, source_code, directory, filename, result, filter);
        }
    }
    /* Handle ternary/conditional expressions like a ? b : c */
    else if (node_sym == ts_symbols.ternary_expression) {
        if (g_debug) {
            debug("[visit_expression] Line %d: Visiting ternary_expression children", line);
        }
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
    /* Handle assignment expressions - delegate to visit_node so handler gets called */
    else if (node_sym == ts_symbols.assignment_expression || node_sym == ts_symbols.augmented_assignment_expression) {
        if (g_debug) {
            debug("[visit_expression] Line %d: Delegating assignment to visit_node", line);
        }
        /* Call visit_node to trigger handle_assignment_expression via dispatch table */
        visit_node(node, source_code, directory, filename, result, filter);
    }
    /* Handle subscript expressions like array[index] */
    else if (node_sym == ts_symbols.subscript_expression) {
        if (g_debug) {
            debug("[visit_expression] Line %d: Visiting subscript_expression children", line);
        }
        TSNode object = ts_node_child_by_field_name(node, "object", 6);
        TSNode index = ts_node_child_by_field_name(node, "index", 5);

        if (!ts_node_is_null(object)) {
            visit_expression(object, source_code, directory, filename, result, filter);
        }
        if (!ts_node_is_null(index)) {
            visit_expression(index, source_code, directory, filename, result, filter);
        }
    }
    /* Handle arrow functions and function expressions - delegate to visit_node */
    else if (node_sym == ts_symbols.arrow_function || node_sym == ts_symbols.function_expression) {
        if (g_debug) {
            debug("[visit_expression] Line %d: Delegating function to visit_node", line);
        }
        /* Call visit_node to trigger handlers via dispatch table */
        visit_node(node, source_code, directory, filename, result, filter);
    }
    /* Handle object literals like {a, b, c} */
    else if (node_sym == ts_symbols.object) {
        if (g_debug) {
            debug("[visit_expression] Line %d: Visiting object literal children", line);
        }
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            visit_expression(child, source_code, directory, filename, result, filter);
        }
    }
    /* Handle array literals like [1, 2, 3] */
    else if (node_sym == ts_symbols.array) {
        if (g_debug) {
            debug("[visit_expression] Line %d: Visiting array literal children", line);
        }
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            visit_expression(child, source_code, directory, filename, result, filter);
        }
    }
    /* Handle template strings - delegate to visit_node */
    else if (node_sym == ts_symbols.template_string) {
        if (g_debug) {
            debug("[visit_expression] Line %d: Delegating template_string to visit_node", line);
        }
        /* Call visit_node to trigger handle_string_literal via dispatch table */
        visit_node(node, source_code, directory, filename, result, filter);
    }
    /* Default case: recursively visit all children for unknown expression types */
    else {
        if (g_debug) {
            const char *node_type = ts_node_type(node);  /* Only for debug output */
            debug("[visit_expression] Line %d: Default case for '%s', visiting children", line, node_type);
        }
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            const char *child_type = ts_node_type(child);
            /* Skip punctuation and keywords */
            if (strcmp(child_type, "(") != 0 && strcmp(child_type, ")") != 0 &&
                strcmp(child_type, ",") != 0 && strcmp(child_type, ";") != 0 &&
                strcmp(child_type, "{") != 0 && strcmp(child_type, "}") != 0 &&
                strcmp(child_type, "[") != 0 && strcmp(child_type, "]") != 0) {
                visit_expression(child, source_code, directory, filename, result, filter);
            }
        }
    }
}

/* Extract access modifier (public, private, protected) from method or property */
static void extract_access_modifier(TSNode node, const char *source_code, char *scope_buf, size_t buf_size, const char *filename) {
    /* Check first child for accessibility_modifier */
    if (ts_node_child_count(node) == 0) {
        scope_buf[0] = '\0';
        return;
    }

    TSNode first_child = ts_node_child(node, 0);
    if (ts_node_symbol(first_child) == ts_symbols.accessibility_modifier) {
        /* Extract text from the accessibility_modifier node */
        safe_extract_node_text(source_code, first_child, scope_buf, buf_size, filename);
    } else {
        scope_buf[0] = '\0';
    }
}

/* Extract behavioral modifiers (static, async, readonly, abstract) from node
 * modifiers_to_check: NULL-terminated array of modifier names to look for
 * Example: const char *mods[] = {"static", "async", NULL};
 * Builds space-separated string in order modifiers appear in array */
static void extract_modifiers(TSNode node, char *modifier_buf, size_t buf_size,
                              const char *modifiers_to_check[]) {
    modifier_buf[0] = '\0';

    if (modifiers_to_check == NULL || buf_size == 0) {
        return;
    }

    /* Count how many modifiers to check for */
    int num_to_check = 0;
    while (modifiers_to_check[num_to_check] != NULL) {
        num_to_check++;
    }

    if (num_to_check == 0) {
        return;
    }

    /* Track which modifiers we found */
    bool found_modifiers[10] = {false}; /* Max 10 different modifier types */

    /* Scan children for modifier nodes */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        /* Check against each modifier in the list */
        for (int j = 0; j < num_to_check; j++) {
            if (strcmp(child_type, modifiers_to_check[j]) == 0) {
                found_modifiers[j] = true;
            }
        }
    }

    /* Build space-separated string in order specified */
    int offset = 0;
    bool first = true;
    for (int j = 0; j < num_to_check; j++) {
        if (found_modifiers[j]) {
            int written = snprintf(modifier_buf + offset, buf_size - (size_t)offset,
                                  "%s%s", first ? "" : " ", modifiers_to_check[j]);
            if (written < 0 || written >= (int)(buf_size - (size_t)offset)) {
                break;  /* Buffer full or error */
            }
            offset += written;
            first = false;
        }
    }
}

/* Type extraction strategy classification
 *
 * Source: tree-sitter-typescript grammar (as of 2024)
 * Files: ./tree-sitter-typescript/common/define-grammar.js (lines 723-1065)
 *        ./tree-sitter-typescript/typescript/src/node-types.json
 *
 * From define-grammar.js:
 *   type: choice(
 *     primary_type,
 *     function_type,
 *     readonly_type,
 *     constructor_type,
 *     infer_type,
 *     ...
 *   )
 *
 *   primary_type: choice(
 *     parenthesized_type,      // (Type)
 *     predefined_type,         // string, number, boolean, void, any, unknown, never, object
 *     _type_identifier,        // User-defined type names (aliased from identifier)
 *     nested_type_identifier,  // Module.Type
 *     generic_type,            // Array<T>, Map<K, V>
 *     object_type,             // { key: Type }
 *     array_type,              // Type[]
 *     tuple_type,              // [Type1, Type2]
 *     flow_maybe_type,         // ?Type
 *     type_query,              // typeof x
 *     index_type_query,        // keyof Type
 *     this_type,               // this
 *     existential_type,        // * (Flow)
 *     literal_type,            // "string" | 123 | true | false | null
 *     lookup_type,             // Type["key"]
 *     conditional_type,        // T extends U ? X : Y
 *     template_literal_type,   // `string ${Type}`
 *     intersection_type,       // Type1 & Type2
 *     union_type,              // Type1 | Type2
 *     'const'                  // const type modifier
 *   )
 *
 *   readonly_type: seq('readonly', type)       // readonly Type
 *   function_type: seq(params, '=>', type)     // (x: T) => R
 *   constructor_type: seq('new', params, '=>', type)  // new (x: T) => R
 *
 * Additional type-related nodes:
 *   - type_annotation: seq(':', type)          // Container for type annotations
 *   - type_predicate: seq(name, 'is', type)    // arg is Type
 *   - type_arguments: seq('<', types, '>')     // Generic type arguments
 *   - mapped_type_clause: ...                  // [K in Type]: Value
 *   - optional_type: seq(type, '?')            // Type?
 *   - rest_type: seq('...', type)              // ...Type
 *
 * Update this enum when the TypeScript language grammar changes.
 */
typedef enum {
    TYPE_EXTRACT_SIMPLE,      /* Simple text extraction (type_identifier, predefined_type) */
    TYPE_EXTRACT_GENERIC,     /* Generic types - custom logic to simplify type arguments */
    TYPE_EXTRACT_SIMPLIFIED,  /* Complex types that get simplified (object_type -> "object") */
    TYPE_EXTRACT_UNION,       /* Union/intersection types - extract full text */
    TYPE_EXTRACT_COMPLEX,     /* Complex types - extract full text */
    TYPE_EXTRACT_RECURSE,     /* Needs recursion (parenthesized_type, readonly_type) */
    TYPE_EXTRACT_SKIP,        /* Don't extract - punctuation, operators */
    TYPE_NOT_A_TYPE           /* Not a recognized type node */
} TypeExtractionStrategy;

static TypeExtractionStrategy classify_type_node_by_symbol(TSSymbol node_sym) {
    /* Simple types: direct text extraction */
    if (node_sym == ts_symbols.type_identifier ||
        node_sym == ts_symbols.predefined_type ||
        node_sym == ts_symbols.identifier ||
        node_sym == ts_symbols.nested_type_identifier ||
        node_sym == ts_symbols.this_type ||
        node_sym == ts_symbols.existential_type) {
        return TYPE_EXTRACT_SIMPLE;
    }

    /* Generic types: special handling to simplify type arguments */
    /* Example: ReadonlyArray<{x: number}> -> ReadonlyArray<object> */
    if (node_sym == ts_symbols.generic_type) {
        return TYPE_EXTRACT_GENERIC;
    }

    /* Complex types that we simplify to single keywords for readability */
    /* These are too verbose to include in full, so we use simplified names */
    /* literal_type: can be huge strings (539+ bytes) - simplify to "string" */
    if (node_sym == ts_symbols.object_type ||
        node_sym == ts_symbols.array_type ||
        node_sym == ts_symbols.tuple_type ||
        node_sym == ts_symbols.literal_type) {
        return TYPE_EXTRACT_SIMPLIFIED;
    }

    /* Union and intersection types: extract full text with operators */
    if (node_sym == ts_symbols.union_type ||
        node_sym == ts_symbols.intersection_type) {
        return TYPE_EXTRACT_UNION;
    }

    /* Complex types: extract full text including structure */
    if (node_sym == ts_symbols.function_type ||
        node_sym == ts_symbols.constructor_type ||
        node_sym == ts_symbols.type_query ||
        node_sym == ts_symbols.index_type_query ||
        node_sym == ts_symbols.literal_type ||
        node_sym == ts_symbols.lookup_type ||
        node_sym == ts_symbols.conditional_type ||
        node_sym == ts_symbols.template_literal_type ||
        node_sym == ts_symbols.infer_type ||
        node_sym == ts_symbols.type_predicate ||
        node_sym == ts_symbols.mapped_type_clause ||
        node_sym == ts_symbols.flow_maybe_type ||
        node_sym == ts_symbols.optional_type ||
        node_sym == ts_symbols.rest_type) {
        return TYPE_EXTRACT_COMPLEX;
    }

    /* Types that need recursion to unwrap/process children */
    if (node_sym == ts_symbols.parenthesized_type ||
        node_sym == ts_symbols.readonly_type) {
        return TYPE_EXTRACT_RECURSE;
    }

    /* Skip punctuation and operators - these are structural elements */
    /* Commented out: punctuation checking with strcmp - not needed with symbol-based approach */
    /* Punctuation symbols won't match any of our type symbols above, so they naturally return TYPE_NOT_A_TYPE */
    /*
    const char *node_type = ts_node_type(???);
    if (strcmp(node_type, ":") == 0 ||
        strcmp(node_type, "<") == 0 ||
        strcmp(node_type, ">") == 0 ||
        strcmp(node_type, ",") == 0 ||
        strcmp(node_type, "|") == 0 ||
        strcmp(node_type, "&") == 0 ||
        strcmp(node_type, "(") == 0 ||
        strcmp(node_type, ")") == 0 ||
        strcmp(node_type, "[") == 0 ||
        strcmp(node_type, "]") == 0 ||
        strcmp(node_type, "{") == 0 ||
        strcmp(node_type, "}") == 0 ||
        strcmp(node_type, "?") == 0) {
        return TYPE_EXTRACT_SKIP;
    }
    */

    return TYPE_NOT_A_TYPE;
}

/* Helper: Check if a node is a valid TypeScript type node */
static bool is_typescript_type_node(TSSymbol node_sym) {
    TypeExtractionStrategy strategy = classify_type_node_by_symbol(node_sym);
    return strategy != TYPE_NOT_A_TYPE && strategy != TYPE_EXTRACT_SKIP;
}

/* Forward declaration for recursive type extraction */
static void extract_type_recursively(TSNode type_node, const char *source_code,
                                     char *type_buffer, size_t type_size, const char *filename);

/* Extract generic type with simplified type arguments
 * Example: ReadonlyArray<{ complex }> -> ReadonlyArray<object> */
static void extract_generic_type_simplified(TSNode generic_node, const char *source_code,
                                            char *type_buffer, size_t type_size, const char *filename) {
    char base_type[SYMBOL_MAX_LENGTH] = "";
    char simplified_args[SYMBOL_MAX_LENGTH] = "";
    int offset = 0;

    uint32_t child_count = ts_node_child_count(generic_node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(generic_node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        /* Extract base type identifier (e.g., "Array", "ReadonlyArray") */
        if (child_sym == ts_symbols.type_identifier) {
            safe_extract_node_text(source_code, child, base_type, sizeof(base_type), filename);
        }
        /* Process type arguments - simplify each argument */
        else if (child_sym == ts_symbols.type_arguments) {
            uint32_t arg_count = ts_node_child_count(child);
            int first_arg = 1;
            for (uint32_t j = 0; j < arg_count; j++) {
                TSNode arg = ts_node_child(child, j);
                const char *arg_type = ts_node_type(arg);

                /* Skip angle brackets and commas */
                if (strcmp(arg_type, "<") == 0 || strcmp(arg_type, ">") == 0 || strcmp(arg_type, ",") == 0) {
                    if (strcmp(arg_type, ",") == 0 && offset < (int)sizeof(simplified_args) - 3) {
                        offset += snprintf(simplified_args + offset, sizeof(simplified_args) - (size_t)offset, ", ");
                    }
                    continue;
                }

                /* Recursively extract/simplify each type argument */
                char arg_text[SYMBOL_MAX_LENGTH];
                extract_type_recursively(arg, source_code, arg_text, sizeof(arg_text), filename);

                if (arg_text[0] && offset < (int)sizeof(simplified_args) - 1) {
                    if (!first_arg && simplified_args[offset - 2] != ',') {
                        offset += snprintf(simplified_args + offset, sizeof(simplified_args) - (size_t)offset, ", ");
                    }
                    offset += snprintf(simplified_args + offset, sizeof(simplified_args) - (size_t)offset, "%s", arg_text);
                    first_arg = 0;
                }
            }
        }
    }

    /* Build final type: BaseType<args> */
    if (simplified_args[0]) {
        snprintf(type_buffer, type_size, "%s<%s>", base_type, simplified_args);
    } else {
        snprintf(type_buffer, type_size, "%s", base_type);
    }
}

/* Recursively extract type, using classification strategy */
static void extract_type_recursively(TSNode type_node, const char *source_code,
                                     char *type_buffer, size_t type_size, const char *filename) {
    type_buffer[0] = '\0';

    if (ts_node_is_null(type_node)) {
        return;
    }

    TSSymbol node_sym = ts_node_symbol(type_node);
    TypeExtractionStrategy strategy = classify_type_node_by_symbol(node_sym);

    switch (strategy) {
        case TYPE_EXTRACT_SIMPLE:
            /* Simple types: direct text extraction */
            safe_extract_node_text(source_code, type_node, type_buffer, type_size, filename);
            break;

        case TYPE_EXTRACT_GENERIC:
            /* Generic types: custom logic to simplify type arguments */
            /* Example: ReadonlyArray<{x: number}> -> ReadonlyArray<object> */
            extract_generic_type_simplified(type_node, source_code, type_buffer, type_size, filename);
            break;

        case TYPE_EXTRACT_SIMPLIFIED:
            /* Complex types simplified to single keywords */
            if (node_sym == ts_symbols.object_type) {
                snprintf(type_buffer, type_size, "object");
            } else if (node_sym == ts_symbols.array_type) {
                snprintf(type_buffer, type_size, "array");
            } else if (node_sym == ts_symbols.tuple_type) {
                snprintf(type_buffer, type_size, "tuple");
            } else if (node_sym == ts_symbols.literal_type) {
                /* Literal types: 'string' | 123 | true | false | null */
                /* Simplify to "string" to avoid huge literal strings in type column */
                snprintf(type_buffer, type_size, "string");
            }
            break;

        case TYPE_EXTRACT_UNION:
            /* Union and intersection types: simplify to avoid buffer overflow */
            /* These can be extremely large (e.g., 696+ bytes) */
            if (node_sym == ts_symbols.union_type) {
                snprintf(type_buffer, type_size, "union");
            } else if (node_sym == ts_symbols.intersection_type) {
                snprintf(type_buffer, type_size, "intersection");
            }
            break;

        case TYPE_EXTRACT_COMPLEX:
            /* Complex types: extract full text */
            safe_extract_node_text(source_code, type_node, type_buffer, type_size, filename);
            break;

        case TYPE_EXTRACT_RECURSE:
            /* Types that need recursion to unwrap children */
            /* For parenthesized_type and readonly_type, recurse to inner type */
            {
                uint32_t child_count = ts_node_child_count(type_node);
                for (uint32_t i = 0; i < child_count; i++) {
                    TSNode child = ts_node_child(type_node, i);
                    if (is_typescript_type_node(ts_node_symbol(child))) {
                        extract_type_recursively(child, source_code, type_buffer, type_size, filename);
                        return;
                    }
                }
            }
            break;

        case TYPE_EXTRACT_SKIP:
        case TYPE_NOT_A_TYPE:
            /* Skip - leave buffer empty */
            break;
    }
}

static void extract_type_from_annotation(TSNode type_annotation_node, const char *source_code,
                                         char *type_buffer, size_t type_size, const char *filename) {
    type_buffer[0] = '\0';

    if (ts_node_is_null(type_annotation_node)) {
        return;
    }

    /* type_annotation contains a colon ':' followed by the actual type node
     * Find the first non-colon child and extract its full text */
    uint32_t child_count = ts_node_child_count(type_annotation_node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(type_annotation_node, i);
        const char *child_type = ts_node_type(child);

        /* Skip the colon ':' */
        if (strcmp(child_type, ":") == 0) {
            continue;
        }

        /* Use recursive extraction to handle nested complex types */
        extract_type_recursively(child, source_code, type_buffer, type_size, filename);
        return;
    }
}

static void handle_import_statement(TSNode node, const char *source_code, const char *directory,
                                    const char *filename, ParseResult *result, SymbolFilter *filter,
                                    int line) {
    /* Query-based approach - replaces 4 nested loops with declarative pattern matching */
    const char *query_string =
        "(import_statement"
        "  (import_clause"
        "    (named_imports"
        "      (import_specifier"
        "        name: (identifier) @importname))))"
        "\n"
        "(import_statement"
        "  (import_clause"
        "    (named_imports"
        "      (import_specifier"
        "        !name"
        "        (identifier) @importdirect))))";

    TSQuery *query = compile_query(tree_sitter_typescript(), &import_query, query_string);
    if (!query) return;

    TSQueryCursor *cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, query, node);

    TSQueryMatch match;
    char symbol[SYMBOL_MAX_LENGTH];

    while (ts_query_cursor_next_match(cursor, &match)) {
        for (uint16_t i = 0; i < match.capture_count; i++) {
            TSQueryCapture capture = match.captures[i];
            get_capture_text(source_code, capture.node, symbol, sizeof(symbol), filename);

            if (filter_should_index(filter, symbol)) {
                add_entry(result, symbol, line, CONTEXT_IMPORT, directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
            }
        }
    }

    ts_query_cursor_delete(cursor);

    /* Original loop-based approach (commented out for comparison)
    char symbol[SYMBOL_MAX_LENGTH];
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_symbol(child) == ts_symbols.import_clause) {
            uint32_t clause_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < clause_count; j++) {
                TSNode clause_child = ts_node_child(child, j);
                if (ts_node_symbol(clause_child) == ts_symbols.named_imports) {
                    uint32_t spec_count = ts_node_child_count(clause_child);
                    for (uint32_t k = 0; k < spec_count; k++) {
                        TSNode spec = ts_node_child(clause_child, k);
                        if (ts_node_symbol(spec) == ts_symbols.import_specifier) {
                            TSNode id = ts_node_child_by_field_name(spec, "name", 4);
                            if (ts_node_is_null(id)) {
                                uint32_t id_count = ts_node_child_count(spec);
                                for (uint32_t m = 0; m < id_count; m++) {
                                    TSNode id_child = ts_node_child(spec, m);
                                    if (ts_node_symbol(id_child) == ts_symbols.identifier) {
                                        safe_extract_node_text(source_code, id_child, symbol, sizeof(symbol), filename);
                                        if (filter_should_index(filter, symbol)) {
                                            add_entry(result, symbol, line, CONTEXT_IMPORT, directory, filename, NO_EXTENSIBLE_COLUMNS);
                                        }
                                        break;
                                    }
                                }
                            } else {
                                safe_extract_node_text(source_code, id, symbol, sizeof(symbol), filename);
                                if (filter_should_index(filter, symbol)) {
                                    add_entry(result, symbol, line, CONTEXT_IMPORT, directory, filename, NO_EXTENSIBLE_COLUMNS);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    */

    /* Visit children to process any nested declarations */
    process_children(node, source_code, directory, filename, result, filter);
}

static void handle_export_statement(TSNode node, const char *source_code, const char *directory,
                                    const char *filename, ParseResult *result, SymbolFilter *filter,
                                    int line) {
    /* Query-based approach - replaces 3 nested code paths with declarative patterns */
    char symbol[SYMBOL_MAX_LENGTH];

    /* Query 1: export { A, B } or export { C as D } */
    const char *clause_query_str =
        "(export_statement"
        "  (export_clause"
        "    (export_specifier"
        "      alias: (identifier) @exportalias)))"
        "\n"
        "(export_statement"
        "  (export_clause"
        "    (export_specifier"
        "      !alias"
        "      name: (identifier) @exportname)))";

    TSQuery *clause_query = compile_query(tree_sitter_typescript(), &export_clause_query, clause_query_str);
    if (clause_query) {
        TSQueryCursor *cursor = ts_query_cursor_new();
        ts_query_cursor_exec(cursor, clause_query, node);
        TSQueryMatch match;

        while (ts_query_cursor_next_match(cursor, &match)) {
            for (uint16_t i = 0; i < match.capture_count; i++) {
                get_capture_text(source_code, match.captures[i].node, symbol, sizeof(symbol), filename);
                if (filter_should_index(filter, symbol)) {
                    add_entry(result, symbol, line, CONTEXT_EXPORT, directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
                }
            }
        }
        ts_query_cursor_delete(cursor);
    }

    /* Query 2: export class/function/interface/type
     * NOTE: Classes/interfaces/types use type_identifier, functions use identifier */
    const char *decl_query_str =
        "(export_statement (class_declaration name: (type_identifier) @exportdecl))"
        "\n"
        "(export_statement (function_declaration name: (identifier) @exportdecl))"
        "\n"
        "(export_statement (interface_declaration name: (type_identifier) @exportdecl))"
        "\n"
        "(export_statement (type_alias_declaration name: (type_identifier) @exportdecl))";

    TSQuery *decl_query = compile_query(tree_sitter_typescript(), &export_decl_query, decl_query_str);
    if (decl_query) {
        TSQueryCursor *cursor = ts_query_cursor_new();
        ts_query_cursor_exec(cursor, decl_query, node);
        TSQueryMatch match;

        while (ts_query_cursor_next_match(cursor, &match)) {
            for (uint16_t i = 0; i < match.capture_count; i++) {
                get_capture_text(source_code, match.captures[i].node, symbol, sizeof(symbol), filename);
                if (filter_should_index(filter, symbol)) {
                    add_entry(result, symbol, line, CONTEXT_EXPORT, directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
                }
            }
        }
        ts_query_cursor_delete(cursor);
    }

    /* Query 3: export const/let */
    const char *var_query_str =
        "(export_statement"
        "  (lexical_declaration"
        "    (variable_declarator"
        "      name: (identifier) @exportvar)))";

    TSQuery *var_query = compile_query(tree_sitter_typescript(), &export_var_query, var_query_str);
    if (var_query) {
        TSQueryCursor *cursor = ts_query_cursor_new();
        ts_query_cursor_exec(cursor, var_query, node);
        TSQueryMatch match;

        while (ts_query_cursor_next_match(cursor, &match)) {
            for (uint16_t i = 0; i < match.capture_count; i++) {
                get_capture_text(source_code, match.captures[i].node, symbol, sizeof(symbol), filename);
                if (filter_should_index(filter, symbol)) {
                    add_entry(result, symbol, line, CONTEXT_EXPORT, directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
                }
            }
        }
        ts_query_cursor_delete(cursor);
    }

    /* Original loop-based approach (commented out for comparison)
    char symbol[SYMBOL_MAX_LENGTH];
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == ts_symbols.export_clause) {
            uint32_t spec_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < spec_count; j++) {
                TSNode spec = ts_node_child(child, j);
                if (ts_node_symbol(spec) == ts_symbols.export_specifier) {
                    TSNode name_node = ts_node_child_by_field_name(spec, "alias", 5);
                    if (ts_node_is_null(name_node)) {
                        name_node = ts_node_child_by_field_name(spec, "name", 4);
                    }
                    if (!ts_node_is_null(name_node)) {
                        safe_extract_node_text(source_code, name_node, symbol, sizeof(symbol), filename);
                        if (filter_should_index(filter, symbol)) {
                            add_entry(result, symbol, line, CONTEXT_EXPORT, directory, filename, NO_EXTENSIBLE_COLUMNS);
                        }
                    }
                }
            }
        }
        else if (child_sym == ts_symbols.class_declaration ||
                 child_sym == ts_symbols.function_declaration ||
                 child_sym == ts_symbols.interface_declaration ||
                 child_sym == ts_symbols.type_alias_declaration) {
            TSNode name_node = ts_node_child_by_field_name(child, "name", 4);
            if (!ts_node_is_null(name_node)) {
                safe_extract_node_text(source_code, name_node, symbol, sizeof(symbol), filename);
                if (filter_should_index(filter, symbol)) {
                    add_entry(result, symbol, line, CONTEXT_EXPORT, directory, filename, NO_EXTENSIBLE_COLUMNS);
                }
            }
        }
        else if (child_sym == ts_symbols.lexical_declaration) {
            uint32_t decl_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < decl_count; j++) {
                TSNode decl_child = ts_node_child(child, j);
                if (ts_node_symbol(decl_child) == ts_symbols.variable_declarator) {
                    TSNode name_node = ts_node_child_by_field_name(decl_child, "name", 4);
                    if (!ts_node_is_null(name_node)) {
                        safe_extract_node_text(source_code, name_node, symbol, sizeof(symbol), filename);
                        if (filter_should_index(filter, symbol)) {
                            add_entry(result, symbol, line, CONTEXT_EXPORT, directory, filename, NO_EXTENSIBLE_COLUMNS);
                        }
                    }
                }
            }
        }
    }
    */

    /* Visit children to process the underlying declarations (class, interface, type, etc.) */
    process_children(node, source_code, directory, filename, result, filter);
}

static void handle_template_substitution(TSNode node, const char *source_code, const char *directory,
                                         const char *filename, ParseResult *result, SymbolFilter *filter,
                                         int line) {
    /* Extract identifiers from inside ${...} - the middle child is the expression */
    uint32_t child_count = ts_node_child_count(node);

    /* template_substitution has 3 children: ${, expression, } */
    /* The expression (child 1) could be an identifier, member_expression, etc. */
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);
        const char *child_type = ts_node_type(child);

        /* Skip the delimiters ${  and } */
        if (strcmp(child_type, "${") == 0 || strcmp(child_type, "}") == 0) {
            continue;
        }

        /* Handle identifier directly */
        if (child_sym == ts_symbols.identifier) {
            char identifier[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, identifier, sizeof(identifier), filename);

            if (filter_should_index(filter, identifier)) {
                add_entry(result, identifier, line, CONTEXT_VARIABLE, directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
            }
        } else if (child_sym == ts_symbols.member_expression) {
            /* For member expressions like this.name, extract the property */
            TSNode property = ts_node_child_by_field_name(child, "property", 8);
            if (!ts_node_is_null(property)) {
                char prop_name[SYMBOL_MAX_LENGTH];
                safe_extract_node_text(source_code, property, prop_name, sizeof(prop_name), filename);

                if (filter_should_index(filter, prop_name)) {
                    /* Get parent object for context */
                    TSNode object = ts_node_child_by_field_name(child, "object", 6);
                    char parent_name[SYMBOL_MAX_LENGTH] = {0};
                    if (!ts_node_is_null(object)) {
                        safe_extract_node_text(source_code, object, parent_name, sizeof(parent_name), filename);
                    }

                    add_entry(result, prop_name, line, CONTEXT_PROPERTY, directory, filename, NULL,
                        &(ExtColumns){.parent = parent_name[0] ? parent_name : NULL, .definition = "1"});
                }
            }
        } else {
            /* For other complex expressions, recurse */
            process_children(child, source_code, directory, filename, result, filter);
        }
    }
}

static void handle_string_literal(TSNode node, const char *source_code, const char *directory,
                                  const char *filename, ParseResult *result, SymbolFilter *filter,
                                  int line) {
    /* Find string_fragment children */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_symbol(child) == ts_symbols.string_fragment) {
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
                                add_entry(result, cleaned, line, CONTEXT_STRING, directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
                            }
                        }
                    }
                    word_start = p + 1;
                    if (*p == '\0') break;
                }
            }
        }
    }

    /* Process all children to handle template_substitution nodes (${...}) */
    process_children(node, source_code, directory, filename, result, filter);
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
                        add_entry(result, cleaned, line, CONTEXT_COMMENT, directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
                    }
                }
            }
            word_start = p + 1;
            if (*p == '\0') break;
        }
    }
}

static void handle_class_declaration(TSNode node, const char *source_code, const char *directory,
                                     const char *filename, ParseResult *result, SymbolFilter *filter,
                                     int line) {
    char symbol[SYMBOL_MAX_LENGTH];
    char modifier[256];

    /* Classes can be abstract in TypeScript */
    const char *class_modifiers[] = {"abstract", NULL};
    extract_modifiers(node, modifier, sizeof(modifier), class_modifiers);

    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (!ts_node_is_null(name_node)) {
        safe_extract_node_text(source_code, name_node, symbol, sizeof(symbol), filename);
        if (filter_should_index(filter, symbol)) {
            add_entry(result, symbol, line, CONTEXT_CLASS, directory, filename, NULL,
                &(ExtColumns){.modifier = modifier, .definition = "1"});
        }
    }

    /* Process type_parameters for generic classes like class Box<T> */
    TSNode type_params = ts_node_child_by_field_name(node, "type_parameters", 15);
    if (!ts_node_is_null(type_params)) {
        process_children(type_params, source_code, directory, filename, result, filter);
    }

    /* Process class body to index properties and methods */
    TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body_node)) {
        process_children(body_node, source_code, directory, filename, result, filter);
    }
}

static void handle_interface_declaration(TSNode node, const char *source_code, const char *directory,
                                         const char *filename, ParseResult *result, SymbolFilter *filter,
                                         int line) {
    char symbol[SYMBOL_MAX_LENGTH];
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (!ts_node_is_null(name_node)) {
        safe_extract_node_text(source_code, name_node, symbol, sizeof(symbol), filename);
        if (filter_should_index(filter, symbol)) {
            add_entry(result, symbol, line, CONTEXT_INTERFACE, directory, filename, NULL, &(ExtColumns){.definition = "1"});
        }
    }

    /* Process interface body to index property signatures and methods */
    TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body_node)) {
        process_children(body_node, source_code, directory, filename, result, filter);
    }
}

static void handle_type_alias(TSNode node, const char *source_code, const char *directory,
                              const char *filename, ParseResult *result, SymbolFilter *filter,
                              int line) {
    char symbol[SYMBOL_MAX_LENGTH];
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (!ts_node_is_null(name_node)) {
        safe_extract_node_text(source_code, name_node, symbol, sizeof(symbol), filename);
        if (filter_should_index(filter, symbol)) {
            add_entry(result, symbol, line, CONTEXT_TYPE, directory, filename, NULL, &(ExtColumns){.definition = "1"});
        }
    }
    /* Process the type alias value (e.g., object_type, union_type) */
    process_children(node, source_code, directory, filename, result, filter);
}

static void handle_type_parameter(TSNode node, const char *source_code, const char *directory,
                                   const char *filename, ParseResult *result, SymbolFilter *filter,
                                   int line) {
    /* Extract generic type parameter name (e.g., TValue in <TValue>) */
    char symbol[SYMBOL_MAX_LENGTH];
    int found = 0;

    /* First try named field (in case some contexts use it) */
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (!ts_node_is_null(name_node)) {
        safe_extract_node_text(source_code, name_node, symbol, sizeof(symbol), filename);
        found = 1;
    } else {
        /* Fallback: type_parameter contains a type_identifier child (not a named field) */
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            const char *child_type = ts_node_type(child);
            if (strcmp(child_type, "type_identifier") == 0) {
                safe_extract_node_text(source_code, child, symbol, sizeof(symbol), filename);
                found = 1;
                break;
            }
        }
    }

    if (found && filter_should_index(filter, symbol)) {
        if (g_debug) {
            fprintf(stderr, "[DEBUG] handle_type_parameter: indexing '%s' at line %d\n", symbol, line);
        }
        add_entry(result, symbol, line, CONTEXT_TYPE, directory, filename, NULL, &(ExtColumns){.definition = "1"});
    }

    /* Don't process children - type_identifier will be handled by identifier handler */
}

static void handle_return_statement(TSNode node, const char *source_code, const char *directory,
                                     const char *filename, ParseResult *result, SymbolFilter *filter,
                                     int line) {
    (void)line; /* Line number comes from the expression itself */

    if (g_debug) {
        debug("[handle_return_statement] Processing return_statement");
    }

    /* Iterate through children to find the return expression */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        /* Skip 'return' keyword and semicolon */
        if (strcmp(child_type, "return") != 0 && strcmp(child_type, ";") != 0) {
            if (g_debug) {
                debug("[handle_return_statement] Visiting return expression: %s", child_type);
            }
            visit_expression(child, source_code, directory, filename, result, filter);
        }
    }
}

static void handle_function_declaration(TSNode node, const char *source_code, const char *directory,
                                        const char *filename, ParseResult *result, SymbolFilter *filter,
                                        int line) {
    char symbol[SYMBOL_MAX_LENGTH];
    char modifier[256];
    char type_str[SYMBOL_MAX_LENGTH];
    char location[128];

    /* Top-level functions can only be async (not static) */
    const char *function_modifiers[] = {"async", NULL};
    extract_modifiers(node, modifier, sizeof(modifier), function_modifiers);

    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (!ts_node_is_null(name_node)) {
        safe_extract_node_text(source_code, name_node, symbol, sizeof(symbol), filename);

        /* Extract return type annotation if present */
        type_str[0] = '\0';
        TSNode type_annotation = ts_node_child_by_field_name(node, "return_type", 11);
        if (!ts_node_is_null(type_annotation)) {
            extract_type_from_annotation(type_annotation, source_code, type_str, sizeof(type_str), filename);
        }

        if (filter_should_index(filter, symbol)) {
            /* Extract source location for full function definition */
            format_source_location(node, location, sizeof(location));

            add_entry(result, symbol, line, CONTEXT_FUNCTION, directory, filename, location,
                &(ExtColumns){.modifier = modifier, .type = type_str[0] ? type_str : NULL, .definition = "1"});
        }
    }

    /* Process type_parameters for generic functions like function foo<T>() */
    TSNode type_params = ts_node_child_by_field_name(node, "type_parameters", 15);
    if (!ts_node_is_null(type_params)) {
        process_children(type_params, source_code, directory, filename, result, filter);
    }

    /* Extract function parameters */
    TSNode params_node = ts_node_child_by_field_name(node, "parameters", 10);
    extract_parameters(params_node, source_code, directory, filename, result, filter, 1);

    /* Process function body to index local variables, strings, calls, etc. */
    TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body_node)) {
        process_children(body_node, source_code, directory, filename, result, filter);
    }
}

static void handle_method_definition(TSNode node, const char *source_code, const char *directory,
                                     const char *filename, ParseResult *result, SymbolFilter *filter,
                                     int line) {
    char symbol[SYMBOL_MAX_LENGTH];
    char scope[SYMBOL_MAX_LENGTH];
    char modifier[256];
    char type_str[SYMBOL_MAX_LENGTH];
    char location[128];

    if (g_debug) fprintf(stderr, "[DEBUG] handle_method_definition called at line %d\n", line);

    /* Extract access modifier (public, private, protected) */
    extract_access_modifier(node, source_code, scope, sizeof(scope), filename);

    /* Extract behavioral modifiers */
    const char *method_modifiers[] = {"static", "async", "readonly", "abstract", NULL};
    extract_modifiers(node, modifier, sizeof(modifier), method_modifiers);

    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (!ts_node_is_null(name_node)) {
        safe_extract_node_text(source_code, name_node, symbol, sizeof(symbol), filename);

        /* Check for ES2019 private method identifier (#) */
        TSSymbol name_sym = ts_node_symbol(name_node);
        if (g_debug) fprintf(stderr, "[DEBUG] handle_method_definition: name_type='%s', symbol='%s'\n", ts_node_type(name_node), symbol);
        if (name_sym == ts_symbols.private_property_identifier) {
            /* ES2019 private methods are always private, override scope */
            if (g_debug) fprintf(stderr, "[DEBUG] handle_method_definition: detected private_property_identifier, setting scope to private\n");
            snprintf(scope, sizeof(scope), "private");
        }

        /* Extract return type annotation if present */
        type_str[0] = '\0';
        TSNode type_annotation = ts_node_child_by_field_name(node, "return_type", 11);
        if (!ts_node_is_null(type_annotation)) {
            extract_type_from_annotation(type_annotation, source_code, type_str, sizeof(type_str), filename);
        }

        if (filter_should_index(filter, symbol)) {
            /* Extract source location for full method definition */
            format_source_location(node, location, sizeof(location));

            add_entry(result, symbol, line, CONTEXT_FUNCTION, directory, filename, location,
                &(ExtColumns){.scope = scope, .modifier = modifier, .type = type_str[0] ? type_str : NULL, .definition = "1"});
        }
    }
    /* Extract method parameters */
    TSNode params_node = ts_node_child_by_field_name(node, "parameters", 10);
    extract_parameters(params_node, source_code, directory, filename, result, filter, 1);

    /* Process method body to index local variables, strings, calls, etc. */
    TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body_node)) {
        process_children(body_node, source_code, directory, filename, result, filter);
    }
}

static void handle_variable_declaration(TSNode node, const char *source_code, const char *directory,
                                        const char *filename, ParseResult *result, SymbolFilter *filter,
                                        int line) {
    char symbol[SYMBOL_MAX_LENGTH];
    char type_str[SYMBOL_MAX_LENGTH];
    char location[128];
    TSNode declarator = ts_node_child_by_field_name(node, "declarator", 10);
    if (ts_node_is_null(declarator)) {
        /* Try to find variable_declarator child */
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            if (ts_node_symbol(child) == ts_symbols.variable_declarator) {
                TSNode name_node = ts_node_child_by_field_name(child, "name", 4);
                if (!ts_node_is_null(name_node)) {
                    TSSymbol name_sym = ts_node_symbol(name_node);
                    /* Skip destructuring patterns - they're not meaningful symbol names */
                    if (name_sym == ts_symbols.array_pattern || name_sym == ts_symbols.object_pattern) {
                        continue;
                    }
                    safe_extract_node_text(source_code, name_node, symbol, sizeof(symbol), filename);

                    /* Extract type annotation if present */
                    type_str[0] = '\0';
                    TSNode type_annotation = ts_node_child_by_field_name(child, "type", 4);
                    if (!ts_node_is_null(type_annotation)) {
                        extract_type_from_annotation(type_annotation, source_code, type_str, sizeof(type_str), filename);
                    }

                    if (filter_should_index(filter, symbol)) {
                        /* Extract source location for full variable declaration */
                        format_source_location(node, location, sizeof(location));

                        add_entry(result, symbol, line, CONTEXT_VARIABLE, directory, filename, location,
                            &(ExtColumns){.type = type_str[0] ? type_str : NULL, .definition = "1"});
                    }
                }
            }
        }
    }

    /* Process children to traverse into the value (RHS) where lambdas may be */
    process_children(node, source_code, directory, filename, result, filter);
}

static void handle_property_signature(TSNode node, const char *source_code, const char *directory,
                                      const char *filename, ParseResult *result, SymbolFilter *filter,
                                      int line) {
    char symbol[SYMBOL_MAX_LENGTH];
    char scope[SYMBOL_MAX_LENGTH];
    char modifier[256];
    char type_str[SYMBOL_MAX_LENGTH];

    /* Extract access modifier (public, private, protected) */
    extract_access_modifier(node, source_code, scope, sizeof(scope), filename);

    /* Extract behavioral modifiers */
    const char *property_modifiers[] = {"static", "async", "readonly", "abstract", NULL};
    extract_modifiers(node, modifier, sizeof(modifier), property_modifiers);

    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (!ts_node_is_null(name_node)) {
        safe_extract_node_text(source_code, name_node, symbol, sizeof(symbol), filename);

        /* Check for ES2019 private property identifier (#) */
        TSSymbol name_sym = ts_node_symbol(name_node);
        if (g_debug) fprintf(stderr, "[DEBUG] handle_property_signature: name_type='%s', symbol='%s'\n", ts_node_type(name_node), symbol);
        if (name_sym == ts_symbols.private_property_identifier) {
            /* ES2019 private fields are always private, override scope */
            if (g_debug) fprintf(stderr, "[DEBUG] handle_property_signature: detected private_property_identifier, setting scope to private\n");
            snprintf(scope, sizeof(scope), "private");
        }

        /* Extract type annotation if present */
        type_str[0] = '\0';
        TSNode type_annotation = ts_node_child_by_field_name(node, "type", 4);
        if (!ts_node_is_null(type_annotation)) {
            extract_type_from_annotation(type_annotation, source_code, type_str, sizeof(type_str), filename);
        }

        if (filter_should_index(filter, symbol)) {
            add_entry(result, symbol, line, CONTEXT_PROPERTY, directory, filename, NULL,
                &(ExtColumns){.scope = scope, .modifier = modifier, .type = type_str[0] ? type_str : NULL});
        }
    }

    /* Visit type annotation to process nested object type properties */
    TSNode type_annotation = ts_node_child_by_field_name(node, "type", 4);
    if (!ts_node_is_null(type_annotation)) {
        process_children(type_annotation, source_code, directory, filename, result, filter);
    }
}

static void handle_call_expression(TSNode node, const char *source_code, const char *directory,
                                   const char *filename, ParseResult *result, SymbolFilter *filter,
                                   int line) {
    char symbol[SYMBOL_MAX_LENGTH];
    char parent[SYMBOL_MAX_LENGTH];
    parent[0] = '\0';  /* Default: no parent */

    TSNode function_node = ts_node_child_by_field_name(node, "function", 8);
    if (!ts_node_is_null(function_node)) {
        TSSymbol function_sym = ts_node_symbol(function_node);

        /* Direct call: foo() */
        if (function_sym == ts_symbols.identifier) {
            safe_extract_node_text(source_code, function_node, symbol, sizeof(symbol), filename);
            if (filter_should_index(filter, symbol)) {
                add_entry(result, symbol, line, CONTEXT_CALL, directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
            }
        }
        /* Member call: obj.method() or this.target.getBounds() */
        else if (function_sym == ts_symbols.member_expression) {
            /* Get the "object" field (left side of member expression) */
            TSNode object_node = ts_node_child_by_field_name(function_node, "object", 6);
            if (!ts_node_is_null(object_node)) {
                TSSymbol object_sym = ts_node_symbol(object_node);

                if (object_sym == ts_symbols.this_keyword) {
                    /* Direct: this.method() - parent is "this" */
                    snprintf(parent, sizeof(parent), "this");
                } else if (object_sym == ts_symbols.identifier) {
                    /* Direct: obj.method() - parent is the identifier */
                    safe_extract_node_text(source_code, object_node, parent, sizeof(parent), filename);
                } else if (object_sym == ts_symbols.member_expression) {
                    /* Nested: this.target.getBounds() - get "target" from nested property */
                    TSNode nested_prop = ts_node_child_by_field_name(object_node, "property", 8);
                    if (!ts_node_is_null(nested_prop)) {
                        safe_extract_node_text(source_code, nested_prop, parent, sizeof(parent), filename);
                    }
                }
            }

            /* Extract the method/property name */
            TSNode property_node = ts_node_child_by_field_name(function_node, "property", 8);
            if (!ts_node_is_null(property_node)) {
                safe_extract_node_text(source_code, property_node, symbol, sizeof(symbol), filename);
                if (filter_should_index(filter, symbol)) {
                    add_entry(result, symbol, line, CONTEXT_CALL, directory, filename, NULL,
                        &(ExtColumns){.parent = parent});
                }
            }
        }
    }

    /* Index identifier arguments with function name as clue */
    if (symbol[0] != '\0') {
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            TSSymbol child_sym = ts_node_symbol(child);

            if (child_sym == ts_symbols.arguments) {
                /* Iterate through arguments children */
                uint32_t arg_count = ts_node_child_count(child);
                for (uint32_t j = 0; j < arg_count; j++) {
                    TSNode arg = ts_node_child(child, j);
                    TSSymbol arg_sym = ts_node_symbol(arg);

                    /* Index identifier arguments with function name as clue */
                    if (arg_sym == ts_symbols.identifier) {
                        char arg_symbol[SYMBOL_MAX_LENGTH];
                        safe_extract_node_text(source_code, arg, arg_symbol, sizeof(arg_symbol), filename);

                        /* Design decision: Don't filter keywords when used as actual identifiers
                         * (variable names, property names, function names, etc.).
                         * Keywords should only be filtered when used in their keyword context,
                         * not when repurposed as identifiers in code. Strings and comments
                         * are filtered separately by context type. */
                        // if (filter_should_index(filter, arg_symbol)) {
                            add_entry(result, arg_symbol, line, CONTEXT_ARGUMENT,
                                    directory, filename, NULL, &(ExtColumns){.clue = symbol});
                        // }
                    }
                    /* Index member expressions: extract the final property with parent */
                    else if (arg_sym == ts_symbols.member_expression) {
                        TSNode property_node = ts_node_child_by_field_name(arg, "property", 8);
                        if (!ts_node_is_null(property_node)) {
                            char arg_symbol[SYMBOL_MAX_LENGTH];
                            char arg_parent[SYMBOL_MAX_LENGTH] = "";
                            safe_extract_node_text(source_code, property_node, arg_symbol, sizeof(arg_symbol), filename);

                            /* Extract parent from the object side of member expression */
                            TSNode object_node = ts_node_child_by_field_name(arg, "object", 6);
                            if (!ts_node_is_null(object_node)) {
                                TSSymbol object_sym = ts_node_symbol(object_node);
                                if (object_sym == ts_symbols.member_expression) {
                                    /* Nested member: get the property from the parent */
                                    TSNode parent_prop = ts_node_child_by_field_name(object_node, "property", 8);
                                    if (!ts_node_is_null(parent_prop)) {
                                        safe_extract_node_text(source_code, parent_prop, arg_parent, sizeof(arg_parent), filename);
                                    }
                                } else if (object_sym == ts_symbols.identifier || object_sym == ts_symbols.this_keyword) {
                                    /* Direct: obj.prop or this.prop */
                                    safe_extract_node_text(source_code, object_node, arg_parent, sizeof(arg_parent), filename);
                                }
                            }

                            add_entry(result, arg_symbol, line, CONTEXT_ARGUMENT,
                                    directory, filename, NULL, &(ExtColumns){
                                        .parent = arg_parent[0] ? arg_parent : NULL,
                                        .clue = symbol
                                    });
                        }
                    }
                }
                break;  /* Found arguments, no need to continue */
            }
        }
    }

    /* Process children to traverse into argument expressions (e.g., lambdas passed as arguments) */
    process_children(node, source_code, directory, filename, result, filter);
}

static void handle_assignment_expression(TSNode node, const char *source_code, const char *directory,
                                         const char *filename, ParseResult *result, SymbolFilter *filter,
                                         int line) {
    char symbol[SYMBOL_MAX_LENGTH];
    char parent[SYMBOL_MAX_LENGTH];

    if (g_debug) {
        fprintf(stderr, "[DEBUG] handle_assignment_expression: called at line %d\n", line);
    }

    /* Get the left side of the assignment */
    TSNode left_node = ts_node_child_by_field_name(node, "left", 4);
    if (ts_node_is_null(left_node)) {
        return;
    }

    const char *left_type = ts_node_type(left_node);
    if (!left_type || strcmp(left_type, "member_expression") != 0) {
        return;
    }

    /* Extract and index all levels of the member expression chain */
    /* For this.config.timeout, we want to index:
     *   - "timeout" with parent="config"
     *   - "config" with parent="this"
     */
    TSNode current = left_node;
    while (!ts_node_is_null(current)) {
        const char *current_type = ts_node_type(current);
        if (!current_type || strcmp(current_type, "member_expression") != 0) {
            break;
        }

        parent[0] = '\0';

        /* Get the property being assigned */
        TSNode property_node = ts_node_child_by_field_name(current, "property", 8);
        if (ts_node_is_null(property_node)) {
            break;
        }

        safe_extract_node_text(source_code, property_node, symbol, sizeof(symbol), filename);

        /* Get the object (parent) */
        TSNode object_node = ts_node_child_by_field_name(current, "object", 6);
        if (ts_node_is_null(object_node)) {
            break;
        }

        const char *object_type = ts_node_type(object_node);
        if (!object_type) {
            break;
        }

        if (strcmp(object_type, "this") == 0) {
            /* Parent is "this" */
            snprintf(parent, sizeof(parent), "this");
        } else if (strcmp(object_type, "identifier") == 0) {
            /* Parent is an identifier (variable name) */
            safe_extract_node_text(source_code, object_node, parent, sizeof(parent), filename);
        } else if (strcmp(object_type, "member_expression") == 0) {
            /* Parent is another member expression - get its property */
            TSNode parent_property_node = ts_node_child_by_field_name(object_node, "property", 8);
            if (!ts_node_is_null(parent_property_node)) {
                safe_extract_node_text(source_code, parent_property_node, parent, sizeof(parent), filename);
            }
        }

        /* Index this property */
        if (filter_should_index(filter, symbol)) {
            if (g_debug) {
                fprintf(stderr, "[DEBUG] handle_assignment_expression: INDEXING LHS '%s' as PROP at line %d parent='%s'\n",
                        symbol, line, parent[0] ? parent : "(none)");
            }
            add_entry(result, symbol, line, CONTEXT_PROPERTY, directory, filename, NULL,
                &(ExtColumns){.parent = parent[0] ? parent : NULL, .definition = "1"});
        }

        /* Move up the chain */
        current = object_node;
    }

    /* Index the RHS identifier if it's a simple identifier (parameter usage) */
    TSNode right_node = ts_node_child_by_field_name(node, "right", 5);
    if (!ts_node_is_null(right_node)) {
        const char *right_type = ts_node_type(right_node);
        if (right_type && strcmp(right_type, "identifier") == 0) {
            char rhs_symbol[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, right_node, rhs_symbol, sizeof(rhs_symbol), filename);

            if (filter_should_index(filter, rhs_symbol)) {
                if (g_debug) {
                    fprintf(stderr, "[DEBUG] handle_assignment_expression: INDEXING RHS '%s' as VAR at line %d\n",
                            rhs_symbol, line);
                }
                add_entry(result, rhs_symbol, line, CONTEXT_VARIABLE,
                    directory, filename, NULL, &(ExtColumns){.definition = "0"});
            }
        }
    }

    /* Process children to index RHS (e.g., function calls, complex expressions) */
    /* Skip identifier and member_expression children since we already indexed them above */
    if (g_debug) {
        fprintf(stderr, "[DEBUG] handle_assignment_expression: calling process_children with skip at line %d\n", line);
    }
    process_children(node, source_code, directory, filename, result, filter);
}

static void handle_shorthand_property_identifier(TSNode node, const char *source_code, const char *directory,
                                                  const char *filename, ParseResult *result, SymbolFilter *filter,
                                                  int line) {
    char symbol[SYMBOL_MAX_LENGTH];

    /* Extract the property name from shorthand syntax: { id } */
    safe_extract_node_text(source_code, node, symbol, sizeof(symbol), filename);

    if (filter_should_index(filter, symbol)) {
        add_entry(result, symbol, line, CONTEXT_PROPERTY, directory, filename, NULL,
            &(ExtColumns){.definition = "0"});
    }
}

static void handle_pair(TSNode node, const char *source_code, const char *directory,
                        const char *filename, ParseResult *result, SymbolFilter *filter,
                        int line) {
    char symbol[SYMBOL_MAX_LENGTH];

    /* Extract the property name from pair syntax: { name: 'value' } */
    TSNode key_node = ts_node_child_by_field_name(node, "key", 3);
    if (!ts_node_is_null(key_node)) {
        const char *key_type = ts_node_type(key_node);

        /* Only index property_identifier keys, not computed/string keys */
        if (key_type && strcmp(key_type, "property_identifier") == 0) {
            safe_extract_node_text(source_code, key_node, symbol, sizeof(symbol), filename);

            if (filter_should_index(filter, symbol)) {
                add_entry(result, symbol, line, CONTEXT_PROPERTY, directory, filename, NULL,
                    &(ExtColumns){.definition = "0"});
            }
        }
    }
}

static void handle_arrow_function(TSNode node, const char *source_code, const char *directory,
                                   const char *filename, ParseResult *result, SymbolFilter *filter,
                                   int line) {
    /* arrow_function structure:
     * - optional async modifier
     * - formal_parameters
     * - optional type_annotation (return type)
     * - => operator
     * - body (expression or statement_block)
     *
     * We index the lambda itself as "<lambda>" symbol with CONTEXT_LAMBDA
     */

    if (g_debug) fprintf(stderr, "[DEBUG] handle_arrow_function called at line %d in %s\n", line, filename);

    char modifier[256] = "";
    char location[128];

    /* Check for async modifier */
    const char *arrow_modifiers[] = {"async", NULL};
    extract_modifiers(node, modifier, sizeof(modifier), arrow_modifiers);

    /* Extract source location (line range) for the full arrow function */
    format_source_location(node, location, sizeof(location));

    /* Index the arrow function itself */
    add_entry(result, "<lambda>", line, CONTEXT_LAMBDA, directory, filename, location,
        &(ExtColumns){.modifier = modifier[0] ? modifier : NULL, .definition = "1"});

    if (g_debug) fprintf(stderr, "[DEBUG] handle_arrow_function: indexed <lambda> at line %d, location=%s\n", line, location);

    /* Extract arrow function parameters */
    TSNode params_node = ts_node_child_by_field_name(node, "parameters", 10);
    extract_parameters(params_node, source_code, directory, filename, result, filter, 1);

    /* Process remaining children (body, etc.) */
    process_children(node, source_code, directory, filename, result, filter);
}

static void handle_function_expression(TSNode node, const char *source_code, const char *directory,
                                        const char *filename, ParseResult *result, SymbolFilter *filter,
                                        int line) {
    /* function_expression structure:
     * - optional async modifier
     * - function keyword
     * - optional name (for named function expressions - we don't index those separately)
     * - formal_parameters
     * - optional type_annotation (return type)
     * - statement_block (body)
     *
     * We index the lambda itself as "<lambda>" symbol with CONTEXT_LAMBDA
     */

    if (g_debug) fprintf(stderr, "[DEBUG] handle_function_expression called at line %d in %s\n", line, filename);

    char modifier[256] = "";
    char location[128];

    /* Check for async modifier */
    const char *func_expr_modifiers[] = {"async", NULL};
    extract_modifiers(node, modifier, sizeof(modifier), func_expr_modifiers);

    /* Extract source location (line range) for the full function expression */
    format_source_location(node, location, sizeof(location));

    /* Index the function expression itself */
    add_entry(result, "<lambda>", line, CONTEXT_LAMBDA, directory, filename, location,
        &(ExtColumns){.modifier = modifier[0] ? modifier : NULL, .definition = "1"});

    if (g_debug) fprintf(stderr, "[DEBUG] handle_function_expression: indexed <lambda> at line %d, location=%s\n", line, location);

    /* Extract function expression parameters */
    TSNode params_node = ts_node_child_by_field_name(node, "parameters", 10);
    extract_parameters(params_node, source_code, directory, filename, result, filter, 1);

    /* Process remaining children (body, etc.) */
    process_children(node, source_code, directory, filename, result, filter);
}

static void visit_node(TSNode node, const char *source_code, const char *directory,
                      const char *filename, ParseResult *result, SymbolFilter *filter) {
    TSSymbol node_sym = ts_node_symbol(node);
    TSPoint start_point = ts_node_start_point(node);
    int line = (int)(start_point.row + 1);

    if (g_debug) {
        const char *node_type = ts_node_type(node);
        /* Print all node types within class_body for debugging private members */
        TSNode parent = ts_node_parent(node);
        if (!ts_node_is_null(parent)) {
            TSSymbol parent_sym = ts_node_symbol(parent);
            if (parent_sym == ts_symbols.class_body) {
                fprintf(stderr, "[DEBUG] visit_node: found '%s' at line %d inside class_body\n", node_type, line);
            }
        }
        /* Print arrow_function and function_expression nodes for debugging */
        if (node_sym == ts_symbols.arrow_function || node_sym == ts_symbols.function_expression) {
            fprintf(stderr, "[DEBUG] visit_node: found %s at line %d in %s\n", node_type, line, filename);
        }
    }

    /* Fast symbol-based dispatch - if-else chain with early returns */
    if (node_sym == ts_symbols.class_declaration || node_sym == ts_symbols.abstract_class_declaration) {
        handle_class_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == ts_symbols.interface_declaration) {
        handle_interface_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == ts_symbols.type_alias_declaration) {
        handle_type_alias(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == ts_symbols.type_parameter) {
        handle_type_parameter(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == ts_symbols.return_statement) {
        handle_return_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == ts_symbols.function_declaration) {
        handle_function_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == ts_symbols.method_definition) {
        handle_method_definition(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == ts_symbols.lexical_declaration || node_sym == ts_symbols.variable_declaration) {
        handle_variable_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == ts_symbols.property_signature || node_sym == ts_symbols.public_field_definition) {
        handle_property_signature(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == ts_symbols.import_statement) {
        handle_import_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == ts_symbols.export_statement) {
        handle_export_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == ts_symbols.string || node_sym == ts_symbols.template_string || node_sym == ts_symbols.literal_type) {
        handle_string_literal(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == ts_symbols.template_substitution) {
        handle_template_substitution(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == ts_symbols.comment) {
        handle_comment(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == ts_symbols.call_expression) {
        handle_call_expression(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == ts_symbols.assignment_expression || node_sym == ts_symbols.augmented_assignment_expression) {
        handle_assignment_expression(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == ts_symbols.shorthand_property_identifier) {
        handle_shorthand_property_identifier(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == ts_symbols.pair) {
        handle_pair(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == ts_symbols.arrow_function) {
        if (g_debug) {
            fprintf(stderr, "[DEBUG] visit_node: calling handler for arrow_function\n");
        }
        handle_arrow_function(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == ts_symbols.function_expression) {
        if (g_debug) {
            fprintf(stderr, "[DEBUG] visit_node: calling handler for function_expression\n");
        }
        handle_function_expression(node, source_code, directory, filename, result, filter, line);
        return;
    }

    /* No handler found, recursively process children */
    if (g_debug && (node_sym == ts_symbols.arrow_function || node_sym == ts_symbols.function_expression)) {
        fprintf(stderr, "[DEBUG] visit_node: NO HANDLER FOUND for %s, processing children\n", ts_node_type(node));
    }
    process_children(node, source_code, directory, filename, result, filter);
}

int parser_init(TypeScriptParser *parser, SymbolFilter *filter) {
    parser->filter = filter;
    return 0;
}

int parser_parse_file(TypeScriptParser *parser, const char *filepath, const char *project_root, ParseResult *result) {
    if (g_debug) fprintf(stderr, "[DEBUG] parser_parse_file: Starting to parse %s (g_debug=%d)\n", filepath, g_debug);

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
    const TSLanguage *ts_language = tree_sitter_typescript();
    if (!ts_parser_set_language(ts_parser, ts_language)) {
        fprintf(stderr, "ERROR: Failed to set TypeScript language\n");
        free(source_code);
        ts_parser_delete(ts_parser);
        return -1;
    }

    /* Initialize symbol lookup table (only happens once) */
    init_ts_symbols(ts_language);

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

void parser_set_debug(TypeScriptParser *parser, int debug) {
    parser->debug = debug;
    g_debug = debug;
}

void parser_free(TypeScriptParser *parser) {
    (void)parser;
    free_queries();
}
