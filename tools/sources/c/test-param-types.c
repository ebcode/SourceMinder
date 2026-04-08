/* Test file for parameter type extraction */

typedef struct {
    int count;
} FileFilterList;

/* Test various parameter type patterns */
void simple_type(int x, char y);

void pointer_type(int *ptr, char *str);

void custom_type(FileFilterList *file_filter);

void const_pointer(const char *message);

void double_pointer(char **argv);

void array_param(int arr[10]);

void mixed_params(int count, FileFilterList *filter, const char *name);
