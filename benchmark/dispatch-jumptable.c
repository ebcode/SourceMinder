/*
 * Benchmark: Jump table dispatch (proposed optimization)
 * Simulates visit_node() pattern with 18 node types
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

#define NUM_NODE_TYPES 18
#define ITERATIONS 10000000

// Simulate node type symbols (like TSSymbol)
typedef uint16_t NodeType;

// Simulate node type constants
enum {
    FUNC_DEF = 0,
    DECLARATION,
    STRUCT_SPEC,
    UNION_SPEC,
    TYPE_DEF,
    ENUM_SPEC,
    COMMENT,
    STRING_LIT,
    PREPROC_DEF,
    PREPROC_FUNC,
    PREPROC_INCLUDE,
    EXPR_STMT,
    RETURN_STMT,
    IF_STMT,
    WHILE_STMT,
    DO_STMT,
    FOR_STMT,
    SWITCH_STMT
};

// Global counter to prevent optimization
volatile uint64_t g_counter = 0;

// Handler functions (simplified)
static inline void handle_func_def(void) { g_counter++; }
static inline void handle_declaration(void) { g_counter++; }
static inline void handle_struct_spec(void) { g_counter++; }
static inline void handle_union_spec(void) { g_counter++; }
static inline void handle_type_def(void) { g_counter++; }
static inline void handle_enum_spec(void) { g_counter++; }
static inline void handle_comment(void) { g_counter++; }
static inline void handle_string_lit(void) { g_counter++; }
static inline void handle_preproc_def(void) { g_counter++; }
static inline void handle_preproc_func(void) { g_counter++; }
static inline void handle_preproc_include(void) { g_counter++; }
static inline void handle_expr_stmt(void) { g_counter++; }
static inline void handle_return_stmt(void) { g_counter++; }
static inline void handle_if_stmt(void) { g_counter++; }
static inline void handle_while_stmt(void) { g_counter++; }
static inline void handle_do_stmt(void) { g_counter++; }
static inline void handle_for_stmt(void) { g_counter++; }
static inline void handle_switch_stmt(void) { g_counter++; }

// Function pointer type
typedef void (*HandlerFunc)(void);

// Jump table (initialized at startup)
static HandlerFunc handler_table[NUM_NODE_TYPES];

// Initialize jump table
static void init_jump_table(void) {
    handler_table[FUNC_DEF] = handle_func_def;
    handler_table[DECLARATION] = handle_declaration;
    handler_table[STRUCT_SPEC] = handle_struct_spec;
    handler_table[UNION_SPEC] = handle_union_spec;
    handler_table[TYPE_DEF] = handle_type_def;
    handler_table[ENUM_SPEC] = handle_enum_spec;
    handler_table[COMMENT] = handle_comment;
    handler_table[STRING_LIT] = handle_string_lit;
    handler_table[PREPROC_DEF] = handle_preproc_def;
    handler_table[PREPROC_FUNC] = handle_preproc_func;
    handler_table[PREPROC_INCLUDE] = handle_preproc_include;
    handler_table[EXPR_STMT] = handle_expr_stmt;
    handler_table[RETURN_STMT] = handle_return_stmt;
    handler_table[IF_STMT] = handle_if_stmt;
    handler_table[WHILE_STMT] = handle_while_stmt;
    handler_table[DO_STMT] = handle_do_stmt;
    handler_table[FOR_STMT] = handle_for_stmt;
    handler_table[SWITCH_STMT] = handle_switch_stmt;
}

// Jump table dispatch (proposed implementation)
static inline void dispatch_jumptable(NodeType type) {
    if (type < NUM_NODE_TYPES && handler_table[type]) {
        handler_table[type]();
    }
}

int main(void) {
    // Initialize jump table
    init_jump_table();

    // Generate random node types
    srand(42); // Fixed seed for reproducibility (same as if-else version)
    NodeType *nodes = malloc(ITERATIONS * sizeof(NodeType));
    if (!nodes) {
        fprintf(stderr, "Failed to allocate memory\n");
        return 1;
    }

    for (int i = 0; i < ITERATIONS; i++) {
        nodes[i] = rand() % NUM_NODE_TYPES;
    }

    // Benchmark
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < ITERATIONS; i++) {
        dispatch_jumptable(nodes[i]);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    // Calculate elapsed time
    double elapsed = (end.tv_sec - start.tv_sec) +
                    (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("Jump table dispatch: %.3f seconds (%.0f Mops/sec)\n",
           elapsed, ITERATIONS / elapsed / 1e6);
    printf("Counter: %lu\n", (unsigned long)g_counter);

    free(nodes);
    return 0;
}
