// tests/run-tests.c - Portable test runner for indexer regression tests
//
// USAGE:
//   ./run-tests                    # Run all tests
//   ./run-tests --update           # Update expected output files
//   ./run-tests typescript         # Run only typescript tests
//
// TEST STRUCTURE:
//   tests/{language}/{test-name}/
//     {test-name}.{ext}            # Input fixture
//     expected.qi.output           # Expected qi output
//
// HOW IT WORKS:
//   For each test:
//     1. Index the fixture file: index-{lang} fixture.ext --db-file /tmp/test-{pid}.db
//     2. Query the database: qi % -v --db-file /tmp/test-{pid}.db
//     3. Compare actual output to expected.qi.output
//     4. Report pass/fail
//
// EXIT CODES:
//   0 - All tests passed
//   1 - One or more tests failed
//   2 - Usage error or system error

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

#define MAX_PATH 4096
#define MAX_CMD 8192

typedef struct {
    const char *name;
    const char *extension;
    const char *indexer;
} Language;

static const Language languages[] = {
    {"c", "c", "./index-c"},
    {"typescript", "ts", "./index-ts"},
    {"php", "php", "./index-php"},
    {"go", "go", "./index-go"},
    {"python", "py", "./index-python"},
    {"rust", "rs", "./index-rust"},
    {NULL, NULL, NULL}
};

static int update_mode = 0;
static int passed = 0;
static int failed = 0;
static int total = 0;

static const char *daemon_test_symbol = "daemon_delete_probe";

// Get language config by directory name
static const Language *get_language(const char *dir_name) {
    for (int i = 0; languages[i].name != NULL; i++) {
        if (strcmp(languages[i].name, dir_name) == 0) {
            return &languages[i];
        }
    }
    return NULL;
}

// Check if file exists
static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// Check if directory exists
static int dir_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// Run command and capture output to file
static int run_command(const char *cmd, const char *output_file) {
    char full_cmd[MAX_CMD];
    snprintf(full_cmd, sizeof(full_cmd), "%s > %s 2>&1", cmd, output_file);

    int status = system(full_cmd);
    if (status == -1) {
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// Compare two files
static int files_differ(const char *file1, const char *file2) {
    char cmd[MAX_CMD];
    snprintf(cmd, sizeof(cmd), "diff -q %s %s > /dev/null 2>&1", file1, file2);
    int ret = system(cmd);
    return ret != 0;  // 0 = files same, non-zero = differ
}

// Show diff between two files
static void show_diff(const char *file1, const char *file2) {
    char cmd[MAX_CMD];
    snprintf(cmd, sizeof(cmd), "diff -u %s %s", file1, file2);
    system(cmd);
}

// Copy file
static int copy_file(const char *src, const char *dst) {
    char cmd[MAX_CMD];
    snprintf(cmd, sizeof(cmd), "cp %s %s", src, dst);
    return system(cmd);
}

static int write_text_file(const char *path, const char *contents) {
    FILE *fp = fopen(path, "w");
    if (!fp) {
        return -1;
    }

    if (fputs(contents, fp) == EOF) {
        fclose(fp);
        return -1;
    }

    return fclose(fp);
}

static int file_contains_text(const char *path, const char *needle) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return 0;
    }

    char buffer[4096];
    int found = 0;
    while (fgets(buffer, sizeof(buffer), fp)) {
        if (strstr(buffer, needle) != NULL) {
            found = 1;
            break;
        }
    }

    fclose(fp);
    return found;
}

