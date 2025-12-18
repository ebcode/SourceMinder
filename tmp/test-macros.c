/* test-macros.c - Example showing preprocessor macro usage */

#include <stdio.h>

/* Simple constant macros */
#define MAX_SIZE 1024
#define MIN_SIZE 16
#define BUFFER_SIZE 512

/* Function-like macros */
#define MIN(aa, bb) ((aa) < (bb) ? (aa) : (bb))
#define MAX(aa, bb) ((aa) > (bb) ? (aa) : (bb))
#define SQUARE(xx) ((xx) * (xx))

/* Conditional compilation */
#ifdef DEBUG
#define LOG(msg) printf("[DEBUG] %s\n", msg)
#else
#define LOG(msg)
#endif

/* More complex conditional */
#ifndef PRODUCTION
#define ENABLE_LOGGING 1
#define VERBOSE_MODE 1
#else
#define ENABLE_LOGGING 0
#define VERBOSE_MODE 0
#endif

/* Multi-line macro */
#define INIT_ARRAY(arr, size) \
    do { \
        for (int ii = 0; ii < size; ii++) { \
            arr[ii] = 0; \
        } \
    } while(0)

int main() {
    /* Macro usages - constant macros */
    int buffer[MAX_SIZE];           // Line 40: MAX_SIZE usage
    int small[MIN_SIZE];            // Line 41: MIN_SIZE usage
    int medium[BUFFER_SIZE];        // Line 42: BUFFER_SIZE usage

    /* Macro usages - function-like macros */
    int num1 = 10;
    int num2 = 20;
    int smaller = MIN(num1, num2);  // Line 47: MIN usage
    int larger = MAX(num1, num2);   // Line 48: MAX usage
    int squared = SQUARE(5);        // Line 49: SQUARE usage

    /* Conditional macro usage */
    LOG("Starting program");        // Line 52: LOG usage

    /* Multi-line macro usage */
    INIT_ARRAY(buffer, MAX_SIZE);   // Line 55: INIT_ARRAY and MAX_SIZE usage

    /* Nested macro usage */
    int result = MIN(MAX(num1, 15), num2);  // Line 58: MIN and MAX usage

    return 0;
}
