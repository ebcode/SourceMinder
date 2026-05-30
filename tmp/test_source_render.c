/* Boundary parity test (precept 20) for the source-render web twins.
 *
 * Strategy: the reference functions below are faithful transcriptions of the
 * native renderers (print_context_lines in query-index.c, print_lines_range in
 * shared/file_utils.c) with ONLY the output sink changed (printf/putchar ->
 * fprintf(out,)/fputc(,out)) so we can capture their bytes via open_memstream.
 * They read from disk via fopen, exactly like the CLI.  The twin reads the same
 * file from an in-memory buffer.  We diff the two byte-for-byte.
 *
 * Build: gcc -std=c11 -I../shared-web tmp/test_source_render.c \
 *            shared-web/source-render-web.c -o tmp/test_source_render
 * (run from repo root)
 *
 * The missing-file/404 path is intentionally NOT diffed here: the native warning
 * goes to stderr while the twin deliberately surfaces it in the terminal output
 * (plan section 4).  That divergence is by design, verified separately. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "source-render-web.h"

#define LINE_BUFFER_LARGE 1024

/* ---- faithful native references (sink swapped to FILE *out) ---- */

static void ref_print_context_lines(FILE *out, const char *filepath, int target_line,
                                     char **patterns, int pattern_count,
                                     int before, int after, int raw) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) { fprintf(stderr, "ref: cannot open %s\n", filepath); return; }

    char line[LINE_BUFFER_LARGE];
    int current_line = 0;
    int start_line = (target_line - before > 0) ? target_line - before : 1;
    int end_line = target_line + after;
    const char *GREEN = "\033[42m";
    const char *RESET = "\033[0m";

    if (!raw) fprintf(out, "--\n");

    while (fgets(line, sizeof(line), fp)) {
        current_line++;
        if (current_line >= start_line && current_line <= end_line) {
            if (raw) {
                fprintf(out, "%s", line);
            } else {
                char output[LINE_BUFFER_LARGE * 2];
                char *src = line;
                char *dst = output;
                size_t remaining = sizeof(output) - 1;
                while (*src && remaining > 0) {
                    int matched = 0;
                    for (int i = 0; i < pattern_count; i++) {
                        if (strchr(patterns[i], '%') != NULL || strchr(patterns[i], '_') != NULL)
                            continue;
                        size_t pattern_len = strlen(patterns[i]);
                        if (strncasecmp(src, patterns[i], pattern_len) == 0) {
                            size_t color_len = strlen(GREEN);
                            if (remaining > color_len) {
                                memcpy(dst, GREEN, color_len); dst += color_len; remaining -= color_len;
                            }
                            size_t copy_len = (pattern_len < remaining) ? pattern_len : remaining;
                            memcpy(dst, src, copy_len); dst += copy_len; src += pattern_len; remaining -= copy_len;
                            size_t reset_len = strlen(RESET);
                            if (remaining > reset_len) {
                                memcpy(dst, RESET, reset_len); dst += reset_len; remaining -= reset_len;
                            }
                            matched = 1;
                            break;
                        }
                    }
                    if (!matched && remaining > 0) { *dst++ = *src++; remaining--; }
                }
                *dst = '\0';
                fprintf(out, "%d:%s", current_line, output);
            }
        }
        if (current_line > end_line) break;
    }
    fclose(fp);
}

static void ref_print_lines_range(FILE *out, const char *filepath, int start_line, int end_line,
                                  int start_column, int end_column, int raw) {
    if (start_line < 1 || end_line < start_line) return;
    FILE *fp = fopen(filepath, "r");
    if (!fp) { fprintf(stderr, "ref: cannot open %s\n", filepath); return; }

    char line[4096];
    int current_line = 0;

    if (!raw) fprintf(out, "--\n");
    while (fgets(line, sizeof(line), fp)) {
        current_line++;
        if (current_line >= start_line && current_line <= end_line) {
            if (raw) {
                fprintf(out, "%s", line);
            } else {
                if (current_line == start_line && current_line == end_line) {
                    fprintf(out, "%d:", current_line);
                    for (int i = start_column; i <= end_column && line[i] != '\0'; i++) fputc(line[i], out);
                    if (line[end_column] != '\n' && line[end_column] != '\0') fputc('\n', out);
                } else if (current_line == start_line) {
                    fprintf(out, "%d:%s", current_line, &line[start_column]);
                } else if (current_line == end_line) {
                    fprintf(out, "%d:", current_line);
                    for (int i = 0; i <= end_column && line[i] != '\0'; i++) fputc(line[i], out);
                    if (line[end_column] != '\n' && line[end_column] != '\0') fputc('\n', out);
                } else {
                    fprintf(out, "%d:%s", current_line, line);
                }
            }
        }
        if (current_line > end_line) break;
    }
    fclose(fp);
}

