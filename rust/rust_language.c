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

/* ABI of the enclosing extern block (e.g. `extern "C"`); applied as the
 * modifier of the foreign declarations inside it. */
static char g_extern_abi[SYMBOL_MAX_LENGTH] = "";

/* Parent symbol for the struct literal currently being visited; gives
 * field initializers (Greeter { label: ... }) their parent_symbol (the
 * let-bound variable, or the enclosing field for nested literals).
 * Mirrors the C indexer's designated-initializer handling. */
static char g_initializer_parent[SYMBOL_MAX_LENGTH] = "";

/* Bounded copy between symbol buffers. strncat-based copies of a
 * possibly-full equal-sized source buffer trip -Wstringop-truncation. */
static void copy_symbol(char *dst, size_t dst_size, const char *src) {
    size_t len = strnlength(src, dst_size - 1);
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* Forward declarations */
static void visit_node(TSNode node, const char *source_code, const char *directory,
                       const char *filename, ParseResult *result, SymbolFilter *filter);
static void process_children(TSNode node, const char *source_code, const char *directory,
                             const char *filename, ParseResult *result, SymbolFilter *filter);
static void handle_string_literal(TSNode node, const char *source_code,
                                  const char *directory, const char *filename,
                                  ParseResult *result, SymbolFilter *filter, int line);

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
        /* Inner attributes (#![...]) belong to the enclosing scope, not to
         * the item that happens to follow them */
        if (strcmp(st, "attribute_item") != 0) {
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

/* #[derive(Debug, Clone)]: each derived trait is a TRAIT usage. The names
 * sit as plain identifier tokens inside the attribute's token_tree. */
static void handle_attribute_item(TSNode node, const char *source_code,
                                  const char *directory, const char *filename,
                                  ParseResult *result, SymbolFilter *filter) {
    TSNode attr = {0};
    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        TSNode c = ts_node_child(node, i);
        if (strcmp(ts_node_type(c), "attribute") == 0) { attr = c; break; }
    }
    if (ts_node_is_null(attr)) return;

    /* Only derive lists name traits; other attribute arguments are config */
    TSNode path = ts_node_child(attr, 0);
    if (ts_node_is_null(path) || strcmp(ts_node_type(path), "identifier") != 0) return;
    char path_str[SYMBOL_MAX_LENGTH];
    safe_extract_node_text(source_code, path, path_str, sizeof(path_str), filename);
    if (strcmp(path_str, "derive") != 0) return;

    uint32_t ac = ts_node_child_count(attr);
    for (uint32_t i = 0; i < ac; i++) {
        TSNode c = ts_node_child(attr, i);
        if (strcmp(ts_node_type(c), "token_tree") != 0) continue;
        uint32_t tc = ts_node_child_count(c);
        for (uint32_t j = 0; j < tc; j++) {
            TSNode tok = ts_node_child(c, j);
            if (strcmp(ts_node_type(tok), "identifier") != 0) continue;
            char name[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, tok, name, sizeof(name), filename);
            if (name[0] && filter_should_index(filter, name)) {
                TSPoint sp = ts_node_start_point(tok);
                add_entry(result, name, (int)(sp.row + 1), CONTEXT_TRAIT,
                          directory, filename, NULL,
                          &(ExtColumns){.clue = "derive"});
            }
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
/* Index each trait named in a trait_bounds node as a TRAIT usage:
 * `T: Eq + std::hash::Hash` yields Eq and Hash (ns std::hash).
 * Lifetimes and higher-ranked/fn bounds are skipped. */
static void index_trait_bounds(TSNode bounds, const char *source_code,
                               const char *directory, const char *filename,
                               ParseResult *result, SymbolFilter *filter) {
    if (ts_node_is_null(bounds)) return;
    uint32_t n = ts_node_child_count(bounds);
    for (uint32_t i = 0; i < n; i++) {
        TSNode c = ts_node_child(bounds, i);
        const char *t = ts_node_type(c);
        char sym[SYMBOL_MAX_LENGTH] = "";
        char ns[SYMBOL_MAX_LENGTH] = "";

        if (strcmp(t, "type_identifier") == 0) {
            safe_extract_node_text(source_code, c, sym, sizeof(sym), filename);
        } else if (strcmp(t, "scoped_type_identifier") == 0) {
            TSNode name = ts_node_child_by_field_name(c, "name", 4);
            TSNode path = ts_node_child_by_field_name(c, "path", 4);
            if (!ts_node_is_null(name)) {
                safe_extract_node_text(source_code, name, sym, sizeof(sym), filename);
            }
            if (!ts_node_is_null(path)) {
                safe_extract_node_text(source_code, path, ns, sizeof(ns), filename);
            }
        } else if (strcmp(t, "generic_type") == 0) {
            /* Iterator<Item = &'a str> -> Iterator */
            extract_base_type_name(c, source_code, sym, sizeof(sym), filename);
        } else {
            continue;
        }

        if (sym[0] && filter_should_index(filter, sym)) {
            TSPoint sp = ts_node_start_point(c);
            add_entry(result, sym, (int)(sp.row + 1), CONTEXT_TRAIT,
                      directory, filename, NULL,
                      &(ExtColumns){.namespace = ns[0] ? ns : NULL});
        }
    }
}

/* Index the trait bounds attached to an item: supertraits / associated-type
 * bounds (direct trait_bounds child), inline generic bounds (<T: Clone>),
 * and where clauses. */
static void index_generic_constraints(TSNode item, const char *source_code,
                                      const char *directory, const char *filename,
                                      ParseResult *result, SymbolFilter *filter) {
    uint32_t n = ts_node_child_count(item);
    for (uint32_t i = 0; i < n; i++) {
        TSNode c = ts_node_child(item, i);
        const char *t = ts_node_type(c);
        if (strcmp(t, "trait_bounds") == 0) {
            index_trait_bounds(c, source_code, directory, filename, result, filter);
        } else if (strcmp(t, "type_parameters") == 0 ||
                   strcmp(t, "where_clause") == 0) {
            /* type_parameter / where_predicate children each may carry bounds */
            uint32_t m = ts_node_child_count(c);
            for (uint32_t j = 0; j < m; j++) {
                TSNode p = ts_node_child(c, j);
                uint32_t k = ts_node_child_count(p);
                for (uint32_t q = 0; q < k; q++) {
                    TSNode b = ts_node_child(p, q);
                    if (strcmp(ts_node_type(b), "trait_bounds") == 0) {
                        index_trait_bounds(b, source_code, directory, filename,
                                           result, filter);
                    }
                }
            }
        }
    }
}

/* True if the node has a direct mutable_specifier child
 * (let mut x, static mut X, fn f(mut x: T)) */
static int has_mut_specifier(TSNode node) {
    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        if (strcmp(ts_node_type(ts_node_child(node, i)), "mutable_specifier") == 0) {
            return 1;
        }
    }
    return 0;
}

static void index_pattern_identifiers(TSNode pattern, const char *source_code,
                                      const char *directory, const char *filename,
                                      ParseResult *result, SymbolFilter *filter,
                                      int line, const char *type_str,
                                      const char *modifier) {
    if (ts_node_is_null(pattern)) return;
    const char *t = ts_node_type(pattern);

    /* shorthand_field_identifier: `Rect { width, height }` binds width/height */
    if (strcmp(t, "identifier") == 0 ||
        strcmp(t, "shorthand_field_identifier") == 0) {
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
                          .type = (type_str && type_str[0]) ? type_str : NULL,
                          .modifier = (modifier && modifier[0]) ? modifier : NULL
                      });
        }
        return;
    }

    /* Scoped paths in patterns are constructor/constant references
     * (e.g. `AppError::NotFound`), never bindings: index them as variant
     * usages with the path as namespace, like scoped calls. */
    if (strcmp(t, "scoped_identifier") == 0 ||
        strcmp(t, "scoped_type_identifier") == 0) {
        TSNode name = ts_node_child_by_field_name(pattern, "name", 4);
        TSNode path = ts_node_child_by_field_name(pattern, "path", 4);
        char ns[SYMBOL_MAX_LENGTH] = "";
        if (!ts_node_is_null(path)) {
            safe_extract_node_text(source_code, path, ns, sizeof(ns), filename);
        }
        if (!ts_node_is_null(name)) {
            char sym[SYMBOL_MAX_LENGTH];
            safe_extract_node_text(source_code, name, sym, sizeof(sym), filename);
            if (sym[0] && filter_should_index(filter, sym)) {
                add_entry(result, sym, line, CONTEXT_ENUM_CASE,
                          directory, filename, NULL,
                          &(ExtColumns){.namespace = ns[0] ? ns : NULL});
            }
        }
        return;
    }

    if (strcmp(t, "tuple_struct_pattern") == 0 ||
        strcmp(t, "struct_pattern") == 0) {
        TSNode type_node = ts_node_child_by_field_name(pattern, "type", 4);
        uint32_t n = ts_node_child_count(pattern);
        for (uint32_t i = 0; i < n; i++) {
            TSNode c = ts_node_child(pattern, i);
            if (!ts_node_is_null(type_node) &&
                ts_node_start_byte(c) == ts_node_start_byte(type_node) &&
                ts_node_end_byte(c) == ts_node_end_byte(type_node)) {
                /* The constructor: scoped paths recurse into the handler
                 * above; bare names (Some(x), Point { .. }) are indexed as
                 * type usages, matching struct-literal handling. */
                const char *ct = ts_node_type(c);
                if (strcmp(ct, "scoped_identifier") == 0 ||
                    strcmp(ct, "scoped_type_identifier") == 0) {
                    index_pattern_identifiers(c, source_code, directory, filename,
                                              result, filter, line, NULL, NULL);
                } else if (strcmp(ct, "identifier") == 0 ||
                           strcmp(ct, "type_identifier") == 0) {
                    char sym[SYMBOL_MAX_LENGTH];
                    safe_extract_node_text(source_code, c, sym, sizeof(sym), filename);
                    if (sym[0] && filter_should_index(filter, sym)) {
                        add_entry(result, sym, line, CONTEXT_CLASS,
                                  directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
                    }
                }
                continue;
            }
            index_pattern_identifiers(c, source_code, directory, filename,
                                      result, filter, line, NULL, modifier);
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
                                  result, filter, line, NULL, modifier);
        return;
    }

    /* All other patterns: recurse into children
     * (tuple_pattern, mut_pattern, reference_pattern, ref_pattern,
     *  captured_pattern, or_pattern, slice_pattern, ...);
     * bindings under a mut_pattern carry the "mut" modifier */
    if (strcmp(t, "mut_pattern") == 0) modifier = "mut";
    uint32_t n = ts_node_child_count(pattern);
    for (uint32_t i = 0; i < n; i++) {
        index_pattern_identifiers(ts_node_child(pattern, i), source_code,
                                  directory, filename, result, filter, line,
                                  NULL, modifier);
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

    char location[SOURCE_LOCATION_MAX_LENGTH];
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

    index_generic_constraints(node, source_code, directory, filename, result, filter);

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
                      .type = type_str[0] ? type_str : NULL,
                      .modifier = has_mut_specifier(node) ? "mut" : NULL
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

    char location[SOURCE_LOCATION_MAX_LENGTH];
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

    index_generic_constraints(node, source_code, directory, filename, result, filter);

    /* Process body fields; they belong to the struct */
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) {
        char saved[SYMBOL_MAX_LENGTH];
        snprintf(saved, sizeof(saved), "%s", g_current_impl);
        snprintf(g_current_impl, sizeof(g_current_impl), "%s", name);
        process_children(body, source_code, directory, filename, result, filter);
        snprintf(g_current_impl, sizeof(g_current_impl), "%s", saved);
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
                      .type = type_str[0] ? type_str : NULL,
                      .parent = g_current_impl[0] ? g_current_impl : NULL
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

    char location[SOURCE_LOCATION_MAX_LENGTH];
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

    index_generic_constraints(node, source_code, directory, filename, result, filter);

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

    /* Process variant body for nested types/fields; the fields of
     * `Rect { width, height }` belong to the variant */
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) {
        char saved[SYMBOL_MAX_LENGTH];
        snprintf(saved, sizeof(saved), "%s", g_current_impl);
        snprintf(g_current_impl, sizeof(g_current_impl), "%s", name);
        process_children(body, source_code, directory, filename, result, filter);
        snprintf(g_current_impl, sizeof(g_current_impl), "%s", saved);
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

    char location[SOURCE_LOCATION_MAX_LENGTH];
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

    index_generic_constraints(node, source_code, directory, filename, result, filter);

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
        char location[SOURCE_LOCATION_MAX_LENGTH];
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

    index_generic_constraints(node, source_code, directory, filename, result, filter);

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

    char attrs[SYMBOL_MAX_LENGTH];
    extract_attributes(node, source_code, attrs, sizeof(attrs), filename);

    char location[SOURCE_LOCATION_MAX_LENGTH];
    format_source_location(node, location, sizeof(location));

    if (filter_should_index(filter, name)) {
        add_entry(result, name, line, CONTEXT_NAMESPACE,
                  directory, filename, location,
                  &(ExtColumns){
                      .definition = "1",
                      .modifier = vis[0] ? vis : NULL,
                      .clue = attrs[0] ? attrs : NULL
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
/* Join an accumulated use-path prefix with a nested path segment: "a", "b::c"
 * -> "a::b::c". Either side may be empty. */
static void join_use_path(const char *prefix, const char *path,
                          char *out, size_t out_size) {
    if (prefix && prefix[0] && path && path[0]) {
        snprintf(out, out_size, "%s::%s", prefix, path);
    } else if (path && path[0]) {
        snprintf(out, out_size, "%s", path);
    } else if (prefix && prefix[0]) {
        snprintf(out, out_size, "%s", prefix);
    } else {
        out[0] = '\0';
    }
}

static void index_use_tree(TSNode node, const char *source_code,
                           const char *directory, const char *filename,
                           ParseResult *result, SymbolFilter *filter, int line,
                           const char *ns_prefix) {
    if (ts_node_is_null(node)) return;
    const char *t = ts_node_type(node);

    if (strcmp(t, "identifier") == 0 || strcmp(t, "type_identifier") == 0) {
        char name[SYMBOL_MAX_LENGTH];
        safe_extract_node_text(source_code, node, name, sizeof(name), filename);
        if (name[0] && filter_should_index(filter, name)) {
            add_entry(result, name, line, CONTEXT_IMPORT,
                      directory, filename, NULL,
                      &(ExtColumns){
                          .namespace = (ns_prefix && ns_prefix[0]) ? ns_prefix : NULL
                      });
        }
        return;
    }

    if (strcmp(t, "scoped_identifier") == 0) {
        /* import the trailing name; accumulated prefix + path is the namespace */
        TSNode name = ts_node_child_by_field_name(node, "name", 4);
        TSNode path = ts_node_child_by_field_name(node, "path", 4);
        char path_str[SYMBOL_MAX_LENGTH] = "";
        if (!ts_node_is_null(path)) {
            safe_extract_node_text(source_code, path, path_str, sizeof(path_str), filename);
        }
        char ns[SYMBOL_MAX_LENGTH];
        join_use_path(ns_prefix, path_str, ns, sizeof(ns));
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
        char path_str[SYMBOL_MAX_LENGTH] = "";
        if (!ts_node_is_null(path)) {
            safe_extract_node_text(source_code, path, path_str, sizeof(path_str), filename);
        }
        char ns[SYMBOL_MAX_LENGTH];
        join_use_path(ns_prefix, path_str, ns, sizeof(ns));
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
        /* For scoped_use_list, fold the prefix `path` into the namespace and
         * recurse into the list so every leaf inherits it */
        TSNode list_node = node;
        char ns[SYMBOL_MAX_LENGTH];
        join_use_path(ns_prefix, NULL, ns, sizeof(ns));
        if (strcmp(t, "scoped_use_list") == 0) {
            TSNode path = ts_node_child_by_field_name(node, "path", 4);
            if (!ts_node_is_null(path)) {
                char path_str[SYMBOL_MAX_LENGTH];
                safe_extract_node_text(source_code, path, path_str, sizeof(path_str), filename);
                join_use_path(ns_prefix, path_str, ns, sizeof(ns));
            }
            TSNode list = ts_node_child_by_field_name(node, "list", 4);
            if (!ts_node_is_null(list)) list_node = list;
        }
        uint32_t n = ts_node_child_count(list_node);
        for (uint32_t i = 0; i < n; i++) {
            index_use_tree(ts_node_child(list_node, i), source_code,
                           directory, filename, result, filter, line, ns);
        }
        return;
    }

    /* Fallback: descend */
    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        index_use_tree(ts_node_child(node, i), source_code,
                       directory, filename, result, filter, line, ns_prefix);
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
    index_use_tree(arg, source_code, directory, filename, result, filter, line, NULL);
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
                              result, filter, line, type_str,
                              has_mut_specifier(node) ? "mut" : NULL);

    /* Visit value so calls/identifiers inside get indexed */
    if (!ts_node_is_null(value)) {
        /* Struct-literal fields get the let-bound variable as their
         * parent_symbol: let greeter = Greeter { label: ... } */
        if (strcmp(ts_node_type(value), "struct_expression") == 0 &&
            !ts_node_is_null(pattern) &&
            strcmp(ts_node_type(pattern), "identifier") == 0) {
            safe_extract_node_text(source_code, pattern, g_initializer_parent,
                                   sizeof(g_initializer_parent), filename);
        }
        visit_node(value, source_code, directory, filename, result, filter);
        g_initializer_parent[0] = '\0';
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
                              result, filter, line, NULL, NULL);
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
                              result, filter, line, NULL, NULL);
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
                                              result, filter, arm_line, NULL, NULL);
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
    if (!modifiers[0] && g_extern_abi[0]) {
        /* Declarations in an extern block inherit the block's ABI */
        snprintf(modifiers, sizeof(modifiers), "%s", g_extern_abi);
    }

    char attrs[SYMBOL_MAX_LENGTH];
    extract_attributes(node, source_code, attrs, sizeof(attrs), filename);

    char return_type[SYMBOL_MAX_LENGTH] = "";
    TSNode ret_node = ts_node_child_by_field_name(node, "return_type", 11);
    if (!ts_node_is_null(ret_node)) {
        safe_extract_node_text(source_code, ret_node, return_type, sizeof(return_type), filename);
    }

    char location[SOURCE_LOCATION_MAX_LENGTH];
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

    index_generic_constraints(node, source_code, directory, filename, result, filter);

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

    char location[SOURCE_LOCATION_MAX_LENGTH];
    format_source_location(node, location, sizeof(location));

    /* static mut COUNTER, plus the ABI when inside an extern block */
    char mods[SYMBOL_MAX_LENGTH + 8] = "";
    int is_mut = has_mut_specifier(node);
    if (g_extern_abi[0] && is_mut) {
        snprintf(mods, sizeof(mods), "%s mut", g_extern_abi);
    } else if (g_extern_abi[0]) {
        snprintf(mods, sizeof(mods), "%s", g_extern_abi);
    } else if (is_mut) {
        snprintf(mods, sizeof(mods), "mut");
    }

    /* Associated consts (direct members of a trait/impl declaration_list)
     * belong to the enclosing trait/impl; function-local consts do not. */
    const char *parent = NULL;
    TSNode enclosing = ts_node_parent(node);
    if (!ts_node_is_null(enclosing) && g_current_impl[0] &&
        strcmp(ts_node_type(enclosing), "declaration_list") == 0) {
        parent = g_current_impl;
    }

    if (filter_should_index(filter, name)) {
        add_entry(result, name, line, CONTEXT_VARIABLE,
                  directory, filename, location,
                  &(ExtColumns){
                      .definition = "1",
                      .scope = vis[0] ? vis : NULL,
                      .type = type_str[0] ? type_str : NULL,
                      .modifier = mods[0] ? mods : NULL,
                      .parent = parent
                  });
    }

    if (!ts_node_is_null(value)) {
        visit_node(value, source_code, directory, filename, result, filter);
    }
}

static void handle_extern_crate(TSNode node, const char *source_code,
                                const char *directory, const char *filename,
                                ParseResult *result, SymbolFilter *filter, int line) {
    TSNode name = ts_node_child_by_field_name(node, "name", 4);
    TSNode alias = ts_node_child_by_field_name(node, "alias", 5);

    char name_str[SYMBOL_MAX_LENGTH] = "";
    if (!ts_node_is_null(name)) {
        safe_extract_node_text(source_code, name, name_str, sizeof(name_str), filename);
    }

    if (!ts_node_is_null(alias)) {
        /* `extern crate foo as bar` — index the alias, like use_as_clause */
        char sym[SYMBOL_MAX_LENGTH];
        safe_extract_node_text(source_code, alias, sym, sizeof(sym), filename);
        if (sym[0] && filter_should_index(filter, sym)) {
            add_entry(result, sym, line, CONTEXT_IMPORT,
                      directory, filename, NULL,
                      &(ExtColumns){
                          .namespace = name_str[0] ? name_str : NULL,
                          .clue = "as"
                      });
        }
    } else if (name_str[0] && filter_should_index(filter, name_str)) {
        add_entry(result, name_str, line, CONTEXT_IMPORT,
                  directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
    }
}

static void handle_foreign_mod_item(TSNode node, const char *source_code,
                                    const char *directory, const char *filename,
                                    ParseResult *result, SymbolFilter *filter) {
    /* extern "C" { ... }: remember the ABI so the declarations inside get it
     * as their modifier. The extern_modifier is consumed here, not descended
     * into, so its ABI string never leaks into the index as a string literal. */
    char saved[SYMBOL_MAX_LENGTH];
    snprintf(saved, sizeof(saved), "%s", g_extern_abi);

    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        TSNode c = ts_node_child(node, i);
        if (strcmp(ts_node_type(c), "extern_modifier") == 0) {
            safe_extract_node_text(source_code, c, g_extern_abi,
                                   sizeof(g_extern_abi), filename);
            break;
        }
    }

    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) {
        process_children(body, source_code, directory, filename, result, filter);
    }
    snprintf(g_extern_abi, sizeof(g_extern_abi), "%s", saved);
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
                      .type = type_str[0] ? type_str : NULL,
                      .parent = g_current_impl[0] ? g_current_impl : NULL
                  });
    }

    /* Associated-type bounds: type Iter<'a>: Iterator<...> */
    index_generic_constraints(node, source_code, directory, filename, result, filter);
}

