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
#include "indexer_main.h"
#include "database.h"
#include "filter.h"
#include "file_walker.h"
#include "file_watcher.h"
#include "validation.h"
#include "string_utils.h"
#include "file_opener.h"
#include "constants.h"
#include "parse_result.h"
#include "version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>

typedef enum {
    MODE_DIRECTORIES,
    MODE_FILES
} IndexMode;

/* Global flag for graceful shutdown */
static volatile sig_atomic_t keep_running = 1;

/* Signal handler for graceful shutdown */
static void signal_handler(int signum) {
    (void)signum;  /* Unused */
    keep_running = 0;
}

static int is_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode);
}

static int has_valid_extension(const char *filename, const FileExtensions *extensions) {
    if (!extensions || extensions->count == 0) return 0;

    size_t len = strnlength(filename, FILENAME_MAX_LENGTH);

    for (int i = 0; i < extensions->count; i++) {
        size_t ext_len = strnlength(extensions->extensions[i], FILE_EXTENSION_MAX_LENGTH);
        if (len > ext_len && strcmp(filename + len - ext_len, extensions->extensions[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int db_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

/* Flag bits for tracking which CLI flags are present */
#define FLAG_ONCE        (1 << 0)
#define FLAG_QUIET_INIT  (1 << 1)
#define FLAG_SILENT      (1 << 2)
#define FLAG_VERBOSE     (1 << 3)
#define FLAG_DB_FILE     (1 << 4)
#define FLAG_EXCLUDE_DIR (1 << 5)
#define FLAG_ECHO        (1 << 6)

/* Scan CLI arguments to detect which flags are present (before config loading) */
static int scan_cli_flags(int argc, char *argv[]) {
    int flags = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--once") == 0) flags |= FLAG_ONCE;
        else if (strcmp(argv[i], "--quiet-init") == 0) flags |= FLAG_QUIET_INIT;
        else if (strcmp(argv[i], "--silent") == 0) flags |= FLAG_SILENT;
        else if (strcmp(argv[i], "--verbose") == 0) flags |= FLAG_VERBOSE;
        else if (strcmp(argv[i], "--db-file") == 0 || strcmp(argv[i], "-f") == 0) flags |= FLAG_DB_FILE;
        else if (strcmp(argv[i], "--exclude-dir") == 0) flags |= FLAG_EXCLUDE_DIR;
        else if (strcmp(argv[i], "--echo") == 0) flags |= FLAG_ECHO;
    }
    return flags;
}

/* Check if a config line should be skipped (CLI flag takes precedence) */
static int should_skip_config_line(const char *line, int cli_flags) {
    if ((cli_flags & FLAG_ONCE) && strstr(line, "--once") == line) return 1;
    if ((cli_flags & FLAG_QUIET_INIT) && strstr(line, "--quiet-init") == line) return 1;
    if ((cli_flags & FLAG_SILENT) && strstr(line, "--silent") == line) return 1;
    if ((cli_flags & FLAG_VERBOSE) && strstr(line, "--verbose") == line) return 1;
    if ((cli_flags & FLAG_DB_FILE) && (strstr(line, "--db-file") == line || strstr(line, "-f") == line)) return 1;
    if ((cli_flags & FLAG_EXCLUDE_DIR) && strstr(line, "--exclude-dir") == line) return 1;
    if ((cli_flags & FLAG_ECHO) && strstr(line, "--echo") == line) return 1;
    return 0;
}

/* Load config file from ~/.smconfig and prepend args to argv */
static void load_config_file(int *argc_ptr, char ***argv_ptr, int cli_flags) {
    /* Security note: We trust HOME environment variable for config file location.
     * If an attacker can set HOME, they can already execute arbitrary code in this
     * process context. Config file is optional and only affects indexer defaults. */
    const char *home = getenv("HOME");
    if (!home) return;

    char config_path[PATH_MAX_LENGTH];
    int written = snprintf(config_path, sizeof(config_path), "%s/%s", home, CONFIG_FILENAME);
    if (written < 0 || (size_t)written >= sizeof(config_path)) {
        fprintf(stderr, "Warning: HOME path too long, skipping config file\n");
        return;
    }

    FILE *f = safe_fopen(config_path, "r", 1);
    if (!f) return;  /* No config file is fine */

    /* Count lines and allocate space for new argv */
    char line[LINE_BUFFER_MEDIUM];
    int config_arg_count = 0;
    char *config_args[MAX_PATTERNS * 2];  /* Max config arguments */

    /* Track if we're in the [ic] section */
    int in_ic_section = 0;

    /* Parse config file */
    while (fgets(line, sizeof(line), f)) {
        /* Remove trailing newline */
        size_t len = strnlength(line, sizeof(line));
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        /* Skip empty lines and comments */
        char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        if (*trimmed == '\0' || *trimmed == '#') continue;

        /* Check for section headers */
        if (*trimmed == '[') {
            in_ic_section = (strcmp(trimmed, "[ic]") == 0);
            continue;
        }

        /* Only process lines in the [ic] section */
        if (!in_ic_section) continue;

        /* Skip lines for flags present in CLI */
        if (should_skip_config_line(trimmed, cli_flags)) continue;

        /* Parse line as space-separated arguments (use strtok_r for thread safety) */
        char *saveptr;
        char *token = strtok_r(trimmed, " \t", &saveptr);
        while (token != NULL && config_arg_count < MAX_PATTERNS * 2) {
            config_args[config_arg_count++] = safe_strdup_ctx(token, "Failed to allocate memory for config argument");
            token = strtok_r(NULL, " \t", &saveptr);
        }
    }
    fclose(f);

    if (config_arg_count == 0) return;

    /* Create new argv with: [program name] + [original CLI args] + [config args] */
    int old_argc = *argc_ptr;
    char **old_argv = *argv_ptr;

    int new_argc = old_argc + config_arg_count;
    char **new_argv = (char **)malloc(sizeof(char *) * (size_t)(new_argc + 1));
    if (!new_argv) {
        fprintf(stderr, "Error: Failed to allocate memory for config args\n");
        return;
    }

    /* Copy program name and original CLI args */
    for (int i = 0; i < old_argc; i++) {
        new_argv[i] = old_argv[i];
    }

    /* Append config args */
    for (int i = 0; i < config_arg_count; i++) {
        new_argv[old_argc + i] = config_args[i];
    }

    new_argv[new_argc] = NULL;

    /* Update argc and argv */
    *argc_ptr = new_argc;
    *argv_ptr = new_argv;
}

static void print_usage(const IndexerConfig *config) {
    printf("Usage: %s <directories...> [OPTIONS]\n", config->name);
    printf("   or: %s <files...> [OPTIONS]\n", config->name);
    printf("Index source code files and store symbols in a SQLite database for fast search.\n");
    printf("Example: %s ./src --once\n", config->name);
    printf("\n");

    printf("Quick Start:\n");
    printf("  %s ./src                    # Watch and re-index on changes\n", config->name);
    printf("  %s ./src --once             # Index once and exit\n", config->name);
    printf("  %s ./src --quiet-init       # Quiet initial index, show re-index\n", config->name);
    printf("  %s ./src --silent           # Completely silent\n", config->name);
    printf("\n");

    printf("Tip: Use --once to disable daemon mode and run a single indexing pass\n");
    printf("\n");

    printf("Options:\n");
    printf("      --once                     run once and exit (disable daemon mode)\n");
    printf("      --quiet-init               suppress initial indexing output (still shows re-index messages)\n");
    printf("      --silent                   suppress all output (initial + re-index messages)\n");
    printf("      --verbose                  show preflight checks and validation\n");
    printf("      --exclude-dir DIR...       exclude directories (can specify multiple)\n");
    printf("  -f, --db-file PATH             database file location (default: code-index.db)\n");
    printf("      --echo MESSAGE             print message and continue (for testing)\n");
    printf("\n");

    printf("Daemon Mode (Default):\n");
    printf("  By default, %s runs in daemon mode, watching for file changes.\n", config->name);
    printf("  Press Ctrl+C to stop gracefully.\n");
    printf("\n");

    printf("  Note: Daemon mode only works with directory mode, not individual files.\n");
    printf("        Use --once to index files and exit immediately.\n");
    printf("\n");

    printf("Examples:\n");
    printf("  %s ./src                             # Watch and re-index on changes\n", config->name);
    printf("  %s ./src --once                      # Index once and exit\n", config->name);
    printf("  %s ./src ./lib                       # Index multiple directories\n", config->name);
    printf("  %s ./src --quiet-init                # Quiet initial index, show re-index\n", config->name);
    printf("  %s ./src --silent                    # Completely silent\n", config->name);
    printf("  %s ../ --exclude-dir indexer tests   # Exclude specific directories\n", config->name);
    printf("  %s ./src -f /dev/shm/code-index.db  # Use custom database location\n", config->name);
    printf("\n");

    printf("Configuration:\n");
    printf("  Config file: ~/.smconfig (CLI flags override config)\n");
    printf("  Format: [ic] section header, then one flag per line (e.g., \"--once\" or \"--quiet-init\")\n");
    printf("\n");
}

int indexer_main(int argc, char *argv[], const IndexerConfig *config) {
    /* Check for --help flag first */
    int show_help = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            show_help = 1;
            break;
        }
    }

    /* Save original argv and argc for cleanup */
    char **original_argv = argv;
    int original_argc = argc;

    /* Load config file only if not showing help (CLI flags override config) */
    if (!show_help) {
        int cli_flags = scan_cli_flags(argc, argv);
        load_config_file(&argc, &argv, cli_flags);
    }

    if (argc < 2 || show_help) {
        print_usage(config);
        return show_help ? 0 : 1;
    }

    int quiet_init = 0;
    int silent = 0;
    int verbose = 0;
    int debug = 0;
    int daemon_mode = 1;  /* Daemon mode enabled by default */
    ExcludeDirs exclude_dirs = { .count = 0 };
    char *targets[MAX_TARGETS];
    int target_count = 0;
    IndexMode mode = MODE_DIRECTORIES;
    const char *db_file = "code-index.db"; /* Default database location */

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--once") == 0) {
            daemon_mode = 0;
        } else if (strcmp(argv[i], "--quiet-init") == 0) {
            quiet_init = 1;
        } else if (strcmp(argv[i], "--silent") == 0) {
            silent = 1;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "--debug") == 0) {
            debug = 1;
        } else if (strcmp(argv[i], "--echo") == 0) {
            if (i + 1 < argc) {
                i++;
                printf("%s\n", argv[i]);
            } else {
                fprintf(stderr, "Error: --echo requires an argument\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--db-file") == 0 || strcmp(argv[i], "-f") == 0) {
            if (i + 1 < argc) {
                i++;
                db_file = argv[i];
            } else {
                fprintf(stderr, "Error: --db-file/-f requires an argument\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--exclude-dir") == 0) {
            /* Collect all exclude dirs until we hit another flag or end */
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                if (exclude_dirs.count < MAX_EXCLUDE_DIRS) {
                    snprintf(exclude_dirs.dirs[exclude_dirs.count], FILENAME_MAX_LENGTH, "%s", argv[i + 1]);
                    exclude_dirs.count++;
                }
                i++;
            }
        } else if (argv[i][0] != '-') {
            if (target_count < MAX_TARGETS) {
                targets[target_count++] = argv[i];
            }
        }
    }

    if (target_count == 0) {
        fprintf(stderr, "Error: at least one directory or file required\n");
        return 1;
    }

    /* Determine mode: files or directories */
    int dir_count = 0;
    int file_count = 0;
    for (int i = 0; i < target_count; i++) {
        if (is_directory(targets[i])) {
            dir_count++;
        } else {
            file_count++;
        }
    }

    if (dir_count > 0 && file_count > 0) {
        fprintf(stderr, "Error: cannot mix directories and files in the same command\n");
        return 1;
    }

    mode = (file_count > 0) ? MODE_FILES : MODE_DIRECTORIES;

    /* Daemon mode only works with directory mode */
    if (mode == MODE_FILES && daemon_mode) {
        daemon_mode = 0;  /* Silently disable for file mode */
    }

    /* PREFLIGHT VALIDATION - Check ALL configuration before proceeding */
    if (preflight_validation(config->data_dir, verbose) != 0) {
        return EXIT_FAILURE;
    }

    /* Initialize filter (all files validated) */
    SymbolFilter *filter = malloc(sizeof(SymbolFilter));
    if (!filter) {
        fprintf(stderr, "Failed to allocate memory for filter\n");
        return 1;
    }
    if (filter_init(filter, config->data_dir) != 0) {
        fprintf(stderr, "Warning: Failed to load filter data\n");
    }

    const FileExtensions *extensions = filter_get_extensions(filter);
    const WordSet *ignore_dirs = filter_get_ignore_dirs(filter);

    /* Validate file extensions if in file mode */
    if (mode == MODE_FILES) {
        for (int i = 0; i < target_count; i++) {
            if (!has_valid_extension(targets[i], extensions)) {
                fprintf(stderr, "Error: File '%s' does not match configured extensions:", targets[i]);
                for (int j = 0; j < extensions->count; j++) {
                    fprintf(stderr, " %s", extensions->extensions[j]);
                }
                fprintf(stderr, "\n");
                filter_free_regex(filter);
                free(filter);
                return 1;
            }
        }
    }

    if (!quiet_init && !silent) {
        if (mode == MODE_DIRECTORIES) {
            printf("Indexing files in directories:");
            for (int i = 0; i < target_count; i++) {
                printf(" %s", targets[i]);
            }
            printf("\n");
        } else {
            printf("Indexing files:");
            for (int i = 0; i < target_count; i++) {
                printf(" %s", targets[i]);
            }
            printf("\n");
        }
        printf("File extensions:");
        for (int i = 0; i < extensions->count; i++) {
            printf(" %s", extensions->extensions[i]);
        }
        printf("\n");
        if (exclude_dirs.count > 0) {
            printf("Excluding directories:");
            for (int i = 0; i < exclude_dirs.count; i++) {
                printf(" %s", exclude_dirs.dirs[i]);
            }
            printf("\n");
        }
    }

    /* Initialize database */
    CodeIndexDatabase db;
    if (db_init(&db, db_file) != SQLITE_OK) {
        fprintf(stderr, "Failed to initialize database\n");
        filter_free_regex(filter);
        free(filter);
        return 1;
    }

    /* Enable WAL mode for concurrent access */
    if (db_enable_concurrent_writes(&db) != SQLITE_OK) {
        fprintf(stderr, "Warning: Failed to enable WAL mode. Concurrent indexing may not work.\n");
        /* Continue anyway - not fatal */
    }

    /* Initialize parser using language-specific callback */
    void *parser = config->parser_init(filter);
    if (!parser) {
        fprintf(stderr, "Failed to initialize parser\n");
        filter_free_regex(filter);
        free(filter);
        db_close(&db);
        return 1;
    }

    /* Enable debug mode if requested */
    if (debug && config->parser_set_debug) {
        config->parser_set_debug(parser, 1);
    }

    /* Allocate and initialize parse result buffer */
    ParseResult *result = malloc(sizeof(ParseResult));
    if (!result) {
        fprintf(stderr, "Failed to allocate memory for parse result\n");
        config->parser_free(parser);
        filter_free_regex(filter);
        free(filter);
        db_close(&db);
        return 1;
    }

    if (init_parse_result(result) != 0) {
        fprintf(stderr, "Failed to initialize parse result\n");
        free(result);
        config->parser_free(parser);
        filter_free_regex(filter);
        free(filter);
        db_close(&db);
        return 1;
    }

    int total_files_processed = 0;

    /* Begin transaction for better performance */
    db_begin_transaction(&db);

    if (mode == MODE_FILES) {
        /* File mode: index individual files */
        int db_already_exists = db_exists(db_file);

        /* Get current working directory for relative path calculation */
        char cwd[PATH_MAX_LENGTH];
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            snprintf(cwd, sizeof(cwd), ".");
        }

        for (int i = 0; i < target_count; i++) {
            /* Parse the file to get directory and filename */
            if (config->parser_parse(parser, targets[i], cwd, result) == 0) {
                if (result->count > 0) {
                    /* Delete existing entries if database existed */
                    if (db_already_exists) {
                        db_delete_by_file(&db, result->entries[0].directory, result->entries[0].filename);
                    }

                    /* Insert new entries */
                    for (int j = 0; j < result->count; j++) {
                        db_insert(&db, &result->entries[j]);
                    }
                }

                if (!quiet_init && !silent) {
                    printf("Indexed %s: %d entries\n", targets[i], result->count);
                }
                total_files_processed++;
            }
        }
    } else {
        /* Directory mode: walk directories and find files */
        FileList *files = malloc(sizeof(FileList));
        if (!files) {
            fprintf(stderr, "Failed to allocate memory for file list\n");
            free_parse_result(result);
            free(result);
            config->parser_free(parser);
            filter_free_regex(filter);
            free(filter);
            db_close(&db);
            return 1;
        }
        init_file_list(files);

        /* Get current working directory for relative path calculation */
        char cwd[PATH_MAX_LENGTH];
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            snprintf(cwd, sizeof(cwd), ".");
        }

        for (int dir_idx = 0; dir_idx < target_count; dir_idx++) {
            /* Reset file list for each directory */
            if (dir_idx > 0) {
                free_file_list(files);
                init_file_list(files);
            }
            find_files(targets[dir_idx], files, &exclude_dirs, extensions, ignore_dirs);

            if (!quiet_init && !silent && target_count > 1) {
                printf("Found %d files in %s\n", files->count, targets[dir_idx]);
            }

            /* Index each file */
            for (int i = 0; i < files->count; i++) {
                if (config->parser_parse(parser, files->files[i], cwd, result) == 0) {
                    if (result->count > 0) {
                        /* Delete existing entries for this file */
                        db_delete_by_file(&db, result->entries[0].directory, result->entries[0].filename);

                        /* Insert new entries */
                        for (int j = 0; j < result->count; j++) {
                            db_insert(&db, &result->entries[j]);
                        }
                    }

                    if (!quiet_init && !silent) {
                        printf("Indexed %s: %d entries\n", files->files[i], result->count);
                    }
                }
            }

            total_files_processed += files->count;
        }

        free_file_list(files);
        free(files);
    }

    /* Commit transaction */
    db_commit_transaction(&db);

    if (!quiet_init && !silent) {
        printf("Indexing complete: %d files processed\n", total_files_processed);
    }

    /* Enter daemon mode if enabled and in directory mode */
    if (daemon_mode && mode == MODE_DIRECTORIES) {
        /* Setup signal handlers for graceful shutdown */
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        if (!silent) {
            printf("Watching for file changes (Press Ctrl+C to stop)...\n");
        }

        /* Initialize file watcher */
        FileWatcher *watcher = file_watcher_init();
        if (!watcher) {
            fprintf(stderr, "Failed to initialize file watcher\n");
            free_parse_result(result);
            free(result);
            config->parser_free(parser);
            filter_free_regex(filter);
            free(filter);
            db_close(&db);
            return 1;
        }

        /* Add directories to watch */
        /* Create array of pointers for extensions (extensions is a 2D array, not array of pointers) */
        const char *ext_ptrs[MAX_FILE_EXTENSIONS];
        for (int i = 0; i < extensions->count; i++) {
            ext_ptrs[i] = extensions->extensions[i];
        }

        for (int i = 0; i < target_count; i++) {
            if (file_watcher_add_directory(watcher, targets[i],
                                          ext_ptrs,
                                          extensions->count) != 0) {
                fprintf(stderr, "Warning: Failed to watch directory: %s\n", targets[i]);
            }
        }

        /* Watch loop */
        FileEvent events[MAX_FILE_EVENTS];
        while (keep_running) {
            int event_count = file_watcher_wait(watcher, events, MAX_FILE_EVENTS);
            if (event_count < 0) {
                if (keep_running) {  /* Only error if not interrupted */
                    fprintf(stderr, "Error: file watcher failed\n");
                }
                break;
            }

            if (event_count == 0) continue;

            /* Re-index changed files */
            db_begin_transaction(&db);

            for (int i = 0; i < event_count; i++) {
                /* Check if file should be ignored based on ignore_files.txt
                 * Need to check each directory component in the path */
                int should_skip = 0;
                char path_copy[PATH_MAX_LENGTH];
                int written = snprintf(path_copy, sizeof(path_copy), "%s", events[i].filepath);
                if (written >= (int)sizeof(path_copy)) {
                    fprintf(stderr, "FATAL: filepath exceeds PATH_MAX_LENGTH (%d): %s\n",
                            PATH_MAX_LENGTH, events[i].filepath);
                    exit(1);
                }

                /* Use strtok_r for thread safety (future-proofing) */
                char *saveptr;
                char *token = strtok_r(path_copy, "/", &saveptr);
                char partial_path[PATH_MAX_LENGTH] = "";

                while (token != NULL) {
                    /* Build partial path */
                    if (partial_path[0] != '\0') {
                        strncat(partial_path, "/", sizeof(partial_path) - strlen(partial_path) - 1);
                    }
                    strncat(partial_path, token, sizeof(partial_path) - strlen(partial_path) - 1);

                    /* Check if this directory component should be ignored */
                    if (is_path_ignored(partial_path, token, ignore_dirs)) {
                        should_skip = 1;
                        break;
                    }
                    token = strtok_r(NULL, "/", &saveptr);
                }

                if (should_skip) {
                    continue;  /* Skip ignored files */
                }

                /* Get current working directory for relative path calculation */
                char cwd[PATH_MAX_LENGTH];
                if (getcwd(cwd, sizeof(cwd)) == NULL) {
                    snprintf(cwd, sizeof(cwd), ".");
                }

                if (config->parser_parse(parser, events[i].filepath, cwd, result) == 0) {
                    if (result->count > 0) {
                        /* Delete existing entries for this file */
                        db_delete_by_file(&db, result->entries[0].directory, result->entries[0].filename);

                        /* Insert new entries */
                        for (int j = 0; j < result->count; j++) {
                            db_insert(&db, &result->entries[j]);
                        }
                    }

                    if (!silent) {
                        printf("Reindexed: %s (%d symbols)\n", events[i].filepath, result->count);
                    }
                }
            }

            db_commit_transaction(&db);
        }

        if (!silent) {
            printf("\nShutting down gracefully...\n");
        }

        file_watcher_free(watcher);
    }

    /* Cleanup */
    free_parse_result(result);
    free(result);
    config->parser_free(parser);
    filter_free_regex(filter);
    free(filter);
    db_close(&db);

    /* Free config args if we allocated a new argv */
    if (argv != original_argv) {
        /* Free config arg strings (everything after original CLI args) */
        for (int j = original_argc; j < argc; j++) {
            free(argv[j]);
        }
        free(argv);  /* Free the argv array itself */
    }

    return 0;
}
