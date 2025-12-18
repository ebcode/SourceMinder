#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../shared/indexer_main.h"
#include "../shared/constants.h"
#include "../shared/version.h"
#include "ts_language.h"
#include "grammar_version.h"

/* Wrapper for parser_init to match IndexerConfig signature */
static void* parser_init_wrapper(SymbolFilter *filter) {
    TypeScriptParser *parser = malloc(sizeof(TypeScriptParser));
    if (!parser) {
        return NULL;
    }
    if (parser_init(parser, filter) != 0) {
        free(parser);
        return NULL;
    }
    return parser;
}

/* Wrapper for parser_parse_file to match IndexerConfig signature */
static int parser_parse_wrapper(void *parser, const char *filepath, const char *project_root, ParseResult *result) {
    return parser_parse_file((TypeScriptParser*)parser, filepath, project_root, result);
}

/* Wrapper for parser_free to match IndexerConfig signature */
static void parser_free_wrapper(void *parser) {
    if (parser) {
        parser_free((TypeScriptParser*)parser);
        free(parser);
    }
}

/* Wrapper for parser_set_debug to match IndexerConfig signature */
static void parser_set_debug_wrapper(void *parser, int debug) {
    parser_set_debug((TypeScriptParser*)parser, debug);
}

int main(int argc, char *argv[]) {
    /* Check for --version flag */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            print_version_with_grammar(GRAMMAR_NAME, GRAMMAR_VERSION);
            return 0;
        }
    }

    IndexerConfig config = {
        .name = "index-ts",
        .data_dir = "typescript/" CONFIG_DIR,
        .parser_init = parser_init_wrapper,
        .parser_parse = parser_parse_wrapper,
        .parser_free = parser_free_wrapper,
        .parser_set_debug = parser_set_debug_wrapper
    };

    return indexer_main(argc, argv, &config);
}
