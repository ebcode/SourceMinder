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
#include "rust_language.h"
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

/* External Rust language function from tree-sitter-rust */
extern const TSLanguage *tree_sitter_rust(void);

/* Global debug flag */
static int g_debug = 0;

/* Current impl target type - acts as parent for methods inside an impl block.
 * Rust's tree-sitter parser doesn't expose this directly, so we track it as
 * we descend into impl_item nodes and restore it on exit. */
static char g_current_impl[SYMBOL_MAX_LENGTH] = "";

/* Forward declarations */
static void visit_node(TSNode node, const char *source_code, const char *directory,
                       const char *filename, ParseResult *result, SymbolFilter *filter);
static void process_children(TSNode node, const char *source_code, const char *directory,
                             const char *filename, ParseResult *result, SymbolFilter *filter);

static void process_children(TSNode node, const char *source_code, const char *directory,
                             const char *filename, ParseResult *result, SymbolFilter *filter) {
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        visit_node(child, source_code, directory, filename, result, filter);
    }
}

/* ---------------- Helpers ---------------- */

/* Extract visibility modifier text ("pub", "pub(crate)", etc.) from direct
 * children of an item node. Returns "" if none. */
static void extract_visibility(TSNode node, const char *source_code,
                               char *out, size_t out_size, const char *filename) {
    out[0] = '\0';
    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        TSNode child = ts_node_child(node, i);
        if (strcmp(ts_node_type(child), "visibility_modifier") == 0) {
            safe_extract_node_text(source_code, child, out, out_size, filename);
            return;
        }
    }
}

/* Extract function modifier keywords (async, unsafe, const, extern) from a
 * function_item or function_signature_item. Visibility (`pub`) is handled
 * separately via extract_visibility(). Returns "" if none. */
static void extract_fn_modifiers(TSNode node, const char *source_code,
                                 char *out, size_t out_size, const char *filename) {
    out[0] = '\0';
    size_t pos = 0;
    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        TSNode child = ts_node_child(node, i);
        const char *t = ts_node_type(child);
        if (strcmp(t, "function_modifiers") == 0) {
            uint32_t mn = ts_node_child_count(child);
            for (uint32_t j = 0; j < mn; j++) {
                TSNode m = ts_node_child(child, j);
                const char *mt = ts_node_type(m);
                if (strcmp(mt, "async") == 0 || strcmp(mt, "unsafe") == 0 ||
                    strcmp(mt, "const") == 0 || strcmp(mt, "extern") == 0 ||
                    strcmp(mt, "default") == 0 || strcmp(mt, "extern_modifier") == 0) {
                    char kw[SYMBOL_MAX_LENGTH];
                    safe_extract_node_text(source_code, m, kw, sizeof(kw), filename);
                    int w = snprintf(out + pos, out_size - pos,
                                     "%s%s", pos ? " " : "", kw);
                    if (w > 0 && (size_t)w < out_size - pos) pos += (size_t)w;
                }
            }
        } else if (strcmp(t, "async") == 0 || strcmp(t, "unsafe") == 0 ||
                   strcmp(t, "extern_modifier") == 0) {
            int w = snprintf(out + pos, out_size - pos,
                             "%s%s", pos ? " " : "", t);
            if (w > 0 && (size_t)w < out_size - pos) pos += (size_t)w;
        }
    }
}

/* Extract attribute paths from preceding attribute_item siblings.
 * E.g. "#[derive(Debug, Clone)] #[cfg(test)] fn foo()" yields
 * "#[derive],#[cfg]". Returns "" if none. */
static void extract_attributes(TSNode node, const char *source_code,
                               char *out, size_t out_size, const char *filename) {
    out[0] = '\0';
    TSNode parent = ts_node_parent(node);
    if (ts_node_is_null(parent)) return;

    uint32_t pc = ts_node_child_count(parent);
    /* Locate this node's index within the parent */
    int my_idx = -1;
    for (uint32_t i = 0; i < pc; i++) {
        TSNode c = ts_node_child(parent, i);
        if (ts_node_start_byte(c) == ts_node_start_byte(node) &&
            ts_node_end_byte(c) == ts_node_end_byte(node)) {
            my_idx = (int)i;
            break;
        }
    }
    if (my_idx <= 0) return;

    /* Walk backwards collecting contiguous attribute_item siblings */
    size_t pos = 0;
    int first = 1;
    for (int i = my_idx - 1; i >= 0; i--) {
        TSNode sib = ts_node_child(parent, (uint32_t)i);
        const char *st = ts_node_type(sib);
        if (strcmp(st, "attribute_item") != 0 &&
            strcmp(st, "inner_attribute_item") != 0) {
            break;
        }
        /* Inside attribute_item, find the `attribute` child node (not a field) */
        TSNode attr_node = {0};
        uint32_t sc = ts_node_child_count(sib);
        for (uint32_t k = 0; k < sc; k++) {
            TSNode c = ts_node_child(sib, k);
            if (strcmp(ts_node_type(c), "attribute") == 0) {
                attr_node = c;
                break;
            }
        }
        char path[SYMBOL_MAX_LENGTH] = "";
        if (!ts_node_is_null(attr_node)) {
            /* First identifier/scoped_identifier child holds the path */
            uint32_t ac = ts_node_child_count(attr_node);
            for (uint32_t k = 0; k < ac; k++) {
                TSNode c = ts_node_child(attr_node, k);
                const char *ct = ts_node_type(c);
                if (strcmp(ct, "identifier") == 0 ||
                    strcmp(ct, "scoped_identifier") == 0) {
                    safe_extract_node_text(source_code, c, path, sizeof(path), filename);
                    break;
                }
            }
        }
        if (!path[0]) continue;
        int w = snprintf(out + pos, out_size - pos,
                         "%s#[%s]", first ? "" : ",", path);
        if (w > 0 && (size_t)w < out_size - pos) {
            pos += (size_t)w;
            first = 0;
        }
    }
}

/* Walk a `type` node and extract the base type_identifier, peeling off
 * generic_type wrappers and scoped_type_identifier qualifiers.
 * Falls back to the full text if no identifier is found. */
