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

/* Ruby indexer.
 *
 * Follows the handler-ownership pattern (see docs/SOURCEMINDER_ARCHITECTURE.md):
 * visit_node dispatches on the tree-sitter symbol id and RETURNS after calling a
 * handler, so each handler is responsible for visiting exactly the children it
 * wants. There are no generic identifier handlers, which keeps indexing free of
 * duplicates.
 *
 * Ruby's essential wrinkle is lexical nesting: a method's owner and a constant's
 * namespace are determined by the enclosing class/module. We thread that scope
 * through the traversal as two extra arguments (`parent` = nearest enclosing
 * class/module name, `ns` = full "Foo::Bar" path) rather than re-walking parents
 * at every node.
 */
#include "ruby_language.h"
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

/* Declare the tree-sitter Ruby language */
TSLanguage *tree_sitter_ruby(void);

/* Global debug flag */
static int g_debug = 0;

/* Invariant per-file context. Kept separate from the traversal's changing scope
 * (parent/ns) so handler signatures stay narrow. */
typedef struct {
    const char *src;
    const char *directory;
    const char *filename;
    ParseResult *result;
    SymbolFilter *filter;
} Ctx;

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

    /* Structural node types checked inside handlers */
    TSSymbol identifier;
    TSSymbol constant;
    TSSymbol instance_variable;
    TSSymbol class_variable;
    TSSymbol global_variable;
    TSSymbol scope_resolution;
    TSSymbol simple_symbol;
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
} ruby_symbols;

/* Forward declarations */
static void visit_node(TSNode node, const Ctx *cx, const char *parent, const char *ns);
static void process_children(TSNode node, const Ctx *cx, const char *parent, const char *ns);

/* ── Small helpers ─────────────────────────────────────────────────────────── */

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
static void node_text(const Ctx *cx, TSNode node, char *buf, size_t size) {
    safe_extract_node_text(cx->src, node, buf, size, cx->filename);
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
static void index_words(const Ctx *cx, const char *text, int line, ContextType context) {
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
                    if (cleaned[0] && filter_should_index(cx->filter, cleaned)) {
                        add_entry(cx->result, cleaned, line, context,
                                  cx->directory, cx->filename, NULL, NO_EXTENSIBLE_COLUMNS);
                    }
                }
            }
            word_start = p + 1;
            if (*p == '\0') break;
        }
    }
}

/* ── Parameters ────────────────────────────────────────────────────────────── */

/* Index one parameter identifier (owner = enclosing method/block name). */
static void index_param(const Ctx *cx, TSNode name_node, const char *owner) {
    char name[SYMBOL_MAX_LENGTH];
    node_text(cx, name_node, name, sizeof(name));
    const char *sym = strip_sigils(name);
    if (sym[0] && filter_should_index(cx->filter, sym)) {
        ExtColumns ext = { .parent = owner };
        add_entry(cx->result, sym, node_line(name_node), CONTEXT_ARGUMENT,
                  cx->directory, cx->filename, NULL, &ext);
    }
}

/* Extract parameters from a method_parameters / block_parameters /
 * lambda_parameters node. Handles the full Ruby parameter zoo: plain, optional
 * (default), keyword, *splat, **double-splat, and &block. Default/keyword values
 * are visited so nested calls in them are still indexed. */
static void extract_parameters(TSNode params, const Ctx *cx, const char *owner,
                               const char *parent, const char *ns) {
    uint32_t n = ts_node_child_count(params);
    for (uint32_t i = 0; i < n; i++) {
        TSNode child = ts_node_child(params, i);
        TSSymbol sym = ts_node_symbol(child);

        if (sym == ruby_symbols.identifier) {
            index_param(cx, child, owner);
        } else if (sym == ruby_symbols.optional_parameter ||
                   sym == ruby_symbols.keyword_parameter) {
            TSNode name = ts_node_child_by_field_name(child, "name", 4);
            if (!ts_node_is_null(name)) index_param(cx, name, owner);
            TSNode value = ts_node_child_by_field_name(child, "value", 5);
            if (!ts_node_is_null(value)) visit_node(value, cx, parent, ns);
        } else if (sym == ruby_symbols.splat_parameter ||
                   sym == ruby_symbols.hash_splat_parameter ||
                   sym == ruby_symbols.block_parameter) {
            TSNode name = ts_node_child_by_field_name(child, "name", 4);
            if (!ts_node_is_null(name)) index_param(cx, name, owner);
        }
    }
}

