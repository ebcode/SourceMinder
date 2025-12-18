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
#include "python_language.h"
#include "../shared/constants.h"
#include "../shared/string_utils.h"
#include "../shared/comment_utils.h"
#include "../shared/file_opener.h"
#include "../shared/file_utils.h"
#include "../shared/filter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

/* Declare the tree-sitter Python language */
TSLanguage *tree_sitter_python(void);

/* Global debug flag */
static int g_debug = 0;

/* Symbol table for fast node type comparisons */
static struct {
    /* Dispatch handler symbols */
    TSSymbol function_definition;
    TSSymbol class_definition;
    TSSymbol assignment;
    TSSymbol augmented_assignment;
    TSSymbol named_expression;
    TSSymbol import_statement;
    TSSymbol import_from_statement;
    TSSymbol expression_statement;
    TSSymbol return_statement;
    TSSymbol yield;
    TSSymbol if_statement;
    TSSymbol while_statement;
    TSSymbol for_statement;
    TSSymbol except_clause;
    TSSymbol case_clause;
    TSSymbol list_comprehension;
    TSSymbol dictionary_comprehension;
    TSSymbol set_comprehension;
    TSSymbol generator_expression;
    TSSymbol call;
    TSSymbol lambda;
    TSSymbol interpolation;
    TSSymbol string;
    TSSymbol comment;

    /* Frequently-checked node types */
    TSSymbol identifier;
    TSSymbol attribute;
    TSSymbol decorated_definition;
    TSSymbol decorator;
    TSSymbol pattern_list;
    TSSymbol typed_parameter;
    TSSymbol default_parameter;
    TSSymbol typed_default_parameter;
    TSSymbol list_splat_pattern;
    TSSymbol dictionary_splat_pattern;
    TSSymbol dotted_name;
    TSSymbol aliased_import;
    TSSymbol string_content;
    TSSymbol binary_operator;
    TSSymbol unary_operator;
} python_symbols;

/* Helper function to extract text from a node */
/* Removed: Now using safe_extract_node_text() from shared/string_utils.h */

/* Helper function to process children nodes recursively */
static void process_children(TSNode node, const char *source_code, const char *directory,
                            const char *filename, ParseResult *result, SymbolFilter *filter);

/* Forward declarations */
static void visit_node(TSNode node, const char *source_code, const char *directory,
                      const char *filename, ParseResult *result, SymbolFilter *filter);

static void visit_expression(TSNode node, const char *source_code,
                             const char *directory, const char *filename,
                             ParseResult *result, SymbolFilter *filter);

/* Helper function to extract decorators from a function/class definition */
static void extract_decorators(TSNode node, const char *source_code, char *decorators, size_t decorators_size,
                               const char *directory, const char *filename, ParseResult *result, SymbolFilter *filter) {
    decorators[0] = '\0';

    TSNode parent_node = ts_node_parent(node);
    if (ts_node_is_null(parent_node)) {
        return;
    }

    TSSymbol parent_sym = ts_node_symbol(parent_node);
    if (parent_sym != python_symbols.decorated_definition) {
        return;
    }

    /* Extract decorator names */
    uint32_t parent_child_count = ts_node_child_count(parent_node);
    int first_decorator = 1;

    for (uint32_t i = 0; i < parent_child_count; i++) {
        TSNode sibling = ts_node_child(parent_node, i);
        TSSymbol sibling_sym = ts_node_symbol(sibling);

        if (sibling_sym == python_symbols.decorator) {
            /* Get the decorator identifier (skip the @ symbol) */
            uint32_t dec_child_count = ts_node_child_count(sibling);
            for (uint32_t j = 0; j < dec_child_count; j++) {
                TSNode dec_child = ts_node_child(sibling, j);
                TSSymbol dec_child_sym = ts_node_symbol(dec_child);

                if (dec_child_sym == python_symbols.identifier ||
                    dec_child_sym == python_symbols.attribute) {
                    char decorator_name[SYMBOL_MAX_LENGTH];
                    safe_extract_node_text(source_code, dec_child, decorator_name, sizeof(decorator_name), filename);

                    /* Append to decorators list with @ prefix */
                    if (first_decorator) {
                        int written = snprintf(decorators, decorators_size, "@%s", decorator_name);
                        if (written >= (int)decorators_size) {
                            fprintf(stderr, "FATAL: decorator string exceeds buffer size in %s\n", filename);
                            exit(1);
                        }
                        first_decorator = 0;
                    } else {
                        size_t current_len = strlen(decorators);
                        int written = snprintf(decorators + current_len, decorators_size - current_len,
                                ",@%s", decorator_name);
                        if (written >= (int)(decorators_size - current_len)) {
                            fprintf(stderr, "FATAL: decorator string exceeds buffer size in %s\n", filename);
                            exit(1);
                        }
                    }
                    break;
                } else if (dec_child_sym == python_symbols.call) {
                    /* For decorator calls like @override_settings(...), extract just the function name */
                    TSNode func_node = ts_node_child(dec_child, 0);
                    if (!ts_node_is_null(func_node)) {
                        char decorator_name[SYMBOL_MAX_LENGTH];
                        safe_extract_node_text(source_code, func_node, decorator_name, sizeof(decorator_name), filename);

                        /* Append to decorators list with @ prefix */
                        if (first_decorator) {
                            int written = snprintf(decorators, decorators_size, "@%s", decorator_name);
                            if (written >= (int)decorators_size) {
                                fprintf(stderr, "FATAL: decorator string exceeds buffer size in %s\n", filename);
                                exit(1);
                            }
                            first_decorator = 0;
                        } else {
                            size_t current_len = strlen(decorators);
                            int written = snprintf(decorators + current_len, decorators_size - current_len,
                                    ",@%s", decorator_name);
                            if (written >= (int)(decorators_size - current_len)) {
                                fprintf(stderr, "FATAL: decorator string exceeds buffer size in %s\n", filename);
                                exit(1);
                            }
                        }

                        /* Recursively process the call to index all symbols in the arguments */
                        visit_expression(dec_child, source_code, directory, filename, result, filter);
                    }
                    break;
                }
            }
        }
    }
}