static void extract_base_type_name(TSNode type_node, const char *source_code,
                                   char *out, size_t out_size, const char *filename) {
    if (ts_node_is_null(type_node)) { out[0] = '\0'; return; }
    const char *t = ts_node_type(type_node);
    if (strcmp(t, "type_identifier") == 0 || strcmp(t, "identifier") == 0) {
        safe_extract_node_text(source_code, type_node, out, out_size, filename);
        return;
    }
    if (strcmp(t, "generic_type") == 0) {
        TSNode inner = ts_node_child_by_field_name(type_node, "type", 4);
        if (ts_node_is_null(inner) && ts_node_child_count(type_node) > 0) {
            inner = ts_node_child(type_node, 0);
        }
        extract_base_type_name(inner, source_code, out, out_size, filename);
        return;
    }
    if (strcmp(t, "scoped_type_identifier") == 0 ||
        strcmp(t, "scoped_identifier") == 0) {
        TSNode name = ts_node_child_by_field_name(type_node, "name", 4);
        if (!ts_node_is_null(name)) {
            safe_extract_node_text(source_code, name, out, out_size, filename);
            return;
        }
    }
    /* reference_type, etc. — drop the reference and recurse */
    if (strcmp(t, "reference_type") == 0) {
        TSNode inner = ts_node_child_by_field_name(type_node, "type", 4);
        if (!ts_node_is_null(inner)) {
            extract_base_type_name(inner, source_code, out, out_size, filename);
            return;
        }
    }
    safe_extract_node_text(source_code, type_node, out, out_size, filename);
}

/* Walk a pattern node and emit each binding identifier as a CONTEXT_VARIABLE.
 * Skips `_`, constructor names in tuple_struct_pattern/struct_pattern, and
 * descends through wrappers like mut_pattern, reference_pattern, etc. */
static void index_pattern_identifiers(TSNode pattern, const char *source_code,
                                      const char *directory, const char *filename,
                                      ParseResult *result, SymbolFilter *filter,
                                      int line, const char *type_str) {
    if (ts_node_is_null(pattern)) return;
    const char *t = ts_node_type(pattern);

    if (strcmp(t, "identifier") == 0) {
        char name[SYMBOL_MAX_LENGTH];
        safe_extract_node_text(source_code, pattern, name, sizeof(name), filename);
        if (name[0] == '_' && name[1] == '\0') return;
        /* Rust convention: identifiers starting with uppercase in pattern
         * position are constructors/constants (e.g. `None`, `MAX`), not
         * bindings. Skip them. */
        if (name[0] >= 'A' && name[0] <= 'Z') return;
        if (filter_should_index(filter, name)) {
            add_entry(result, name, line, CONTEXT_VARIABLE,
                      directory, filename, NULL,
                      &(ExtColumns){
                          .definition = "1",
                          .type = (type_str && type_str[0]) ? type_str : NULL
                      });
        }
        return;
    }

    /* Scoped paths in patterns are always constructor/constant references
     * (e.g. `AppError::NotFound`), never bindings. */
    if (strcmp(t, "scoped_identifier") == 0 ||
        strcmp(t, "scoped_type_identifier") == 0) {
        return;
    }

    if (strcmp(t, "tuple_struct_pattern") == 0 ||
        strcmp(t, "struct_pattern") == 0) {
        /* Skip the constructor (the `type` field). */
        TSNode type_node = ts_node_child_by_field_name(pattern, "type", 4);
        uint32_t n = ts_node_child_count(pattern);
        for (uint32_t i = 0; i < n; i++) {
            TSNode c = ts_node_child(pattern, i);
            if (!ts_node_is_null(type_node) &&
                ts_node_start_byte(c) == ts_node_start_byte(type_node) &&
                ts_node_end_byte(c) == ts_node_end_byte(type_node)) {
                continue;
            }
            index_pattern_identifiers(c, source_code, directory, filename,
                                      result, filter, line, NULL);
        }
        return;
    }

    if (strcmp(t, "field_pattern") == 0) {
        /* `field: pattern` (binding renamed) or shorthand `field` */
        TSNode bound = ts_node_child_by_field_name(pattern, "pattern", 7);
        if (ts_node_is_null(bound)) {
            bound = ts_node_child_by_field_name(pattern, "name", 4);
        }
        index_pattern_identifiers(bound, source_code, directory, filename,
                                  result, filter, line, NULL);
        return;
    }

    /* All other patterns: recurse into children
     * (tuple_pattern, mut_pattern, reference_pattern, ref_pattern,
     *  captured_pattern, or_pattern, slice_pattern, ...) */
    uint32_t n = ts_node_child_count(pattern);
    for (uint32_t i = 0; i < n; i++) {
        index_pattern_identifiers(ts_node_child(pattern, i), source_code,
                                  directory, filename, result, filter, line, NULL);
    }
}

/* ---------------- Item handlers ---------------- */

static void handle_function_item(TSNode node, const char *source_code,
                                 const char *directory, const char *filename,
                                 ParseResult *result, SymbolFilter *filter, int line) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name_node)) return;

    char fn_name[SYMBOL_MAX_LENGTH];
    safe_extract_node_text(source_code, name_node, fn_name, sizeof(fn_name), filename);
    if (!fn_name[0] || !filter_should_index(filter, fn_name)) {
        /* Still process body/parameters */
    }

    char modifiers[SYMBOL_MAX_LENGTH];
    extract_fn_modifiers(node, source_code, modifiers, sizeof(modifiers), filename);

    char vis[SYMBOL_MAX_LENGTH];
    extract_visibility(node, source_code, vis, sizeof(vis), filename);

    char attrs[SYMBOL_MAX_LENGTH];
    extract_attributes(node, source_code, attrs, sizeof(attrs), filename);

    char return_type[SYMBOL_MAX_LENGTH] = "";
    TSNode ret_node = ts_node_child_by_field_name(node, "return_type", 11);
    if (!ts_node_is_null(ret_node)) {
        safe_extract_node_text(source_code, ret_node, return_type, sizeof(return_type), filename);
    }

    char location[128];
    format_source_location(node, location, sizeof(location));

    if (fn_name[0] && filter_should_index(filter, fn_name)) {
        add_entry(result, fn_name, line, CONTEXT_FUNCTION,
                  directory, filename, location,
                  &(ExtColumns){
                      .definition = "1",
                      .scope = vis[0] ? vis : NULL,
                      .modifier = modifiers[0] ? modifiers : NULL,
                      .clue = attrs[0] ? attrs : NULL,
                      .type = return_type[0] ? return_type : NULL,
                      .parent = g_current_impl[0] ? g_current_impl : NULL
                  });
    }

    /* Process type parameters, parameters, return type, body so their inner
     * identifiers/types/calls get indexed too. */
    TSNode type_params = ts_node_child_by_field_name(node, "type_parameters", 15);
    if (!ts_node_is_null(type_params)) {
        process_children(type_params, source_code, directory, filename, result, filter);
    }
    TSNode params_node = ts_node_child_by_field_name(node, "parameters", 10);
    if (!ts_node_is_null(params_node)) {
        process_children(params_node, source_code, directory, filename, result, filter);
    }
    if (!ts_node_is_null(ret_node)) {
        process_children(ret_node, source_code, directory, filename, result, filter);
    }
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) {
        process_children(body, source_code, directory, filename, result, filter);
    }
}