/* ── Definition handlers ───────────────────────────────────────────────────── */

/* method / singleton_method: instance methods, class methods (def self.x),
 * and per-object singleton methods (def obj.x). */
static void handle_method(TSNode node, const Ctx *cx, const char *parent, const char *ns) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name_node)) { process_children(node, cx, parent, ns); return; }

    char name[SYMBOL_MAX_LENGTH];
    node_text(cx, name_node, name, sizeof(name));

    /* def self.foo / def Klass.foo → a class-level method. Record "self" (or the
     * receiver) as a modifier so `-m self` finds class methods. */
    const char *modifier = NULL;
    char object_buf[SYMBOL_MAX_LENGTH];
    TSNode object = ts_node_child_by_field_name(node, "object", 6);
    if (!ts_node_is_null(object)) {
        node_text(cx, object, object_buf, sizeof(object_buf));
        modifier = object_buf;
    }

    if (name[0] && filter_should_index(cx->filter, name)) {
        char location[SYMBOL_MAX_LENGTH];
        format_source_location(node, location, sizeof(location));
        ExtColumns ext = {
            .parent = parent,
            .namespace = (ns && ns[0]) ? ns : NULL,
            .modifier = modifier,
            .definition = "1"
        };
        add_entry(cx->result, name, node_line(node), CONTEXT_FUNCTION,
                  cx->directory, cx->filename, location, &ext);
    }

    /* Parameters belong to this method; the body keeps the SAME enclosing scope
     * (so ivar/constant assignments inside resolve to the enclosing class). */
    TSNode params = ts_node_child_by_field_name(node, "parameters", 10);
    if (!ts_node_is_null(params)) extract_parameters(params, cx, name, parent, ns);

    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) process_children(body, cx, parent, ns);
}

/* class Foo < Bar ... end */
static void handle_class(TSNode node, const Ctx *cx, const char *parent, const char *ns) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name_node)) { process_children(node, cx, parent, ns); return; }

    char name[SYMBOL_MAX_LENGTH];
    node_text(cx, name_node, name, sizeof(name));

    /* Superclass (if any) is recorded as the type, e.g. `qi Dog -i class -t Animal`. */
    const char *type = NULL;
    char super_buf[SYMBOL_MAX_LENGTH];
    TSNode super = ts_node_child_by_field_name(node, "superclass", 10);
    if (!ts_node_is_null(super)) {
        /* superclass node is "< Constant"; index the constant text only. */
        char raw[SYMBOL_MAX_LENGTH];
        node_text(cx, super, raw, sizeof(raw));
        const char *p = raw;
        while (*p == '<' || isspace((unsigned char)*p)) p++;
        snprintf(super_buf, sizeof(super_buf), "%s", p);
        type = super_buf;
    }

    if (name[0] && filter_should_index(cx->filter, name)) {
        char location[SYMBOL_MAX_LENGTH];
        format_source_location(node, location, sizeof(location));
        ExtColumns ext = {
            .parent = parent,
            .namespace = (ns && ns[0]) ? ns : NULL,
            .type = type,
            .definition = "1"
        };
        add_entry(cx->result, name, node_line(node), CONTEXT_CLASS,
                  cx->directory, cx->filename, location, &ext);
    }

    char scope[SYMBOL_MAX_LENGTH];
    child_ns(ns, name, scope, sizeof(scope));
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) process_children(body, cx, name, scope);
}

/* module Foo ... end — a namespace (or mixin). */
static void handle_module(TSNode node, const Ctx *cx, const char *parent, const char *ns) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name_node)) { process_children(node, cx, parent, ns); return; }

    char name[SYMBOL_MAX_LENGTH];
    node_text(cx, name_node, name, sizeof(name));

    if (name[0] && filter_should_index(cx->filter, name)) {
        char location[SYMBOL_MAX_LENGTH];
        format_source_location(node, location, sizeof(location));
        ExtColumns ext = {
            .parent = parent,
            .namespace = (ns && ns[0]) ? ns : NULL,
            .definition = "1"
        };
        add_entry(cx->result, name, node_line(node), CONTEXT_NAMESPACE,
                  cx->directory, cx->filename, location, &ext);
    }

    char scope[SYMBOL_MAX_LENGTH];
    child_ns(ns, name, scope, sizeof(scope));
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) process_children(body, cx, name, scope);
}