/* Extract parameters from function definition */
static void extract_parameters(TSNode parameters_node, const char *source_code,
                               const char *directory, const char *filename,
                               ParseResult *result, SymbolFilter *filter,
                               int line, const char *parent_function) {
    uint32_t param_count = ts_node_child_count(parameters_node);

    for (uint32_t i = 0; i < param_count; i++) {
        TSNode child = ts_node_child(parameters_node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        /* Handle different parameter types */
        if (child_sym == python_symbols.identifier) {
            char param_name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, param_name, sizeof(param_name), filename);

            /* Skip self and cls - they're conventions, not real parameters */
            if (strcmp(param_name, "self") == 0 || strcmp(param_name, "cls") == 0) {
                continue;
            }

            /* Check if parameter should be indexed */
            if (!filter_should_index(filter, param_name)) {
                continue;
            }

            add_entry(result, param_name, line,
                                 CONTEXT_ARGUMENT, directory, filename, NULL,
                                 &(ExtColumns){.parent = parent_function});
        } else if (child_sym == python_symbols.typed_parameter ||
                   child_sym == python_symbols.default_parameter ||
                   child_sym == python_symbols.typed_default_parameter) {
            /* Get the identifier from typed/default parameters by iterating children */
            uint32_t typed_child_count = ts_node_child_count(child);
            TSNode id_node = {0};
            TSNode type_node = {0};

            for (uint32_t j = 0; j < typed_child_count; j++) {
                TSNode typed_child = ts_node_child(child, j);
                TSSymbol typed_child_sym = ts_node_symbol(typed_child);

                if (typed_child_sym == python_symbols.identifier) {
                    id_node = typed_child;
                } else if (strcmp(ts_node_type(typed_child), "type") == 0) {
                    type_node = typed_child;
                }
            }

            if (!ts_node_is_null(id_node)) {
                char param_name[SYMBOL_MAX_LENGTH];
                safe_extract_node_text(source_code, id_node, param_name, sizeof(param_name), filename);

                if (strcmp(param_name, "self") != 0 && strcmp(param_name, "cls") != 0 &&
                    filter_should_index(filter, param_name)) {
                    /* Extract type annotation if present */
                    char type_str[SYMBOL_MAX_LENGTH] = "";
                    if (!ts_node_is_null(type_node)) {
                        safe_extract_node_text(source_code, type_node, type_str, sizeof(type_str), filename);
                    }

                    add_entry(result, param_name, line,
                                         CONTEXT_ARGUMENT, directory, filename, NULL,
                                         &(ExtColumns){.parent = parent_function, .type = type_str});
                }
            }
        } else if (child_sym == python_symbols.list_splat_pattern) {
            /* Handle *args - variadic positional arguments */
            uint32_t splat_child_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < splat_child_count; j++) {
                TSNode splat_child = ts_node_child(child, j);
                TSSymbol splat_child_sym = ts_node_symbol(splat_child);

                if (splat_child_sym == python_symbols.identifier) {
                    char param_name[SYMBOL_MAX_LENGTH];
                    safe_extract_node_text(source_code, splat_child, param_name, sizeof(param_name), filename);

                    if (filter_should_index(filter, param_name)) {
                        add_entry(result, param_name, line,
                                             CONTEXT_ARGUMENT, directory, filename, NULL,
                                             &(ExtColumns){.parent = parent_function, .clue = "*args"});
                    }
                    break;
                }
            }
        } else if (child_sym == python_symbols.dictionary_splat_pattern) {
            /* Handle **kwargs - variadic keyword arguments */
            uint32_t splat_child_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < splat_child_count; j++) {
                TSNode splat_child = ts_node_child(child, j);
                TSSymbol splat_child_sym = ts_node_symbol(splat_child);

                if (splat_child_sym == python_symbols.identifier) {
                    char param_name[SYMBOL_MAX_LENGTH];
                    safe_extract_node_text(source_code, splat_child, param_name, sizeof(param_name), filename);

                    if (filter_should_index(filter, param_name)) {
                        add_entry(result, param_name, line,
                                             CONTEXT_ARGUMENT, directory, filename, NULL,
                                             &(ExtColumns){.parent = parent_function, .clue = "**kwargs"});
                    }
                    break;
                }
            }
        }
    }
}

/* Handle function definition */
static void handle_function_definition(TSNode node, const char *source_code,
                                      const char *directory, const char *filename,
                                      ParseResult *result, SymbolFilter *filter,
                                      int line) {
    /* Get function name */
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name_node)) {
        return;
    }

    char function_name[SYMBOL_MAX_LENGTH];
    safe_extract_node_text(source_code, name_node, function_name, sizeof(function_name), filename);

    /* Check if function is async */
    const char *modifier = NULL;
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);
        if (strcmp(child_type, "async") == 0) {
            modifier = "async";
            break;
        }
    }

    /* Extract decorators */
    char decorators[SYMBOL_MAX_LENGTH];
    extract_decorators(node, source_code, decorators, sizeof(decorators), directory, filename, result, filter);

    /* Get return type if present */
    char return_type[SYMBOL_MAX_LENGTH] = "";
    TSNode return_type_node = ts_node_child_by_field_name(node, "return_type", 11);
    if (!ts_node_is_null(return_type_node)) {
        safe_extract_node_text(source_code, return_type_node, return_type, sizeof(return_type), filename);
    }

    /* Extract source location for full function definition */
    char location[128];
    format_source_location(node, location, sizeof(location));

    /* Add function to results */
    add_entry(result, function_name, line,
                         CONTEXT_FUNCTION, directory, filename, location,
                         &(ExtColumns){.type = return_type, .definition = "1", .modifier = modifier,
                                      .clue = decorators[0] ? decorators : NULL});

    /* Extract parameters */
    TSNode params_node = ts_node_child_by_field_name(node, "parameters", 10);
    if (!ts_node_is_null(params_node)) {
        extract_parameters(params_node, source_code, directory, filename,
                          result, filter, line, function_name);
    }

    /* Process function body */
    TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body_node)) {
        process_children(body_node, source_code, directory, filename, result, filter);
    }
}

/* Handle class definition */
static void handle_class_definition(TSNode node, const char *source_code,
                                   const char *directory, const char *filename,
                                   ParseResult *result, SymbolFilter *filter,
                                   int line) {
    /* Get class name */
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name_node)) {
        return;
    }

    char class_name[SYMBOL_MAX_LENGTH];
    safe_extract_node_text(source_code, name_node, class_name, sizeof(class_name), filename);

    /* Extract decorators */
    char decorators[SYMBOL_MAX_LENGTH];
    extract_decorators(node, source_code, decorators, sizeof(decorators), directory, filename, result, filter);

    /* Extract source location for full class definition */
    char location[128];
    format_source_location(node, location, sizeof(location));

    /* Extract base class (parent) from argument_list if present */
    char parent_class[SYMBOL_MAX_LENGTH] = "";
    TSNode arg_list = ts_node_child_by_field_name(node, "superclasses", 12);
    if (!ts_node_is_null(arg_list)) {
        /* Find first identifier in argument_list (first base class) */
        uint32_t arg_count = ts_node_child_count(arg_list);
        for (uint32_t i = 0; i < arg_count; i++) {
            TSNode arg_child = ts_node_child(arg_list, i);
            const char *arg_type = ts_node_type(arg_child);

            if (strcmp(arg_type, "identifier") == 0) {
                safe_extract_node_text(source_code, arg_child, parent_class, sizeof(parent_class), filename);
                break;  /* Only track first base class for now */
            }
        }
    }

    /* Add class to results */
    add_entry(result, class_name, line,
                         CONTEXT_CLASS, directory, filename, location,
                         &(ExtColumns){
                             .definition = "1",
                             .parent = parent_class[0] ? parent_class : NULL,
                             .clue = decorators[0] ? decorators : NULL
                         });

    /* Process class body */
    TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body_node)) {
        process_children(body_node, source_code, directory, filename, result, filter);
    }
}

