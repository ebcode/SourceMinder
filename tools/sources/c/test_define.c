// Test file for #define directives
#define MAX_SIZE 100
#define MIN_SIZE 10
#define PI 3.14159

// Function-like macros
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define SQUARE(x) ((x)*(x))

// Multi-line define
#define COMPLEX_MACRO(x, y) \
    do { \
        int temp = x; \
        x = y; \
        y = temp; \
    } while(0)

int main() {
    int size = MAX_SIZE;
    return 0;
}