/* class << self ... end — reopen the singleton; just recurse the body. */
static void handle_singleton_class(TSNode node, const Ctx *cx, const char *parent, const char *ns) {
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) process_children(body, cx, parent, ns);
    else process_children(node, cx, parent, ns);
}

/* ── Assignment ────────────────────────────────────────────────────────────── */

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

static void index_lvalue(TSNode target, const Ctx *cx, const char *parent) {
    ContextType context;
    if (!classify_lvalue(ts_node_symbol(target), &context)) return;

    char raw[SYMBOL_MAX_LENGTH];
    node_text(cx, target, raw, sizeof(raw));
    const char *sym = strip_sigils(raw);
    if (!sym[0] || !filter_should_index(cx->filter, sym)) return;

    ExtColumns ext = { .parent = parent };
    add_entry(cx->result, sym, node_line(target), context,
              cx->directory, cx->filename, NULL, &ext);
}

/* assignment / operator_assignment. Index the left-hand target(s), then visit the
 * right-hand side so calls/blocks/lambdas in it are indexed too. */
static void handle_assignment(TSNode node, const Ctx *cx, const char *parent, const char *ns) {
    TSNode left = ts_node_child_by_field_name(node, "left", 4);
    if (!ts_node_is_null(left)) {
        if (ts_node_symbol(left) == ruby_symbols.left_assignment_list) {
            uint32_t n = ts_node_child_count(left);
            for (uint32_t i = 0; i < n; i++) index_lvalue(ts_node_child(left, i), cx, parent);
        } else {
            index_lvalue(left, cx, parent);
        }
    }

    TSNode right = ts_node_child_by_field_name(node, "right", 5);
    if (!ts_node_is_null(right)) visit_node(right, cx, parent, ns);
}

/* ── Calls ─────────────────────────────────────────────────────────────────── */

/* Resolve the searchable name from a symbol/string argument to a declarative
 * call (attr_*, define_method). A plain literal yields its text; an interpolated
 * string such as "#{field}=" yields the interpolated expression ("field").
 * *dynamic is set true for the interpolated case: the real name is only known at
 * runtime, so the caller records the extracted name as a reference rather than a
 * definition. Returns false when there is nothing indexable. */