static void handle_parameter(TSNode node, const char *source_code,
                             const char *directory, const char *filename,
                             ParseResult *result, SymbolFilter *filter, int line) {
    TSNode pattern = ts_node_child_by_field_name(node, "pattern", 7);
    TSNode type_node = ts_node_child_by_field_name(node, "type", 4);

    char param_name[SYMBOL_MAX_LENGTH] = "";
    if (!ts_node_is_null(pattern)) {
        /* Walk to find first identifier inside the pattern */
        const char *pt = ts_node_type(pattern);
        if (strcmp(pt, "identifier") == 0) {
            safe_extract_node_text(source_code, pattern, param_name, sizeof(param_name), filename);
        } else {
            /* mut x, _, (a, b), etc. — process_children handles recursive cases */
            process_children(pattern, source_code, directory, filename, result, filter);
        }
    }

    char type_str[SYMBOL_MAX_LENGTH] = "";
    if (!ts_node_is_null(type_node)) {
        safe_extract_node_text(source_code, type_node, type_str, sizeof(type_str), filename);
    }

    if (param_name[0] && filter_should_index(filter, param_name)) {
        add_entry(result, param_name, line, CONTEXT_ARGUMENT,
                  directory, filename, NULL,
                  &(ExtColumns){
                      .type = type_str[0] ? type_str : NULL
                  });
    }

    /* Still visit the type so generics/lifetimes inside get indexed */
    if (!ts_node_is_null(type_node)) {
        process_children(type_node, source_code, directory, filename, result, filter);
    }
}

static void handle_struct_item(TSNode node, const char *source_code,
                               const char *directory, const char *filename,
                               ParseResult *result, SymbolFilter *filter, int line) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name_node)) return;

    char name[SYMBOL_MAX_LENGTH];
    safe_extract_node_text(source_code, name_node, name, sizeof(name), filename);

    char vis[SYMBOL_MAX_LENGTH];
    extract_visibility(node, source_code, vis, sizeof(vis), filename);
    char attrs[SYMBOL_MAX_LENGTH];
    extract_attributes(node, source_code, attrs, sizeof(attrs), filename);

    char location[128];
    format_source_location(node, location, sizeof(location));

    if (filter_should_index(filter, name)) {
        add_entry(result, name, line, CONTEXT_CLASS,
                  directory, filename, location,
                  &(ExtColumns){
                      .definition = "1",
                      .scope = vis[0] ? vis : NULL,
                      .clue = attrs[0] ? attrs : NULL
                  });
    }

    /* Process body fields */
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) {
        process_children(body, source_code, directory, filename, result, filter);
    }
}

static void handle_field_declaration(TSNode node, const char *source_code,
                                     const char *directory, const char *filename,
                                     ParseResult *result, SymbolFilter *filter, int line) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    TSNode type_node = ts_node_child_by_field_name(node, "type", 4);
    if (ts_node_is_null(name_node)) {
        process_children(node, source_code, directory, filename, result, filter);
        return;
    }

    char name[SYMBOL_MAX_LENGTH];
    safe_extract_node_text(source_code, name_node, name, sizeof(name), filename);

    char vis[SYMBOL_MAX_LENGTH];
    extract_visibility(node, source_code, vis, sizeof(vis), filename);

    char type_str[SYMBOL_MAX_LENGTH] = "";
    if (!ts_node_is_null(type_node)) {
        safe_extract_node_text(source_code, type_node, type_str, sizeof(type_str), filename);
    }

    if (filter_should_index(filter, name)) {
        add_entry(result, name, line, CONTEXT_PROPERTY,
                  directory, filename, NULL,
                  &(ExtColumns){
                      .definition = "1",
                      .scope = vis[0] ? vis : NULL,
                      .type = type_str[0] ? type_str : NULL
                  });
    }
}

static void handle_enum_item(TSNode node, const char *source_code,
                             const char *directory, const char *filename,
                             ParseResult *result, SymbolFilter *filter, int line) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name_node)) return;

    char name[SYMBOL_MAX_LENGTH];
    safe_extract_node_text(source_code, name_node, name, sizeof(name), filename);

    char vis[SYMBOL_MAX_LENGTH];
    extract_visibility(node, source_code, vis, sizeof(vis), filename);
    char attrs[SYMBOL_MAX_LENGTH];
    extract_attributes(node, source_code, attrs, sizeof(attrs), filename);

    char location[128];
    format_source_location(node, location, sizeof(location));

    if (filter_should_index(filter, name)) {
        add_entry(result, name, line, CONTEXT_ENUM,
                  directory, filename, location,
                  &(ExtColumns){
                      .definition = "1",
                      .scope = vis[0] ? vis : NULL,
                      .clue = attrs[0] ? attrs : NULL
                  });
    }

    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) {
        /* Pass the enum name as the parent for variants */
        char saved[SYMBOL_MAX_LENGTH];
        snprintf(saved, sizeof(saved), "%s", g_current_impl);
        snprintf(g_current_impl, sizeof(g_current_impl), "%s", name);
        process_children(body, source_code, directory, filename, result, filter);
        snprintf(g_current_impl, sizeof(g_current_impl), "%s", saved);
    }
}

