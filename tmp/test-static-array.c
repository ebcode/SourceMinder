/* Test file for static array declarations */

typedef struct {
    const char *name;
    int value;
} Entry;

/* Test 1: Static array with initializer */
static Entry test_table[] = {
    {"first", 1},
    {"second", 2},
    {NULL, 0}
};

/* Test 2: Regular static variable */
static int simple_var = 42;

/* Test 3: Static array without initializer */
static char buffer[256];

/* Test 4: Non-static array */
Entry public_table[] = {
    {"public", 99}
};

int main() {
    return 0;
}
