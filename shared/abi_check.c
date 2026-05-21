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
#include "abi_check.h"
#include "interactive.h"
#include <tree_sitter/api.h>
#include <stdio.h>
#include <string.h>

#if defined(__APPLE__)
#  include <mach-o/dyld.h>
#  include <stdlib.h>
#elif defined(_WIN32)
#  include <windows.h>
#else
#  include <unistd.h>
#endif

/* ── Resolve path to this executable ────────────────────────────────────── */

static int get_exe_path(char *buf, size_t size) {
#if defined(__APPLE__)
    uint32_t sz = (uint32_t)size;
    if (_NSGetExecutablePath(buf, &sz) != 0) return -1;
    char *resolved = realpath(buf, NULL);
    if (!resolved) return -1;
    strncpy(buf, resolved, size - 1);
    buf[size - 1] = '\0';
    free(resolved);
    return 0;
#elif defined(_WIN32)
    DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)size);
    return (n == 0 || n >= (DWORD)size) ? -1 : 0;
#else
    ssize_t n = readlink("/proc/self/exe", buf, size - 1);
    if (n < 0) return -1;
    buf[n] = '\0';
    return 0;
#endif
}

/* Strip trailing newline in-place */
static void chomp(char *s) {
    size_t n = strlen(s);
    if (n > 0 && s[n - 1] == '\n') s[n - 1] = '\0';
}

/* Run cmd via popen; add each output line as a PF_HINT.  Falls back to a
 * single PF_HINT with fallback_msg if popen fails or produces no output. */
static void popen_to_report(PreflightReport *report, const char *cmd,
                            const char *fallback_msg) {
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        preflight_report_add(report, PF_HINT, "%s", fallback_msg);
        return;
    }
    char buf[512];
    int got = 0;
    while (fgets(buf, sizeof(buf), fp)) {
        chomp(buf);
        if (buf[0]) { preflight_report_add(report, PF_HINT, "%s", buf); got = 1; }
    }
    pclose(fp);
    if (!got) preflight_report_add(report, PF_HINT, "%s", fallback_msg);
}

/* ── Troubleshoot helpers ────────────────────────────────────────────────── */

static void report_installed_version(PreflightReport *report) {
#if defined(_WIN32)
    preflight_report_add(report, PF_HINT, "Installed tree-sitter (pacman):");
    popen_to_report(report,
        "pacman -Q mingw-w64-ucrt-x86_64-tree-sitter 2>&1",
        "(not found — try: pacman -Q mingw-w64-ucrt-x86_64-tree-sitter)");
#else
    preflight_report_add(report, PF_HINT, "Installed tree-sitter (pkg-config):");
    FILE *fp = popen("pkg-config --modversion tree-sitter 2>&1", "r");
    int got = 0;
    if (fp) {
        char buf[256];
        while (fgets(buf, sizeof(buf), fp)) {
            chomp(buf);
            if (buf[0]) { preflight_report_add(report, PF_HINT, "  %s", buf); got = 1; }
        }
        pclose(fp);
    }
    if (!got) {
#  if defined(__APPLE__)
        preflight_report_add(report, PF_HINT,
            "  (pkg-config found nothing — trying brew)");
        popen_to_report(report,
            "brew list --versions tree-sitter 2>&1",
            "  (not found via brew either)");
#  else
        preflight_report_add(report, PF_HINT,
            "  (pkg-config unavailable or tree-sitter not registered)");
#  endif
    }
#endif
}

static void report_runtime_linkage(PreflightReport *report) {
    char exe[4096];
    char cmd[4096 + 64];

    if (get_exe_path(exe, sizeof(exe)) != 0) {
        preflight_report_add(report, PF_HINT,
            "Runtime linkage: (could not determine executable path)");
        return;
    }

#if defined(__APPLE__)
    preflight_report_add(report, PF_HINT, "Runtime linkage (otool -L):");
    snprintf(cmd, sizeof(cmd), "otool -L \"%s\" 2>&1 | grep tree-sitter", exe);
#else
    preflight_report_add(report, PF_HINT, "Runtime linkage (ldd):");
    snprintf(cmd, sizeof(cmd), "ldd \"%s\" 2>&1 | grep tree-sitter", exe);
#endif

    popen_to_report(report, cmd, "  (no tree-sitter entry found)");
}

/* ── Compatible grammar tag search ──────────────────────────────────────── */

#define MAX_TAGS_TO_SCAN 20

/* Scan grammar_dir's git tags newest-first for the most recent tag whose
 * parser.c LANGUAGE_VERSION falls within the library's supported ABI range.
 * Writes the tag name into tag_out on success.  Returns 1 if found, 0 if not. */
