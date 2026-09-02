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
#include "ruby_language.h"
#include "../shared/constants.h"
#include "../shared/string_utils.h"
#include "../shared/comment_utils.h"
#include "../shared/file_opener.h"
#include "../shared/file_utils.h"
#include "../shared/filter.h"
#include "../shared/debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <tree_sitter/api.h>

/* External Ruby language function from tree-sitter-ruby */
extern const TSLanguage *tree_sitter_ruby(void);

/* Global debug flag */
static int g_debug = 0;

/* Parent symbol for the hash literal currently being visited; gives hash keys
 * ({ name: value }) their parent_symbol — the assigned variable, or the
 * enclosing key for a nested hash. Set by handle_assignment, swapped by
 * handle_pair. Mirrors the C reference's g_initializer_parent. */
static char g_hash_parent[SYMBOL_MAX_LENGTH] = "";

/* Symbol table for fast node-type comparisons (uint16_t equality, no strcmp). */
static struct {
    /* Dispatched node types */
    TSSymbol method;
    TSSymbol singleton_method;
    TSSymbol class_;
    TSSymbol singleton_class;
    TSSymbol module;
    TSSymbol assignment;
    TSSymbol operator_assignment;
    TSSymbol call;
    TSSymbol lambda;
    TSSymbol do_block;
    TSSymbol block;
    TSSymbol string;
    TSSymbol comment;
    TSSymbol heredoc_body;
    TSSymbol pair;

    /* Structural node types checked inside handlers */
    TSSymbol identifier;
    TSSymbol constant;
    TSSymbol instance_variable;
    TSSymbol class_variable;
    TSSymbol global_variable;
    TSSymbol scope_resolution;
    TSSymbol simple_symbol;
    TSSymbol hash_key_symbol;
    TSSymbol hash;
    TSSymbol operator_;
    TSSymbol optional_parameter;
    TSSymbol keyword_parameter;
    TSSymbol splat_parameter;
    TSSymbol hash_splat_parameter;
    TSSymbol block_parameter;
    TSSymbol left_assignment_list;
    TSSymbol string_content;
    TSSymbol heredoc_content;
    TSSymbol interpolation;

    /* case/in pattern matching (bindings are definitions; pins/literals are reads) */
    TSSymbol in_clause;
    TSSymbol match_pattern;
    TSSymbol test_pattern;
    TSSymbol array_pattern;
    TSSymbol find_pattern;
    TSSymbol hash_pattern;
    TSSymbol keyword_pattern;
    TSSymbol as_pattern;
    TSSymbol variable_reference_pattern;
} ruby_symbols;

/* Forward declarations */
static void visit_node(TSNode node, const char *source_code, const char *directory,
                       const char *filename, ParseResult *result, SymbolFilter *filter,
                       const char *parent, const char *ns);
static void process_children(TSNode node, const char *source_code, const char *directory,
                             const char *filename, ParseResult *result, SymbolFilter *filter,
                             const char *parent, const char *ns);

static int node_line(TSNode node) {
    return (int)(ts_node_start_point(node).row + 1);
}

/* Ruby sigils (@ivar, @@cvar, $global) are noise for search; users query the
 * bare name. Return a pointer past any leading sigil characters. */
static const char *strip_sigils(const char *name) {
    while (*name == '@' || *name == '$') name++;
    return name;
}

/* Extract a node's source text into buffer (bounded). */
static void node_text(TSNode node, const char *source_code, const char *filename,
                      char *buf, size_t size) {
    safe_extract_node_text(source_code, node, buf, size, filename);
}

/* Bounded copy between symbol buffers. strncat-based copies of a
 * possibly-full equal-sized source buffer trip -Wstringop-truncation. */
static void copy_symbol(char *dst, size_t dst_size, const char *src) {
    size_t len = strnlength(src, dst_size - 1);
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* Build the child namespace path "ns::name" (or just "name" at top level).
 * Built via copy_symbol (bounded) rather than a two-%s snprintf, which the
 * compiler cannot prove fits (-Wformat-truncation); falls back to the bare name
 * if the joined path would not fit rather than truncating mid-name. */
static void child_ns(const char *ns, const char *name, char *out, size_t size) {
    if (ns && ns[0]) {
        size_t nl = strnlength(ns, size);
        if (nl + 2 < size) {
            copy_symbol(out, size, ns);
            out[nl] = ':';
            out[nl + 1] = ':';
            copy_symbol(out + nl + 2, size - nl - 2, name);
            return;
        }
    }
    copy_symbol(out, size, name);
}

/* Split text on whitespace and index each cleaned, indexable word. Shared by the
 * string, comment, and heredoc handlers. */
static void index_words(const char *text, int line, ContextType context,
                        const char *directory, const char *filename,
                        ParseResult *result, SymbolFilter *filter) {
    char word[CLEANED_WORD_BUFFER];
    char cleaned[CLEANED_WORD_BUFFER];
    const char *word_start = text;

    for (const char *p = text; ; p++) {
        if (*p == '\0' || isspace((unsigned char)*p)) {
            if (p > word_start) {
                size_t word_len = (size_t)(p - word_start);
                if (word_len < sizeof(word)) {
                    snprintf(word, sizeof(word), "%.*s", (int)word_len, word_start);
                    filter_clean_string_symbol(word, cleaned, sizeof(cleaned));
                    if (cleaned[0] && filter_should_index(filter, cleaned)) {
                        add_entry(result, cleaned, line, context,
                                  directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
                    }
                }
            }
            word_start = p + 1;
            if (*p == '\0') break;
        }
    }
}

/* Index one parameter identifier (owner = enclosing method/block name). */
static void index_param(TSNode name_node, const char *source_code, const char *directory,
                        const char *filename, ParseResult *result, SymbolFilter *filter,
                        const char *owner) {
    char name[SYMBOL_MAX_LENGTH];
    node_text(name_node, source_code, filename, name, sizeof(name));
    const char *sym = strip_sigils(name);
    if (sym[0] && filter_should_index(filter, sym)) {
        ExtColumns ext = { .parent = owner };
        add_entry(result, sym, node_line(name_node), CONTEXT_ARGUMENT,
                  directory, filename, NULL, &ext);
    }
}

/* Extract parameters from a method_parameters / block_parameters /
 * lambda_parameters node. Handles the full Ruby parameter zoo: plain, optional
 * (default), keyword, *splat, **double-splat, and &block. Default/keyword values
 * are visited so nested calls in them are still indexed. */
static void extract_parameters(TSNode params, const char *source_code, const char *directory,
                               const char *filename, ParseResult *result, SymbolFilter *filter,
                               const char *owner, const char *parent, const char *ns) {
    uint32_t n = ts_node_child_count(params);
    for (uint32_t i = 0; i < n; i++) {
        TSNode child = ts_node_child(params, i);
        TSSymbol sym = ts_node_symbol(child);

        if (sym == ruby_symbols.identifier) {
            index_param(child, source_code, directory, filename, result, filter, owner);
        } else if (sym == ruby_symbols.optional_parameter ||
                   sym == ruby_symbols.keyword_parameter) {
            TSNode name = ts_node_child_by_field_name(child, "name", 4);
            if (!ts_node_is_null(name)) {
                index_param(name, source_code, directory, filename, result, filter, owner);
            }
            TSNode value = ts_node_child_by_field_name(child, "value", 5);
            if (!ts_node_is_null(value)) {
                visit_node(value, source_code, directory, filename, result, filter, parent, ns);
            }
        } else if (sym == ruby_symbols.splat_parameter ||
                   sym == ruby_symbols.hash_splat_parameter ||
                   sym == ruby_symbols.block_parameter) {
            TSNode name = ts_node_child_by_field_name(child, "name", 4);
            if (!ts_node_is_null(name)) {
                index_param(name, source_code, directory, filename, result, filter, owner);
            }
        }
    }
}

/* method / singleton_method: instance methods, class methods (def self.x),
 * and per-object singleton methods (def obj.x). */
static void handle_method(TSNode node, const char *source_code, const char *directory,
                          const char *filename, ParseResult *result, SymbolFilter *filter,
                          int line, const char *parent, const char *ns) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name_node)) {
        process_children(node, source_code, directory, filename, result, filter, parent, ns);
        return;
    }

    char name[SYMBOL_MAX_LENGTH];
    node_text(name_node, source_code, filename, name, sizeof(name));

    /* def self.foo / def Klass.foo → a class-level method. Record "self" (or the
     * receiver) as a modifier so `-m self` finds class methods. */
    const char *modifier = NULL;
    char object_buf[SYMBOL_MAX_LENGTH];
    TSNode object = ts_node_child_by_field_name(node, "object", 6);
    if (!ts_node_is_null(object)) {
        node_text(object, source_code, filename, object_buf, sizeof(object_buf));
        modifier = object_buf;
    }

    if (name[0] && filter_should_index(filter, name)) {
        char location[SYMBOL_MAX_LENGTH];
        format_source_location(node, location, sizeof(location));
        ExtColumns ext = {
            .parent = parent,
            .namespace = (ns && ns[0]) ? ns : NULL,
            .modifier = modifier,
            .definition = "1"
        };
        add_entry(result, name, line, CONTEXT_FUNCTION,
                  directory, filename, location, &ext);
    }

    /* Parameters belong to this method; the body keeps the SAME enclosing scope
     * (so ivar/constant assignments inside resolve to the enclosing class). */
    TSNode params = ts_node_child_by_field_name(node, "parameters", 10);
    if (!ts_node_is_null(params)) {
        extract_parameters(params, source_code, directory, filename, result, filter,
                           name, parent, ns);
    }

    /* Visit the body node rather than only recursing into its children. For a
     * normal method the body is a `body_statement` container (no handler), so
     * visit_node falls through to process_children — identical to before. For an
     * endless method (`def name(args) = expr`) the body field IS the expression
     * itself (a `call`, `assignment`, …); visiting it dispatches to
     * handle_call/handle_assignment instead of generically walking the
     * expression's children, which had dropped the call receiver/parent
     * (`Money.new` → bare VAR `new`) and ivar-assignment PROPs. */
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) {
        visit_node(body, source_code, directory, filename, result, filter, parent, ns);
    }
}

