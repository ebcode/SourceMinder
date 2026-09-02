// test/sm-verify.c - Per-file golden-snapshot harness for indexer output
//
// USAGE:
//   ./test/sm-verify <file>...                verify snapshots against goldens
//   ./test/sm-verify --update <file>...       create/refresh goldens
//
// For each source file, sm-verify:
//   1. Indexes the file into a scratch database:
//        index-<lang> <file> --db-file <scratch> --once --silent
//   2. Reads the normalized row set straight out of SQLite:
//        (line, symbol, context, parent, d)
//      ordered by (line, symbol, context, parent, d, rowid)
//   3. Compares it to the committed golden, or writes one with --update.
//
// WHY SQLITE-DIRECT AND NOT `qi`:
//   qi merges ./.smconfig and ~/.smconfig into every invocation and has no
//   opt-out, so its output (banner, column set, file headers) varies with the
//   environment. A golden snapshot must be a function of the indexer alone.
//   Reading the scratch DB directly makes the snapshot config-independent and
//   path-free, so a tuning round shows up as a one-line diff instead of chrome.
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
#include <sqlite3.h>

#define MAX_PATH 4096
#define MAX_CMD 8192

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

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* The golden tree must stay under test/golden/, so require a repository-root-
 * relative path with no ".." components. */
static int path_is_safe(const char *path) {
    return path[0] != '/' && strstr(path, "..") == NULL;
}

static const Language *get_language(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot || dot == path) return NULL;
    for (int i = 0; languages[i].ext != NULL; i++) {
        if (strcmp(dot + 1, languages[i].ext) == 0) return &languages[i];
    }
    return NULL;
}

static int run_command(const char *cmd) {
    int status = system(cmd);
    if (status == -1) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int files_differ(const char *file1, const char *file2) {
    char cmd[MAX_CMD + MAX_PATH];
    snprintf(cmd, sizeof(cmd), "diff -q %s %s > /dev/null 2>&1", file1, file2);
    return run_command(cmd) != 0;
}

static void show_diff(const char *file1, const char *file2) {
    char cmd[MAX_CMD + MAX_PATH];
    snprintf(cmd, sizeof(cmd), "diff -u %s %s", file1, file2);
    int rc = system(cmd);
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

/* Render the file's rows as a normalized, tab-separated
 * (line, symbol, context, parent, d) table. Only one file is indexed into the
 * scratch DB, so no filename filter is needed. */
static int emit_snapshot(sqlite3 *db, FILE *out) {
    const char *sql =
        "SELECT line, symbol, context, parent_symbol, is_definition "
        "FROM code_index "
        "ORDER BY line, symbol, context, parent_symbol, is_definition, rowid;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "sm-verify: query failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    fprintf(out, "line\tsymbol\tcontext\tparent\td\n");
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int line = sqlite3_column_int(stmt, 0);
        const char *symbol = (const char *)sqlite3_column_text(stmt, 1);
        const char *context = (const char *)sqlite3_column_text(stmt, 2);
        const char *parent = (const char *)sqlite3_column_text(stmt, 3);
        int d = sqlite3_column_int(stmt, 4);
        fprintf(out, "%d\t%s\t%s\t%s\t%d\n", line,
                symbol ? symbol : "", context ? context : "",
                parent ? parent : "", d);
    }
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "sm-verify: query failed: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
}

static void cleanup_scratch(const char *db_path, const char *actual_path) {
    char sidecar[MAX_PATH + 8];
    unlink(db_path);
    snprintf(sidecar, sizeof(sidecar), "%s-wal", db_path);
    unlink(sidecar);
    snprintf(sidecar, sizeof(sidecar), "%s-shm", db_path);
    unlink(sidecar);
    unlink(actual_path);
}

static void verify_file(const char *path) {
    char db_path[MAX_PATH];
    char actual_path[MAX_PATH];
    char golden_path[MAX_PATH];
    char golden_dir[MAX_PATH];
    char cmd[MAX_CMD];

    const Language *lang = get_language(path);
    if (!lang) {
        printf("  %s ... SKIP (unsupported extension)\n", path);
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

    snprintf(db_path, sizeof(db_path), "/tmp/sm-verify-%d.db", getpid());
    snprintf(actual_path, sizeof(actual_path), "/tmp/sm-verify-%d.out", getpid());
    snprintf(golden_path, sizeof(golden_path), "test/golden/%s.snapshot", path);
    snprintf(golden_dir, sizeof(golden_dir), "%s", golden_path);
    char *last_slash = strrchr(golden_dir, '/');
    if (last_slash) *last_slash = '\0';

    printf("  %s ... ", path);
    fflush(stdout);

    /* Step 1: index the file into a fresh scratch DB */
    snprintf(cmd, sizeof(cmd), "%s \"%s\" --db-file %s --once --silent > /dev/null 2>&1",
             lang->indexer, path, db_path);
    if (run_command(cmd) != 0) {
        printf("FAIL (indexer failed)\n");
        failed++;
        cleanup_scratch(db_path, actual_path);
        return;
    }

    /* Step 2: read the normalized rows directly from SQLite */
    sqlite3 *db = NULL;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        printf("FAIL (cannot open scratch db)\n");
        failed++;
        cleanup_scratch(db_path, actual_path);
        return;
    }
    FILE *actual = fopen(actual_path, "w");
    if (!actual) {
        printf("FAIL (cannot write scratch output)\n");
        sqlite3_close(db);
        failed++;
        cleanup_scratch(db_path, actual_path);
        return;
    }
    int emit_ok = emit_snapshot(db, actual);
    fclose(actual);
    sqlite3_close(db);
    if (emit_ok != 0) {
        printf("FAIL (query failed)\n");
        failed++;
        cleanup_scratch(db_path, actual_path);
        return;
    }

    /* Step 3: compare or update */
    if (update_mode) {
        if (mkdir_p(golden_dir) != 0 || copy_file(actual_path, golden_path) != 0) {
            printf("FAIL (could not write golden)\n");
            failed++;
        } else {
            printf("UPDATED\n");
            passed++;
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

    cleanup_scratch(db_path, actual_path);
}

static void print_usage(const char *prog) {
    printf("Usage: %s [--update] <file>...\n", prog);
    printf("\n");
    printf("Index each file and compare its (line, symbol, context, parent, d)\n");
    printf("rows against the committed golden at test/golden/<file>.snapshot.\n");
    printf("\n");
    printf("Options:\n");
    printf("  --update    create/refresh the golden snapshots\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s tools/sources/ruby/blocks.rb\n", prog);
    printf("  %s --update tools/sources/ruby/*.rb\n", prog);
    printf("  %s tools/sources/ruby tools/sources/c\n", prog);
}

int main(int argc, char *argv[]) {
    const char *files[MAX_PATH];
    int file_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--update") == 0) {
            update_mode = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            if (file_count >= MAX_PATH) {
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

    /* Must run from the repo root so ./index-<lang> and test/golden/ resolve */
    if (!file_exists("README.md")) {
        fprintf(stderr, "Error: README.md not found\n");
        fprintf(stderr, "   Run from the project root: ./test/sm-verify <file>\n");
        return 2;
    }

    for (int i = 0; i < file_count; i++) {
        verify_file(files[i]);
    }

    printf("\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
