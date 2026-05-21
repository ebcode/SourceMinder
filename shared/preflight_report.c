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
#include "preflight_report.h"
#include <stdarg.h>
#include <stdio.h>

void preflight_report_add(PreflightReport *report, PreflightSeverity severity,
                          const char *fmt, ...) {
    if (report->count >= PREFLIGHT_MAX_MESSAGES) return;

    PreflightMessage *msg = &report->messages[report->count++];
    msg->severity = severity;

    va_list args;
    va_start(args, fmt);
    vsnprintf(msg->text, PREFLIGHT_MESSAGE_LEN, fmt, args);
    va_end(args);

    if (severity == PF_ERROR) report->error_count++;
}