/* A `Scope::Name` constant, split into parts. */
typedef struct {
    TSNode name;              /* the terminal constant */
    TSNode scope;             /* null if absent */
    const char *scope_text;   /* points into the caller's buffer; NULL if unusable */
} ScopeRef;

/* Split `Scope::Name` so every site records it alike: terminal is the symbol,
 * scope is the parent. The scope stays as written -- Ruby resolves constants at
 * run time, so the real path is unknowable here. False if there is no name. */
static bool split_scope_ref(TSNode node, const char *source_code, const char *filename,
                            char *scope_buf, size_t scope_len, ScopeRef *out) {
    TSNode name = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name)) return false;

    out->name = name;
    out->scope = ts_node_child_by_field_name(node, "scope", 5);
    out->scope_text = NULL;
    if (!ts_node_is_null(out->scope)) {
        node_text(out->scope, source_code, filename, scope_buf, scope_len);
        /* Newlines would break a single-line column. */
        if (scope_buf[0] && !strchr(scope_buf, '\n')) out->scope_text = scope_buf;
    }
    return true;
}

/* class Foo < Bar ... end */
static void handle_class(TSNode node, const char *source_code, const char *directory,
                         const char *filename, ParseResult *result, SymbolFilter *filter,
                         int line, const char *parent, const char *ns) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name_node)) {
        process_children(node, source_code, directory, filename, result, filter, parent, ns);
        return;
    }

    /* `class Scope::Name` names the same class as `module Scope; class Name`, so
     * record it the same way. Ruby reads the scope to find where to define the
     * class -- `class Missing::Thing` raises NameError -- so index it as a read. */
    char class_scope_buf[SYMBOL_MAX_LENGTH];
    const char *class_scope = NULL;
    if (ts_node_symbol(name_node) == ruby_symbols.scope_resolution) {
        ScopeRef sr;
        if (split_scope_ref(name_node, source_code, filename, class_scope_buf,
                            sizeof(class_scope_buf), &sr)) {
            name_node = sr.name;
            class_scope = sr.scope_text;
            if (!ts_node_is_null(sr.scope))
                visit_node(sr.scope, source_code, directory, filename, result, filter, parent, ns);
        }
    }

    char name[SYMBOL_MAX_LENGTH];
    node_text(name_node, source_code, filename, name, sizeof(name));

    /* Superclass (if any) is recorded as the type, e.g. `qi Dog -i class -t Animal`. */
    const char *type = NULL;
    char super_buf[SYMBOL_MAX_LENGTH];
    TSNode super = ts_node_child_by_field_name(node, "superclass", 10);
    if (!ts_node_is_null(super)) {
        /* superclass node is "< Constant"; index the constant text only. */
        char raw[SYMBOL_MAX_LENGTH];
        node_text(super, source_code, filename, raw, sizeof(raw));
        const char *p = raw;
        while (*p == '<' || isspace((unsigned char)*p)) p++;
        snprintf(super_buf, sizeof(super_buf), "%s", p);
        type = super_buf;

        /* Record the superclass reference. When it names a constant (plain, or the
         * terminal of a Scope::Name), it is a CLASS usage; a dynamic superclass
         * (Struct.new(...), a variable) is visited as its natural CALL/VAR form. */
        TSNode super_expr = ts_node_named_child(super, 0);
        if (!ts_node_is_null(super_expr)) {
            TSSymbol s = ts_node_symbol(super_expr);
            bool is_const = (s == ruby_symbols.constant);
            TSNode super_name = super_expr;
            /* For a qualified Scope::Name, the scope is the terminal's parent, and is
             * itself recorded as a read — matching handle_scope_resolution. */
            const char *super_parent = NULL;
            char scope_buf[SYMBOL_MAX_LENGTH];
            if (s == ruby_symbols.scope_resolution) {
                ScopeRef sr;
                is_const = split_scope_ref(super_expr, source_code, filename,
                                           scope_buf, sizeof(scope_buf), &sr);
                if (is_const) {
                    super_name = sr.name;
                    super_parent = sr.scope_text;
                    if (!ts_node_is_null(sr.scope))
                        visit_node(sr.scope, source_code, directory, filename, result, filter, parent, ns);
                }
            }
            if (is_const) {
                char cbuf[SYMBOL_MAX_LENGTH];
                node_text(super_name, source_code, filename, cbuf, sizeof(cbuf));
                if (cbuf[0] && filter_should_index(filter, cbuf)) {
                    /* Record the namespace this reference sits in, as every other
                     * entry does, so -ns filters don't skip superclass references. */
                    ExtColumns ext = {
                        .parent = super_parent,
                        .namespace = (ns && ns[0]) ? ns : NULL
                    };
                    add_entry(result, cbuf, node_line(super_name), CONTEXT_CLASS,
                              directory, filename, NULL, &ext);
                }
            } else {
                visit_node(super_expr, source_code, directory, filename, result, filter, parent, ns);
            }
        }
    }

    if (name[0] && filter_should_index(filter, name)) {
        char location[SYMBOL_MAX_LENGTH];
        format_source_location(node, location, sizeof(location));
        /* A written-out scope wins over the enclosing one: it says where the class
         * lives, while the enclosing namespace is only where the text sits. */
        ExtColumns ext = {
            .parent = class_scope ? class_scope : parent,
            .namespace = class_scope ? class_scope : ((ns && ns[0]) ? ns : NULL),
            .type = type,
            .definition = "1"
        };
        add_entry(result, name, line, CONTEXT_CLASS,
                  directory, filename, location, &ext);
    }

    char scope[SYMBOL_MAX_LENGTH];
    child_ns(class_scope ? class_scope : ns, name, scope, sizeof(scope));
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) {
        process_children(body, source_code, directory, filename, result, filter, name, scope);
    }
}