/* ---- harness ---- */

static char *read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "harness: cannot open %s\n", path); return NULL; }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    size_t got = fread(buf, 1, (size_t)sz, fp);
    buf[got] = '\0';
    fclose(fp);
    return buf;
}

static int fails = 0;
static int total = 0;

static void show_diff(const char *got, const char *exp) {
    fprintf(stderr, "  --- expected (native) ---\n%s\n  --- got (twin) ---\n%s\n", exp, got);
}

static void check_context(const char *label, const char *path, int target,
                          char **pats, int npat, int before, int after, int raw) {
    total++;
    char *content = read_file(path);
    if (!content) { fails++; return; }

    char *exp = NULL; size_t exp_n = 0;
    FILE *m = open_memstream(&exp, &exp_n);
    ref_print_context_lines(m, path, target, pats, npat, before, after, raw);
    fclose(m);

    WebOutput wo; wo_init(&wo);
    print_context_lines_web(&wo, content, path, target, pats, npat, before, after, raw);
    char *got = wo_steal(&wo);

    if (strcmp(got ? got : "", exp ? exp : "") == 0) {
        printf("PASS  %s\n", label);
    } else {
        printf("FAIL  %s\n", label);
        show_diff(got ? got : "(null)", exp ? exp : "(null)");
        fails++;
    }
    free(got); free(exp); free(content);
}

static void check_range(const char *label, const char *path, int sl, int el,
                        int sc, int ec, int raw) {
    total++;
    char *content = read_file(path);
    if (!content) { fails++; return; }

    char *exp = NULL; size_t exp_n = 0;
    FILE *m = open_memstream(&exp, &exp_n);
    ref_print_lines_range(m, path, sl, el, sc, ec, raw);
    fclose(m);

    WebOutput wo; wo_init(&wo);
    print_lines_range_web(&wo, content, path, sl, el, sc, ec, raw);
    char *got = wo_steal(&wo);

    if (strcmp(got ? got : "", exp ? exp : "") == 0) {
        printf("PASS  %s\n", label);
    } else {
        printf("FAIL  %s\n", label);
        show_diff(got ? got : "(null)", exp ? exp : "(null)");
        fails++;
    }
    free(got); free(exp); free(content);
}

static void write_file(const char *path, const char *data) {
    FILE *fp = fopen(path, "wb");
    fputs(data, fp);
    fclose(fp);
}

/* Twin-only helpers for cases the native reference can't model (e.g. native
 * infinite-loops on an empty pattern, so there is nothing to diff against). */
static char *twin_context(const char *path, int target, char **pats, int npat,
                          int before, int after, int raw) {
    char *content = read_file(path);
    WebOutput wo; wo_init(&wo);
    print_context_lines_web(&wo, content, path, target, pats, npat, before, after, raw);
    char *got = wo_steal(&wo);
    free(content);
    return got;
}

static char *twin_orch(const char *path, int line, const char *srcloc, int isdef,
                       int expand, int before, int after, char **pats, int np, int raw) {
    char *content = read_file(path);
    WebOutput wo; wo_init(&wo);
    print_expansion_or_context_web(&wo, content, path, line, srcloc, isdef,
                                   expand, before, after, pats, np, raw);
    char *got = wo_steal(&wo);
    free(content);
    return got;
}

static void assert_eq(const char *label, char *got, const char *exp) {
    total++;
    if (strcmp(got ? got : "(null)", exp ? exp : "(null)") == 0) {
        printf("PASS  %s\n", label);
    } else {
        printf("FAIL  %s\n", label);
        show_diff(got ? got : "(null)", exp ? exp : "(null)");
        fails++;
    }
}

