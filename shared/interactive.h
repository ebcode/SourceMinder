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
#ifndef INTERACTIVE_H
#define INTERACTIVE_H

/* Cross-platform isatty check.
 * MSYS2/MinGW defines __MINGW32__ or __MINGW64__ and provides POSIX isatty().
 * Only native MSVC (_WIN32 without MinGW) needs the _isatty/_fileno form. */
#if defined(_WIN32) && !defined(__MINGW32__) && !defined(__MINGW64__) && !defined(__CYGWIN__)
#  include <io.h>
#  include <stdio.h>
#  define STDIN_IS_TTY() (_isatty(_fileno(stdin)))
#else
#  include <unistd.h>
#  define STDIN_IS_TTY() (isatty(STDIN_FILENO))
#endif

/* Prompt the user with question and a [y/N] suffix.  Reads one character.
 * Accepts only 'y' or 'Y' as yes; everything else (including bare Enter)
 * is treated as no.  Returns 1 for yes, 0 for no.
 *
 * Only call this when STDIN_IS_TTY() is true. */
int prompt_yes_no(const char *question);

#endif /* INTERACTIVE_H */