/* module Foo ... end — a namespace (or mixin). */
static void handle_module(TSNode node, const char *source_code, const char *directory,
                          const char *filename, ParseResult *result, SymbolFilter *filter,
                          int line, const char *parent, const char *ns) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name_node)) {
        process_children(node, source_code, directory, filename, result, filter, parent, ns);
        return;
    }

    char name[SYMBOL_MAX_LENGTH];
    node_text(name_node, source_code, filename, name, sizeof(name));

    if (name[0] && filter_should_index(filter, name)) {
        char location[SYMBOL_MAX_LENGTH];
        format_source_location(node, location, sizeof(location));
        ExtColumns ext = {
            .parent = parent,
            .namespace = (ns && ns[0]) ? ns : NULL,
            .definition = "1"
        };
        add_entry(result, name, line, CONTEXT_NAMESPACE,
                  directory, filename, location, &ext);
    }

    char scope[SYMBOL_MAX_LENGTH];
    child_ns(ns, name, scope, sizeof(scope));
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) {
        process_children(body, source_code, directory, filename, result, filter, name, scope);
    }
}

/* class << self ... end — reopen the singleton; just recurse the body. */
static void handle_singleton_class(TSNode node, const char *source_code, const char *directory,
                                   const char *filename, ParseResult *result, SymbolFilter *filter,
                                   int line, const char *parent, const char *ns) {
    (void)line;
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) {
        process_children(body, source_code, directory, filename, result, filter, parent, ns);
    } else {
        process_children(node, source_code, directory, filename, result, filter, parent, ns);
    }
}

/* Map an lvalue node to a context type; return false if it isn't a simple,
 * indexable target (e.g. element_reference `a[i] =`, or a setter `o.x =`). */
static bool classify_lvalue(TSSymbol sym, ContextType *ctx_out) {
    if (sym == ruby_symbols.instance_variable || sym == ruby_symbols.class_variable) {
        *ctx_out = CONTEXT_PROPERTY;   /* @ivar / @@cvar → object/class state */
        return true;
    }
    if (sym == ruby_symbols.constant) { *ctx_out = CONTEXT_VARIABLE; return true; }
    if (sym == ruby_symbols.global_variable) { *ctx_out = CONTEXT_VARIABLE; return true; }
    if (sym == ruby_symbols.identifier) { *ctx_out = CONTEXT_VARIABLE; return true; }
    return false;
}

/* Fill *out with the searchable (sigil-stripped) name of a simple, indexable
 * lvalue target, or return false if it isn't one (setter `o.x =`, element
 * reference `a[i] =`). Shared by index_lvalue and the hash-parent setup in
 * handle_assignment. */
static bool simple_lvalue_name(TSNode target, const char *source_code, const char *filename,
                               ContextType *ctx_out, char *out, size_t size) {
    if (!classify_lvalue(ts_node_symbol(target), ctx_out)) return false;
    char raw[SYMBOL_MAX_LENGTH];
    node_text(target, source_code, filename, raw, sizeof(raw));
    const char *sym = strip_sigils(raw);
    if (!sym[0]) return false;
    copy_symbol(out, size, sym);
    return true;
}

static void index_lvalue(TSNode target, const char *source_code, const char *directory,
                         const char *filename, ParseResult *result, SymbolFilter *filter,
                         const char *parent, bool defining) {
    ContextType context;
    char sym[SYMBOL_MAX_LENGTH];
    if (!simple_lvalue_name(target, source_code, filename, &context, sym, sizeof(sym))) return;
    if (!filter_should_index(filter, sym)) return;

    /* Ruby has no separate declaration, so a plain `=` to a local, constant, or
     * global (all CONTEXT_VARIABLE) is that binding's definition. Compound
     * assignments (+=, ||=) mutate an existing binding, and @ivar/@@cvar writes
     * mirror C field assignments (`p->x = ...`). This matches
     * the C reference: declaration D=1, mutation/read D=0. */
    const char *definition = (defining && context == CONTEXT_VARIABLE) ? "1" : NULL;
    ExtColumns ext = { .parent = parent, .definition = definition };
    add_entry(result, sym, node_line(target), context,
              directory, filename, NULL, &ext);
}

/* assignment / operator_assignment. Index the left-hand target(s), then visit the
 * right-hand side so calls/blocks/lambdas in it are indexed too. */
