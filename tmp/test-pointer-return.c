// Test file to debug pointer return type indexing

void normal_function(void) {
    // Regular function
}

int return_int(void) {
    return 42;
}

char *return_char_pointer(void) {
    return "test";
}

int *return_int_pointer(void) {
    static int x = 5;
    return &x;
}
