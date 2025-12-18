#!/bin/bash
# Add INT_COLUMN macro before each #include "shared/column_schema.def" that doesn't have it

awk '
BEGIN { in_macro = 0; macro_lines = ""; has_int_column = 0; }

/^#define COLUMN/ {
    in_macro = 1
    macro_lines = $0 "\n"
    has_int_column = 0
    next
}

in_macro && /^#define INT_COLUMN/ {
    has_int_column = 1
    macro_lines = macro_lines $0 "\n"
    next
}

in_macro && /#include "shared\/column_schema.def"/ {
    printf "%s", macro_lines
    if (!has_int_column) {
        # Add INT_COLUMN macro (same as COLUMN macro but with INT_COLUMN name)
        gsub(/#define COLUMN/, "#define INT_COLUMN", macro_lines)
        printf "%s", macro_lines
    }
    print
    in_macro = 0
    macro_lines = ""
    next
}

in_macro {
    macro_lines = macro_lines $0 "\n"
    next
}

!in_macro { print }
' "$1"