static void handle_assignment(TSNode node, const char *source_code, const char *directory,
                              const char *filename, ParseResult *result, SymbolFilter *filter,
                              int line, const char *parent, const char *ns) {
    (void)line;
    /* A plain `assignment` (=) initializes the target; `operator_assignment`
     * (+=, ||=, …) only mutates an existing binding, so it is never a definition. */
    bool defining = (ts_node_symbol(node) == ruby_symbols.assignment);

    /* A single, simple target becomes the parent of a hash literal directly on
     * the right, mirroring how the C reference gives designated-initializer keys
     * the declared variable as parent ({.name = ...} → parent config). Complex or
     * multiple targets (o.x =, a[i] =, a, b =) are skipped. */
    char lvalue[SYMBOL_MAX_LENGTH] = "";
    bool has_lvalue = false;
    TSNode left = ts_node_child_by_field_name(node, "left", 4);
    if (!ts_node_is_null(left)) {
        if (ts_node_symbol(left) == ruby_symbols.left_assignment_list) {
            uint32_t n = ts_node_child_count(left);
            for (uint32_t i = 0; i < n; i++) {
                index_lvalue(ts_node_child(left, i), source_code, directory, filename,
                             result, filter, parent, defining);
            }
        } else {
            index_lvalue(left, source_code, directory, filename, result, filter, parent, defining);
            ContextType ctx;
            has_lvalue = simple_lvalue_name(left, source_code, filename, &ctx, lvalue, sizeof(lvalue));
        }
    }

    TSNode right = ts_node_child_by_field_name(node, "right", 5);
    if (ts_node_is_null(right)) return;

    /* The hash may be the direct RHS ({ ... }) or the receiver of a method call
     * on it ({ ... }.freeze — the common constant idiom). A hash nested
     * elsewhere on the right (call arg, etc.) has no declared owner and stays
     * parentless. */
    bool rhs_hash = (ts_node_symbol(right) == ruby_symbols.hash);
    if (!rhs_hash && ts_node_symbol(right) == ruby_symbols.call) {
        TSNode recv = ts_node_child_by_field_name(right, "receiver", 8);
        rhs_hash = !ts_node_is_null(recv) && ts_node_symbol(recv) == ruby_symbols.hash;
    }

    if (has_lvalue && rhs_hash) {
        char saved[SYMBOL_MAX_LENGTH];
        copy_symbol(saved, sizeof(saved), g_hash_parent);
        copy_symbol(g_hash_parent, sizeof(g_hash_parent), lvalue);
        visit_node(right, source_code, directory, filename, result, filter, parent, ns);
        copy_symbol(g_hash_parent, sizeof(g_hash_parent), saved);
    } else {
        visit_node(right, source_code, directory, filename, result, filter, parent, ns);
    }
}


/* Resolve the searchable name from a symbol/string argument to a declarative
 * call (attr_*, define_method). A plain literal yields its text; an interpolated
 * string such as "#{field}=" yields the interpolated expression ("field").
 * *dynamic is set true for the interpolated case: the real name is only known at
 * runtime, so the caller records the extracted name as a reference rather than a
 * definition. Returns false when there is nothing indexable. */
static bool symbol_arg_name(TSNode arg, const char *source_code, const char *filename,
                            char *out, size_t size, bool *dynamic) {
    TSSymbol sym = ts_node_symbol(arg);
    *dynamic = false;

    if (sym == ruby_symbols.simple_symbol) {
        char raw[SYMBOL_MAX_LENGTH];
        node_text(arg, source_code, filename, raw, sizeof(raw));
        copy_symbol(out, size, raw[0] == ':' ? raw + 1 : raw);  /* :name -> name */
        return out[0] != '\0';
    }
    if (sym != ruby_symbols.string) return false;

    uint32_t n = ts_node_child_count(arg);

    /* Interpolated: take the first interpolation's inner expression, dropping the
     * surrounding "#{" and "}". */
    for (uint32_t i = 0; i < n; i++) {
        TSNode child = ts_node_child(arg, i);
        if (ts_node_symbol(child) == ruby_symbols.interpolation) {
            char raw[SYMBOL_MAX_LENGTH];
            node_text(child, source_code, filename, raw, sizeof(raw));  /* "#{field}" */
            char *p = raw;
            if (p[0] == '#' && p[1] == '{') p += 2;
            size_t l = strnlength(p, sizeof(raw));
            if (l && p[l - 1] == '}') p[l - 1] = '\0';
            copy_symbol(out, size, p);
            *dynamic = true;
            return out[0] != '\0';
        }
    }

    /* Plain literal: the string_content child is the text without the quotes. */
    for (uint32_t i = 0; i < n; i++) {
        TSNode child = ts_node_child(arg, i);
        if (ts_node_symbol(child) == ruby_symbols.string_content) {
            char raw[SYMBOL_MAX_LENGTH];
            node_text(child, source_code, filename, raw, sizeof(raw));
            copy_symbol(out, size, raw);
            return out[0] != '\0';
        }
    }
    return false;  /* empty string literal */
}

/* Index each symbol/string argument of a call as `context`, owned by `parent`.
 * Used for attr_accessor/reader/writer (properties) and define_method (methods).
 * A literal argument is a real definition; a name pulled from an interpolation
 * (dynamic) is only a reference to the interpolated variable, so it is recorded
 * as a VARIABLE usage rather than a definition of `context`. */
static void index_symbol_args(TSNode call, const char *source_code, const char *directory,
                              const char *filename, ParseResult *result, SymbolFilter *filter,
                              const char *parent, ContextType context, const char *clue) {
    TSNode args = ts_node_child_by_field_name(call, "arguments", 9);
    if (ts_node_is_null(args)) return;
    uint32_t n = ts_node_child_count(args);
    for (uint32_t i = 0; i < n; i++) {
        TSNode arg = ts_node_child(args, i);
        char name[SYMBOL_MAX_LENGTH];
        bool dynamic = false;
        if (!symbol_arg_name(arg, source_code, filename, name, sizeof(name), &dynamic)) continue;
        if (!filter_should_index(filter, name)) continue;

        if (dynamic) {
            ExtColumns ext = { .parent = parent, .clue = clue };
            add_entry(result, name, node_line(arg), CONTEXT_VARIABLE,
                      directory, filename, NULL, &ext);
        } else {
            ExtColumns ext = { .parent = parent, .clue = clue, .definition = "1" };
            add_entry(result, name, node_line(arg), context,
                      directory, filename, NULL, &ext);
        }
    }
}

/* Index the string argument of a require/require_relative as an import. */
static void index_require(TSNode call, const char *source_code, const char *directory,
                          const char *filename, ParseResult *result, SymbolFilter *filter,
                          const char *clue) {
    TSNode args = ts_node_child_by_field_name(call, "arguments", 9);
    if (ts_node_is_null(args)) return;
    uint32_t n = ts_node_child_count(args);
    for (uint32_t i = 0; i < n; i++) {
        TSNode arg = ts_node_child(args, i);
        if (ts_node_symbol(arg) != ruby_symbols.string) continue;
        char raw[SYMBOL_MAX_LENGTH];
        node_text(arg, source_code, filename, raw, sizeof(raw));
        size_t len = strlen(raw);
        char path[SYMBOL_MAX_LENGTH];
        if (len >= 2 && (raw[0] == '"' || raw[0] == '\'')) {
            snprintf(path, sizeof(path), "%.*s", (int)(len - 2), raw + 1);
        } else {
            snprintf(path, sizeof(path), "%s", raw);
        }
        if (path[0] && filter_should_index(filter, path)) {
            ExtColumns ext = { .clue = clue };
            add_entry(result, path, node_line(arg), CONTEXT_IMPORT,
                      directory, filename, NULL, &ext);
        }
    }
}

/* Method call. Indexes the method name as CONTEXT_CALL (receiver → parent),
 * with special handling for the common declarative DSL calls (require, attr_*,
 * define_method). Always visits arguments and any attached block. */
