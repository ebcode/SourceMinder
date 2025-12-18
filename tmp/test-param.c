#include <stdio.h>

void my_function(const char *db_file) {
    int x = 5;
    printf("%s\n", db_file);
    stat(db_file, &x);
}
