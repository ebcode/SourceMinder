/* Test file for argument-to-call indexing */
#include <stdio.h>

typedef struct {
    int x;
    int y;
} Point;

void process_data(int value, char *buffer, Point *point) {
    printf("Processing: %d\n", value);
}

void init_system(void) {
    printf("System initialized\n");
}

int main(void) {
    int my_value = 42;
    char my_buffer[256];
    Point my_point = {10, 20};

    /* Simple identifier arguments */
    process_data(my_value, my_buffer, &my_point);

    /* Mixed: identifier + literal + expression */
    process_data(my_value, "literal string", &my_point);

    /* No arguments */
    init_system();

    /* Nested call */
    printf("Value: %d\n", my_value);

    return 0;
}
