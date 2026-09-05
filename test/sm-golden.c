// test/sm-golden.c - Per-file golden-snapshot harness for indexer output
//
// USAGE:
//   ./test/sm-golden <file>...                verify snapshots against goldens
//   ./test/sm-golden --update <file>...       create/refresh goldens
//
// For each source file, sm-golden:
//   1. Indexes the file into a fresh scratch database:
//        index-<lang> <file> --db-file <scratch> --once --silent --no-config
//   2. Reads the rows back through qi, the same query layer a user gets:
//        ./qi % --no-config -q -v --db-file <scratch>
//   3. Canonicalizes that display output into a tab-separated table, sorts it,
//      and compares it to the committed golden -- or writes one with --update.
//
// WHY qi AND NOT SQLITE-DIRECT:
//   Reading rows the way a user reads them means the harness exercises qi too,
//   so a change that breaks the query layer shows up here instead of in
//   somebody's terminal. `--no-config` makes the run depend on argv alone: it
//   skips ./.smconfig and ~/.smconfig, so the same command gives the same rows
//   on any machine.
//
// CANONICALIZING qi's OUTPUT:
//   qi's table is a display format, so it needs three fixes before it is data.
//   - Column widths follow the widest value in each result set, so two runs
//     differ in padding on every line. Trim each field.
//   - Values contain the separator, so the row cannot be split on '|'. Cut at
//     the byte offsets of the '|' characters in that run's header line instead.
//     qi pads by byte, not display width, so those offsets hold even for rows
//     with multibyte characters -- such rows look ragged on screen and cut
//     correctly here.
//   - qi prints the filename as a group header line, not a column. It is
//     dropped: the golden's own path already says which file it came from, so
//     snapshots stay path-free and moving a sample file churns nothing.
//   Rows are then sorted here rather than trusting qi's ORDER BY, so a golden
//   is a function of the row set alone. Duplicate rows are kept: same-line
//   duplicates are legitimate, and a comparison must notice two becoming one.
//
// WHAT BELONGS IN test/golden/:
//   Only files from tools/sources/. Real-world corpora (brew, redmine) are for
//   comparing two builds, never for blessing -- they move under us, they are
//   large, and a golden tree mirroring one would be unreviewable.
//
// GOLDEN LAYOUT:
//   The golden mirrors the indexed file's path under test/golden/:
//     test/golden/tools/sources/ruby/blocks.rb.snapshot
//   Run from the repository root so relative paths and ./index-<lang> resolve.
//
// EXIT CODES:
//   0 - all snapshots match
//   1 - one or more snapshots differ (or a golden is missing)
//   2 - usage or system error

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>

#define MAX_PATH 4096
#define MAX_CMD 8192
#define MAX_FILES 4096
#define MAX_COLUMNS 64
/* Scratch paths are built here, not supplied, so they are short and bounded:
 * "tmp/sm-golden-<pid>.raw" is under 30 bytes. Sizing them at MAX_PATH would
 * make the command buffer look overrunnable to the compiler. */
#define MAX_SCRATCH_PATH 64

typedef struct {
    const char *ext;
    const char *indexer;
} Language;

static const Language languages[] = {
    {"c",   "./index-c"},
    {"h",   "./index-c"},
    {"ts",  "./index-ts"},
    {"php", "./index-php"},
    {"go",  "./index-go"},
    {"py",  "./index-python"},
    {"rb",  "./index-ruby"},
    {"rs",  "./index-rust"},
    {"pl",  "./index-perl"},
    {NULL, NULL}
};

static int update_mode = 0;
static int passed = 0;
static int failed = 0;
static int updated = 0;

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* The golden tree must stay under test/golden/, so require a repository-root-
 * relative path with no ".." component. A ".." inside a name (foo..bar.rb) is
 * fine; only a whole component escapes. */
static int path_is_safe(const char *path) {
    if (path[0] == '/') return 0;
    for (const char *p = path; *p; ) {
        if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0')) return 0;
        const char *slash = strchr(p, '/');
        if (!slash) break;
        p = slash + 1;
    }
    return 1;
}

static const Language *get_language(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot || dot == path) return NULL;
    for (int i = 0; languages[i].ext != NULL; i++) {
        if (strcmp(dot + 1, languages[i].ext) == 0) return &languages[i];
    }
    return NULL;
}