/* Handle assignment statements (variables) */
static void handle_assignment(TSNode node, const char *source_code,
                             const char *directory, const char *filename,
                             ParseResult *result, SymbolFilter *filter,
                             int line) {
    /* Get left side of assignment */
    TSNode left_node = ts_node_child_by_field_name(node, "left", 4);
    if (ts_node_is_null(left_node)) {
        return;
    }

    const char *left_type = ts_node_type(left_node);

    /* Handle simple variable assignment */
    if (strcmp(left_type, "identifier") == 0) {
        char var_name[SYMBOL_MAX_LENGTH];
        safe_extract_node_text(source_code, left_node, var_name, sizeof(var_name), filename);

        if (!filter_should_index(filter, var_name)) {
            return;
        }

        /* Try to extract type from right side or annotation */
        char type_str[SYMBOL_MAX_LENGTH] = "";
        TSNode type_node = ts_node_child_by_field_name(node, "type", 4);
        if (!ts_node_is_null(type_node)) {
            safe_extract_node_text(source_code, type_node, type_str, sizeof(type_str), filename);
        }

        /* Extract source location for full assignment */
        char location[128];
        format_source_location(node, location, sizeof(location));

        add_entry(result, var_name, line,
                             CONTEXT_VARIABLE, directory, filename, location,
                             &(ExtColumns){.type = type_str, .definition = "1"});
    }
    /* Handle tuple unpacking (a, b = 1, 2) */
    else if (strcmp(left_type, "pattern_list") == 0) {
        /* Extract source location for full assignment */
        char location[128];
        format_source_location(node, location, sizeof(location));

        /* Extract each identifier from the pattern_list */
        uint32_t child_count = ts_node_child_count(left_node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(left_node, i);
            const char *child_type = ts_node_type(child);

            if (strcmp(child_type, "identifier") == 0) {
                char var_name[SYMBOL_MAX_LENGTH];
                safe_extract_node_text(source_code, child, var_name, sizeof(var_name), filename);

                if (!filter_should_index(filter, var_name)) {
                    continue;
                }

                add_entry(result, var_name, line,
                                     CONTEXT_VARIABLE, directory, filename, location,
                                     &(ExtColumns){.definition = "1"});
            }
        }
    }
    /* Handle attribute assignment (self.x = ...) */
    else if (strcmp(left_type, "attribute") == 0) {
        TSNode attr_node = ts_node_child_by_field_name(left_node, "attribute", 9);
        if (!ts_node_is_null(attr_node)) {
            char attr_name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, attr_node, attr_name, sizeof(attr_name), filename);

            /* Get the object (e.g., "self") */
            TSNode obj_node = ts_node_child_by_field_name(left_node, "object", 6);
            char parent_obj[SYMBOL_MAX_LENGTH] = "";
            if (!ts_node_is_null(obj_node)) {
                safe_extract_node_text(source_code, obj_node, parent_obj, sizeof(parent_obj), filename);
            }

            /* Extract source location for full assignment */
            char location[128];
            format_source_location(node, location, sizeof(location));

            add_entry(result, attr_name, line,
                                 CONTEXT_PROPERTY, directory, filename, location,
                                 &(ExtColumns){.parent = parent_obj, .definition = "1"});
        }
    }

    /* Process the right-hand side of assignment (e.g., lambda expressions) */
    TSNode right_node = ts_node_child_by_field_name(node, "right", 5);
    if (!ts_node_is_null(right_node)) {
        visit_node(right_node, source_code, directory, filename, result, filter);
    }
}

/* Handle import statements */
static void handle_import_statement(TSNode node, const char *source_code,
                                   const char *directory, const char *filename,
                                   ParseResult *result, SymbolFilter *filter,
                                   int line) {
    (void)filter;  /* Imports are always indexed regardless of filter */
    /* Process import names */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == python_symbols.dotted_name) {
            char import_name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, import_name, sizeof(import_name), filename);

            add_entry(result, import_name, line,
                                 CONTEXT_IMPORT, directory, filename, NULL,
                                 NO_EXTENSIBLE_COLUMNS);
        }
        /* Handle aliased imports: import numpy as np -> index "np" with "numpy" as clue */
        else if (child_sym == python_symbols.aliased_import) {
            /* Extract the original module name from dotted_name */
            char module_name[SYMBOL_MAX_LENGTH] = "";
            TSNode dotted_node = ts_node_child_by_field_name(child, "name", 4);
            if (!ts_node_is_null(dotted_node)) {
                safe_extract_node_text(source_code, dotted_node, module_name, sizeof(module_name), filename);
            }

            /* Extract the alias identifier */
            TSNode alias_node = ts_node_child_by_field_name(child, "alias", 5);
            if (!ts_node_is_null(alias_node)) {
                char alias_name[SYMBOL_MAX_LENGTH];
                safe_extract_node_text(source_code, alias_node, alias_name, sizeof(alias_name), filename);

                add_entry(result, alias_name, line,
                                     CONTEXT_IMPORT, directory, filename, NULL,
                                     &(ExtColumns){.clue = module_name});
            }
        }
    }
}

/* Handle import from statements */
static void handle_import_from_statement(TSNode node, const char *source_code,
                                        const char *directory, const char *filename,
                                        ParseResult *result, SymbolFilter *filter,
                                        int line) {
    (void)filter;  /* Imports are always indexed regardless of filter */
    /* Get the module being imported from */
    TSNode module_node = ts_node_child_by_field_name(node, "module_name", 11);
    (void)module_node;  /* Reserved for future use tracking import sources */

    /* Process imported names */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        if (child_sym == python_symbols.dotted_name) {
            char import_name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, import_name, sizeof(import_name), filename);

            add_entry(result, import_name, line,
                                 CONTEXT_IMPORT, directory, filename, NULL,
                                 NO_EXTENSIBLE_COLUMNS);
        }
        /* Handle aliased imports: from x import y as z -> index "z" with "y" as clue */
        else if (child_sym == python_symbols.aliased_import) {
            /* Extract the original name from dotted_name */
            char original_name[SYMBOL_MAX_LENGTH] = "";
            TSNode dotted_node = ts_node_child_by_field_name(child, "name", 4);
            if (!ts_node_is_null(dotted_node)) {
                safe_extract_node_text(source_code, dotted_node, original_name, sizeof(original_name), filename);
            }

            /* Extract the alias identifier */
            TSNode alias_node = ts_node_child_by_field_name(child, "alias", 5);
            if (!ts_node_is_null(alias_node)) {
                char alias_name[SYMBOL_MAX_LENGTH];
                safe_extract_node_text(source_code, alias_node, alias_name, sizeof(alias_name), filename);

                add_entry(result, alias_name, line,
                                     CONTEXT_IMPORT, directory, filename, NULL,
                                     &(ExtColumns){.clue = original_name});
            }
        }
    }
}

/* Handle f-string interpolations - visit the expression inside */
static void handle_interpolation(TSNode node, const char *source_code,
                                 const char *directory, const char *filename,
                                 ParseResult *result, SymbolFilter *filter,
                                 int line) {
    (void)line;

    /* Visit children of interpolation - these are expressions */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        /* Skip the braces { } */
        if (strcmp(child_type, "{") == 0 || strcmp(child_type, "}") == 0) {
            continue;
        }

        /* Visit the expression inside the interpolation */
        visit_expression(child, source_code, directory, filename, result, filter);
    }
}

/* Handle string literals */
static void handle_string(TSNode node, const char *source_code,
                         const char *directory, const char *filename,
                         ParseResult *result, SymbolFilter *filter,
                         int line) {
    /* Extract words only from string_content nodes (literal parts), not interpolations */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_sym = ts_node_symbol(child);

        /* Only process string_content nodes - skip interpolations */
        if (child_sym == python_symbols.string_content) {
            char string_content[CLEANED_WORD_BUFFER];
            safe_extract_node_text(source_code, child, string_content, sizeof(string_content), filename);

            /* Extract words from string - split on whitespace */
            char word[CLEANED_WORD_BUFFER];
            char cleaned[CLEANED_WORD_BUFFER];
            char *word_start = string_content;

            for (char *p = string_content; ; p++) {
                if (*p == '\0' || isspace(*p)) {
                    if (p > word_start) {
                        size_t word_len = (size_t)(p - word_start);
                        if (word_len < sizeof(word)) {
                            snprintf(word, sizeof(word), "%.*s", (int)word_len, word_start);

                            /* Clean the word */
                            filter_clean_string_symbol(word, cleaned, sizeof(cleaned));

                            if (cleaned[0] && filter_should_index(filter, cleaned)) {
                                add_entry(result, cleaned, line,
                                                     CONTEXT_STRING, directory, filename, NULL,
                                                     NO_EXTENSIBLE_COLUMNS);
                            }
                        }
                    }
                    word_start = p + 1;
                    if (*p == '\0') break;
                }
            }
        }
    }

    /* Process children to handle interpolations in f-strings */
    process_children(node, source_code, directory, filename, result, filter);
}