static void handle_call(TSNode node, const char *source_code, const char *directory,
                        const char *filename, ParseResult *result, SymbolFilter *filter,
                        int line, const char *parent, const char *ns) {
    TSNode method = ts_node_child_by_field_name(node, "method", 6);
    char mname[SYMBOL_MAX_LENGTH] = "";
    if (!ts_node_is_null(method)) {
        node_text(method, source_code, filename, mname, sizeof(mname));
    }

    /* Receiver becomes the call's parent, e.g. `qi each -p numbers`. */
    const char *call_parent = NULL;
    char recv_buf[SYMBOL_MAX_LENGTH];
    TSNode receiver = ts_node_child_by_field_name(node, "receiver", 8);
    if (!ts_node_is_null(receiver)) {
        node_text(receiver, source_code, filename, recv_buf, sizeof(recv_buf));
        if (recv_buf[0] && !strchr(recv_buf, '\n')) call_parent = recv_buf;
    }

    /* Declarative DSL calls that define searchable symbols. `parent` here is the
     * enclosing class/module so the created members resolve to their owner. */
    int special = 0;
    if (!ts_node_is_null(method) && ts_node_is_null(receiver)) {
        if (strcmp(mname, "require") == 0 || strcmp(mname, "require_relative") == 0 ||
            strcmp(mname, "load") == 0 || strcmp(mname, "autoload") == 0) {
            index_require(node, source_code, directory, filename, result, filter, mname);
            special = 1;
        } else if (strcmp(mname, "attr_accessor") == 0 || strcmp(mname, "attr_reader") == 0 ||
                   strcmp(mname, "attr_writer") == 0 || strcmp(mname, "attr") == 0) {
            index_symbol_args(node, source_code, directory, filename, result, filter,
                              parent, CONTEXT_PROPERTY, mname);
            special = 1;
        } else if (strcmp(mname, "define_method") == 0) {
            index_symbol_args(node, source_code, directory, filename, result, filter,
                              parent, CONTEXT_FUNCTION, "define_method");
            special = 1;
        }
    }

    if (!special && mname[0] && filter_should_index(filter, mname)) {
        ExtColumns ext = { .parent = call_parent };
        add_entry(result, mname, line, CONTEXT_CALL,
                  directory, filename, NULL, &ext);
    }

    /* Visit the receiver so it is recorded as a usage in its own right: a simple
     * receiver (`numbers` in numbers.each) becomes a VAR read, and a complex one
     * (an array/hash literal, or an inner call in a chain like str.strip.downcase)
     * has its nested symbols/keys/calls indexed. The method name (a separate field)
     * is already handled above, so it is not re-visited. */
    if (!ts_node_is_null(receiver)) {
        visit_node(receiver, source_code, directory, filename, result, filter, parent, ns);
    }

    /* Visit arguments for nested symbols/reads, unless this is a declarative call
     * (require, attr_ helpers, define_method): those fully consume their
     * symbol/string args above, and re-walking would double-index them. */
    TSNode args = ts_node_child_by_field_name(node, "arguments", 9);
    if (!special && !ts_node_is_null(args)) {
        process_children(args, source_code, directory, filename, result, filter, parent, ns);
    }
    TSNode blk = ts_node_child_by_field_name(node, "block", 5);
    if (!ts_node_is_null(blk)) {
        visit_node(blk, source_code, directory, filename, result, filter, parent, ns);
    }
}

/* do..end / { } block attached to a call: extract block params, visit the body.
 * Block params are anonymous-scope locals — the block has no name — so they get no
 * owner (NULL). The body keeps `parent` so define_method members inside still
 * resolve to the enclosing class/module. */
static void handle_block(TSNode node, const char *source_code, const char *directory,
                         const char *filename, ParseResult *result, SymbolFilter *filter,
                         int line, const char *parent, const char *ns) {
    (void)line;
    TSNode params = ts_node_child_by_field_name(node, "parameters", 10);
    if (!ts_node_is_null(params)) {
        extract_parameters(params, source_code, directory, filename, result, filter,
                           NULL, parent, ns);
    }

    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) {
        process_children(body, source_code, directory, filename, result, filter, parent, ns);
    } else {
        /* Braced blocks expose statements directly rather than via a body field. */
        uint32_t n = ts_node_child_count(node);
        for (uint32_t i = 0; i < n; i++) {
            TSNode child = ts_node_child(node, i);
            if (ts_node_eq(child, params)) continue;
            visit_node(child, source_code, directory, filename, result, filter, parent, ns);
        }
    }
}

/* -> (args) { body } lambda literal. Like a block, a lambda is anonymous, so its
 * params get no owner (NULL). */
static void handle_lambda(TSNode node, const char *source_code, const char *directory,
                          const char *filename, ParseResult *result, SymbolFilter *filter,
                          int line, const char *parent, const char *ns) {
    (void)line;
    TSNode params = ts_node_child_by_field_name(node, "parameters", 10);
    if (!ts_node_is_null(params)) {
        extract_parameters(params, source_code, directory, filename, result, filter,
                           NULL, parent, ns);
    }
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) {
        process_children(body, source_code, directory, filename, result, filter, parent, ns);
    }
}

static void handle_string(TSNode node, const char *source_code, const char *directory,
                          const char *filename, ParseResult *result, SymbolFilter *filter,
                          int line, const char *parent, const char *ns) {
    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_symbol(child) == ruby_symbols.string_content) {
            char text[CLEANED_WORD_BUFFER];
            node_text(child, source_code, filename, text, sizeof(text));
            index_words(text, line, CONTEXT_STRING, directory, filename, result, filter);
        }
    }
    /* #{...} interpolations may contain calls; visit them. */
    process_children(node, source_code, directory, filename, result, filter, parent, ns);
}

static void handle_comment(TSNode node, const char *source_code, const char *directory,
                           const char *filename, ParseResult *result, SymbolFilter *filter,
                           int line, const char *parent, const char *ns) {
    (void)parent; (void)ns;
    char text[COMMENT_TEXT_BUFFER];
    node_text(node, source_code, filename, text, sizeof(text));
    char *start = strip_comment_delimiters(text);
    index_words(start, line, CONTEXT_COMMENT, directory, filename, result, filter);
}

static void handle_heredoc(TSNode node, const char *source_code, const char *directory,
                           const char *filename, ParseResult *result, SymbolFilter *filter,
                           int line, const char *parent, const char *ns) {
    (void)line;
    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_symbol(child) == ruby_symbols.heredoc_content) {
            char text[CLEANED_WORD_BUFFER];
            node_text(child, source_code, filename, text, sizeof(text));
            index_words(text, node_line(child), CONTEXT_STRING, directory, filename, result, filter);
        }
    }
    /* Interpolations inside the heredoc. */
    process_children(node, source_code, directory, filename, result, filter, parent, ns);
}

/* A bare identifier reached during traversal is either a variable read or a
 * receiver-less method call (private, module_function, a DSL macro). Ruby cannot
 * tell the two apart syntactically, so — like the C reference (a bare identifier →
 * CONTEXT_VARIABLE usage) — we record it as a usage. Identifiers that are
 * definitions, parameters, method names, or call receivers are consumed by their
 * owning handlers via field lookups and never reach here, so nothing is doubled. */