int main(void) {
    char *pats[] = { "wo", "line" };

    /* Real repo file: context, various positions, raw + non-raw */
    const char *real = "shared-web/source-render-web.c";
    check_context("ctx mid non-raw",       real, 40, pats, 2, 3, 3, 0);
    check_context("ctx mid raw",           real, 40, pats, 2, 3, 3, 1);
    check_context("ctx near top (clamp)",  real, 2,  pats, 2, 3, 3, 0);
    check_context("ctx asym -A only",      real, 40, pats, 2, 0, 4, 0);
    check_context("ctx asym -B only",      real, 40, pats, 2, 4, 0, 0);
    check_context("ctx no patterns",       real, 40, NULL, 0, 2, 2, 0);

    /* Range / expand.  Column-sensitive cases use a fixed-width fixture rather
     * than the (shifting) live source file: end_column must land within the last
     * line, as it always does from a valid source_location.  Native reads
     * line[end_column] unconditionally, so an out-of-range column reads
     * uninitialized stack (UB); the twin guards it -- a deliberate divergence
     * we keep out of the diff by using in-range columns here. */
    const char *cols = "tmp/_cols.txt";  /* 6 lines, each 30 'x' chars + '\n' */
    write_file(cols, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n"
                     "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n"
                     "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n"
                     "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n"
                     "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n"
                     "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n");
    check_range("range multi non-raw",     real, 38, 49, 0,  0, 0);  /* last line trimmed to col 0 */
    check_range("range multi raw",         real, 38, 50, 0,  0, 1);  /* raw ignores columns */
    check_range("range single-line cols",  cols, 2,  2,  4, 20, 0);
    check_range("range first/last cols",   cols, 2,  5,  8, 12, 0);

    /* Crafted edge cases */
    write_file("tmp/_no_trailing_nl.txt", "alpha\nbeta\ngamma");      /* no final newline */
    check_context("edge no-trailing-nl ctx", "tmp/_no_trailing_nl.txt", 3, pats, 0, 2, 2, 0);
    check_range("edge no-trailing-nl range",  "tmp/_no_trailing_nl.txt", 1, 3, 0, 4, 0);
    check_range("edge no-trailing-nl raw",    "tmp/_no_trailing_nl.txt", 1, 3, 0, 4, 1);

    write_file("tmp/_blanks.txt", "one\n\nthree\n\n\nsix\n");          /* blank lines */
    check_context("edge blanks ctx",  "tmp/_blanks.txt", 3, pats, 0, 2, 2, 0);
    check_range("edge blanks range",   "tmp/_blanks.txt", 1, 6, 0, 3, 0);

    write_file("tmp/_oneline.txt", "single line only\n");
    check_range("edge one-line single", "tmp/_oneline.txt", 1, 1, 0, 5, 0);

    /* Hardening cases (twin-only): empty/NULL patterns must be ignored, never
     * loop or crash, and yield the same output as if absent. */
    {
        char *base     = twin_context(real, 40, (char*[]){"wo"},       1, 3, 3, 0);
        char *w_empty  = twin_context(real, 40, (char*[]){"wo", ""},   2, 3, 3, 0);
        char *w_null   = twin_context(real, 40, (char*[]){"wo", NULL}, 2, 3, 3, 0);
        char *nullarr  = twin_context(real, 40, NULL,                  3, 3, 3, 0);
        char *nopat    = twin_context(real, 40, NULL,                  0, 3, 3, 0);
        char *allempty = twin_context(real, 40, (char*[]){"", ""},     2, 3, 3, 0);
        assert_eq("empty pattern ignored",        w_empty,  base);
        assert_eq("null pattern entry ignored",   w_null,   base);
        assert_eq("null patterns array safe",     nullarr,  nopat);
        assert_eq("all-empty == no highlighting", allempty, nopat);
        free(base); free(w_empty); free(w_null); free(nullarr); free(nopat); free(allempty);
    }

    /* Malformed / out-of-range source_location must render nothing (parse
     * rejects it), not malformed or glued output. */
    {
        char *garbage = twin_orch(real, 40, "not a location", 1, 1, 0, 0, NULL, 0, 0);
        char *negcol  = twin_orch(real, 40, "5:-1 - 7:3",     1, 1, 0, 0, NULL, 0, 0);
        char *revrange= twin_orch(real, 40, "9:0 - 3:0",      1, 1, 0, 0, NULL, 0, 0);
        assert_eq("malformed source_location -> empty", garbage,  "");
        assert_eq("negative column -> empty",           negcol,   "");
        assert_eq("reversed range -> empty",            revrange, "");
        free(garbage); free(negcol); free(revrange);
    }

    printf("\n%d/%d passed, %d failed\n", total - fails, total, fails);
    return fails ? 1 : 0;
}