static void handle_enum_variant(TSNode node, const char *source_code,
                                const char *directory, const char *filename,
                                ParseResult *result, SymbolFilter *filter, int line) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name_node)) {
        process_children(node, source_code, directory, filename, result, filter);
        return;
    }

    char name[SYMBOL_MAX_LENGTH];
    safe_extract_node_text(source_code, name_node, name, sizeof(name), filename);

    if (filter_should_index(filter, name)) {
        add_entry(result, name, line, CONTEXT_ENUM_CASE,
                  directory, filename, NULL,
                  &(ExtColumns){
                      .definition = "1",
                      .parent = g_current_impl[0] ? g_current_impl : NULL
                  });
    }

    /* Process variant body for nested types/fields */
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) {
        process_children(body, source_code, directory, filename, result, filter);
    }
}

static void handle_trait_item(TSNode node, const char *source_code,
                              const char *directory, const char *filename,
                              ParseResult *result, SymbolFilter *filter, int line) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name_node)) return;

    char name[SYMBOL_MAX_LENGTH];
    safe_extract_node_text(source_code, name_node, name, sizeof(name), filename);

    char vis[SYMBOL_MAX_LENGTH];
    extract_visibility(node, source_code, vis, sizeof(vis), filename);
    char attrs[SYMBOL_MAX_LENGTH];
    extract_attributes(node, source_code, attrs, sizeof(attrs), filename);

    char location[128];
    format_source_location(node, location, sizeof(location));

    if (filter_should_index(filter, name)) {
        add_entry(result, name, line, CONTEXT_TRAIT,
                  directory, filename, location,
                  &(ExtColumns){
                      .definition = "1",
                      .scope = vis[0] ? vis : NULL,
                      .clue = attrs[0] ? attrs : NULL
                  });
    }

    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) {
        /* Methods inside a trait belong to the trait */
        char saved[SYMBOL_MAX_LENGTH];
        snprintf(saved, sizeof(saved), "%s", g_current_impl);
        snprintf(g_current_impl, sizeof(g_current_impl), "%s", name);
        process_children(body, source_code, directory, filename, result, filter);
        snprintf(g_current_impl, sizeof(g_current_impl), "%s", saved);
    }
}

static void handle_impl_item(TSNode node, const char *source_code,
                             const char *directory, const char *filename,
                             ParseResult *result, SymbolFilter *filter, int line) {
    TSNode type_node = ts_node_child_by_field_name(node, "type", 4);
    TSNode trait_node = ts_node_child_by_field_name(node, "trait", 5);

    char target[SYMBOL_MAX_LENGTH] = "";
    if (!ts_node_is_null(type_node)) {
        extract_base_type_name(type_node, source_code, target, sizeof(target), filename);
    }
    char trait_name[SYMBOL_MAX_LENGTH] = "";
    if (!ts_node_is_null(trait_node)) {
        extract_base_type_name(trait_node, source_code, trait_name, sizeof(trait_name), filename);
    }

    /* Record the impl block itself as a class-like definition so users can
     * locate "impl X" via qi. Symbol is the target type; clue holds the trait. */
    if (target[0] && filter_should_index(filter, target)) {
        char location[128];
        format_source_location(node, location, sizeof(location));
        char clue[SYMBOL_MAX_LENGTH * 2];
        if (trait_name[0]) {
            snprintf(clue, sizeof(clue), "impl %s", trait_name);
        } else {
            snprintf(clue, sizeof(clue), "impl");
        }
        add_entry(result, target, line, CONTEXT_CLASS,
                  directory, filename, location,
                  &(ExtColumns){
                      .definition = "1",
                      .clue = clue
                  });
    }

    /* Descend into body with g_current_impl set to target */
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) {
        char saved[SYMBOL_MAX_LENGTH];
        snprintf(saved, sizeof(saved), "%s", g_current_impl);
        snprintf(g_current_impl, sizeof(g_current_impl), "%s", target);
        process_children(body, source_code, directory, filename, result, filter);
        snprintf(g_current_impl, sizeof(g_current_impl), "%s", saved);
    }
}

static void handle_mod_item(TSNode node, const char *source_code,
                            const char *directory, const char *filename,
                            ParseResult *result, SymbolFilter *filter, int line) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name_node)) return;

    char name[SYMBOL_MAX_LENGTH];
    safe_extract_node_text(source_code, name_node, name, sizeof(name), filename);

    char vis[SYMBOL_MAX_LENGTH];
    extract_visibility(node, source_code, vis, sizeof(vis), filename);

    char location[128];
    format_source_location(node, location, sizeof(location));

    if (filter_should_index(filter, name)) {
        add_entry(result, name, line, CONTEXT_NAMESPACE,
                  directory, filename, location,
                  &(ExtColumns){
                      .definition = "1",
                      .modifier = vis[0] ? vis : NULL
                  });
    }

    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) {
        process_children(body, source_code, directory, filename, result, filter);
    }
}

/* Recursively walk a use-tree extracting imported identifiers. Each leaf
 * (use_as_clause's alias, scoped_identifier's name, identifier, or
 * use_wildcard) gets indexed as CONTEXT_IMPORT. */