static void sleep_ms(int milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static int wait_for_symbol_state(const char *db_path, const char *symbol,
                                 const char *filename, int expect_present,
                                 int timeout_ms) {
    char output_path[MAX_PATH];
    char cmd[MAX_CMD];
    const int poll_ms = 100;
    int elapsed = 0;

    snprintf(output_path, sizeof(output_path), "scratch/daemon-delete-query-%d.txt", getpid());

    while (elapsed <= timeout_ms) {
        snprintf(cmd, sizeof(cmd), "./qi %s --db-file %s --files", symbol, db_path);
        unlink(output_path);
        if (run_command(cmd, output_path) == 0) {
            int found = file_contains_text(output_path, filename);
            if (found == expect_present) {
                unlink(output_path);
                return 0;
            }
        }

        sleep_ms(poll_ms);
        elapsed += poll_ms;
    }

    unlink(output_path);
    return -1;
}

static void cleanup_daemon_test_paths(const char *db_path, const char *fixture_path,
                                      const char *temp_dir) {
    unlink(fixture_path);
    unlink(db_path);
    rmdir(temp_dir);
}

static void run_daemon_delete_smoke_test(void) {
    char temp_dir[MAX_PATH];
    char fixture_path[MAX_PATH];
    char db_path[MAX_PATH];
    const char *fixture_source = "int daemon_delete_probe(void) { return 7; }\n";
    pid_t daemon_pid;
    int status;

    total++;
    printf("  daemon/delete-smoke ... ");
    fflush(stdout);

    mkdir("scratch", 0755);

    if (snprintf(temp_dir, sizeof(temp_dir), "scratch/sm-daemon-delete-%d", getpid()) >= (int)sizeof(temp_dir) ||
        snprintf(fixture_path, sizeof(fixture_path), "%s/watched.c", temp_dir) >= (int)sizeof(fixture_path) ||
        snprintf(db_path, sizeof(db_path), "scratch/sm-daemon-delete-%d.db", getpid()) >= (int)sizeof(db_path)) {
        printf("FAIL (path too long)\n");
        failed++;
        cleanup_daemon_test_paths("", "", temp_dir);
        return;
    }

    unlink(db_path);
    if (mkdir(temp_dir, 0700) != 0 && errno != EEXIST) {
        printf("FAIL (could not create temp dir)\n");
        failed++;
        cleanup_daemon_test_paths(db_path, fixture_path, temp_dir);
        return;
    }
    if (write_text_file(fixture_path, fixture_source) != 0) {
        printf("FAIL (could not create fixture)\n");
        failed++;
        cleanup_daemon_test_paths(db_path, fixture_path, temp_dir);
        return;
    }

    daemon_pid = fork();
    if (daemon_pid == -1) {
        printf("FAIL (could not start daemon)\n");
        failed++;
        cleanup_daemon_test_paths(db_path, fixture_path, temp_dir);
        return;
    }

    if (daemon_pid == 0) {
        execl("./index-c", "./index-c", temp_dir, "--db-file", db_path, "--quiet-init", (char *)NULL);
        _exit(127);
    }

    if (wait_for_symbol_state(db_path, daemon_test_symbol, "watched.c", 1, 5000) != 0) {
        kill(daemon_pid, SIGTERM);
        waitpid(daemon_pid, &status, 0);
        printf("FAIL (symbol never appeared)\n");
        failed++;
        cleanup_daemon_test_paths(db_path, fixture_path, temp_dir);
        return;
    }

    if (unlink(fixture_path) != 0) {
        kill(daemon_pid, SIGTERM);
        waitpid(daemon_pid, &status, 0);
        printf("FAIL (could not delete fixture)\n");
        failed++;
        cleanup_daemon_test_paths(db_path, fixture_path, temp_dir);
        return;
    }

    if (wait_for_symbol_state(db_path, daemon_test_symbol, "watched.c", 0, 5000) != 0) {
        kill(daemon_pid, SIGTERM);
        waitpid(daemon_pid, &status, 0);
        printf("FAIL (symbol remained after delete)\n");
        failed++;
        cleanup_daemon_test_paths(db_path, fixture_path, temp_dir);
        return;
    }

    kill(daemon_pid, SIGTERM);
    waitpid(daemon_pid, &status, 0);

    printf("PASS\n");
    passed++;
    cleanup_daemon_test_paths(db_path, fixture_path, temp_dir);
}

// Run a single test
// Read optional per-test qi arguments (first line of qi.args), falling back to
// the default. Lets a test exercise a specific qi mode (e.g. "% --toc") instead
// of the standard verbose dump.
static void read_qi_args(const char *args_path, char *buf, size_t buflen, const char *default_args) {
    FILE *f = fopen(args_path, "r");
    if (!f) {
        snprintf(buf, buflen, "%s", default_args);
        return;
    }
    if (!fgets(buf, (int)buflen, f)) {
        snprintf(buf, buflen, "%s", default_args);
        fclose(f);
        return;
    }
    fclose(f);
    // Strip trailing whitespace/newline
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
                       buf[len - 1] == ' ' || buf[len - 1] == '\t')) {
        buf[--len] = '\0';
    }
    if (len == 0) {
        snprintf(buf, buflen, "%s", default_args);
    }
}