/* Handle comments */
static void handle_comment(TSNode node, const char *source_code,
                          const char *directory, const char *filename,
                          ParseResult *result, SymbolFilter *filter,
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
                        add_entry(result, cleaned, line,
                                             CONTEXT_COMMENT, directory, filename, NULL,
                                             NO_EXTENSIBLE_COLUMNS);
                    }
                }
            }
            word_start = p + 1;
            if (*p == '\0') break;
        }
    }
}

/* Handle function call expressions */
static void handle_call(TSNode node, const char *source_code,
                       const char *directory, const char *filename,
                       ParseResult *result, SymbolFilter *filter,
                       int line) {
    if (g_debug) fprintf(stderr, "[DEBUG] handle_call called at line %d\n", line);

    /* Check if this call is awaited */
    const char *modifier = NULL;
    TSNode parent_node = ts_node_parent(node);
    if (!ts_node_is_null(parent_node)) {
        const char *parent_type = ts_node_type(parent_node);
        if (strcmp(parent_type, "await") == 0) {
            modifier = "await";
        }
    }

    /* Get the function being called */
    TSNode function_node = ts_node_child_by_field_name(node, "function", 8);
    if (ts_node_is_null(function_node)) {
        if (g_debug) fprintf(stderr, "[DEBUG] handle_call: function_node is null, returning\n");
        /* Arguments will be processed by visit_node's process_children call */
        return;
    }

    const char *func_type = ts_node_type(function_node);
    if (g_debug) fprintf(stderr, "[DEBUG] handle_call: func_type=%s\n", func_type);

    if (strcmp(func_type, "attribute") == 0) {
        /* Handle method call: obj.method() */
        TSNode attr_node = ts_node_child_by_field_name(function_node, "attribute", 9);
        TSNode object_node = ts_node_child_by_field_name(function_node, "object", 6);

        if (!ts_node_is_null(attr_node)) {
            char method_name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, attr_node, method_name, sizeof(method_name), filename);

            if (method_name[0] && filter_should_index(filter, method_name)) {
                /* Try to get parent object name if simple */
                char parent_name[SYMBOL_MAX_LENGTH] = "";
                if (!ts_node_is_null(object_node)) {
                    const char *object_type = ts_node_type(object_node);
                    if (strcmp(object_type, "identifier") == 0) {
                        safe_extract_node_text(source_code, object_node, parent_name, sizeof(parent_name), filename);
                    }
                }

                add_entry(result, method_name, line,
                                     CONTEXT_CALL, directory, filename, NULL,
                                     &(ExtColumns){.parent = parent_name[0] ? parent_name : NULL,
                                                   .modifier = modifier});
            }
        }
    } else if (strcmp(func_type, "identifier") == 0) {
        /* Handle simple function call: foo() */
        char func_name[SYMBOL_MAX_LENGTH];
        safe_extract_node_text(source_code, function_node, func_name, sizeof(func_name), filename);

        if (g_debug) fprintf(stderr, "[DEBUG] handle_call: func_name=%s\n", func_name);

        if (func_name[0] && filter_should_index(filter, func_name)) {
            if (g_debug) fprintf(stderr, "[DEBUG] handle_call: indexing %s as CALL\n", func_name);
            add_entry(result, func_name, line,
                                 CONTEXT_CALL, directory, filename, NULL,
                                 &(ExtColumns){.modifier = modifier});
        } else {
            if (g_debug) fprintf(stderr, "[DEBUG] handle_call: NOT indexing %s (filtered or empty)\n", func_name);
        }
    }

    /* Process arguments */
    TSNode arguments = ts_node_child_by_field_name(node, "arguments", 9);
    if (!ts_node_is_null(arguments)) {
        /* Get function name for clue */
        char func_name[SYMBOL_MAX_LENGTH] = "";
        if (strcmp(func_type, "identifier") == 0) {
            safe_extract_node_text(source_code, function_node, func_name, sizeof(func_name), filename);
        } else if (strcmp(func_type, "attribute") == 0) {
            TSNode attr_node = ts_node_child_by_field_name(function_node, "attribute", 9);
            if (!ts_node_is_null(attr_node)) {
                safe_extract_node_text(source_code, attr_node, func_name, sizeof(func_name), filename);
            }
        }

        uint32_t arg_count = ts_node_child_count(arguments);
        for (uint32_t i = 0; i < arg_count; i++) {
            TSNode arg = ts_node_child(arguments, i);
            const char *arg_type = ts_node_type(arg);

            /* Index identifier arguments with function name as clue */
            if (strcmp(arg_type, "identifier") == 0 && func_name[0] != '\0') {
                char arg_symbol[SYMBOL_MAX_LENGTH];
                safe_extract_node_text(source_code, arg, arg_symbol, sizeof(arg_symbol), filename);

                if (filter_should_index(filter, arg_symbol)) {
                    add_entry(result, arg_symbol, line,
                                         CONTEXT_ARGUMENT, directory, filename, NULL,
                                         &(ExtColumns){.clue = func_name});
                }
            }
            /* Visit non-identifier arguments to index complex expressions */
            else if (strcmp(arg_type, "identifier") != 0) {
                visit_expression(arg, source_code, directory, filename, result, filter);
            }
            /* Skip visiting simple identifiers - already indexed as ARG above */
        }
    }
}