static void index_use_tree(TSNode node, const char *source_code,
                           const char *directory, const char *filename,
                           ParseResult *result, SymbolFilter *filter, int line) {
    if (ts_node_is_null(node)) return;
    const char *t = ts_node_type(node);

    if (strcmp(t, "identifier") == 0 || strcmp(t, "type_identifier") == 0) {
        char name[SYMBOL_MAX_LENGTH];
        safe_extract_node_text(source_code, node, name, sizeof(name), filename);
        if (name[0] && filter_should_index(filter, name)) {
            add_entry(result, name, line, CONTEXT_IMPORT,
                      directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
        }
        return;
    }

    if (strcmp(t, "scoped_identifier") == 0) {
        /* import the trailing name (and let the path become parent) */
        TSNode name = ts_node_child_by_field_name(node, "name", 4);
        TSNode path = ts_node_child_by_field_name(node, "path", 4);
        char ns[SYMBOL_MAX_LENGTH] = "";
        if (!ts_node_is_null(path)) {
            safe_extract_node_text(source_code, path, ns, sizeof(ns), filename);
        }
        if (!ts_node_is_null(name)) {
            char sym[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, name, sym, sizeof(sym), filename);
            if (sym[0] && filter_should_index(filter, sym)) {
                add_entry(result, sym, line, CONTEXT_IMPORT,
                          directory, filename, NULL,
                          &(ExtColumns){
                              .namespace = ns[0] ? ns : NULL
                          });
            }
        }
        return;
    }

    if (strcmp(t, "use_as_clause") == 0) {
        /* `path::Original as Alias` — index the alias */
        TSNode alias = ts_node_child_by_field_name(node, "alias", 5);
        TSNode path = ts_node_child_by_field_name(node, "path", 4);
        char ns[SYMBOL_MAX_LENGTH] = "";
        if (!ts_node_is_null(path)) {
            safe_extract_node_text(source_code, path, ns, sizeof(ns), filename);
        }
        if (!ts_node_is_null(alias)) {
            char sym[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, alias, sym, sizeof(sym), filename);
            if (sym[0] && filter_should_index(filter, sym)) {
                add_entry(result, sym, line, CONTEXT_IMPORT,
                          directory, filename, NULL,
                          &(ExtColumns){
                              .namespace = ns[0] ? ns : NULL,
                              .clue = "as"
                          });
            }
        }
        return;
    }

    if (strcmp(t, "use_list") == 0 || strcmp(t, "scoped_use_list") == 0) {
        /* For scoped_use_list, the prefix `path` is the namespace; recurse into list */
        TSNode list_node = node;
        char ns[SYMBOL_MAX_LENGTH] = "";
        if (strcmp(t, "scoped_use_list") == 0) {
            TSNode path = ts_node_child_by_field_name(node, "path", 4);
            if (!ts_node_is_null(path)) {
                safe_extract_node_text(source_code, path, ns, sizeof(ns), filename);
            }
            TSNode list = ts_node_child_by_field_name(node, "list", 4);
            if (!ts_node_is_null(list)) list_node = list;
        }
        (void)ns; /* namespace tracking is handled per-leaf */
        uint32_t n = ts_node_child_count(list_node);
        for (uint32_t i = 0; i < n; i++) {
            index_use_tree(ts_node_child(list_node, i), source_code,
                           directory, filename, result, filter, line);
        }
        return;
    }

    /* Fallback: descend */
    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        index_use_tree(ts_node_child(node, i), source_code,
                       directory, filename, result, filter, line);
    }
}

static void handle_use_declaration(TSNode node, const char *source_code,
                                   const char *directory, const char *filename,
                                   ParseResult *result, SymbolFilter *filter, int line) {
    TSNode arg = ts_node_child_by_field_name(node, "argument", 8);
    if (ts_node_is_null(arg)) {
        process_children(node, source_code, directory, filename, result, filter);
        return;
    }
    index_use_tree(arg, source_code, directory, filename, result, filter, line);
}

static void handle_let_declaration(TSNode node, const char *source_code,
                                   const char *directory, const char *filename,
                                   ParseResult *result, SymbolFilter *filter, int line) {
    TSNode pattern = ts_node_child_by_field_name(node, "pattern", 7);
    TSNode type_node = ts_node_child_by_field_name(node, "type", 4);
    TSNode value = ts_node_child_by_field_name(node, "value", 5);

    char type_str[SYMBOL_MAX_LENGTH] = "";
    if (!ts_node_is_null(type_node)) {
        safe_extract_node_text(source_code, type_node, type_str, sizeof(type_str), filename);
    }

    index_pattern_identifiers(pattern, source_code, directory, filename,
                              result, filter, line, type_str);

    /* Visit value so calls/identifiers inside get indexed */
    if (!ts_node_is_null(value)) {
        visit_node(value, source_code, directory, filename, result, filter);
    }
}

/* `if let PAT = EXPR` / `while let PAT = EXPR` condition */
static void handle_let_condition(TSNode node, const char *source_code,
                                 const char *directory, const char *filename,
                                 ParseResult *result, SymbolFilter *filter, int line) {
    /* let_condition has children: `let`, <pattern>, `=`, <value>. The pattern
     * is the first named child that isn't `let` or `=`. */
    TSNode pattern = {0};
    TSNode value = {0};
    int seen_eq = 0;
    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        TSNode c = ts_node_child(node, i);
        const char *t = ts_node_type(c);
        if (strcmp(t, "let") == 0) continue;
        if (strcmp(t, "=") == 0) { seen_eq = 1; continue; }
        if (!seen_eq && ts_node_is_null(pattern)) pattern = c;
        else if (seen_eq && ts_node_is_null(value)) value = c;
    }
    index_pattern_identifiers(pattern, source_code, directory, filename,
                              result, filter, line, NULL);
    if (!ts_node_is_null(value)) {
        visit_node(value, source_code, directory, filename, result, filter);
    }
}

static void handle_for_expression(TSNode node, const char *source_code,
                                  const char *directory, const char *filename,
                                  ParseResult *result, SymbolFilter *filter, int line) {
    TSNode pattern = ts_node_child_by_field_name(node, "pattern", 7);
    TSNode value = ts_node_child_by_field_name(node, "value", 5);
    TSNode body = ts_node_child_by_field_name(node, "body", 4);

    index_pattern_identifiers(pattern, source_code, directory, filename,
                              result, filter, line, NULL);
    if (!ts_node_is_null(value)) {
        visit_node(value, source_code, directory, filename, result, filter);
    }
    if (!ts_node_is_null(body)) {
        process_children(body, source_code, directory, filename, result, filter);
    }
}

static void handle_match_expression(TSNode node, const char *source_code,
                                    const char *directory, const char *filename,
                                    ParseResult *result, SymbolFilter *filter, int line) {
    (void)line;
    TSNode value = ts_node_child_by_field_name(node, "value", 5);
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(value)) {
        visit_node(value, source_code, directory, filename, result, filter);
    }
    if (ts_node_is_null(body)) return;

    /* match_block contains match_arm nodes; each has a match_pattern child
     * and a body expression. */
    uint32_t n = ts_node_child_count(body);
    for (uint32_t i = 0; i < n; i++) {
        TSNode arm = ts_node_child(body, i);
        if (strcmp(ts_node_type(arm), "match_arm") != 0) continue;

        uint32_t an = ts_node_child_count(arm);
        for (uint32_t j = 0; j < an; j++) {
            TSNode ac = ts_node_child(arm, j);
            const char *act = ts_node_type(ac);
            if (strcmp(act, "match_pattern") == 0) {
                TSPoint sp = ts_node_start_point(ac);
                int arm_line = (int)(sp.row + 1);
                /* The actual pattern is inside match_pattern; possibly with a
                 * `condition` field for guards. Walk its children. */
                uint32_t mn = ts_node_child_count(ac);
                for (uint32_t k = 0; k < mn; k++) {
                    TSNode mc = ts_node_child(ac, k);
                    index_pattern_identifiers(mc, source_code, directory, filename,
                                              result, filter, arm_line, NULL);
                }
            } else if (strcmp(act, "=>") != 0 && strcmp(act, ",") != 0) {
                /* The arm body expression */
                visit_node(ac, source_code, directory, filename, result, filter);
            }
        }
    }
}