static void run_test(const char *lang_name, const char *test_name, const Language *lang) {
    char fixture_path[MAX_PATH];
    char expected_path[MAX_PATH];
    char actual_path[MAX_PATH];
    char db_path[MAX_PATH];
    char args_path[MAX_PATH];
    char qi_args[MAX_CMD];
    char cmd[MAX_CMD];

    total++;

    // Construct paths
    int n;
    n = snprintf(fixture_path, sizeof(fixture_path), "tests/%s/%s/%s.%s",
                 lang_name, test_name, test_name, lang->extension);
    if (n >= (int)sizeof(fixture_path)) {
        printf("SKIP (path too long)\n");
        failed++;
        return;
    }

    n = snprintf(expected_path, sizeof(expected_path), "tests/%s/%s/expected.qi.output",
                 lang_name, test_name);
    if (n >= (int)sizeof(expected_path)) {
        printf("SKIP (path too long)\n");
        failed++;
        return;
    }

    snprintf(actual_path, sizeof(actual_path), "/tmp/actual-qi-output-%d.txt", getpid());
    snprintf(db_path, sizeof(db_path), "/tmp/test-db-%d.db", getpid());

    printf("  %s/%s ... ", lang_name, test_name);
    fflush(stdout);

    // Check fixture exists
    if (!file_exists(fixture_path)) {
        printf("SKIP (fixture not found: %s)\n", fixture_path);
        failed++;
        return;
    }

    // Clean up any existing temp files
    unlink(db_path);
    unlink(actual_path);

    // Step 1: Index the fixture
    n = snprintf(cmd, sizeof(cmd), "%s %s --db-file %s --once --quiet",
                 lang->indexer, fixture_path, db_path);
    if (n >= (int)sizeof(cmd)) {
        printf("FAIL (command too long)\n");
        failed++;
        return;
    }

    if (system(cmd) != 0) {
        printf("FAIL (indexer failed)\n");
        failed++;
        unlink(db_path);
        return;
    }

    // Step 2: Query the database (default "% -v"; a test may override via qi.args)
    snprintf(args_path, sizeof(args_path), "tests/%s/%s/qi.args", lang_name, test_name);
    read_qi_args(args_path, qi_args, sizeof(qi_args), "% -v");
    n = snprintf(cmd, sizeof(cmd), "./qi %s --db-file %s -f %s.%s",
                 qi_args, db_path, test_name, lang->extension);
    if (n >= (int)sizeof(cmd)) {
        printf("FAIL (command too long)\n");
        failed++;
        unlink(db_path);
        return;
    }

    if (run_command(cmd, actual_path) != 0) {
        printf("FAIL (qi failed)\n");
        failed++;
        unlink(db_path);
        unlink(actual_path);
        return;
    }

    // Step 3: Compare or update
    if (update_mode) {
        copy_file(actual_path, expected_path);
        printf("UPDATED\n");
        passed++;
    } else {
        if (!file_exists(expected_path)) {
            printf("FAIL (expected output not found: %s)\n", expected_path);
            printf("     Run with --update to create it\n");
            failed++;
        } else if (files_differ(expected_path, actual_path)) {
            printf("FAIL (output differs)\n");
            show_diff(expected_path, actual_path);
            failed++;
        } else {
            printf("PASS\n");
            passed++;
        }
    }

    // Cleanup
    unlink(db_path);
    unlink(actual_path);
}

