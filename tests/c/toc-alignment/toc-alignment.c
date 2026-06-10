/*
 * Test fixture: TOC dot-leader alignment (qi % -f ... --toc)
 *
 * Exercises print_section() in shared/toc.c. The FUNCTIONS section below mixes
 * symbol widths to cover every alignment branch:
 *   - very short (2 chars)          -> longest dot run within the aligned column
 *   - mid length (12 chars)         -> partial dot run
 *   - exactly TOC_ALIGN_MAX_COLUMN  -> aligns with a single-dot gap
 *   - wider than the column         -> overflows: no dot leader at all
 *
 * The widest-aligned symbol (40 chars) sets the shared column; the 49-char
 * overflow symbol must NOT drag the column out or emit a wall of dots.
 */

#include <stdio.h>

/* 2 chars: shortest symbol, longest aligned dot run */
void fn(void) {}

/* 12 chars: mid-length symbol */
int do_one_thing(void) { return 0; }

/* 40 chars: exactly TOC_ALIGN_MAX_COLUMN, sets the aligned column */
void function_name_at_exactly_forty_chars_wid(void) {}

/* 49 chars: overflow — prints symbol + line number, no dot leader */
void function_name_that_overflows_the_alignment_column(void) {}

/* TYPES section: short vs overflow */
typedef struct Pt { int x, y; } Pt;
typedef struct struct_name_at_exactly_forty_characters_ { int v; }
    struct_name_at_exactly_forty_characters_;

/* MACROS section: short vs overflow */
#define ON 1
#define MACRO_NAME_OVERFLOWING_THE_ALIGNMENT_COLUMN_LIMIT 2

int main(void) {
    fn();
    return do_one_thing();
}