/* Visit an expression to index variable references */
static void visit_expression(TSNode node, const char *source_code,
                             const char *directory, const char *filename,
                             ParseResult *result, SymbolFilter *filter) {
    if (ts_node_is_null(node)) {
        return;
    }

    TSSymbol node_sym = ts_node_symbol(node);
    const char *node_type = ts_node_type(node);
    TSPoint start_point = ts_node_start_point(node);
    int line = (int)(start_point.row + 1);

    if (g_debug) fprintf(stderr, "[DEBUG] visit_expression: type=%s, line=%d\n", node_type, line);

    /* Handle different expression types */
    if (node_sym == python_symbols.identifier) {
        /* Variable reference */
        char symbol[SYMBOL_MAX_LENGTH];
        safe_extract_node_text(source_code, node, symbol, sizeof(symbol), filename);

        if (filter_should_index(filter, symbol)) {
            add_entry(result, symbol, line,
                                 CONTEXT_VARIABLE, directory, filename, NULL,
                                 NO_EXTENSIBLE_COLUMNS);
        }
    }
    else if (node_sym == python_symbols.binary_operator) {
        /* Binary operations: a + b (uses field names) */
        TSNode left = ts_node_child_by_field_name(node, "left", 4);
        TSNode right = ts_node_child_by_field_name(node, "right", 5);

        if (!ts_node_is_null(left)) {
            visit_expression(left, source_code, directory, filename, result, filter);
        }
        if (!ts_node_is_null(right)) {
            visit_expression(right, source_code, directory, filename, result, filter);
        }
    }
    else if (strcmp(node_type, "boolean_operator") == 0 ||
             strcmp(node_type, "comparison_operator") == 0) {
        /* Boolean/comparison operations: a and b, a > b (no field names, use children) */
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            const char *child_type = ts_node_type(child);
            /* Skip operators like 'and', 'or', '>', '<', etc. */
            if (strcmp(child_type, "and") != 0 &&
                strcmp(child_type, "or") != 0 &&
                strcmp(child_type, "<") != 0 &&
                strcmp(child_type, ">") != 0 &&
                strcmp(child_type, "<=") != 0 &&
                strcmp(child_type, ">=") != 0 &&
                strcmp(child_type, "==") != 0 &&
                strcmp(child_type, "!=") != 0 &&
                strcmp(child_type, "in") != 0 &&
                strcmp(child_type, "not") != 0 &&
                strcmp(child_type, "is") != 0) {
                visit_expression(child, source_code, directory, filename, result, filter);
            }
        }
    }
    else if (node_sym == python_symbols.unary_operator ||
             strcmp(node_type, "not_operator") == 0) {
        /* Unary operations: -x, not x */
        TSNode argument = ts_node_child_by_field_name(node, "argument", 8);
        if (!ts_node_is_null(argument)) {
            visit_expression(argument, source_code, directory, filename, result, filter);
        }
    }
    else if (strcmp(node_type, "subscript") == 0) {
        /* Subscript: array[index] */
        TSNode value = ts_node_child_by_field_name(node, "value", 5);
        TSNode subscript = ts_node_child_by_field_name(node, "subscript", 9);

        if (!ts_node_is_null(value)) {
            visit_expression(value, source_code, directory, filename, result, filter);
        }
        if (!ts_node_is_null(subscript)) {
            visit_expression(subscript, source_code, directory, filename, result, filter);
        }
    }
    else if (node_sym == python_symbols.attribute) {
        /* Attribute access: obj.attr - already handled by handle_call for method calls */
        TSNode object = ts_node_child_by_field_name(node, "object", 6);
        if (!ts_node_is_null(object)) {
            visit_expression(object, source_code, directory, filename, result, filter);
        }
    }
    else if (strcmp(node_type, "parenthesized_expression") == 0) {
        /* Parentheses: (expr) */
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            visit_expression(child, source_code, directory, filename, result, filter);
        }
    }
    else if (strcmp(node_type, "conditional_expression") == 0) {
        /* Ternary: a if condition else b */
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            const char *child_type = ts_node_type(child);
            if (strcmp(child_type, "if") != 0 && strcmp(child_type, "else") != 0) {
                visit_expression(child, source_code, directory, filename, result, filter);
            }
        }
    }
    else if (strcmp(node_type, "list") == 0 ||
             strcmp(node_type, "tuple") == 0 ||
             strcmp(node_type, "set") == 0) {
        /* Collection literals: [a, b], (a, b), {a, b} */
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            visit_expression(child, source_code, directory, filename, result, filter);
        }
    }
    else if (strcmp(node_type, "dictionary") == 0) {
        /* Dictionary: {key: value} */
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            const char *child_type = ts_node_type(child);
            if (strcmp(child_type, "pair") == 0) {
                TSNode key = ts_node_child_by_field_name(child, "key", 3);
                TSNode value = ts_node_child_by_field_name(child, "value", 5);
                if (!ts_node_is_null(key)) {
                    visit_expression(key, source_code, directory, filename, result, filter);
                }
                if (!ts_node_is_null(value)) {
                    visit_expression(value, source_code, directory, filename, result, filter);
                }
            }
        }
    }
    /* Delegate to dedicated handlers via visit_node */
    else if (strcmp(node_type, "call") == 0 ||
             strcmp(node_type, "lambda") == 0 ||
             strcmp(node_type, "named_expression") == 0 ||
             strcmp(node_type, "string") == 0) {
        if (g_debug) fprintf(stderr, "[DEBUG] visit_expression: delegating %s to visit_node\n", node_type);
        visit_node(node, source_code, directory, filename, result, filter);
    }
    /* Skip other literals */
    else if (strcmp(node_type, "integer") != 0 &&
             strcmp(node_type, "float") != 0 &&
             strcmp(node_type, "true") != 0 &&
             strcmp(node_type, "false") != 0 &&
             strcmp(node_type, "none") != 0) {
        /* For other expression types, recursively visit children */
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            visit_expression(child, source_code, directory, filename, result, filter);
        }
    }
}

/* Handle expression statements */
static void handle_expression_statement(TSNode node, const char *source_code,
                                        const char *directory, const char *filename,
                                        ParseResult *result, SymbolFilter *filter,
                                        int line) {
    (void)line; /* Line number comes from the expression itself */

    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        visit_expression(child, source_code, directory, filename, result, filter);
    }
}

/* Handle return statements */
static void handle_return_statement(TSNode node, const char *source_code,
                                    const char *directory, const char *filename,
                                    ParseResult *result, SymbolFilter *filter,
                                    int line) {
    (void)line; /* Line number comes from the expression itself */

    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);
        /* Skip the 'return' keyword */
        if (strcmp(child_type, "return") != 0) {
            visit_expression(child, source_code, directory, filename, result, filter);
        }
    }
}

/* Handle if statements */
static void handle_if_statement(TSNode node, const char *source_code,
                                const char *directory, const char *filename,
                                ParseResult *result, SymbolFilter *filter,
                                int line) {
    (void)line;

    if (g_debug) fprintf(stderr, "[DEBUG] handle_if_statement called at line %d\n", line);

    /* Visit condition */
    TSNode condition = ts_node_child_by_field_name(node, "condition", 9);
    if (!ts_node_is_null(condition)) {
        if (g_debug) fprintf(stderr, "[DEBUG] handle_if_statement: visiting condition, type=%s\n", ts_node_type(condition));
        visit_expression(condition, source_code, directory, filename, result, filter);
    }

    /* Visit consequence (if body) */
    TSNode consequence = ts_node_child_by_field_name(node, "consequence", 11);
    if (!ts_node_is_null(consequence)) {
        process_children(consequence, source_code, directory, filename, result, filter);
    }

    /* Visit alternative (else/elif) */
    TSNode alternative = ts_node_child_by_field_name(node, "alternative", 11);
    if (!ts_node_is_null(alternative)) {
        const char *alt_type = ts_node_type(alternative);
        if (strcmp(alt_type, "if_statement") == 0 || strcmp(alt_type, "elif_clause") == 0) {
            visit_node(alternative, source_code, directory, filename, result, filter);
        } else {
            process_children(alternative, source_code, directory, filename, result, filter);
        }
    }
}

/* Handle while statements */
static void handle_while_statement(TSNode node, const char *source_code,
                                   const char *directory, const char *filename,
                                   ParseResult *result, SymbolFilter *filter,
                                   int line) {
    (void)line;

    /* Visit condition */
    TSNode condition = ts_node_child_by_field_name(node, "condition", 9);
    if (!ts_node_is_null(condition)) {
        visit_expression(condition, source_code, directory, filename, result, filter);
    }

    /* Visit body */
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) {
        process_children(body, source_code, directory, filename, result, filter);
    }
}

