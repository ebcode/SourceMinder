/* Test file for definition vs usage detection */

/* Type definition (typedef) */
typedef struct {
    int x;
    int y;
} Point;

/* Function definition */
void init_point(Point *p, int x, int y) {
    p->x = x;  /* Field usage */
    p->y = y;  /* Field usage */
}

/* Variable definition (global) */
static int counter = 0;

/* Function definition with type usage in parameter */
int get_x(Point p) {  /* Type usage: Point */
    return p.x;       /* Variable usage: p, field usage: x */
}

/* Main function - definitions and usages */
int main() {
    Point p1;              /* Variable definition, type usage */
    Point p2 = {0, 0};     /* Variable definition, type usage */

    init_point(&p1, 5, 10); /* Function call (usage), variable usage */
    int result = get_x(p1); /* Function call (usage), variable definitions & usages */

    counter++;              /* Variable usage */

    return result;          /* Variable usage */
}
