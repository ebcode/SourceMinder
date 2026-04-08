// Test file for Phase 2 C indexer
#include <stdio.h>
#include <stdlib.h>

// Global variables
int global_count = 0;
static char *static_buffer;

// Struct definition
struct Point {
    int x;
    int y;
};

// Typedef with anonymous struct
typedef struct {
    char *name;
    int age;
    struct Point *location;
} Person;

// Enum definition
enum Color {
    RED,
    GREEN,
    BLUE,
    YELLOW
};

// Function with simple parameters
int add(int a, int b) {
    return a + b;
}

// Function with pointer parameters
void process_data(char *str, int *ptr, Person *person) {
    printf("Processing data");
}

// Function with multiple variables
void complex_function() {
    int x = 10, y = 20, z = 30;
    char *message = "hello world";
    Person user;
    user.age = 25;
}

// Union definition
union Data {
    int integer;
    float decimal;
    char byte;
};

// Another struct with pointer fields
struct Node {
    int value;
    struct Node *next;
    struct Node *prev;
};
