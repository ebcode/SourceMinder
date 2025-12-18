#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

    if (length < 50 && ts_node_child_count(node) == 0) {
        printf(" \"");
        for (uint32_t i = start_byte; i < end_byte && i < start_byte + 50; i++) {
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

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.ts>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "r");
    if (!f) {
        fprintf(stderr, "Cannot open file: %s\n", argv[1]);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *source_code = malloc(size + 1);
    size_t bytes_read = fread(source_code, 1, size, f);
    source_code[bytes_read] = '\0';
    fclose(f);

    TSParser *parser = ts_parser_new();
    if (!ts_parser_set_language(parser, tree_sitter_typescript())) {
        fprintf(stderr, "ERROR: Failed to set TypeScript language\n");
        free(source_code);
        ts_parser_delete(parser);
        return 1;
    }

    TSTree *tree = ts_parser_parse_string(parser, NULL, source_code, size);
    if (!tree) {
        fprintf(stderr, "ERROR: Failed to parse file\n");
        free(source_code);
        ts_parser_delete(parser);
        return 1;
    }

    TSNode root = ts_tree_root_node(tree);

    printf("=== TypeScript Language AST Structure ===\n\n");
    print_tree(root, source_code, 0);

    ts_tree_delete(tree);
    ts_parser_delete(parser);
    free(source_code);

    return 0;
}