/* Handle for statements */
static void handle_for_statement(TSNode node, const char *source_code,
                                 const char *directory, const char *filename,
                                 ParseResult *result, SymbolFilter *filter,
                                 int line) {
    (void)line;

    /* Index the loop variable (left side) */
    TSNode left = ts_node_child_by_field_name(node, "left", 4);
    if (!ts_node_is_null(left)) {
        TSSymbol left_sym = ts_node_symbol(left);
        if (left_sym == python_symbols.identifier) {
            char var_name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, left, var_name, sizeof(var_name), filename);

            if (filter_should_index(filter, var_name)) {
                add_entry(result, var_name, line,
                                     CONTEXT_VARIABLE, directory, filename, NULL, NULL);
            }
        } else if (left_sym == python_symbols.pattern_list) {
            /* Handle multiple loop variables: for a, b in items */
            uint32_t child_count = ts_node_child_count(left);
            for (uint32_t i = 0; i < child_count; i++) {
                TSNode child = ts_node_child(left, i);
                TSSymbol child_sym = ts_node_symbol(child);

                if (child_sym == python_symbols.identifier) {
                    char var_name[SYMBOL_MAX_LENGTH];
                    safe_extract_node_text(source_code, child, var_name, sizeof(var_name), filename);

                    if (filter_should_index(filter, var_name)) {
                        add_entry(result, var_name, line,
                                             CONTEXT_VARIABLE, directory, filename, NULL, NULL);
                    }
                }
            }
        }
    }

    /* Visit the iterable expression (right side) */
    TSNode right = ts_node_child_by_field_name(node, "right", 5);
    if (!ts_node_is_null(right)) {
        visit_expression(right, source_code, directory, filename, result, filter);
    }

    /* Visit body */
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) {
        process_children(body, source_code, directory, filename, result, filter);
    }
}

/* Handle named expressions (walrus operator :=) */
static void handle_named_expression(TSNode node, const char *source_code,
                                    const char *directory, const char *filename,
                                    ParseResult *result, SymbolFilter *filter,
                                    int line) {
    if (g_debug) fprintf(stderr, "[DEBUG] handle_named_expression called at line %d\n", line);

    /* Get the target (left side of :=) */
    TSNode target_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(target_node)) {
        return;
    }

    char var_name[SYMBOL_MAX_LENGTH];
    safe_extract_node_text(source_code, target_node, var_name, sizeof(var_name), filename);

    if (g_debug) fprintf(stderr, "[DEBUG] handle_named_expression: var_name=%s\n", var_name);

    if (!filter_should_index(filter, var_name)) {
        return;
    }

    add_entry(result, var_name, line,
                         CONTEXT_VARIABLE, directory, filename, NULL,
                         &(ExtColumns){.clue = ":="});

    /* Process the value expression */
    TSNode value_node = ts_node_child_by_field_name(node, "value", 5);
    if (!ts_node_is_null(value_node)) {
        if (g_debug) fprintf(stderr, "[DEBUG] handle_named_expression: processing value, type=%s\n", ts_node_type(value_node));
        visit_node(value_node, source_code, directory, filename, result, filter);
    }
}

/* Handle lambda expressions */
static void handle_lambda(TSNode node, const char *source_code,
                         const char *directory, const char *filename,
                         ParseResult *result, SymbolFilter *filter,
                         int line) {
    /* Index the lambda expression itself */
    char location[128];
    format_source_location(node, location, sizeof(location));

    add_entry(result, "<lambda>", line, CONTEXT_LAMBDA,
                         directory, filename, location,
                         &(ExtColumns){.definition = "1"});

    /* Find the lambda_parameters node by iterating through children */
    TSNode params_node = {0};
    uint32_t child_count = ts_node_child_count(node);

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "lambda_parameters") == 0) {
            params_node = child;
            break;
        }
    }

    if (!ts_node_is_null(params_node)) {
        /* Process each parameter in the lambda_parameters node */
        uint32_t param_count = ts_node_child_count(params_node);

        for (uint32_t i = 0; i < param_count; i++) {
            TSNode child = ts_node_child(params_node, i);
            const char *child_type = ts_node_type(child);

            /* Handle regular identifier parameters */
            if (strcmp(child_type, "identifier") == 0) {
                char param_name[SYMBOL_MAX_LENGTH];
                safe_extract_node_text(source_code, child, param_name, sizeof(param_name), filename);

                if (filter_should_index(filter, param_name)) {
                    add_entry(result, param_name, line,
                                         CONTEXT_ARGUMENT, directory, filename, NULL,
                                         &(ExtColumns){.parent = "lambda", .clue = "lambda"});
                }
            }
            /* Handle *args in lambda */
            else if (strcmp(child_type, "list_splat_pattern") == 0) {
                uint32_t splat_child_count = ts_node_child_count(child);
                for (uint32_t j = 0; j < splat_child_count; j++) {
                    TSNode splat_child = ts_node_child(child, j);
                    const char *splat_child_type = ts_node_type(splat_child);

                    if (strcmp(splat_child_type, "identifier") == 0) {
                        char param_name[SYMBOL_MAX_LENGTH];
                        safe_extract_node_text(source_code, splat_child, param_name, sizeof(param_name), filename);

                        if (filter_should_index(filter, param_name)) {
                            add_entry(result, param_name, line,
                                                 CONTEXT_ARGUMENT, directory, filename, NULL,
                                                 &(ExtColumns){.parent = "lambda", .clue = "lambda,*args"});
                        }
                        break;
                    }
                }
            }
            /* Handle **kwargs in lambda */
            else if (strcmp(child_type, "dictionary_splat_pattern") == 0) {
                uint32_t splat_child_count = ts_node_child_count(child);
                for (uint32_t j = 0; j < splat_child_count; j++) {
                    TSNode splat_child = ts_node_child(child, j);
                    const char *splat_child_type = ts_node_type(splat_child);

                    if (strcmp(splat_child_type, "identifier") == 0) {
                        char param_name[SYMBOL_MAX_LENGTH];
                        safe_extract_node_text(source_code, splat_child, param_name, sizeof(param_name), filename);

                        if (filter_should_index(filter, param_name)) {
                            add_entry(result, param_name, line,
                                                 CONTEXT_ARGUMENT, directory, filename, NULL,
                                                 &(ExtColumns){.parent = "lambda", .clue = "lambda,**kwargs"});
                        }
                        break;
                    }
                }
            }
        }
    }

    /* Process the lambda body */
    process_children(node, source_code, directory, filename, result, filter);
}

