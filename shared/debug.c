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
#include "debug.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

void f_debug_printf(const char *file, uint32_t line, const char *fmt, ...) {
    va_list args;
    fprintf(stderr, "%s:%u ", file, line);
    va_start(args, fmt);
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
    vfprintf(stderr, fmt, args);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    va_end(args);
    fprintf(stderr, "\n");
}
