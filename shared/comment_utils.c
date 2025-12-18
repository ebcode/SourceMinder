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
#include "comment_utils.h"
#include "string_utils.h"
#include "constants.h"
#include <string.h>
#include <ctype.h>

char* strip_comment_delimiters(char *comment_text) {
    if (!comment_text || !comment_text[0]) {
        return comment_text;
    }

    char *text_start = comment_text;

    /* Strip leading comment markers */
    if (text_start[0] == '/' && text_start[1] == '/') {
        text_start += 2;  /* Skip double-slash */
    } else if (text_start[0] == '/' && text_start[1] == '*') {
        text_start += 2;  /* Skip slash-star */
    } else if (text_start[0] == '#') {
        text_start += 1;  /* Skip hash */
    }

    /* Strip trailing star-slash if present */
    size_t len = strnlength(text_start, COMMENT_TEXT_BUFFER);
    if (len >= 2 && text_start[len - 1] == '/' && text_start[len - 2] == '*') {
        text_start[len - 2] = '\0';
    }

    return text_start;
}
