/* Minimal TS stub for compiling shared/ modules without real tree-sitter.
 * Only the shared functions that DON'T use tree-sitter are called from
 * the WASM module; the tree-sitter-dependent functions compile but are
 * never linked. */
#ifndef TREE_SITTER_API_H_
#define TREE_SITTER_API_H_
#include <stdint.h>

typedef struct { int _dummy[4]; } TSNode;

typedef struct {
    uint32_t row;
    uint32_t column;
} TSPoint;

static inline uint32_t ts_node_start_byte(TSNode n)  { (void)n; return 0; }
static inline uint32_t ts_node_end_byte(TSNode n)    { (void)n; return 0; }
static inline TSPoint ts_node_start_point(TSNode n)  { (void)n; TSPoint p = {0,0}; return p; }
static inline TSPoint ts_node_end_point(TSNode n)    { (void)n; TSPoint p = {0,0}; return p; }
static inline const char *ts_node_type(TSNode n)      { (void)n; return ""; }

#endif