static bool symbol_arg_name(const Ctx *cx, TSNode arg, char *out, size_t size, bool *dynamic) {
    TSSymbol sym = ts_node_symbol(arg);
    *dynamic = false;

    if (sym == ruby_symbols.simple_symbol) {
        char raw[SYMBOL_MAX_LENGTH];
        node_text(cx, arg, raw, sizeof(raw));
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
            node_text(cx, child, raw, sizeof(raw));  /* "#{field}" */
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
            node_text(cx, child, raw, sizeof(raw));
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
static void index_symbol_args(TSNode call, const Ctx *cx, const char *parent,
                              ContextType context, const char *clue) {
    TSNode args = ts_node_child_by_field_name(call, "arguments", 9);
    if (ts_node_is_null(args)) return;
    uint32_t n = ts_node_child_count(args);
    for (uint32_t i = 0; i < n; i++) {
        TSNode arg = ts_node_child(args, i);
        char name[SYMBOL_MAX_LENGTH];
        bool dynamic = false;
        if (!symbol_arg_name(cx, arg, name, sizeof(name), &dynamic)) continue;
        if (!filter_should_index(cx->filter, name)) continue;

        if (dynamic) {
            ExtColumns ext = { .parent = parent, .clue = clue };
            add_entry(cx->result, name, node_line(arg), CONTEXT_VARIABLE,
                      cx->directory, cx->filename, NULL, &ext);
        } else {
            ExtColumns ext = { .parent = parent, .clue = clue, .definition = "1" };
            add_entry(cx->result, name, node_line(arg), context,
                      cx->directory, cx->filename, NULL, &ext);
        }
    }
}

/* Index the string argument of a require/require_relative as an import. */
static void index_require(TSNode call, const Ctx *cx, const char *clue) {
    TSNode args = ts_node_child_by_field_name(call, "arguments", 9);
    if (ts_node_is_null(args)) return;
    uint32_t n = ts_node_child_count(args);
    for (uint32_t i = 0; i < n; i++) {
        TSNode arg = ts_node_child(args, i);
        if (ts_node_symbol(arg) != ruby_symbols.string) continue;
        char raw[SYMBOL_MAX_LENGTH];
        node_text(cx, arg, raw, sizeof(raw));
        size_t len = strlen(raw);
        char path[SYMBOL_MAX_LENGTH];
        if (len >= 2 && (raw[0] == '"' || raw[0] == '\'')) {
            snprintf(path, sizeof(path), "%.*s", (int)(len - 2), raw + 1);
        } else {
            snprintf(path, sizeof(path), "%s", raw);
        }
        if (path[0] && filter_should_index(cx->filter, path)) {
            ExtColumns ext = { .clue = clue };
            add_entry(cx->result, path, node_line(arg), CONTEXT_IMPORT,
                      cx->directory, cx->filename, NULL, &ext);
        }
    }
}

/* A method call. Indexes the method name as CONTEXT_CALL (receiver → parent),
 * with special handling for the common declarative DSL calls (require, attr_*,
 * define_method). Always visits arguments and any attached block. */
static void handle_call(TSNode node, const Ctx *cx, const char *parent, const char *ns) {
    TSNode method = ts_node_child_by_field_name(node, "method", 6);
    char mname[SYMBOL_MAX_LENGTH] = "";
    if (!ts_node_is_null(method)) node_text(cx, method, mname, sizeof(mname));

    /* Receiver (if any) becomes the call's parent, e.g. `qi sleep -p asyncio`. */
    const char *call_parent = NULL;
    char recv_buf[SYMBOL_MAX_LENGTH];
    TSNode receiver = ts_node_child_by_field_name(node, "receiver", 8);
    if (!ts_node_is_null(receiver)) {
        node_text(cx, receiver, recv_buf, sizeof(recv_buf));
        if (recv_buf[0] && !strchr(recv_buf, '\n')) call_parent = recv_buf;
    }

    /* Declarative DSL calls that define searchable symbols. `parent` here is the
     * enclosing class/module so the created members resolve to their owner. */
    int special = 0;
    if (!ts_node_is_null(method) && ts_node_is_null(receiver)) {
        if (strcmp(mname, "require") == 0 || strcmp(mname, "require_relative") == 0 ||
            strcmp(mname, "load") == 0 || strcmp(mname, "autoload") == 0) {
            index_require(node, cx, mname);
            special = 1;
        } else if (strcmp(mname, "attr_accessor") == 0 || strcmp(mname, "attr_reader") == 0 ||
                   strcmp(mname, "attr_writer") == 0 || strcmp(mname, "attr") == 0) {
            index_symbol_args(node, cx, parent, CONTEXT_PROPERTY, mname);
            special = 1;
        } else if (strcmp(mname, "define_method") == 0) {
            index_symbol_args(node, cx, parent, CONTEXT_FUNCTION, "define_method");
            special = 1;
        }
    }

    if (!special && mname[0] && filter_should_index(cx->filter, mname)) {
        ExtColumns ext = { .parent = call_parent };
        add_entry(cx->result, mname, node_line(node), CONTEXT_CALL,
                  cx->directory, cx->filename, NULL, &ext);
    }

    /* Visit arguments and any attached block for nested symbols. The receiver and
     * method name are already handled, so we do not re-visit them. */
    TSNode args = ts_node_child_by_field_name(node, "arguments", 9);
    if (!ts_node_is_null(args)) process_children(args, cx, parent, ns);
    TSNode blk = ts_node_child_by_field_name(node, "block", 5);
    if (!ts_node_is_null(blk)) visit_node(blk, cx, parent, ns);
}

/* do..end / { } block attached to a call: extract block params, visit the body. */
static void handle_block(TSNode node, const Ctx *cx, const char *parent, const char *ns) {
    TSNode params = ts_node_child_by_field_name(node, "parameters", 10);
    if (!ts_node_is_null(params)) extract_parameters(params, cx, parent, parent, ns);

    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) process_children(body, cx, parent, ns);
    else {
        /* Braced blocks expose statements directly rather than via a body field. */
        uint32_t n = ts_node_child_count(node);
        for (uint32_t i = 0; i < n; i++) {
            TSNode child = ts_node_child(node, i);
            if (ts_node_eq(child, params)) continue;
            visit_node(child, cx, parent, ns);
        }
    }
}

/* -> (args) { body } lambda literal. */
static void handle_lambda(TSNode node, const Ctx *cx, const char *parent, const char *ns) {
    TSNode params = ts_node_child_by_field_name(node, "parameters", 10);
    if (!ts_node_is_null(params)) extract_parameters(params, cx, parent, parent, ns);
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) process_children(body, cx, parent, ns);
}