/* Bodyless function (trait method signature or FFI declaration). */
static void handle_function_signature_item(TSNode node, const char *source_code,
                                           const char *directory, const char *filename,
                                           ParseResult *result, SymbolFilter *filter, int line) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name_node)) return;

    char fn_name[SYMBOL_MAX_LENGTH];
    safe_extract_node_text(source_code, name_node, fn_name, sizeof(fn_name), filename);

    char modifiers[SYMBOL_MAX_LENGTH];
    extract_fn_modifiers(node, source_code, modifiers, sizeof(modifiers), filename);

    char attrs[SYMBOL_MAX_LENGTH];
    extract_attributes(node, source_code, attrs, sizeof(attrs), filename);

    char return_type[SYMBOL_MAX_LENGTH] = "";
    TSNode ret_node = ts_node_child_by_field_name(node, "return_type", 11);
    if (!ts_node_is_null(ret_node)) {
        safe_extract_node_text(source_code, ret_node, return_type, sizeof(return_type), filename);
    }

    char location[128];
    format_source_location(node, location, sizeof(location));

    char vis[SYMBOL_MAX_LENGTH];
    extract_visibility(node, source_code, vis, sizeof(vis), filename);

    if (fn_name[0] && filter_should_index(filter, fn_name)) {
        add_entry(result, fn_name, line, CONTEXT_FUNCTION,
                  directory, filename, location,
                  &(ExtColumns){
                      .definition = "1",
                      .scope = vis[0] ? vis : NULL,
                      .modifier = modifiers[0] ? modifiers : NULL,
                      .clue = attrs[0] ? attrs : NULL,
                      .type = return_type[0] ? return_type : NULL,
                      .parent = g_current_impl[0] ? g_current_impl : NULL
                  });
    }

    /* Process parameters so their identifiers/types get indexed */
    TSNode params_node = ts_node_child_by_field_name(node, "parameters", 10);
    if (!ts_node_is_null(params_node)) {
        process_children(params_node, source_code, directory, filename, result, filter);
    }
}

static void handle_const_or_static(TSNode node, const char *source_code,
                                   const char *directory, const char *filename,
                                   ParseResult *result, SymbolFilter *filter,
                                   int line, ContextType ctx) {
    (void)ctx;
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    TSNode type_node = ts_node_child_by_field_name(node, "type", 4);
    TSNode value = ts_node_child_by_field_name(node, "value", 5);

    if (ts_node_is_null(name_node)) {
        process_children(node, source_code, directory, filename, result, filter);
        return;
    }

    char name[SYMBOL_MAX_LENGTH];
    safe_extract_node_text(source_code, name_node, name, sizeof(name), filename);

    char vis[SYMBOL_MAX_LENGTH];
    extract_visibility(node, source_code, vis, sizeof(vis), filename);

    char type_str[SYMBOL_MAX_LENGTH] = "";
    if (!ts_node_is_null(type_node)) {
        safe_extract_node_text(source_code, type_node, type_str, sizeof(type_str), filename);
    }

    char location[128];
    format_source_location(node, location, sizeof(location));

    if (filter_should_index(filter, name)) {
        add_entry(result, name, line, CONTEXT_VARIABLE,
                  directory, filename, location,
                  &(ExtColumns){
                      .definition = "1",
                      .scope = vis[0] ? vis : NULL,
                      .type = type_str[0] ? type_str : NULL
                  });
    }

    if (!ts_node_is_null(value)) {
        visit_node(value, source_code, directory, filename, result, filter);
    }
}

static void handle_type_item(TSNode node, const char *source_code,
                             const char *directory, const char *filename,
                             ParseResult *result, SymbolFilter *filter, int line) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    TSNode type_node = ts_node_child_by_field_name(node, "type", 4);
    if (ts_node_is_null(name_node)) return;

    char name[SYMBOL_MAX_LENGTH];
    safe_extract_node_text(source_code, name_node, name, sizeof(name), filename);

    char vis[SYMBOL_MAX_LENGTH];
    extract_visibility(node, source_code, vis, sizeof(vis), filename);

    char type_str[SYMBOL_MAX_LENGTH] = "";
    if (!ts_node_is_null(type_node)) {
        safe_extract_node_text(source_code, type_node, type_str, sizeof(type_str), filename);
    }

    if (filter_should_index(filter, name)) {
        add_entry(result, name, line, CONTEXT_TYPE,
                  directory, filename, NULL,
                  &(ExtColumns){
                      .definition = "1",
                      .scope = vis[0] ? vis : NULL,
                      .type = type_str[0] ? type_str : NULL
                  });
    }
}

static void handle_macro_definition(TSNode node, const char *source_code,
                                    const char *directory, const char *filename,
                                    ParseResult *result, SymbolFilter *filter, int line) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name_node)) return;

    char name[SYMBOL_MAX_LENGTH];
    safe_extract_node_text(source_code, name_node, name, sizeof(name), filename);

    char location[128];
    format_source_location(node, location, sizeof(location));

    if (filter_should_index(filter, name)) {
        add_entry(result, name, line, CONTEXT_FUNCTION,
                  directory, filename, location,
                  &(ExtColumns){
                      .definition = "1",
                      .clue = "macro_rules!"
                  });
    }
}

static void handle_macro_invocation(TSNode node, const char *source_code,
                                    const char *directory, const char *filename,
                                    ParseResult *result, SymbolFilter *filter, int line) {
    TSNode macro = ts_node_child_by_field_name(node, "macro", 5);
    if (!ts_node_is_null(macro)) {
        char name[SYMBOL_MAX_LENGTH];
        safe_extract_node_text(source_code, macro, name, sizeof(name), filename);
        if (name[0] && filter_should_index(filter, name)) {
            add_entry(result, name, line, CONTEXT_CALL,
                      directory, filename, NULL,
                      &(ExtColumns){.clue = "macro!"});
        }
    }
    /* Walk arguments to surface inner identifiers/strings/calls */
    TSNode args = ts_node_child_by_field_name(node, "arguments", 9);
    if (!ts_node_is_null(args)) {
        process_children(args, source_code, directory, filename, result, filter);
    }
}

