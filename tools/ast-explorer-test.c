#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <tree_sitter/api.h>

extern const TSLanguage *tree_sitter_typescript(void);

void print_tree(TSNode node, const char *source_code, int indent) {
    const char *type = ts_node_type(node);
    TSPoint start = ts_node_start_point(node);
    TSPoint end = ts_node_end_point(node);

    // Print indent
    for (int i = 0; i < indent; i++) printf("  ");

    // Print node type and location
    printf("%s [%d:%d - %d:%d]", type,
           start.row + 1, start.column,
           end.row + 1, end.column);

    // If it's a small node, print its text
    uint32_t start_byte = ts_node_start_byte(node);
    uint32_t end_byte = ts_node_end_byte(node);
    uint32_t length = end_byte - start_byte;

    if (length < 80 && ts_node_child_count(node) == 0) {
        printf(" \"");
        for (uint32_t i = start_byte; i < end_byte && i < start_byte + 80; i++) {
            char c = source_code[i];
            if (c == '\n') printf("\\n");
            else if (c == '\t') printf("\\t");
            else printf("%c", c);
        }
        printf("\"");
    }
    printf("\n");

    // Recurse to children
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        print_tree(child, source_code, indent + 1);
    }
}

int main() {
    // Read Command.ts file
    FILE *fp = fopen("./tmp/Command.ts", "r");
    if (!fp) {
        fprintf(stderr, "Cannot open ./tmp/Command.ts\n");
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *source_code = malloc(file_size + 1);
    if (!source_code) {
        fclose(fp);
        return 1;
    }

    fread(source_code, 1, file_size, fp);
    source_code[file_size] = '\0';
    fclose(fp);

    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_typescript());

    TSTree *tree = ts_parser_parse_string(parser, NULL, source_code, strlen(source_code));
    TSNode root = ts_tree_root_node(tree);

    // Find nodes at specific lines
    printf("=== Searching for line 173: public getOperation() ===\n\n");

    // Traverse to find nodes on line 173
    TSTreeCursor cursor = ts_tree_cursor_new(root);

    do {
        TSNode node = ts_tree_cursor_current_node(&cursor);
        TSPoint start = ts_node_start_point(node);

        if (start.row + 1 == 173 && strcmp(ts_node_type(node), "method_definition") == 0) {
            printf("Found method_definition at line 173:\n");
            print_tree(node, source_code, 0);
            break;
        }
    } while (ts_tree_cursor_goto_next_sibling(&cursor) ||
             (ts_tree_cursor_goto_first_child(&cursor) || ts_tree_cursor_goto_next_sibling(&cursor)));

    ts_tree_cursor_delete(&cursor);

    printf("\n\n=== Searching for line 325: private executeDrag() ===\n\n");

    cursor = ts_tree_cursor_new(root);
    do {
        TSNode node = ts_tree_cursor_current_node(&cursor);
        TSPoint start = ts_node_start_point(node);

        if (start.row + 1 == 325 && strcmp(ts_node_type(node), "method_definition") == 0) {
            printf("Found method_definition at line 325:\n");
            print_tree(node, source_code, 0);
            break;
        }
    } while (ts_tree_cursor_goto_next_sibling(&cursor) ||
             (ts_tree_cursor_goto_first_child(&cursor) || ts_tree_cursor_goto_next_sibling(&cursor)));

    ts_tree_cursor_delete(&cursor);

    printf("\n\n=== Searching for line 328: this.target.getBounds() ===\n\n");

    cursor = ts_tree_cursor_new(root);
    do {
        TSNode node = ts_tree_cursor_current_node(&cursor);
        TSPoint start = ts_node_start_point(node);

        if (start.row + 1 == 328 && strcmp(ts_node_type(node), "call_expression") == 0) {
            printf("Found call_expression at line 328:\n");
            print_tree(node, source_code, 0);
            break;
        }
    } while (ts_tree_cursor_goto_next_sibling(&cursor) ||
             (ts_tree_cursor_goto_first_child(&cursor) || ts_tree_cursor_goto_next_sibling(&cursor)));

    ts_tree_cursor_delete(&cursor);

    ts_tree_delete(tree);
    ts_parser_delete(parser);
    free(source_code);

    return 0;
}
