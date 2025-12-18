#include <tree_sitter/api.h>
#include <stdio.h>
int main() {
    printf("Tree-sitter MIN_COMPATIBLE_LANGUAGE_VERSION: %d\n", TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION);
    printf("Tree-sitter LANGUAGE_VERSION: %d\n", TREE_SITTER_LANGUAGE_VERSION);
    return 0;
}