static void handle_macro_definition(TSNode node, const char *source_code,
                                    const char *directory, const char *filename,
                                    ParseResult *result, SymbolFilter *filter, int line) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name_node)) return;

    char name[SYMBOL_MAX_LENGTH];
    safe_extract_node_text(source_code, name_node, name, sizeof(name), filename);

    char location[SOURCE_LOCATION_MAX_LENGTH];
    format_source_location(node, location, sizeof(location));

    if (filter_should_index(filter, name)) {
        add_entry(result, name, line, CONTEXT_MACRO,
                  directory, filename, location,
                  &(ExtColumns){
                      .definition = "1",
                      .type = "macro_rules!"
                  });
    }
}

/* Macro bodies are unparsed token soup: tree-sitter wraps them in a
 * token_tree of raw tokens rather than expressions. Reconstruct the
 * common shapes by hand: "recv . field" => PROP with parent recv,
 * an identifier followed by a (...) token_tree => CALL, any other
 * identifier => ARG with the macro name as clue. Strings and nested
 * token_trees recurse. */
static void index_token_tree(TSNode tree, const char *source_code,
                             const char *directory, const char *filename,
                             ParseResult *result, SymbolFilter *filter,
                             const char *macro_name) {
    const char *clue = (macro_name && macro_name[0]) ? macro_name : NULL;
    uint32_t child_count = ts_node_child_count(tree);

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode tok = ts_node_child(tree, i);
        const char *tok_type = ts_node_type(tok);
        TSPoint tok_point = ts_node_start_point(tok);
        int tok_line = (int)(tok_point.row + 1);

        if (strcmp(tok_type, "string_literal") == 0 ||
            strcmp(tok_type, "raw_string_literal") == 0) {
            handle_string_literal(tok, source_code, directory, filename,
                                  result, filter, tok_line);
            continue;
        }
        if (strcmp(tok_type, "token_tree") == 0) {
            index_token_tree(tok, source_code, directory, filename,
                             result, filter, macro_name);
            continue;
        }
        if (strcmp(tok_type, "identifier") != 0) {
            continue;
        }

        /* Receivers and paths ("greeter" in "greeter . label", "String"
         * in "String :: from") are captured as parent/namespace of the
         * token they qualify, not indexed separately */
        if (i + 1 < child_count) {
            const char *next_type = ts_node_type(ts_node_child(tree, i + 1));
            if (strcmp(next_type, ".") == 0 || strcmp(next_type, "::") == 0) {
                continue;
            }
        }

        char name[SYMBOL_MAX_LENGTH];
        safe_extract_node_text(source_code, tok, name, sizeof(name), filename);
        if (name[0] == '\0' || !filter_should_index(filter, name)) {
            continue;
        }

        /* Lookbehind: "recv . name" or "path :: name" */
        char parent[SYMBOL_MAX_LENGTH] = "";
        char ns[SYMBOL_MAX_LENGTH] = "";
        if (i >= 2) {
            const char *sep_type = ts_node_type(ts_node_child(tree, i - 1));
            TSNode qual = ts_node_child(tree, i - 2);
            const char *qual_type = ts_node_type(qual);
            if (strcmp(qual_type, "identifier") == 0 || strcmp(qual_type, "self") == 0) {
                if (strcmp(sep_type, ".") == 0) {
                    safe_extract_node_text(source_code, qual, parent, sizeof(parent), filename);
                } else if (strcmp(sep_type, "::") == 0) {
                    safe_extract_node_text(source_code, qual, ns, sizeof(ns), filename);
                }
            }
        }

        /* Lookahead: a (...) token_tree right after means an invocation */
        int is_call = (i + 1 < child_count &&
                       strcmp(ts_node_type(ts_node_child(tree, i + 1)), "token_tree") == 0);

        if (is_call) {
            add_entry(result, name, tok_line, CONTEXT_CALL,
                      directory, filename, NULL,
                      &(ExtColumns){.parent = parent[0] ? parent : NULL,
                                    .namespace = ns[0] ? ns : NULL,
                                    .clue = clue});
        } else if (parent[0]) {
            add_entry(result, name, tok_line, CONTEXT_PROPERTY,
                      directory, filename, NULL,
                      &(ExtColumns){.parent = parent, .clue = clue});
        } else {
            add_entry(result, name, tok_line, CONTEXT_ARGUMENT,
                      directory, filename, NULL,
                      &(ExtColumns){.namespace = ns[0] ? ns : NULL,
                                    .clue = clue});
        }
    }
}