static void handle_identifier(TSNode node, const char *source_code, const char *directory,
                              const char *filename, ParseResult *result, SymbolFilter *filter,
                              int line, const char *parent, const char *ns) {
    (void)parent; (void)ns;
    char name[SYMBOL_MAX_LENGTH];
    node_text(node, source_code, filename, name, sizeof(name));
    if (name[0] && filter_should_index(filter, name)) {
        add_entry(result, name, line, CONTEXT_VARIABLE,
                  directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
    }
}

/* Qualified constant reference `Scope::Name` (e.g. Float::INFINITY). Record the
 * rightmost `name` as a VAR read with the scope as its parent, then visit the
 * scope so it is a read too (and nested A::B::C recurses). */
static void handle_scope_resolution(TSNode node, const char *source_code, const char *directory,
                                    const char *filename, ParseResult *result, SymbolFilter *filter,
                                    int line, const char *parent, const char *ns) {
    (void)line; (void)parent;
    char scope_buf[SYMBOL_MAX_LENGTH];
    ScopeRef sr;
    if (!split_scope_ref(node, source_code, filename, scope_buf, sizeof(scope_buf), &sr)) {
        process_children(node, source_code, directory, filename, result, filter, parent, ns);
        return;
    }

    char nbuf[SYMBOL_MAX_LENGTH];
    node_text(sr.name, source_code, filename, nbuf, sizeof(nbuf));
    if (nbuf[0] && filter_should_index(filter, nbuf)) {
        ExtColumns ext = { .parent = sr.scope_text };
        add_entry(result, nbuf, node_line(sr.name), CONTEXT_VARIABLE,
                  directory, filename, NULL, &ext);
    }

    if (!ts_node_is_null(sr.scope))
        visit_node(sr.scope, source_code, directory, filename, result, filter, parent, ns);
}

/* A :symbol literal used as a value (status = :active, when :circle, role: :admin).
 * The bare name (colon stripped) is the searchable token, recorded as a usage. */
static void handle_symbol(TSNode node, const char *source_code, const char *directory,
                          const char *filename, ParseResult *result, SymbolFilter *filter,
                          int line, const char *parent, const char *ns) {
    (void)parent; (void)ns;
    char raw[SYMBOL_MAX_LENGTH];
    node_text(node, source_code, filename, raw, sizeof(raw));
    const char *sym = (raw[0] == ':') ? raw + 1 : raw;
    if (sym[0] && filter_should_index(filter, sym)) {
        add_entry(result, sym, line, CONTEXT_VARIABLE,
                  directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
    }
}

/* hash pair: `name: value` or `"name" => value`. A `name:` short-form key is the
 * direct analog of a C designated-initializer key (.name = ...), which the C
 * reference records as a PROPERTY usage whose parent is the declared variable.
 * The parent comes from g_hash_parent (set by handle_assignment; swapped to the
 * enclosing key for a nested hash). Other key forms keep their default traversal
 * so string/identifier keys are indexed as before. */
static void handle_pair(TSNode node, const char *source_code, const char *directory,
                        const char *filename, ParseResult *result, SymbolFilter *filter,
                        int line, const char *parent, const char *ns) {
    (void)line;
    TSNode key = ts_node_child_by_field_name(node, "key", 3);
    TSNode value = ts_node_child_by_field_name(node, "value", 5);

    char key_name[SYMBOL_MAX_LENGTH] = "";
    bool symbol_key = false;
    if (!ts_node_is_null(key)) {
        if (ts_node_symbol(key) == ruby_symbols.hash_key_symbol) {
            symbol_key = true;
            node_text(key, source_code, filename, key_name, sizeof(key_name));
            if (key_name[0] && filter_should_index(filter, key_name)) {
                add_entry(result, key_name, node_line(key), CONTEXT_PROPERTY,
                          directory, filename, NULL,
                          g_hash_parent[0] ? &(ExtColumns){.parent = g_hash_parent} : NULL);
            }
        } else {
            visit_node(key, source_code, directory, filename, result, filter, parent, ns);
        }
    }

    if (!ts_node_is_null(value)) {
        /* A nested hash's keys belong to this key (mirrors C swapping
         * g_initializer_parent to the enclosing field), so swap the parent
         * around the value's visit and restore it after. */
        char saved[SYMBOL_MAX_LENGTH];
        copy_symbol(saved, sizeof(saved), g_hash_parent);
        if (symbol_key && ts_node_symbol(value) == ruby_symbols.hash) {
            copy_symbol(g_hash_parent, sizeof(g_hash_parent), key_name);
        }
        visit_node(value, source_code, directory, filename, result, filter, parent, ns);
        copy_symbol(g_hash_parent, sizeof(g_hash_parent), saved);
    }
}

/* Record a pattern binding as a VARIABLE definition (D=1). A case/in clause (and
 * the one-line `expr => pattern` / `expr in pattern` forms) introduces new locals
 * on the pattern side, exactly like method parameters — so the bound names are
 * definitions, not usages. */
static void add_pattern_binding(TSNode name_node, const char *source_code,
                                const char *directory, const char *filename,
                                ParseResult *result, SymbolFilter *filter,
                                const char *parent) {
    if (ts_node_is_null(name_node)) return;
    char name[SYMBOL_MAX_LENGTH];
    node_text(name_node, source_code, filename, name, sizeof(name));
    if (name[0] && filter_should_index(filter, name)) {
        ExtColumns ext = { .parent = parent, .definition = "1" };
        add_entry(result, name, node_line(name_node), CONTEXT_VARIABLE,
                  directory, filename, NULL, &ext);
    }
}

/* Walk a case/in pattern, distinguishing the three roles a name can play:
 *   - a binding (new local)            → VARIABLE definition (D=1)
 *   - a pinned read (^x)               → VARIABLE usage (D=0, via visit_node)
 *   - a key (`name:`)                  → PROPERTY (like a hash key)
 * Literal sub-patterns (:symbols, strings, numbers) and matched constants are
 * visited normally so they land as ordinary usages. This mirrors the parameter
 * model, where bound names are definitions and everything else is a read. */
static void visit_pattern(TSNode node, const char *source_code, const char *directory,
                          const char *filename, ParseResult *result, SymbolFilter *filter,
                          const char *parent, const char *ns) {
    TSSymbol sym = ts_node_symbol(node);

    if (sym == ruby_symbols.identifier) {
        /* A bare identifier in pattern position binds a new local. */
        add_pattern_binding(node, source_code, directory, filename, result, filter, parent);

    } else if (sym == ruby_symbols.splat_parameter ||
               sym == ruby_symbols.hash_splat_parameter) {
        /* *rest / **rest — the name binds; a bare * / ** has no name child. */
        TSNode name = ts_node_child_by_field_name(node, "name", 4);
        add_pattern_binding(name, source_code, directory, filename, result, filter, parent);

    } else if (sym == ruby_symbols.as_pattern) {
        /* <pattern> => name — `name` binds; the aliased sub-pattern (e.g. Integer)
         * is matched, so visit it as a pattern too. */
        add_pattern_binding(ts_node_child_by_field_name(node, "name", 4),
                            source_code, directory, filename, result, filter, parent);
        TSNode value = ts_node_child_by_field_name(node, "value", 5);
        if (!ts_node_is_null(value))
            visit_pattern(value, source_code, directory, filename, result, filter, parent, ns);

    } else if (sym == ruby_symbols.keyword_pattern) {
        /* key: <pattern> — the key is a PROPERTY (like a hash key). With a value,
         * recurse into it; the short form `key:` also binds a local named `key`. */
        TSNode key = ts_node_child_by_field_name(node, "key", 3);
        bool sym_key = !ts_node_is_null(key) &&
                       ts_node_symbol(key) == ruby_symbols.hash_key_symbol;
        if (sym_key) {
            char key_name[SYMBOL_MAX_LENGTH];
            node_text(key, source_code, filename, key_name, sizeof(key_name));
            if (key_name[0] && filter_should_index(filter, key_name)) {
                add_entry(result, key_name, node_line(key), CONTEXT_PROPERTY,
                          directory, filename, NULL, NULL);
            }
        } else if (!ts_node_is_null(key)) {
            visit_node(key, source_code, directory, filename, result, filter, parent, ns);
        }
        TSNode value = ts_node_child_by_field_name(node, "value", 5);
        if (!ts_node_is_null(value)) {
            visit_pattern(value, source_code, directory, filename, result, filter, parent, ns);
        } else if (sym_key) {
            /* `{ host: }` shorthand binds a local named after the key. */
            add_pattern_binding(key, source_code, directory, filename, result, filter, parent);
        }

    } else if (sym == ruby_symbols.variable_reference_pattern) {
        /* ^x pin — a READ of an existing binding, not a new one. */
        TSNode name = ts_node_child_by_field_name(node, "name", 4);
        if (!ts_node_is_null(name))
            visit_node(name, source_code, directory, filename, result, filter, parent, ns);

    } else if (sym == ruby_symbols.array_pattern ||
               sym == ruby_symbols.find_pattern ||
               sym == ruby_symbols.hash_pattern) {
        /* Container patterns: an optional `class` constant (a matched type — a
         * read) plus element sub-patterns. */
        uint32_t n = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < n; i++) {
            visit_pattern(ts_node_named_child(node, i), source_code, directory,
                          filename, result, filter, parent, ns);
        }

    } else {
        /* Literal sub-patterns (:symbol, "string", numbers), matched constants
         * (Integer, Point), and anything else: index as ordinary usages/reads. */
        visit_node(node, source_code, directory, filename, result, filter, parent, ns);
    }
}

/* in_clause: `in <pattern> [guard] then <body>`. The pattern binds; the guard and
 * body are ordinary expressions (reads). */
static void handle_in_clause(TSNode node, const char *source_code, const char *directory,
                             const char *filename, ParseResult *result, SymbolFilter *filter,
                             int line, const char *parent, const char *ns) {
    (void)line;
    TSNode pattern = ts_node_child_by_field_name(node, "pattern", 7);
    if (!ts_node_is_null(pattern))
        visit_pattern(pattern, source_code, directory, filename, result, filter, parent, ns);

    TSNode guard = ts_node_child_by_field_name(node, "guard", 5);
    if (!ts_node_is_null(guard))
        visit_node(guard, source_code, directory, filename, result, filter, parent, ns);

    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body))
        visit_node(body, source_code, directory, filename, result, filter, parent, ns);
}