static void handle_call_expression(TSNode node, const char *source_code,
                                   const char *directory, const char *filename,
                                   ParseResult *result, SymbolFilter *filter, int line) {
    /* If this call is the inner expression of an await_expression, mark it
     * with modifier "await" — matches the Python indexer's convention. */
    const char *modifier = NULL;
    TSNode parent = ts_node_parent(node);
    if (!ts_node_is_null(parent) &&
        strcmp(ts_node_type(parent), "await_expression") == 0) {
        modifier = "await";
    }

    TSNode fn = ts_node_child_by_field_name(node, "function", 8);
    if (!ts_node_is_null(fn)) {
        const char *ft = ts_node_type(fn);
        if (strcmp(ft, "identifier") == 0) {
            char name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, fn, name, sizeof(name), filename);
            if (name[0] && filter_should_index(filter, name)) {
                add_entry(result, name, line, CONTEXT_CALL,
                          directory, filename, NULL,
                          &(ExtColumns){.modifier = modifier});
            }
        } else if (strcmp(ft, "field_expression") == 0) {
            /* obj.method() */
            TSNode field = ts_node_child_by_field_name(fn, "field", 5);
            TSNode value = ts_node_child_by_field_name(fn, "value", 5);
            char par[SYMBOL_MAX_LENGTH] = "";
            if (!ts_node_is_null(value) &&
                strcmp(ts_node_type(value), "identifier") == 0) {
                safe_extract_node_text(source_code, value, par, sizeof(par), filename);
            }
            if (!ts_node_is_null(field)) {
                char name[SYMBOL_MAX_LENGTH];
                safe_extract_node_text(source_code, field, name, sizeof(name), filename);
                if (name[0] && filter_should_index(filter, name)) {
                    add_entry(result, name, line, CONTEXT_CALL,
                              directory, filename, NULL,
                              &(ExtColumns){
                                  .parent = par[0] ? par : NULL,
                                  .modifier = modifier
                              });
                }
            }
            /* Visit the receiver expression too */
            if (!ts_node_is_null(value)) {
                visit_node(value, source_code, directory, filename, result, filter);
            }
        } else if (strcmp(ft, "scoped_identifier") == 0) {
            /* path::func() */
            TSNode name = ts_node_child_by_field_name(fn, "name", 4);
            TSNode path = ts_node_child_by_field_name(fn, "path", 4);
            char ns[SYMBOL_MAX_LENGTH] = "";
            if (!ts_node_is_null(path)) {
                safe_extract_node_text(source_code, path, ns, sizeof(ns), filename);
            }
            if (!ts_node_is_null(name)) {
                char sym[SYMBOL_MAX_LENGTH];
                safe_extract_node_text(source_code, name, sym, sizeof(sym), filename);
                if (sym[0] && filter_should_index(filter, sym)) {
                    add_entry(result, sym, line, CONTEXT_CALL,
                              directory, filename, NULL,
                              &(ExtColumns){
                                  .namespace = ns[0] ? ns : NULL,
                                  .modifier = modifier
                              });
                }
            }
        } else {
            /* Other call shapes - visit children */
            visit_node(fn, source_code, directory, filename, result, filter);
        }
    }
    /* Visit arguments */
    TSNode args = ts_node_child_by_field_name(node, "arguments", 9);
    if (!ts_node_is_null(args)) {
        process_children(args, source_code, directory, filename, result, filter);
    }
}

static void handle_closure_expression(TSNode node, const char *source_code,
                                      const char *directory, const char *filename,
                                      ParseResult *result, SymbolFilter *filter, int line) {
    char location[128];
    format_source_location(node, location, sizeof(location));

    add_entry(result, "<closure>", line, CONTEXT_LAMBDA,
              directory, filename, location,
              &(ExtColumns){.definition = "1"});

    TSNode params = ts_node_child_by_field_name(node, "parameters", 10);
    if (!ts_node_is_null(params)) {
        process_children(params, source_code, directory, filename, result, filter);
    }
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) {
        visit_node(body, source_code, directory, filename, result, filter);
    }
}

/* ---------------- Strings & comments ---------------- */