/* A child process writes to fd 1 directly, so anything still sitting in our
 * stdio buffer must go out first. Without this, piping the run puts diff output
 * ahead of the "FAIL" line that introduces it. */
static int run_command(const char *cmd) {
    fflush(stdout);
    int status = system(cmd);
    if (status == -1) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int files_differ(const char *file1, const char *file2) {
    char cmd[MAX_CMD + MAX_PATH];
    snprintf(cmd, sizeof(cmd), "diff -q \"%s\" \"%s\" > /dev/null 2>&1", file1, file2);
    return run_command(cmd) != 0;
}

static void show_diff(const char *file1, const char *file2) {
    char cmd[MAX_CMD + MAX_PATH];
    snprintf(cmd, sizeof(cmd), "diff -u \"%s\" \"%s\"", file1, file2);
    int rc = run_command(cmd);
    (void)rc;
}

static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "r");
    if (!in) return -1;
    FILE *out = fopen(dst, "w");
    if (!out) {
        fclose(in);
        return -1;
    }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return -1;
        }
    }
    int ok = (ferror(in) == 0);
    fclose(in);
    if (fclose(out) != 0) ok = 0;
    return ok ? 0 : -1;
}

static int mkdir_p(const char *path) {
    char tmp[MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    while (len > 0 && tmp[len - 1] == '/') tmp[--len] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

/* Byte offsets of the '|' separators in qi's header line: the field edges every
 * row below it shares. */
typedef struct {
    size_t off[MAX_COLUMNS];
    int count;
} ColumnEdges;

static void find_edges(const char *header, ColumnEdges *edges) {
    edges->count = 0;
    for (size_t i = 0; header[i]; i++) {
        if (header[i] == '|' && edges->count < MAX_COLUMNS) {
            edges->off[edges->count++] = i;
        }
    }
}

/* A data row carries a '|' at every offset the header did. Anything else is
 * chrome -- the filename group header, or the "... N matches" notice a limit
 * would print -- and is dropped. */
static int is_data_row(const char *line, const ColumnEdges *edges) {
    size_t len = strlen(line);
    for (int i = 0; i < edges->count; i++) {
        if (edges->off[i] >= len || line[edges->off[i]] != '|') return 0;
    }
    return 1;
}

/* Cut one line at the header's separator offsets, trim each field of the
 * padding qi added, and join with tabs. Returns a malloc'd string. */
static char *cut_and_join(const char *line, const ColumnEdges *edges) {
    size_t len = strlen(line);
    char *buf = malloc(len + 2);
    if (!buf) return NULL;

    size_t w = 0;
    size_t start = 0;
    for (int i = 0; i <= edges->count; i++) {
        size_t end = (i < edges->count) ? edges->off[i] : len;
        if (start > len) start = len;
        if (end > len) end = len;

        size_t s = start, e = end;
        while (s < e && line[s] == ' ') s++;
        while (e > s && line[e - 1] == ' ') e--;

        if (i > 0) buf[w++] = '\t';
        memcpy(buf + w, line + s, e - s);
        w += e - s;
        start = end + 1;   /* step past the separator itself */
    }
    buf[w] = '\0';
    return buf;
}

typedef struct {
    long line;
    char *text;
} CanonRow;

/* Sort on the line number numerically first: the canonical text starts with
 * that number, so a plain strcmp would put line 10 before line 9. */
static int row_compare(const void *a, const void *b) {
    const CanonRow *ra = (const CanonRow *)a;
    const CanonRow *rb = (const CanonRow *)b;
    if (ra->line < rb->line) return -1;
    if (ra->line > rb->line) return 1;
    return strcmp(ra->text, rb->text);
}

/* Read qi's display output from raw_path and write the canonical table to out.
 * Returns 0 on success, -1 on a system error, -2 when qi printed no header. */
static int emit_snapshot(const char *raw_path, FILE *out) {
    FILE *in = fopen(raw_path, "r");
    if (!in) return -1;

    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    int rc = -2;                 /* no header seen yet */
    ColumnEdges edges;
    CanonRow *rows = NULL;
    size_t row_count = 0, row_cap = 0;

    while ((n = getline(&line, &cap, in)) != -1) {
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';

        if (rc == -2) {
            /* First line is qi's column header, and it names the columns, so
             * the snapshot header follows whatever -v prints for this build. */
            find_edges(line, &edges);
            if (edges.count == 0) continue;   /* not the header; keep looking */
            char *canon = cut_and_join(line, &edges);
            if (!canon) { rc = -1; goto done; }
            fprintf(out, "%s\n", canon);
            free(canon);
            rc = 0;
            continue;
        }

        if (!is_data_row(line, &edges)) continue;

        char *canon = cut_and_join(line, &edges);
        if (!canon) { rc = -1; goto done; }

        if (row_count == row_cap) {
            size_t next = row_cap ? row_cap * 2 : 256;
            CanonRow *grown = realloc(rows, next * sizeof(*rows));
            if (!grown) { free(canon); rc = -1; goto done; }
            rows = grown;
            row_cap = next;
        }
        rows[row_count].line = strtol(canon, NULL, 10);
        rows[row_count].text = canon;
        row_count++;
    }

    if (rc == 0) {
        qsort(rows, row_count, sizeof(*rows), row_compare);
        for (size_t i = 0; i < row_count; i++) {
            fprintf(out, "%s\n", rows[i].text);
        }
    }

done:
    for (size_t i = 0; i < row_count; i++) free(rows[i].text);
    free(rows);
    free(line);
    fclose(in);
    return rc;
}

/* Print the first line of a scratch file, so a failure reports what the tool
 * actually said instead of a bare status. */
static void report_first_line(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n = getline(&line, &cap, f);
    if (n > 0) {
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (line[0]) printf("    qi said: %s\n", line);
    }
    free(line);
    fclose(f);
}

static void cleanup_scratch(const char *db_path, const char *raw_path,
                            const char *actual_path) {
    char sidecar[MAX_PATH + 8];
    unlink(db_path);
    snprintf(sidecar, sizeof(sidecar), "%s-wal", db_path);
    unlink(sidecar);
    snprintf(sidecar, sizeof(sidecar), "%s-shm", db_path);
    unlink(sidecar);
    unlink(raw_path);
    unlink(actual_path);
}

static void verify_file(const char *path) {
    char db_path[MAX_SCRATCH_PATH];
    char raw_path[MAX_SCRATCH_PATH];
    char actual_path[MAX_SCRATCH_PATH];
    char golden_path[MAX_PATH];
    char golden_dir[MAX_PATH];
    char cmd[MAX_CMD];

    const Language *lang = get_language(path);
    if (!lang) {
        /* Named on the command line but unverifiable: report it, don't skip quietly. */
        printf("  %s ... FAIL (unsupported extension)\n", path);
        failed++;
        return;
    }
    if (!file_exists(path)) {
        printf("  %s ... FAIL (file not found)\n", path);
        failed++;
        return;
    }
    if (!path_is_safe(path)) {
        printf("  %s ... FAIL (path must be repo-root-relative, no '..')\n", path);
        failed++;
        return;
    }

    snprintf(db_path, sizeof(db_path), "tmp/sm-golden-%d.db", getpid());
    snprintf(raw_path, sizeof(raw_path), "tmp/sm-golden-%d.raw", getpid());
    snprintf(actual_path, sizeof(actual_path), "tmp/sm-golden-%d.out", getpid());
    if (snprintf(golden_path, sizeof(golden_path), "test/golden/%s.snapshot", path)
            >= (int)sizeof(golden_path)) {
        printf("  %s ... FAIL (path too long for a golden)\n", path);
        failed++;
        return;
    }
    snprintf(golden_dir, sizeof(golden_dir), "%s", golden_path);
    char *last_slash = strrchr(golden_dir, '/');
    if (last_slash) *last_slash = '\0';

    printf("  %s ... ", path);
    fflush(stdout);

    /* Step 1: index into a genuinely fresh scratch DB. The indexer appends, and a
     * killed run leaves its DB for whoever next reuses the PID, so clear the path
     * first -- inherited rows would silently corrupt the golden. --no-config does
     * for the indexer what it does for qi below: an [index-<lang>] section in
     * somebody's .smconfig would otherwise move rows with nothing in the diff to
     * explain it. */
    cleanup_scratch(db_path, raw_path, actual_path);
    snprintf(cmd, sizeof(cmd), "%s \"%s\" --db-file %s --once --silent --no-config > /dev/null 2>&1",
             lang->indexer, path, db_path);
    if (run_command(cmd) != 0) {
        printf("FAIL (indexer failed)\n");
        failed++;
        cleanup_scratch(db_path, raw_path, actual_path);
        return;
    }

    /* Step 2: read the rows back through qi. --no-config keeps the run dependent
     * on argv alone; -q drops the banner and footer; -v prints every column. */
    snprintf(cmd, sizeof(cmd), "./qi %% --no-config -q -v --db-file %s > %s 2>&1",
             db_path, raw_path);
    if (run_command(cmd) != 0) {
        printf("FAIL (qi failed)\n");
        report_first_line(raw_path);
        failed++;
        cleanup_scratch(db_path, raw_path, actual_path);
        return;
    }

    FILE *actual = fopen(actual_path, "w");
    if (!actual) {
        printf("FAIL (cannot write scratch output)\n");
        failed++;
        cleanup_scratch(db_path, raw_path, actual_path);
        return;
    }
    int emit_rc = emit_snapshot(raw_path, actual);
    fclose(actual);
    if (emit_rc == -2) {
        /* Every indexed file yields at least a FILE row, so a missing header
         * means the indexer produced nothing. That is a finding, not a format. */
        printf("FAIL (qi returned no rows)\n");
        report_first_line(raw_path);
        failed++;
        cleanup_scratch(db_path, raw_path, actual_path);
        return;
    }
    if (emit_rc != 0) {
        printf("FAIL (could not canonicalize qi output)\n");
        failed++;
        cleanup_scratch(db_path, raw_path, actual_path);
        return;
    }

    /* Step 3: compare or update */
    if (update_mode) {
        if (mkdir_p(golden_dir) != 0 || copy_file(actual_path, golden_path) != 0) {
            printf("FAIL (could not write golden)\n");
            failed++;
        } else {
            printf("UPDATED\n");
            updated++;
        }
    } else if (!file_exists(golden_path)) {
        printf("FAIL (no golden: %s; run with --update)\n", golden_path);
        failed++;
    } else if (files_differ(golden_path, actual_path)) {
        printf("FAIL (snapshot differs)\n");
        show_diff(golden_path, actual_path);
        failed++;
    } else {
        printf("PASS\n");
        passed++;
    }

    cleanup_scratch(db_path, raw_path, actual_path);
}

static void print_usage(const char *prog) {
    printf("Usage: %s [--update] <file>...\n", prog);
    printf("\n");
    printf("Index each file, read its rows back through qi, and compare the\n");
    printf("canonicalized table against the golden at test/golden/<file>.snapshot.\n");
    printf("Columns are whatever qi -v prints for this build.\n");
    printf("\n");
    printf("Bless only files from tools/sources/, never a real-world corpus.\n");
    printf("\n");
    printf("Options:\n");
    printf("  --update    create/refresh the golden snapshots\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s tools/sources/ruby/blocks.rb\n", prog);
    printf("  %s --update tools/sources/ruby/*.rb\n", prog);
}

int main(int argc, char *argv[]) {
    const char *files[MAX_FILES];
    int file_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--update") == 0) {
            update_mode = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            if (file_count >= MAX_FILES) {
                fprintf(stderr, "Error: too many files\n");
                return 2;
            }
            files[file_count++] = argv[i];
        }
    }

    if (file_count == 0) {
        print_usage(argv[0]);
        return 2;
    }

    /* Must run from the repo root so ./index-<lang>, ./qi and test/golden/ resolve */
    if (!file_exists("README.md")) {
        fprintf(stderr, "Error: README.md not found\n");
        fprintf(stderr, "   Run from the project root: ./test/sm-golden <file>\n");
        return 2;
    }
    if (!file_exists("./qi")) {
        fprintf(stderr, "Error: ./qi not found\n");
        fprintf(stderr, "   Snapshots are read through qi; build it first.\n");
        return 2;
    }
    if (mkdir_p("tmp") != 0) {
        fprintf(stderr, "Error: cannot create ./tmp for scratch files\n");
        return 2;
    }

    for (int i = 0; i < file_count; i++) {
        verify_file(files[i]);
    }

    printf("\n");
    if (update_mode)
        printf("Results: %d updated, %d failed\n", updated, failed);
    else
        printf("Results: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