/* One-line `expr => pattern` (match_pattern) and `expr in pattern` (test_pattern).
 * The value is a read; the pattern binds. */
static void handle_match_pattern(TSNode node, const char *source_code, const char *directory,
                                 const char *filename, ParseResult *result, SymbolFilter *filter,
                                 int line, const char *parent, const char *ns) {
    (void)line;
    TSNode value = ts_node_child_by_field_name(node, "value", 5);
    if (!ts_node_is_null(value))
        visit_node(value, source_code, directory, filename, result, filter, parent, ns);

    TSNode pattern = ts_node_child_by_field_name(node, "pattern", 7);
    if (!ts_node_is_null(pattern))
        visit_pattern(pattern, source_code, directory, filename, result, filter, parent, ns);
}

static void visit_node(TSNode node, const char *source_code, const char *directory,
                       const char *filename, ParseResult *result, SymbolFilter *filter,
                       const char *parent, const char *ns) {
    /* Guard against deep trees that would otherwise overflow the
     * stack via the visit_node ↔ process_children mutual recursion. Mirrors the
     * C reference's MAX_EXPRESSION_DEPTH check in visit_expression. Tracked as a
     * single balanced enter/exit counter; the dispatch below is an if/else chain
     * (no mid-function returns), so the matching depth-- at the end always runs. */
    static int depth = 0;
    if (depth >= MAX_EXPRESSION_DEPTH) {
        if (g_debug) {
            debug("[visit_node] Max recursion depth %d reached; stopping descent",
                  MAX_EXPRESSION_DEPTH);
        }
        return;
    }
    depth++;

    TSSymbol sym = ts_node_symbol(node);
    int line = node_line(node);

    if (g_debug) {
        debug("[visit_node] Line %d: node_type='%s'", line, ts_node_type(node));
    }

    if (sym == ruby_symbols.method || sym == ruby_symbols.singleton_method) {
        handle_method(node, source_code, directory, filename, result, filter, line, parent, ns);
    } else if (sym == ruby_symbols.class_) {
        handle_class(node, source_code, directory, filename, result, filter, line, parent, ns);
    } else if (sym == ruby_symbols.module) {
        handle_module(node, source_code, directory, filename, result, filter, line, parent, ns);
    } else if (sym == ruby_symbols.singleton_class) {
        handle_singleton_class(node, source_code, directory, filename, result, filter, line, parent, ns);
    } else if (sym == ruby_symbols.assignment || sym == ruby_symbols.operator_assignment) {
        handle_assignment(node, source_code, directory, filename, result, filter, line, parent, ns);
    } else if (sym == ruby_symbols.call) {
        handle_call(node, source_code, directory, filename, result, filter, line, parent, ns);
    } else if (sym == ruby_symbols.lambda) {
        handle_lambda(node, source_code, directory, filename, result, filter, line, parent, ns);
    } else if (sym == ruby_symbols.do_block || sym == ruby_symbols.block) {
        handle_block(node, source_code, directory, filename, result, filter, line, parent, ns);
    } else if (sym == ruby_symbols.string) {
        handle_string(node, source_code, directory, filename, result, filter, line, parent, ns);
    } else if (sym == ruby_symbols.comment) {
        handle_comment(node, source_code, directory, filename, result, filter, line, parent, ns);
    } else if (sym == ruby_symbols.heredoc_body) {
        handle_heredoc(node, source_code, directory, filename, result, filter, line, parent, ns);
    } else if (sym == ruby_symbols.identifier || sym == ruby_symbols.constant) {
        /* A bare constant in read position is a usage, like a bare identifier read. */
        handle_identifier(node, source_code, directory, filename, result, filter, line, parent, ns);
    } else if (sym == ruby_symbols.scope_resolution) {
        handle_scope_resolution(node, source_code, directory, filename, result, filter, line, parent, ns);
    } else if (sym == ruby_symbols.simple_symbol) {
        handle_symbol(node, source_code, directory, filename, result, filter, line, parent, ns);
    } else if (sym == ruby_symbols.pair) {
        handle_pair(node, source_code, directory, filename, result, filter, line, parent, ns);
    } else if (sym == ruby_symbols.in_clause) {
        handle_in_clause(node, source_code, directory, filename, result, filter, line, parent, ns);
    } else if (sym == ruby_symbols.match_pattern || sym == ruby_symbols.test_pattern) {
        handle_match_pattern(node, source_code, directory, filename, result, filter, line, parent, ns);
    } else {
        if (g_debug) {
            debug("[visit_node] Line %d: No handler for '%s', processing children",
                  line, ts_node_type(node));
        }
        process_children(node, source_code, directory, filename, result, filter, parent, ns);
    }

    depth--;
}

