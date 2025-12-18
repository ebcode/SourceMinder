#ifndef TS_LANGUAGE_H
#define TS_LANGUAGE_H

#include "../shared/database.h"
#include "../shared/filter.h"
#include "../shared/constants.h"
#include "../shared/parse_result.h"

typedef struct {
    SymbolFilter *filter;
    int debug;
} TypeScriptParser;

/* Initialize parser with a filter */
int parser_init(TypeScriptParser *parser, SymbolFilter *filter);

/* Parse a TypeScript file and extract symbols */
int parser_parse_file(TypeScriptParser *parser, const char *filepath, const char *project_root, ParseResult *result);

/* Set debug mode */
void parser_set_debug(TypeScriptParser *parser, int debug);

/* Free parser resources */
void parser_free(TypeScriptParser *parser);

#endif