static void handle_macro_invocation(TSNode node, const char *source_code,
                                    const char *directory, const char *filename,
                                    ParseResult *result, SymbolFilter *filter, int line) {
    char name[SYMBOL_MAX_LENGTH] = "";
    TSNode macro = ts_node_child_by_field_name(node, "macro", 5);
    if (!ts_node_is_null(macro)) {
        safe_extract_node_text(source_code, macro, name, sizeof(name), filename);
        if (name[0] && filter_should_index(filter, name)) {
            add_entry(result, name, line, CONTEXT_CALL,
                      directory, filename, NULL,
                      &(ExtColumns){.type = "macro"});
        }
    }
    /* The macro payload is an unnamed token_tree child (println!(...)),
     * not an "arguments" field: walk its tokens */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (strcmp(ts_node_type(child), "token_tree") == 0) {
            index_token_tree(child, source_code, directory, filename,
                             result, filter, name);
        }
    }
}

/* Derive the nearest single-symbol parent name from a method/field receiver:
 * identifier/self -> its text; a.b -> "b"; a.b() / a.b::<T>() -> "b";
 * path::f() -> "f". Returns 1 if a name was written to out. */
static int extract_receiver_parent(TSNode value, const char *source_code,
                                   const char *filename, char *out, size_t out_size) {
    out[0] = '\0';
    if (ts_node_is_null(value)) return 0;
    const char *vt = ts_node_type(value);
    if (strcmp(vt, "identifier") == 0 || strcmp(vt, "self") == 0) {
        safe_extract_node_text(source_code, value, out, out_size, filename);
    } else if (strcmp(vt, "field_expression") == 0) {
        TSNode inner = ts_node_child_by_field_name(value, "field", 5);
        if (!ts_node_is_null(inner)) {
            safe_extract_node_text(source_code, inner, out, out_size, filename);
        }
    } else if (strcmp(vt, "call_expression") == 0) {
        TSNode fn = ts_node_child_by_field_name(value, "function", 8);
        if (!ts_node_is_null(fn) && strcmp(ts_node_type(fn), "generic_function") == 0) {
            TSNode inner = ts_node_child_by_field_name(fn, "function", 8);
            if (!ts_node_is_null(inner)) fn = inner;
        }
        if (!ts_node_is_null(fn)) {
            const char *ft = ts_node_type(fn);
            if (strcmp(ft, "identifier") == 0) {
                safe_extract_node_text(source_code, fn, out, out_size, filename);
            } else if (strcmp(ft, "field_expression") == 0) {
                TSNode f = ts_node_child_by_field_name(fn, "field", 5);
                if (!ts_node_is_null(f)) {
                    safe_extract_node_text(source_code, f, out, out_size, filename);
                }
            } else if (strcmp(ft, "scoped_identifier") == 0) {
                TSNode n = ts_node_child_by_field_name(fn, "name", 4);
                if (!ts_node_is_null(n)) {
                    safe_extract_node_text(source_code, n, out, out_size, filename);
                }
            }
        }
    }
    return out[0] != '\0';
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
        /* Turbofish (foo::<T>(), obj.method::<T>()): the callee is wrapped in
         * a generic_function node; unwrap to the real identifier/field/path. */
        if (strcmp(ft, "generic_function") == 0) {
            TSNode inner = ts_node_child_by_field_name(fn, "function", 8);
            if (!ts_node_is_null(inner)) {
                fn = inner;
                ft = ts_node_type(fn);
            }
        }
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
            char par[SYMBOL_MAX_LENGTH];
            extract_receiver_parent(value, source_code, filename, par, sizeof(par));
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
    char location[SOURCE_LOCATION_MAX_LENGTH];
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
                            /* Skip punctuation-only fragments, e.g. ":?" left
                             * over from format specs like "{:?}" */
                            int has_alnum = 0;
                            for (const char *q = cleaned; *q; q++) {
                                if (isalnum((unsigned char)*q)) { has_alnum = 1; break; }
                            }
                            if (has_alnum && filter_should_index(filter, cleaned)) {
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

/* Field read: greeter.label / self.label / a.b.c. Method calls
 * (greeter.greet()) never reach here -- handle_call_expression consumes
 * the function child itself. */
static void handle_field_expression(TSNode node, const char *source_code,
                                    const char *directory, const char *filename,
                                    ParseResult *result, SymbolFilter *filter) {
    TSNode value = ts_node_child_by_field_name(node, "value", 5);
    TSNode field = ts_node_child_by_field_name(node, "field", 5);

    char parent[SYMBOL_MAX_LENGTH];
    extract_receiver_parent(value, source_code, filename, parent, sizeof(parent));
    int visit_value = 0;
    if (!ts_node_is_null(value)) {
        const char *value_type = ts_node_type(value);
        if (strcmp(value_type, "identifier") != 0 && strcmp(value_type, "self") != 0) {
            visit_value = 1;
        }
    }

    if (!ts_node_is_null(field)) {
        char name[SYMBOL_MAX_LENGTH];
        safe_extract_node_text(source_code, field, name, sizeof(name), filename);
        if (name[0] && filter_should_index(filter, name)) {
            TSPoint field_point = ts_node_start_point(field);
            add_entry(result, name, (int)(field_point.row + 1), CONTEXT_PROPERTY,
                      directory, filename, NULL,
                      &(ExtColumns){.parent = parent[0] ? parent : NULL});
        }
    }

    if (visit_value) {
        visit_node(value, source_code, directory, filename, result, filter);
    }
}

/* Struct literal: Greeter { label: value, ..base }. Fields get
 * g_initializer_parent (the let-bound variable) as parent_symbol,
 * mirroring the C designated-initializer convention; nested literals
 * get the enclosing field name. */
static void handle_struct_expression(TSNode node, const char *source_code,
                                     const char *directory, const char *filename,
                                     ParseResult *result, SymbolFilter *filter) {
    /* The literal names its type: index it as a usage, and keep the name
     * around as the fields' fallback parent */
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    char type_name[SYMBOL_MAX_LENGTH] = "";
    if (!ts_node_is_null(name_node)) {
        extract_base_type_name(name_node, source_code, type_name, sizeof(type_name), filename);
        if (type_name[0] && filter_should_index(filter, type_name)) {
            char ns[SYMBOL_MAX_LENGTH] = "";
            const char *nt = ts_node_type(name_node);
            if (strcmp(nt, "scoped_type_identifier") == 0 ||
                strcmp(nt, "scoped_identifier") == 0) {
                TSNode path = ts_node_child_by_field_name(name_node, "path", 4);
                if (!ts_node_is_null(path)) {
                    safe_extract_node_text(source_code, path, ns, sizeof(ns), filename);
                }
            }
            TSPoint np = ts_node_start_point(name_node);
            add_entry(result, type_name, (int)(np.row + 1), CONTEXT_CLASS,
                      directory, filename, NULL,
                      &(ExtColumns){.namespace = ns[0] ? ns : NULL});
        }
    }

    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (ts_node_is_null(body)) {
        uint32_t n = ts_node_child_count(node);
        for (uint32_t i = 0; i < n; i++) {
            TSNode child = ts_node_child(node, i);
            if (strcmp(ts_node_type(child), "field_initializer_list") == 0) {
                body = child;
                break;
            }
        }
    }
    if (ts_node_is_null(body)) return;

    /* Fields' parent: the let-bound variable or enclosing field if one is
     * active (matches the C indexer's designated-initializer handling);
     * otherwise the literal's own type name, which Rust syntax always
     * provides: Person { name, age } */
    char outer_parent[SYMBOL_MAX_LENGTH];
    copy_symbol(outer_parent, sizeof(outer_parent), g_initializer_parent);
    if (!g_initializer_parent[0] && type_name[0]) {
        copy_symbol(g_initializer_parent, sizeof(g_initializer_parent), type_name);
    }

    uint32_t child_count = ts_node_child_count(body);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(body, i);
        const char *child_type = ts_node_type(child);

        if (strcmp(child_type, "field_initializer") == 0) {
            char field_name[SYMBOL_MAX_LENGTH] = "";
            uint32_t part_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < part_count; j++) {
                TSNode part = ts_node_child(child, j);
                if (strcmp(ts_node_type(part), "field_identifier") == 0) {
                    safe_extract_node_text(source_code, part, field_name, sizeof(field_name), filename);
                    if (field_name[0] && filter_should_index(filter, field_name)) {
                        TSPoint fp = ts_node_start_point(part);
                        add_entry(result, field_name, (int)(fp.row + 1), CONTEXT_PROPERTY,
                                  directory, filename, NULL,
                                  &(ExtColumns){.parent = g_initializer_parent[0]
                                                    ? g_initializer_parent : NULL});
                    }
                } else if (ts_node_is_named(part)) {
                    /* The value expression: nested literals belong to this
                     * field, so it becomes the parent while descending */
                    char saved_parent[SYMBOL_MAX_LENGTH];
                    copy_symbol(saved_parent, sizeof(saved_parent), g_initializer_parent);
                    if (field_name[0]) {
                        copy_symbol(g_initializer_parent,
                                    sizeof(g_initializer_parent), field_name);
                    }
                    visit_node(part, source_code, directory, filename, result, filter);
                    copy_symbol(g_initializer_parent,
                                sizeof(g_initializer_parent), saved_parent);
                }
            }
        } else if (strcmp(child_type, "shorthand_field_initializer") == 0) {
            /* Greeter { label }: the identifier is both field and value */
            uint32_t part_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < part_count; j++) {
                TSNode part = ts_node_child(child, j);
                if (strcmp(ts_node_type(part), "identifier") == 0) {
                    char field_name[SYMBOL_MAX_LENGTH];
                    safe_extract_node_text(source_code, part, field_name, sizeof(field_name), filename);
                    if (field_name[0] && filter_should_index(filter, field_name)) {
                        TSPoint fp = ts_node_start_point(part);
                        add_entry(result, field_name, (int)(fp.row + 1), CONTEXT_PROPERTY,
                                  directory, filename, NULL,
                                  &(ExtColumns){.parent = g_initializer_parent[0]
                                                    ? g_initializer_parent : NULL});
                    }
                }
            }
        } else if (strcmp(child_type, "base_field_initializer") == 0) {
            /* ..base: visit the base expression */
            process_children(child, source_code, directory, filename, result, filter);
        }
    }

    copy_symbol(g_initializer_parent, sizeof(g_initializer_parent), outer_parent);
}

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
    if (strcmp(t, "extern_crate_declaration") == 0) {
        handle_extern_crate(node, source_code, directory, filename, result, filter, line);
        return;
    }
    if (strcmp(t, "foreign_mod_item") == 0) {
        handle_foreign_mod_item(node, source_code, directory, filename, result, filter);
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
    if (strcmp(t, "type_item") == 0 || strcmp(t, "associated_type") == 0) {
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
    if (strcmp(t, "field_expression") == 0) {
        handle_field_expression(node, source_code, directory, filename, result, filter);
        return;
    }
    if (strcmp(t, "struct_expression") == 0) {
        handle_struct_expression(node, source_code, directory, filename, result, filter);
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
    /* Attribute names are harvested into clues via extract_attributes;
     * derive lists additionally yield trait usages. Inner attributes
     * (#![...]) cannot carry derives. */
    if (strcmp(t, "attribute_item") == 0) {
        handle_attribute_item(node, source_code, directory, filename, result, filter);
        return;
    }
    if (strcmp(t, "inner_attribute_item") == 0) {
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
