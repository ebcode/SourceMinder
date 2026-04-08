#include <string.h>

void test_function(const char *input) {
    // Test strcmp in if condition
    if (strcmp(input, "hello") == 0) {
        // do something
    } else if (strcmp(input, "world") == 0 ||
               strcmp(input, "foo") == 0) {
        // do something else
    }
}