/* ── Strings, comments, heredocs ───────────────────────────────────────────── */

static void handle_string(TSNode node, const Ctx *cx, const char *parent, const char *ns) {
    int line = node_line(node);
    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_symbol(child) == ruby_symbols.string_content) {
            char text[CLEANED_WORD_BUFFER];
            node_text(cx, child, text, sizeof(text));
            index_words(cx, text, line, CONTEXT_STRING);
        }
    }
    /* #{...} interpolations may contain calls; visit them. */
    process_children(node, cx, parent, ns);
}

static void handle_comment(TSNode node, const Ctx *cx, const char *parent, const char *ns) {
    (void)parent; (void)ns;
    char text[COMMENT_TEXT_BUFFER];
    node_text(cx, node, text, sizeof(text));
    char *start = strip_comment_delimiters(text);
    index_words(cx, start, node_line(node), CONTEXT_COMMENT);
}

static void handle_heredoc(TSNode node, const Ctx *cx, const char *parent, const char *ns) {
    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_symbol(child) == ruby_symbols.heredoc_content) {
            char text[CLEANED_WORD_BUFFER];
            node_text(cx, child, text, sizeof(text));
            index_words(cx, text, node_line(child), CONTEXT_STRING);
        }
    }
    /* Interpolations inside the heredoc. */
    process_children(node, cx, parent, ns);
}

/* ── Dispatch ──────────────────────────────────────────────────────────────── */

static void visit_node(TSNode node, const Ctx *cx, const char *parent, const char *ns) {
    TSSymbol sym = ts_node_symbol(node);

    if (sym == ruby_symbols.method || sym == ruby_symbols.singleton_method) {
        handle_method(node, cx, parent, ns); return;
    }
    if (sym == ruby_symbols.class_) { handle_class(node, cx, parent, ns); return; }
    if (sym == ruby_symbols.module) { handle_module(node, cx, parent, ns); return; }
    if (sym == ruby_symbols.singleton_class) { handle_singleton_class(node, cx, parent, ns); return; }
    if (sym == ruby_symbols.assignment || sym == ruby_symbols.operator_assignment) {
        handle_assignment(node, cx, parent, ns); return;
    }
    if (sym == ruby_symbols.call) { handle_call(node, cx, parent, ns); return; }
    if (sym == ruby_symbols.lambda) { handle_lambda(node, cx, parent, ns); return; }
    if (sym == ruby_symbols.do_block || sym == ruby_symbols.block) {
        handle_block(node, cx, parent, ns); return;
    }
    if (sym == ruby_symbols.string) { handle_string(node, cx, parent, ns); return; }
    if (sym == ruby_symbols.comment) { handle_comment(node, cx, parent, ns); return; }
    if (sym == ruby_symbols.heredoc_body) { handle_heredoc(node, cx, parent, ns); return; }

    process_children(node, cx, parent, ns);
}

static void process_children(TSNode node, const Ctx *cx, const char *parent, const char *ns) {
    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        visit_node(ts_node_child(node, i), cx, parent, ns);
    }
}

/* ── Symbol table init ─────────────────────────────────────────────────────── */

static void init_ruby_symbols(TSLanguage *language) {
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

    ruby_symbols.identifier = ts_language_symbol_for_name(language, "identifier", 10, true);
    ruby_symbols.constant = ts_language_symbol_for_name(language, "constant", 8, true);
    ruby_symbols.instance_variable = ts_language_symbol_for_name(language, "instance_variable", 17, true);
    ruby_symbols.class_variable = ts_language_symbol_for_name(language, "class_variable", 14, true);
    ruby_symbols.global_variable = ts_language_symbol_for_name(language, "global_variable", 15, true);
    ruby_symbols.scope_resolution = ts_language_symbol_for_name(language, "scope_resolution", 16, true);
    ruby_symbols.simple_symbol = ts_language_symbol_for_name(language, "simple_symbol", 13, true);
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
}

/* ── Public interface ──────────────────────────────────────────────────────── */

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
    TSLanguage *language = tree_sitter_ruby();
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

    Ctx cx = {
        .src = source_code,
        .directory = directory,
        .filename = filename,
        .result = result,
        .filter = parser->filter
    };

    visit_node(ts_tree_root_node(tree), &cx, NULL, NULL);

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
