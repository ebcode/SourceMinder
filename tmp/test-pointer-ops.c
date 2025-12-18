/* test-pointer-ops.c - Example showing pointer operations */

#include <stdio.h>
#include <stdlib.h>

struct Node {
    int value;
    struct Node *next;
};

int main() {
    /* Variable declarations */
    int num = 42;
    int *ptr = &num;              // Line 14: address-of (&num)
    int value_y = *ptr;           // Line 15: dereference (*ptr)

    /* Pointer arithmetic */
    ptr++;                        // Line 18: pointer arithmetic (ptr++)
    ptr += 5;                     // Line 19: pointer arithmetic (ptr += 5)

    /* Array and pointer */
    int arr[10];
    int *arr_p = &arr[0];         // Line 23: address-of (&arr[0])
    int val = *arr_p;             // Line 24: dereference (*arr_p)

    /* Struct pointer operations */
    struct Node node;
    struct Node *node_ptr = &node;     // Line 28: address-of (&node)
    node_ptr->value = 100;              // Line 29: arrow (node_ptr->value)
    node.value = 200;                   // Line 30: dot (node.value)

    int value_z = (*node_ptr).value;    // Line 32: dereference then dot

    /* Sizeof operations */
    size_t size1 = sizeof(int);         // Line 35: sizeof type
    size_t size2 = sizeof(num);         // Line 36: sizeof variable
    size_t size3 = sizeof(*ptr);        // Line 37: sizeof dereference

    /* Type casts */
    void *vptr = (void *)ptr;           // Line 40: cast to void*
    int *iptr = (int *)vptr;            // Line 41: cast from void*
    long addr = (long)ptr;              // Line 42: cast pointer to long

    /* Complex expressions */
    int *arr_ptr = &arr[5];             // Line 45: address-of subscript
    int result = *arr_ptr + *ptr;       // Line 46: multiple dereferences

    return 0;
}
