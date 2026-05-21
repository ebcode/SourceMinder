/* SourceMinder
 * Copyright 2025 Eli Bird
 *
 * This file is part of SourceMinder.
 *
 * SourceMinder is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at
 *  your option) any later version.
 *
 * SourceMinder is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with SourceMinder. If not, see <https://www.gnu.org/licenses/>.
 */
#include "interactive.h"
#include <stdio.h>

int prompt_yes_no(const char *question) {
    printf("%s [y/N]: ", question);
    fflush(stdout);

    int ch = fgetc(stdin);

    /* Drain the rest of the line so subsequent reads aren't affected */
    int drain;
    while ((drain = fgetc(stdin)) != '\n' && drain != EOF)
        ;

    return (ch == 'y' || ch == 'Y') ? 1 : 0;
}