static void handle_line_comment(TSNode node, const char *source_code,
                                const char *directory, const char *filename,
                                ParseResult *result, SymbolFilter *filter, int line) {
    char text[COMMENT_TEXT_BUFFER];
    safe_extract_node_text(source_code, node, text, sizeof(text), filename);
    char *start = strip_comment_delimiters(text);

    char word[CLEANED_WORD_BUFFER];
    char cleaned[CLEANED_WORD_BUFFER];
    char *word_start = start;
    for (char *p = start; ; p++) {
        if (*p == '\0' || isspace((unsigned char)*p)) {
            if (p > word_start) {
                size_t wlen = (size_t)(p - word_start);
                if (wlen < sizeof(word)) {
                    snprintf(word, sizeof(word), "%.*s", (int)wlen, word_start);
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

static void handle_block_comment(TSNode node, const char *source_code,
                                 const char *directory, const char *filename,
                                 ParseResult *result, SymbolFilter *filter, int line) {
    char text[COMMENT_TEXT_BUFFER];
    safe_extract_node_text(source_code, node, text, sizeof(text), filename);
    char *start = strip_comment_delimiters(text);

    /* Emit one entry per word; line number stays at the comment start.
     * Block comments can span many lines; for now we report the start line. */
    char word[CLEANED_WORD_BUFFER];
    char cleaned[CLEANED_WORD_BUFFER];
    char *word_start = start;
    for (char *p = start; ; p++) {
        if (*p == '\0' || isspace((unsigned char)*p)) {
            if (p > word_start) {
                size_t wlen = (size_t)(p - word_start);
                if (wlen < sizeof(word)) {
                    snprintf(word, sizeof(word), "%.*s", (int)wlen, word_start);
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

static void handle_string_literal(TSNode node, const char *source_code,
                                  const char *directory, const char *filename,
                                  ParseResult *result, SymbolFilter *filter, int line) {
    /* Iterate children; index words from string_content nodes only. */
    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        TSNode c = ts_node_child(node, i);
        const char *ct = ts_node_type(c);
        if (strcmp(ct, "string_content") == 0) {
            char content[CLEANED_WORD_BUFFER];
            safe_extract_node_text(source_code, c, content, sizeof(content), filename);

            char word[CLEANED_WORD_BUFFER];
            char cleaned[CLEANED_WORD_BUFFER];
            char *word_start = content;
            for (char *p = content; ; p++) {
                if (*p == '\0' || isspace((unsigned char)*p)) {
                    if (p > word_start) {
                        size_t wlen = (size_t)(p - word_start);
                        if (wlen < sizeof(word)) {
                            snprintf(word, sizeof(word), "%.*s", (int)wlen, word_start);
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

/* ---------------- Dispatcher ---------------- */

static void visit_node(TSNode node, const char *source_code, const char *directory,
                       const char *filename, ParseResult *result, SymbolFilter *filter) {
    if (ts_node_is_null(node)) return;
    const char *t = ts_node_type(node);
    TSPoint sp = ts_node_start_point(node);
    int line = (int)(sp.row + 1);

    if (g_debug) fprintf(stderr, "[rust] visit %s line=%d\n", t, line);

    if (strcmp(t, "function_item") == 0) {
        handle_function_item(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "function_signature_item") == 0) {
        handle_function_signature_item(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "for_expression") == 0) {
        handle_for_expression(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "match_expression") == 0) {
        handle_match_expression(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "let_condition") == 0) {
        handle_let_condition(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "parameter") == 0) {
        handle_parameter(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "struct_item") == 0 || strcmp(t, "union_item") == 0) {
        handle_struct_item(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "field_declaration") == 0) {
        handle_field_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "enum_item") == 0) {
        handle_enum_item(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "enum_variant") == 0) {
        handle_enum_variant(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "trait_item") == 0) {
        handle_trait_item(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "impl_item") == 0) {
        handle_impl_item(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "mod_item") == 0) {
        handle_mod_item(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "use_declaration") == 0) {
        handle_use_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "let_declaration") == 0) {
        handle_let_declaration(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "const_item") == 0) {
        handle_const_or_static(node, source_code, directory, filename, result, filter, line, CONTEXT_VARIABLE);
        return;
    }
    if (strcmp(t, "static_item") == 0) {
        handle_const_or_static(node, source_code, directory, filename, result, filter, line, CONTEXT_VARIABLE);
        return;
    }
    if (strcmp(t, "type_item") == 0) {
        handle_type_item(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "macro_definition") == 0) {
        handle_macro_definition(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "macro_invocation") == 0) {
        handle_macro_invocation(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "call_expression") == 0) {
        handle_call_expression(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "closure_expression") == 0) {
        handle_closure_expression(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "line_comment") == 0) {
        handle_line_comment(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "block_comment") == 0) {
        handle_block_comment(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "string_literal") == 0 || strcmp(t, "raw_string_literal") == 0) {
        handle_string_literal(node, source_code, directory, filename, result, filter, line);
        return;
    }
    /* Skip attribute_item content - already harvested via extract_attributes */
    if (strcmp(t, "attribute_item") == 0 || strcmp(t, "inner_attribute_item") == 0) {
        return;
    }

    /* Default: recurse into children */
    process_children(node, source_code, directory, filename, result, filter);
}

/* ---------------- Lifecycle ---------------- */

int parser_init(RustParser *parser, SymbolFilter *filter) {
    parser->parser = ts_parser_new();
    if (!parser->parser) return -1;

    const TSLanguage *language = tree_sitter_rust();
    if (!ts_parser_set_language(parser->parser, (TSLanguage*)language)) {
        ts_parser_delete(parser->parser);
        return -1;
    }
    parser->filter = filter;
    parser->debug = 0;
    g_debug = 0;
    g_current_impl[0] = '\0';
    return 0;
}

void parser_set_debug(RustParser *parser, int debug) {
    parser->debug = debug;
    g_debug = debug;
}

int parser_parse_file(RustParser *parser, const char *filepath,
                      const char *project_root, ParseResult *result) {
    FILE *fp = safe_fopen(filepath, "rb", 0);
    if (!fp) {
        fprintf(stderr, "Cannot open file: %s\n", filepath);
        return -1;
    }
    int fd = fileno(fp);
    struct stat st;
    if (fstat(fd, &st) != 0) { fclose(fp); return -1; }
    size_t file_size = (size_t)st.st_size;

    char *source_code = malloc(file_size + 1);
    if (!source_code) { fclose(fp); return -1; }
    size_t bytes_read = fread(source_code, 1, file_size, fp);
    if (bytes_read != file_size) {
        fprintf(stderr, "Error reading %s: expected %zu got %zu\n",
                filepath, file_size, bytes_read);
        free(source_code); fclose(fp); return -1;
    }
    source_code[bytes_read] = '\0';
    fclose(fp);

    result->count = 0;
    TSTree *tree = ts_parser_parse_string(parser->parser, NULL,
                                          source_code, (uint32_t)bytes_read);
    if (!tree) {
        fprintf(stderr, "Failed to parse: %s\n", filepath);
        free(source_code);
        return -1;
    }

    TSNode root = ts_tree_root_node(tree);

    char directory[DIRECTORY_MAX_LENGTH];
    char filename[FILENAME_MAX_LENGTH];
    get_relative_path(filepath, project_root, directory, filename);

    /* Filename without extension as CONTEXT_FILENAME */
    char fname_noext[FILENAME_MAX_LENGTH];
    snprintf(fname_noext, sizeof(fname_noext), "%s", filename);
    char *dot = strrchr(fname_noext, '.');
    if (dot) *dot = '\0';
    if (filter_should_index(parser->filter, fname_noext)) {
        add_entry(result, fname_noext, 1, CONTEXT_FILENAME,
                  directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
    }

    g_current_impl[0] = '\0';
    visit_node(root, source_code, directory, filename, result, parser->filter);

    ts_tree_delete(tree);
    free(source_code);
    return 0;
}

void parser_free(RustParser *parser) {
    if (parser->parser) {
        ts_parser_delete(parser->parser);
        parser->parser = NULL;
    }
}