/* Handle comprehensions (list/dict/set/generator) */
static void handle_comprehension(TSNode node, const char *source_code,
                                 const char *directory, const char *filename,
                                 ParseResult *result, SymbolFilter *filter,
                                 int line) {
    (void)line;  /* Line number comes from child nodes */
    /* Find for_in_clause - contains the loop variable(s) */
    uint32_t child_count = ts_node_child_count(node);

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "for_in_clause") == 0) {
            /* The for_in_clause structure:
             *   for [identifier|pattern_list] in [expression]
             * We need to index the identifier(s) */
            uint32_t clause_count = ts_node_child_count(child);

            for (uint32_t j = 0; j < clause_count; j++) {
                TSNode clause_child = ts_node_child(child, j);
                const char *clause_type = ts_node_type(clause_child);

                if (strcmp(clause_type, "identifier") == 0) {
                    /* Single loop variable */
                    char var_name[SYMBOL_MAX_LENGTH];
                    safe_extract_node_text(source_code, clause_child, var_name, sizeof(var_name), filename);

                    if (filter_should_index(filter, var_name)) {
                        TSPoint clause_point = ts_node_start_point(clause_child);
                        int clause_line = (int)(clause_point.row + 1);

                        add_entry(result, var_name, clause_line,
                                             CONTEXT_VARIABLE, directory, filename, NULL, NULL);
                    }
                }
                else if (strcmp(clause_type, "pattern_list") == 0) {
                    /* Multiple loop variables: for k, v in items */
                    uint32_t pattern_count = ts_node_child_count(clause_child);

                    for (uint32_t k = 0; k < pattern_count; k++) {
                        TSNode pattern_item = ts_node_child(clause_child, k);
                        const char *pattern_type = ts_node_type(pattern_item);

                        if (strcmp(pattern_type, "identifier") == 0) {
                            char var_name[SYMBOL_MAX_LENGTH];
                            safe_extract_node_text(source_code, pattern_item, var_name, sizeof(var_name), filename);

                            if (filter_should_index(filter, var_name)) {
                                TSPoint pattern_point = ts_node_start_point(pattern_item);
                                int pattern_line = (int)(pattern_point.row + 1);

                                add_entry(result, var_name, pattern_line,
                                                     CONTEXT_VARIABLE, directory, filename, NULL, NULL);
                            }
                        }
                    }
                }
            }
        }
    }

    /* Process the rest of the comprehension (expressions, conditions) */
    process_children(node, source_code, directory, filename, result, filter);
}

/* Helper function to recursively extract identifiers from case patterns */
static void extract_pattern_variables(TSNode pattern_node, const char *source_code,
                                      const char *directory, const char *filename,
                                      ParseResult *result, SymbolFilter *filter) {
    const char *node_type = ts_node_type(pattern_node);

    if (strcmp(node_type, "identifier") == 0) {
        /* Direct identifier - extract and index it */
        char var_name[SYMBOL_MAX_LENGTH];
        safe_extract_node_text(source_code, pattern_node, var_name, sizeof(var_name), filename);

        /* Skip wildcard pattern _ */
        if (strcmp(var_name, "_") == 0) {
            return;
        }

        if (!filter_should_index(filter, var_name)) {
            return;
        }

        TSPoint point = ts_node_start_point(pattern_node);
        int line = (int)(point.row + 1);

        add_entry(result, var_name, line,
                             CONTEXT_VARIABLE, directory, filename, NULL, NULL);
    } else {
        /* Recursively process all children to find identifiers */
        uint32_t child_count = ts_node_child_count(pattern_node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(pattern_node, i);
            extract_pattern_variables(child, source_code, directory, filename, result, filter);
        }
    }
}

/* Handle match/case statements (Python 3.10+) */
static void handle_case_clause(TSNode node, const char *source_code,
                               const char *directory, const char *filename,
                               ParseResult *result, SymbolFilter *filter,
                               int line) {
    (void)line;

    /* Find the case_pattern node */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "case_pattern") == 0) {
            /* Recursively extract all identifiers from the pattern */
            extract_pattern_variables(child, source_code, directory, filename, result, filter);
        }
    }

    /* Process the case body */
    process_children(node, source_code, directory, filename, result, filter);
}

/* Handle except clauses - extract exception type and variable binding */
static void handle_except_clause(TSNode node, const char *source_code,
                                 const char *directory, const char *filename,
                                 ParseResult *result, SymbolFilter *filter,
                                 int line) {
    (void)line;

    char exception_type[SYMBOL_MAX_LENGTH] = "";
    int exception_line = 0;

    /* Look for exception type and optional "as" binding */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        /* Case 1: except ValueError as ee: */
        if (strcmp(child_type, "as_pattern") == 0) {
            /* Extract exception type (first child of as_pattern) */
            uint32_t as_child_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < as_child_count; j++) {
                TSNode as_child = ts_node_child(child, j);
                const char *as_child_type = ts_node_type(as_child);

                /* Get the exception type identifier */
                if (strcmp(as_child_type, "identifier") == 0 && exception_type[0] == '\0') {
                    safe_extract_node_text(source_code, as_child, exception_type, sizeof(exception_type), filename);
                    TSPoint point = ts_node_start_point(as_child);
                    exception_line = (int)(point.row + 1);
                }
                /* Get the bound variable from as_pattern_target */
                else if (strcmp(as_child_type, "as_pattern_target") == 0) {
                    uint32_t target_child_count = ts_node_child_count(as_child);
                    for (uint32_t k = 0; k < target_child_count; k++) {
                        TSNode target_child = ts_node_child(as_child, k);
                        const char *target_type = ts_node_type(target_child);

                        if (strcmp(target_type, "identifier") == 0) {
                            char var_name[SYMBOL_MAX_LENGTH];
                            safe_extract_node_text(source_code, target_child, var_name, sizeof(var_name), filename);

                            if (filter_should_index(filter, var_name)) {
                                TSPoint point = ts_node_start_point(target_child);
                                int var_line = (int)(point.row + 1);

                                /* Index the bound variable with exception type */
                                add_entry(result, var_name, var_line,
                                                     CONTEXT_EXCEPTION, directory, filename, NULL,
                                                     &(ExtColumns){.type = exception_type});
                            }
                            break;
                        }
                    }
                }
            }
            break;
        }
        /* Case 2: except ValueError: (no binding) */
        else if (strcmp(child_type, "identifier") == 0 && exception_type[0] == '\0') {
            safe_extract_node_text(source_code, child, exception_type, sizeof(exception_type), filename);
            TSPoint point = ts_node_start_point(child);
            exception_line = (int)(point.row + 1);
        }
    }

    /* Index the exception type itself */
    if (exception_type[0] != '\0' && filter_should_index(filter, exception_type)) {
        add_entry(result, exception_type, exception_line,
                             CONTEXT_EXCEPTION, directory, filename, NULL,
                             &(ExtColumns){.type = exception_type});
    }

    /* Process the except clause body */
    process_children(node, source_code, directory, filename, result, filter);
}

/* Handle yield statement - index yielded variables */
static void handle_yield(TSNode node, const char *source_code,
                        const char *directory, const char *filename,
                        ParseResult *result, SymbolFilter *filter,
                        int line) {
    (void)line;  /* Line number comes from child nodes */
    /* yield has child nodes: "yield" keyword and optional expression */
    uint32_t child_count = ts_node_child_count(node);

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);

        /* Index identifiers in yield expression */
        if (strcmp(child_type, "identifier") == 0) {
            char var_name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, child, var_name, sizeof(var_name), filename);

            if (filter_should_index(filter, var_name)) {
                TSPoint point = ts_node_start_point(child);
                int var_line = (int)(point.row + 1);

                add_entry(result, var_name, var_line,
                                     CONTEXT_VARIABLE, directory, filename, NULL,
                                     &(ExtColumns){.clue = "yield"});
            }
        }
        /* For complex expressions, recursively visit */
        else if (strcmp(child_type, "yield") != 0) {
            visit_node(child, source_code, directory, filename, result, filter);
        }
    }
}

