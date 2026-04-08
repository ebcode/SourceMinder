/* Test designated initializers */

typedef struct {
    char *name;
    char *definition;
    int value;
} TestStruct;

void test_function(int is_definition) {
    /* This pattern should show designated initializers */
    TestStruct ext = {
        .name = "test",
        .definition = is_definition ? "1" : "0",
        .value = 42
    };
}