static void process_children(TSNode node, const char *source_code, const char *directory,
                             const char *filename, ParseResult *result, SymbolFilter *filter,
                             const char *parent, const char *ns) {
    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        visit_node(ts_node_child(node, i), source_code, directory, filename, result, filter, parent, ns);
    }
}

static void init_ruby_symbols(const TSLanguage *language) {
    static int initialized = 0;
    if (initialized) return;
    initialized = 1;

    ruby_symbols.method = ts_language_symbol_for_name(language, "method", 6, true);
    ruby_symbols.singleton_method = ts_language_symbol_for_name(language, "singleton_method", 16, true);
    ruby_symbols.class_ = ts_language_symbol_for_name(language, "class", 5, true);
    ruby_symbols.singleton_class = ts_language_symbol_for_name(language, "singleton_class", 15, true);
    ruby_symbols.module = ts_language_symbol_for_name(language, "module", 6, true);
    ruby_symbols.assignment = ts_language_symbol_for_name(language, "assignment", 10, true);
    ruby_symbols.operator_assignment = ts_language_symbol_for_name(language, "operator_assignment", 19, true);
    ruby_symbols.call = ts_language_symbol_for_name(language, "call", 4, true);
    ruby_symbols.lambda = ts_language_symbol_for_name(language, "lambda", 6, true);
    ruby_symbols.do_block = ts_language_symbol_for_name(language, "do_block", 8, true);
    ruby_symbols.block = ts_language_symbol_for_name(language, "block", 5, true);
    ruby_symbols.string = ts_language_symbol_for_name(language, "string", 6, true);
    ruby_symbols.comment = ts_language_symbol_for_name(language, "comment", 7, true);
    ruby_symbols.heredoc_body = ts_language_symbol_for_name(language, "heredoc_body", 12, true);
    ruby_symbols.pair = ts_language_symbol_for_name(language, "pair", 4, true);

    ruby_symbols.identifier = ts_language_symbol_for_name(language, "identifier", 10, true);
    ruby_symbols.constant = ts_language_symbol_for_name(language, "constant", 8, true);
    ruby_symbols.instance_variable = ts_language_symbol_for_name(language, "instance_variable", 17, true);
    ruby_symbols.class_variable = ts_language_symbol_for_name(language, "class_variable", 14, true);
    ruby_symbols.global_variable = ts_language_symbol_for_name(language, "global_variable", 15, true);
    ruby_symbols.scope_resolution = ts_language_symbol_for_name(language, "scope_resolution", 16, true);
    ruby_symbols.simple_symbol = ts_language_symbol_for_name(language, "simple_symbol", 13, true);
    ruby_symbols.hash_key_symbol = ts_language_symbol_for_name(language, "hash_key_symbol", 15, true);
    ruby_symbols.hash = ts_language_symbol_for_name(language, "hash", 4, true);
    ruby_symbols.operator_ = ts_language_symbol_for_name(language, "operator", 8, true);
    ruby_symbols.optional_parameter = ts_language_symbol_for_name(language, "optional_parameter", 18, true);
    ruby_symbols.keyword_parameter = ts_language_symbol_for_name(language, "keyword_parameter", 17, true);
    ruby_symbols.splat_parameter = ts_language_symbol_for_name(language, "splat_parameter", 15, true);
    ruby_symbols.hash_splat_parameter = ts_language_symbol_for_name(language, "hash_splat_parameter", 20, true);
    ruby_symbols.block_parameter = ts_language_symbol_for_name(language, "block_parameter", 15, true);
    ruby_symbols.left_assignment_list = ts_language_symbol_for_name(language, "left_assignment_list", 20, true);
    ruby_symbols.string_content = ts_language_symbol_for_name(language, "string_content", 14, true);
    ruby_symbols.heredoc_content = ts_language_symbol_for_name(language, "heredoc_content", 15, true);
    ruby_symbols.interpolation = ts_language_symbol_for_name(language, "interpolation", 13, true);

    ruby_symbols.in_clause = ts_language_symbol_for_name(language, "in_clause", 9, true);
    ruby_symbols.match_pattern = ts_language_symbol_for_name(language, "match_pattern", 13, true);
    ruby_symbols.test_pattern = ts_language_symbol_for_name(language, "test_pattern", 12, true);
    ruby_symbols.array_pattern = ts_language_symbol_for_name(language, "array_pattern", 13, true);
    ruby_symbols.find_pattern = ts_language_symbol_for_name(language, "find_pattern", 12, true);
    ruby_symbols.hash_pattern = ts_language_symbol_for_name(language, "hash_pattern", 12, true);
    ruby_symbols.keyword_pattern = ts_language_symbol_for_name(language, "keyword_pattern", 15, true);
    ruby_symbols.as_pattern = ts_language_symbol_for_name(language, "as_pattern", 10, true);
    ruby_symbols.variable_reference_pattern = ts_language_symbol_for_name(language, "variable_reference_pattern", 26, true);
}

int parser_init(RubyParser *parser, SymbolFilter *filter) {
    parser->filter = filter;
    return 0;
}

int parser_parse_file(RubyParser *parser, const char *filepath, const char *project_root, ParseResult *result) {
    FILE *fp = safe_fopen(filepath, "rb", 0);  /* binary mode for accurate byte count */
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

    /* Index the filename without extension so files are findable by name. */
    char filename_no_ext[FILENAME_MAX_LENGTH];
    snprintf(filename_no_ext, sizeof(filename_no_ext), "%s", filename);
    char *dot = strrchr(filename_no_ext, '.');
    if (dot) *dot = '\0';
    if (filter_should_index(parser->filter, filename_no_ext)) {
        add_entry(result, filename_no_ext, 1, CONTEXT_FILENAME, directory, filename, NULL, NO_EXTENSIBLE_COLUMNS);
    }

    /* Parse with tree-sitter (fresh parser per file, matching the C reference). */
    TSParser *ts_parser = ts_parser_new();
    const TSLanguage *language = tree_sitter_ruby();
    if (!ts_parser_set_language(ts_parser, language)) {
        fprintf(stderr, "ERROR: Failed to set Ruby language\n");
        free(source_code);
        ts_parser_delete(ts_parser);
        return -1;
    }

    /* Initialize symbol lookup table */
    init_ruby_symbols(language);

    TSTree *tree = ts_parser_parse_string(ts_parser, NULL, source_code, (uint32_t)file_size);
    if (!tree) {
        fprintf(stderr, "ERROR: Failed to parse file: %s\n", filepath);
        free(source_code);
        ts_parser_delete(ts_parser);
        return -1;
    }

    visit_node(ts_tree_root_node(tree), source_code, directory, filename, result, parser->filter, NULL, NULL);

    ts_tree_delete(tree);
    ts_parser_delete(ts_parser);
    free(source_code);
    return 0;
}

void parser_set_debug(RubyParser *parser, int debug) {
    parser->debug = debug;
    g_debug = debug;
}

void parser_free(RubyParser *parser) {
    /* No per-parser resources to free; the TSParser is created per file. */
    (void)parser;
}