/* Visit a node and dispatch to appropriate handler using symbol comparisons */
static void visit_node(TSNode node, const char *source_code, const char *directory,
                      const char *filename, ParseResult *result, SymbolFilter *filter) {
    TSSymbol node_sym = ts_node_symbol(node);
    TSPoint start_point = ts_node_start_point(node);
    int line = (int)(start_point.row + 1);

    /* Dispatch using symbol comparisons (O(1) uint16_t comparisons) */
    if (node_sym == python_symbols.function_definition) {
        handle_function_definition(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == python_symbols.class_definition) {
        handle_class_definition(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == python_symbols.assignment || node_sym == python_symbols.augmented_assignment) {
        handle_assignment(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == python_symbols.named_expression) {
        handle_named_expression(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == python_symbols.import_statement) {
        handle_import_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == python_symbols.import_from_statement) {
        handle_import_from_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == python_symbols.expression_statement) {
        handle_expression_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == python_symbols.return_statement) {
        handle_return_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == python_symbols.yield) {
        handle_yield(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == python_symbols.if_statement) {
        handle_if_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == python_symbols.while_statement) {
        handle_while_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == python_symbols.for_statement) {
        handle_for_statement(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == python_symbols.except_clause) {
        handle_except_clause(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == python_symbols.case_clause) {
        handle_case_clause(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == python_symbols.list_comprehension ||
        node_sym == python_symbols.dictionary_comprehension ||
        node_sym == python_symbols.set_comprehension ||
        node_sym == python_symbols.generator_expression) {
        handle_comprehension(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == python_symbols.call) {
        handle_call(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == python_symbols.lambda) {
        handle_lambda(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == python_symbols.interpolation) {
        handle_interpolation(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == python_symbols.string) {
        handle_string(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (node_sym == python_symbols.comment) {
        handle_comment(node, source_code, directory, filename, result, filter, line);
        return;
    }

    /* No handler found, recursively process children */
    process_children(node, source_code, directory, filename, result, filter);
}

/* Process all children of a node */
static void process_children(TSNode node, const char *source_code, const char *directory,
                            const char *filename, ParseResult *result, SymbolFilter *filter) {
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        visit_node(child, source_code, directory, filename, result, filter);
    }
}

/* Initialize Python symbol table for fast comparisons */
static void init_python_symbols(TSLanguage *language) {
    static int initialized = 0;
    if (initialized) return;
    initialized = 1;

    /* Dispatch handler symbols */
    python_symbols.function_definition = ts_language_symbol_for_name(language, "function_definition", 19, true);
    python_symbols.class_definition = ts_language_symbol_for_name(language, "class_definition", 16, true);
    python_symbols.assignment = ts_language_symbol_for_name(language, "assignment", 10, true);
    python_symbols.augmented_assignment = ts_language_symbol_for_name(language, "augmented_assignment", 20, true);
    python_symbols.named_expression = ts_language_symbol_for_name(language, "named_expression", 16, true);
    python_symbols.import_statement = ts_language_symbol_for_name(language, "import_statement", 16, true);
    python_symbols.import_from_statement = ts_language_symbol_for_name(language, "import_from_statement", 21, true);
    python_symbols.expression_statement = ts_language_symbol_for_name(language, "expression_statement", 20, true);
    python_symbols.return_statement = ts_language_symbol_for_name(language, "return_statement", 16, true);
    python_symbols.yield = ts_language_symbol_for_name(language, "yield", 5, true);
    python_symbols.if_statement = ts_language_symbol_for_name(language, "if_statement", 12, true);
    python_symbols.while_statement = ts_language_symbol_for_name(language, "while_statement", 15, true);
    python_symbols.for_statement = ts_language_symbol_for_name(language, "for_statement", 13, true);
    python_symbols.except_clause = ts_language_symbol_for_name(language, "except_clause", 13, true);
    python_symbols.case_clause = ts_language_symbol_for_name(language, "case_clause", 11, true);
    python_symbols.list_comprehension = ts_language_symbol_for_name(language, "list_comprehension", 18, true);
    python_symbols.dictionary_comprehension = ts_language_symbol_for_name(language, "dictionary_comprehension", 24, true);
    python_symbols.set_comprehension = ts_language_symbol_for_name(language, "set_comprehension", 17, true);
    python_symbols.generator_expression = ts_language_symbol_for_name(language, "generator_expression", 20, true);
    python_symbols.call = ts_language_symbol_for_name(language, "call", 4, true);
    python_symbols.lambda = ts_language_symbol_for_name(language, "lambda", 6, true);
    python_symbols.interpolation = ts_language_symbol_for_name(language, "interpolation", 13, true);
    python_symbols.string = ts_language_symbol_for_name(language, "string", 6, true);
    python_symbols.comment = ts_language_symbol_for_name(language, "comment", 7, true);

    /* Frequently-checked node types */
    python_symbols.identifier = ts_language_symbol_for_name(language, "identifier", 10, true);
    python_symbols.attribute = ts_language_symbol_for_name(language, "attribute", 9, true);
    python_symbols.decorated_definition = ts_language_symbol_for_name(language, "decorated_definition", 20, true);
    python_symbols.decorator = ts_language_symbol_for_name(language, "decorator", 9, true);
    python_symbols.pattern_list = ts_language_symbol_for_name(language, "pattern_list", 12, true);
    python_symbols.typed_parameter = ts_language_symbol_for_name(language, "typed_parameter", 15, true);
    python_symbols.default_parameter = ts_language_symbol_for_name(language, "default_parameter", 17, true);
    python_symbols.typed_default_parameter = ts_language_symbol_for_name(language, "typed_default_parameter", 23, true);
    python_symbols.list_splat_pattern = ts_language_symbol_for_name(language, "list_splat_pattern", 18, true);
    python_symbols.dictionary_splat_pattern = ts_language_symbol_for_name(language, "dictionary_splat_pattern", 24, true);
    python_symbols.dotted_name = ts_language_symbol_for_name(language, "dotted_name", 11, true);
    python_symbols.aliased_import = ts_language_symbol_for_name(language, "aliased_import", 14, true);
    python_symbols.string_content = ts_language_symbol_for_name(language, "string_content", 14, true);
    python_symbols.binary_operator = ts_language_symbol_for_name(language, "binary_operator", 15, true);
    python_symbols.unary_operator = ts_language_symbol_for_name(language, "unary_operator", 14, true);
}

/* Initialize parser */
int parser_init(PythonParser *parser, SymbolFilter *filter) {
    parser->parser = ts_parser_new();
    if (!parser->parser) {
        return -1;
    }

    TSLanguage *language = tree_sitter_python();
    if (!ts_parser_set_language(parser->parser, language)) {
        ts_parser_delete(parser->parser);
        return -1;
    }

    /* Initialize symbol table */
    init_python_symbols(language);

    parser->filter = filter;
    parser->debug = 0;  /* Default to no debug */
    g_debug = 0;
    return 0;
}

/* Set debug mode */
void parser_set_debug(PythonParser *parser, int debug) {
    parser->debug = debug;
    g_debug = debug;
}

/* Parse a Python file */
int parser_parse_file(PythonParser *parser, const char *filepath, const char *project_root, ParseResult *result) {
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

    /* Parse the source code */
    TSTree *tree = ts_parser_parse_string(parser->parser, NULL, source_code, (uint32_t)bytes_read);
    if (!tree) {
        fprintf(stderr, "Failed to parse file: %s\n", filepath);
        free(source_code);
        return -1;
    }

    TSNode root = ts_tree_root_node(tree);

    /* Get directory and filename */
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

    /* Visit the AST */
    visit_node(root, source_code, directory, filename, result, parser->filter);

    /* Cleanup */
    ts_tree_delete(tree);
    free(source_code);

    return 0;
}

/* Free parser resources */
void parser_free(PythonParser *parser) {
    if (parser->parser) {
        ts_parser_delete(parser->parser);
        parser->parser = NULL;
    }
}
