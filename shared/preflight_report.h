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
#ifndef PREFLIGHT_REPORT_H
#define PREFLIGHT_REPORT_H

#define PREFLIGHT_MAX_MESSAGES 64
#define PREFLIGHT_MESSAGE_LEN  512

typedef enum {
    PF_OK = 0,
    PF_WARNING,
    PF_ERROR,
    PF_HINT   /* informational follow-up, e.g. fix instructions */
} PreflightSeverity;

typedef struct {
    PreflightSeverity severity;
    char text[PREFLIGHT_MESSAGE_LEN];
} PreflightMessage;

typedef struct {
    PreflightMessage messages[PREFLIGHT_MAX_MESSAGES];
    int count;
    int error_count;
    char suggested_grammar_tag[256]; /* set by ABI check when a compatible downgrade tag is found */
} PreflightReport;

/* Append a formatted message to the report.  Silently drops messages beyond
 * PREFLIGHT_MAX_MESSAGES.  Increments error_count for PF_ERROR severity. */
void preflight_report_add(PreflightReport *report, PreflightSeverity severity,
                          const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 3, 4)))
#endif
;

#endif /* PREFLIGHT_REPORT_H */
