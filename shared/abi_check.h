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
#ifndef ABI_CHECK_H
#define ABI_CHECK_H

#include "preflight_report.h"

/* Opaque function pointer — caller casts their tree_sitter_*() function to this.
 * The cast is resolved inside abi_check.c which includes <tree_sitter/api.h>.
 */
typedef const void *(*GetLanguageFunc)(void);

/* Check tree-sitter ABI compatibility between the compiled grammar and the
 * installed library.  Appends results to report.  With troubleshoot=1 also
 * shells out for pkg-config/ldd/otool diagnostics.
 *
 * Returns 0 if compatible, -1 on mismatch.
 */
int check_abi_version(GetLanguageFunc get_language, const char *lang_name,
                      const char *grammar_dir,
                      int verbose, int troubleshoot, PreflightReport *report);

/* Offer to run 'git checkout <tag>' interactively after a mismatch.
 * Only call when STDIN_IS_TTY() is true and report->suggested_grammar_tag is set. */
void offer_grammar_downgrade(const char *grammar_dir,
                             const char *suggested_tag);

#endif /* ABI_CHECK_H */