static int find_compatible_grammar_tag(const char *grammar_dir,
                                       char *tag_out, size_t tag_size) {
    char cmd[4096];
    char tag[256];
    int count = 0;

    snprintf(cmd, sizeof(cmd),
             "git -C \"%s\" tag --sort=-version:refname 2>/dev/null", grammar_dir);
    FILE *tags = popen(cmd, "r");
    if (!tags) return 0;

    while (fgets(tag, sizeof(tag), tags) && count < MAX_TAGS_TO_SCAN) {
        chomp(tag);
        if (tag[0] == '\0') continue;
        count++;

        char ver_cmd[4096];
        snprintf(ver_cmd, sizeof(ver_cmd),
            "git -C \"%s\" show \"%s\":src/parser.c 2>/dev/null"
            " | grep -m1 \"^#define LANGUAGE_VERSION\"",
            grammar_dir, tag);

        FILE *fp = popen(ver_cmd, "r");
        if (!fp) continue;

        char line[256];
        int matched = 0;
        if (fgets(line, sizeof(line), fp)) {
            int ver = 0;
            if (sscanf(line, "#define LANGUAGE_VERSION %d", &ver) == 1 &&
                ver >= TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION &&
                ver <= TREE_SITTER_LANGUAGE_VERSION) {
                strncpy(tag_out, tag, tag_size - 1);
                tag_out[tag_size - 1] = '\0';
                matched = 1;
            }
        }
        pclose(fp);

        if (matched) {
            pclose(tags);
            return 1;
        }
    }

    pclose(tags);
    return 0;
}

/* ── Public interface ────────────────────────────────────────────────────── */

int check_abi_version(GetLanguageFunc get_language, const char *lang_name,
                      const char *grammar_dir,
                      int verbose, int troubleshoot, PreflightReport *report) {
    const TSLanguage *lang = (const TSLanguage *)get_language();
    uint32_t grammar_abi = ts_language_abi_version(lang);
    int abi_ok = (grammar_abi >= (uint32_t)TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION &&
                  grammar_abi <= (uint32_t)TREE_SITTER_LANGUAGE_VERSION);

    if (abi_ok) {
        preflight_report_add(report, PF_OK,
            "%s grammar ABI %u compatible (library supports %d-%d)",
            lang_name, grammar_abi,
            TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION,
            TREE_SITTER_LANGUAGE_VERSION);
    } else {
        preflight_report_add(report, PF_ERROR,
            "%s grammar ABI %u incompatible (library supports %d-%d)",
            lang_name, grammar_abi,
            TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION,
            TREE_SITTER_LANGUAGE_VERSION);
        preflight_report_add(report, PF_HINT,
            "Run: %s --troubleshoot  for diagnosis", lang_name);
    }

    if (troubleshoot) {
        preflight_report_add(report, PF_HINT, "-- tree-sitter environment --");
        report_installed_version(report);
        report_runtime_linkage(report);
        if (!abi_ok) {
            preflight_report_add(report, PF_HINT,
                "Option 1 — upgrade your library to one supporting ABI %u:", grammar_abi);
            preflight_report_add(report, PF_HINT,
                "  See docs/TROUBLESHOOTING.md for step-by-step instructions");

            if (grammar_dir) {
                char tag[256];
                preflight_report_add(report, PF_HINT,
                    "Option 2 — downgrade grammar to match your library"
                    " (checking %s tags):", grammar_dir);
                if (find_compatible_grammar_tag(grammar_dir, tag, sizeof(tag))) {
                    strncpy(report->suggested_grammar_tag, tag,
                            sizeof(report->suggested_grammar_tag) - 1);
                    report->suggested_grammar_tag[
                        sizeof(report->suggested_grammar_tag) - 1] = '\0';
                    preflight_report_add(report, PF_HINT,
                        "  Compatible tag found: %s", tag);
                    preflight_report_add(report, PF_HINT,
                        "  (note: older grammar may have reduced parse coverage)");
                } else {
                    preflight_report_add(report, PF_HINT,
                        "  No compatible tag found in last %d tags",
                        MAX_TAGS_TO_SCAN);
                }
            }
        }
    }

    (void)verbose; /* verbose controls output in preflight_validation_end, not here */

    return abi_ok ? 0 : -1;
}

/* ── Interactive grammar downgrade ──────────────────────────────────────── */

void offer_grammar_downgrade(const char *grammar_dir, const char *suggested_tag) {
    printf("\nFound compatible grammar tag: %s\n", suggested_tag);

    char question[512];
    snprintf(question, sizeof(question),
             "Run: git -C %s checkout %s", grammar_dir, suggested_tag);

    if (!prompt_yes_no(question)) {
        printf("Skipped.\n");
        return;
    }

    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "git -C \"%s\" checkout \"%s\" 2>&1", grammar_dir, suggested_tag);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        fprintf(stderr, "  Failed to run git checkout\n");
        return;
    }

    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) printf("  %s", buf);
    int status = pclose(fp);

    if (status == 0) {
        printf("\n  Checked out %s successfully.\n", suggested_tag);
        printf("\nNext steps:\n");
        printf("  1. cd %s && tree-sitter generate\n", grammar_dir);
        printf("  2. Return to the SourceMinder directory and run: make\n");
    } else {
        fprintf(stderr, "  git checkout failed (exit code %d)\n", status);
    }
}
