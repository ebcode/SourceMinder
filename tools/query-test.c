/* Minimal test harness for debugging Tree-Sitter queries */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tree_sitter/api.h>

extern const TSLanguage *tree_sitter_typescript(void);

void test_query(const char *query_string, const char *source_code) {
    printf("\n=== Testing Query ===\n%s\n", query_string);

    /* Parse the source code */
    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_typescript());
    TSTree *tree = ts_parser_parse_string(parser, NULL, source_code, strlen(source_code));
    TSNode root = ts_tree_root_node(tree);

    /* Compile the query */
    uint32_t error_offset;
    TSQueryError error_type;
    TSQuery *query = ts_query_new(
        tree_sitter_typescript(),
        query_string,
        strlen(query_string),
        &error_offset,
        &error_type
    );

    if (error_type != TSQueryErrorNone) {
        printf("❌ Query compilation failed!\n");
        printf("   Error type: %d\n", error_type);
        printf("   Error offset: %u\n", error_offset);
        printf("   Error context: ...%s\n", query_string + (error_offset > 10 ? error_offset - 10 : 0));
        ts_tree_delete(tree);
        ts_parser_delete(parser);
        return;
    }

    printf("✓ Query compiled successfully!\n");

    /* Execute query */
    TSQueryCursor *cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, query, root);

    TSQueryMatch match;
    int match_count = 0;

    while (ts_query_cursor_next_match(cursor, &match)) {
        match_count++;
        printf("  Match %d: %u captures\n", match_count, match.capture_count);

        for (uint16_t i = 0; i < match.capture_count; i++) {
            TSQueryCapture capture = match.captures[i];
            uint32_t length;
            const char *name = ts_query_capture_name_for_id(query, capture.index, &length);

            uint32_t start = ts_node_start_byte(capture.node);
            uint32_t end = ts_node_end_byte(capture.node);
            uint32_t text_len = end - start;

            char text[256];
            if (text_len < sizeof(text)) {
                memcpy(text, source_code + start, text_len);
                text[text_len] = '\0';
            } else {
                strncpy(text, source_code + start, sizeof(text) - 4);
                text[sizeof(text) - 4] = '.';
                text[sizeof(text) - 3] = '.';
                text[sizeof(text) - 2] = '.';
                text[sizeof(text) - 1] = '\0';
            }

            printf("    @%.*s = '%s' (type: %s)\n",
                   (int)length, name, text, ts_node_type(capture.node));
        }
    }

    printf("  Total matches: %d\n", match_count);

    /* Cleanup */
    ts_query_cursor_delete(cursor);
    ts_query_delete(query);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
}

void print_tree_structure(TSNode node, const char *source_code, int depth) {
    if (depth > 5) return; /* Limit depth */

    const char *type = ts_node_type(node);

    /* Print indentation */
    for (int i = 0; i < depth; i++) {
        printf("  ");
    }

    /* Print node type */
    printf("(%s", type);

    /* If it's a leaf node, print its text */
    if (ts_node_child_count(node) == 0) {
        uint32_t start = ts_node_start_byte(node);
        uint32_t end = ts_node_end_byte(node);
        uint32_t len = end - start;
        if (len > 0 && len < 50) {
            char text[51];
            memcpy(text, source_code + start, len);
            text[len] = '\0';
            printf(" \"%s\"", text);
        }
    }
    printf(")\n");

    /* Recursively print children */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        print_tree_structure(child, source_code, depth + 1);
    }
}

void test_all_export_patterns() {
    printf("\n\n========== TESTING ALL EXPORT PATTERNS ==========\n");

    /* Test 1: export class */
    const char *source1 = "export class Foo {}";
    printf("\n--- Source: %s ---\n", source1);
    test_query("(export_statement (class_declaration name: (type_identifier) @name))", source1);

    /* Test 2: export function */
    const char *source2 = "export function bar() {}";
    printf("\n--- Source: %s ---\n", source2);
    test_query("(export_statement (function_declaration name: (identifier) @name))", source2);

    /* Test 3: export interface */
    const char *source3 = "export interface Baz {}";
    printf("\n--- Source: %s ---\n", source3);
    test_query("(export_statement (interface_declaration name: (type_identifier) @name))", source3);

    /* Test 4: export type */
    const char *source4 = "export type Qux = string";
    printf("\n--- Source: %s ---\n", source4);
    test_query("(export_statement (type_alias_declaration name: (type_identifier) @name))", source4);

    /* Test 5: export const */
    const char *source5 = "export const myConst = 1";
    printf("\n--- Source: %s ---\n", source5);
    test_query("(export_statement (lexical_declaration (variable_declarator name: (identifier) @name)))", source5);

    /* Test 6: export { A } */
    const char *source6 = "export { Component }";
    printf("\n--- Source: %s ---\n", source6);
    test_query("(export_statement (export_clause (export_specifier name: (identifier) @name)))", source6);

    /* Test 7: export { A as B } */
    const char *source7 = "export { Component as Comp }";
    printf("\n--- Source: %s ---\n", source7);
    test_query("(export_statement (export_clause (export_specifier alias: (identifier) @alias)))", source7);
}

int main() {
    const char *source = "export class ExportedClass {}";

    printf("Source code: %s\n", source);
    printf("\n=== Tree Structure ===\n");

    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_typescript());
    TSTree *tree = ts_parser_parse_string(parser, NULL, source, strlen(source));
    TSNode root = ts_tree_root_node(tree);

    print_tree_structure(root, source, 0);

    ts_tree_delete(tree);
    ts_parser_delete(parser);

    test_all_export_patterns();

    return 0;
}
