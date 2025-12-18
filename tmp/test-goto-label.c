#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int retval = 0;
    char *buffer = NULL;

    buffer = malloc(100);
    if (!buffer) {
        retval = 1;
        goto cleanup;
    }

    printf("Processing...\n");

    if (retval != 0) {
        goto error_handler;
    }

cleanup:
    free(buffer);
    return retval;

error_handler:
    fprintf(stderr, "Error occurred\n");
    goto cleanup;
}
