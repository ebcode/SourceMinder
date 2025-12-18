/*
 * Benchmark: Switch statement dispatch
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

// Switch statement dispatch
static void dispatch_switch(NodeType type) {
    switch (type) {
        case FUNC_DEF:
            handle_func_def();
            break;
        case DECLARATION:
            handle_declaration();
            break;
        case STRUCT_SPEC:
        case UNION_SPEC:
            handle_struct_spec();
            break;
        case TYPE_DEF:
            handle_type_def();
            break;
        case ENUM_SPEC:
            handle_enum_spec();
            break;
        case COMMENT:
            handle_comment();
            break;
        case STRING_LIT:
            handle_string_lit();
            break;
        case PREPROC_DEF:
            handle_preproc_def();
            break;
        case PREPROC_FUNC:
            handle_preproc_func();
            break;
        case PREPROC_INCLUDE:
            handle_preproc_include();
            break;
        case EXPR_STMT:
            handle_expr_stmt();
            break;
        case RETURN_STMT:
            handle_return_stmt();
            break;
        case IF_STMT:
            handle_if_stmt();
            break;
        case WHILE_STMT:
            handle_while_stmt();
            break;
        case DO_STMT:
            handle_do_stmt();
            break;
        case FOR_STMT:
            handle_for_stmt();
            break;
        case SWITCH_STMT:
            handle_switch_stmt();
            break;
        default:
            break;
    }
}

int main(void) {
    // Generate random node types
    srand(42); // Fixed seed for reproducibility (same as other versions)
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
        dispatch_switch(nodes[i]);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    // Calculate elapsed time
    double elapsed = (end.tv_sec - start.tv_sec) +
                    (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("Switch dispatch: %.3f seconds (%.0f Mops/sec)\n",
           elapsed, ITERATIONS / elapsed / 1e6);
    printf("Counter: %lu\n", (unsigned long)g_counter);

    free(nodes);
    return 0;
}