// Process all tests in a language directory
static void run_language_tests(const char *lang_name, const Language *lang) {
    char lang_dir[MAX_PATH];
    snprintf(lang_dir, sizeof(lang_dir), "tests/%s", lang_name);

    DIR *dir = opendir(lang_dir);
    if (!dir) {
        fprintf(stderr, "Warning: Cannot open directory: %s\n", lang_dir);
        return;
    }

    printf("%s tests:\n", lang_name);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char test_path[MAX_PATH];
        int n = snprintf(test_path, sizeof(test_path), "%s/%s", lang_dir, entry->d_name);
        if (n >= (int)sizeof(test_path)) {
            fprintf(stderr, "Warning: path too long: %s/%s\n", lang_dir, entry->d_name);
            continue;
        }

        if (dir_exists(test_path)) {
            run_test(lang_name, entry->d_name, lang);
        }
    }

    closedir(dir);
    printf("\n");
}

int main(int argc, char *argv[]) {
    const char *filter_lang = NULL;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--update") == 0) {
            update_mode = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--update] [language]\n", argv[0]);
            printf("\n");
            printf("Options:\n");
            printf("  --update    Update expected output files instead of testing\n");
            printf("  language    Run only tests for specified language (c, typescript, etc.)\n");
            printf("\n");
            printf("Examples:\n");
            printf("  %s                    # Run all tests\n", argv[0]);
            printf("  %s --update           # Update all expected outputs\n", argv[0]);
            printf("  %s typescript         # Run only typescript tests\n", argv[0]);
            printf("  %s --update c         # Update only C expected outputs\n", argv[0]);
            return 0;
        } else {
            filter_lang = argv[i];
        }
    }

    printf("=== Running Indexer Regression Tests ===\n");
    if (update_mode) {
        printf("MODE: Updating expected output files\n");
    }
    printf("\n");

    // Check we're in project root
    if (!file_exists("README.md")) {
        fprintf(stderr, "Error: README.md not found\n");
        fprintf(stderr, "   This must be run from the project root directory\n");
        fprintf(stderr, "   Please run: ./tests/run-tests\n");
        return 2;
    }

    // Check required binaries exist
    if (!file_exists("./qi")) {
        fprintf(stderr, "Error: ./qi not found\n");
        fprintf(stderr, "   Please compile first: make\n");
        return 2;
    }

    // Run tests for each language
    if (filter_lang) {
        const Language *lang = get_language(filter_lang);
        if (!lang) {
            fprintf(stderr, "Error: Unknown language: %s\n", filter_lang);
            return 2;
        }
        if (!file_exists(lang->indexer)) {
            fprintf(stderr, "Error: Indexer not found: %s\n", lang->indexer);
            fprintf(stderr, "   Please compile first: make\n");
            return 2;
        }
        if (strcmp(filter_lang, "c") == 0) {
            run_daemon_delete_smoke_test();
        }
        run_language_tests(filter_lang, lang);
    } else {
        if (file_exists("./index-c")) {
            printf("daemon tests:\n");
            run_daemon_delete_smoke_test();
            printf("\n");
        }
        for (int i = 0; languages[i].name != NULL; i++) {
            if (!file_exists(languages[i].indexer)) {
                printf("Skipping %s (indexer not found: %s)\n\n",
                       languages[i].name, languages[i].indexer);
                continue;
            }
            run_language_tests(languages[i].name, &languages[i]);
        }
    }

    // Summary
    printf("==================================\n");
    printf("Results: %d passed, %d failed (total: %d)\n", passed, failed, total);

    if (failed == 0) {
        printf("All tests passed\n");
        return 0;
    } else {
        printf("%d test%s failed\n", failed, failed == 1 ? "" : "s");
        return 1;
    }
}
