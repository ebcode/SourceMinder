// Test various expression patterns
#include <stdio.h>

typedef struct {
    int x;
    int y;
} Point;

typedef struct {
    Point *location;
    int count;
} Container;

int add(int a, int b) {
    return a + b;
}

void test_expressions() {
    // Line 1: Simple variable declaration with function call
    int ret = add(5, 10);
    
    // Line 2: Field access chain
    Container container;
    container.location->x = 100;
    
    // Line 3: Nested function calls with field access
    Point p;
    printf("Value: %d", add(p.x, container.count));
    
    // Line 4: Array subscript with field access
    Container items[10];
    items[0].location->y = items[container.count].location->x;
}
