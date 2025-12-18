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
 * along with Foobar. If not, see <https://www.gnu.org/licenses/>.
 */
#include "file_watcher.h"
#include "constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
/* BSD kqueue implementation (macOS, FreeBSD, OpenBSD, NetBSD, DragonFly BSD) */
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <fcntl.h>

/* File descriptor tracking */
typedef struct {
    int fd;                /* File descriptor */
    char path[4096];       /* File path */
} FileDescriptor;

/* File watcher structure for BSD */
struct FileWatcher {
    int kq;                                    /* kqueue file descriptor */
    FileDescriptor *fds;                       /* Array of file descriptors being watched */
    int fd_count;                              /* Number of file descriptors */
    int fd_capacity;                           /* Capacity of fds array */
    char extensions[MAX_FILE_EXTENSIONS][FILE_EXTENSION_MAX_LENGTH];  /* File extensions */
    int extension_count;                       /* Number of extensions */
};

/* Check if filename has valid extension */
static int has_valid_extension_array(const char *filename, char extensions[][FILE_EXTENSION_MAX_LENGTH], int count) {
    if (!filename) return 0;

    const char *dot = strrchr(filename, '.');
    if (!dot) return 0;

    for (int i = 0; i < count; i++) {
        if (strcmp(dot, extensions[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Add file to watch list */
static int add_file_watch(FileWatcher *watcher, const char *filepath) {
    if (!has_valid_extension_array(filepath, watcher->extensions, watcher->extension_count)) {
        return 0;  /* Skip files with wrong extension */
    }

    /* Check if already watching this file */
    for (int i = 0; i < watcher->fd_count; i++) {
        if (strcmp(watcher->fds[i].path, filepath) == 0) {
            return 0;  /* Already watching */
        }
    }

    /* Expand array if needed */
    if (watcher->fd_count >= watcher->fd_capacity) {
        size_t new_capacity = watcher->fd_capacity == 0 ? FILE_WATCHER_INITIAL_CAPACITY : (size_t)watcher->fd_capacity * 2;
        FileDescriptor *new_fds = realloc(watcher->fds, new_capacity * sizeof(FileDescriptor));
        if (!new_fds) {
            fprintf(stderr, "Error: Failed to expand file descriptor array\n");
            return -1;
        }
        watcher->fds = new_fds;
        watcher->fd_capacity = (int)new_capacity;
    }

    /* Open file for monitoring */
    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        return 0;  /* Skip files we can't open */
    }

    /* Add to kqueue */
    struct kevent change;
    EV_SET(&change, fd, EVFILT_VNODE, EV_ADD | EV_CLEAR,
           NOTE_DELETE | NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_RENAME,
           0, NULL);

    if (kevent(watcher->kq, &change, 1, NULL, 0, NULL) == -1) {
        close(fd);
        return 0;  /* Skip if we can't add to kqueue */
    }

    /* Store in our tracking array */
    watcher->fds[watcher->fd_count].fd = fd;
    strncpy(watcher->fds[watcher->fd_count].path, filepath, sizeof(watcher->fds[0].path) - 1);
    watcher->fds[watcher->fd_count].path[sizeof(watcher->fds[0].path) - 1] = '\0';
    watcher->fd_count++;

    return 0;
}

/* Recursively add all matching files in directory */
static int add_directory_recursive(FileWatcher *watcher, const char *directory) {
    DIR *dir = opendir(directory);
    if (!dir) return 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        /* Build full path */
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

        /* Get file type */
        struct stat st;
        if (stat(path, &st) == -1) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            /* Recursively add subdirectories */
            add_directory_recursive(watcher, path);
        } else if (S_ISREG(st.st_mode)) {
            /* Add regular file */
            add_file_watch(watcher, path);
        }
    }

    closedir(dir);
    return 0;
}

/* Get current time in milliseconds */
static long long current_time_ms(void) {
    struct timespec ts;
#ifdef CLOCK_MONOTONIC
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    clock_gettime(CLOCK_REALTIME, &ts);
#endif
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

FileWatcher* file_watcher_init(void) {
    FileWatcher *watcher = calloc(1, sizeof(FileWatcher));
    if (!watcher) {
        fprintf(stderr, "Error: Failed to allocate file watcher\n");
        return NULL;
    }

    watcher->kq = kqueue();
    if (watcher->kq == -1) {
        fprintf(stderr, "Error: Failed to create kqueue: %s\n", strerror(errno));
        free(watcher);
        return NULL;
    }

    return watcher;
}

int file_watcher_add_directory(FileWatcher *watcher, const char *directory,
                                const char **extensions, int extension_count) {
    if (!watcher || !directory) return -1;

    /* Copy extensions */
    watcher->extension_count = extension_count < MAX_FILE_EXTENSIONS ? extension_count : MAX_FILE_EXTENSIONS;
    for (int i = 0; i < watcher->extension_count; i++) {
        if (extensions[i]) {
            snprintf(watcher->extensions[i], FILE_EXTENSION_MAX_LENGTH, "%s", extensions[i]);
        } else {
            watcher->extensions[i][0] = '\0';
        }
    }

    /* Recursively add all files in directory */
    return add_directory_recursive(watcher, directory);
}

int file_watcher_wait(FileWatcher *watcher, FileEvent *events, int max_events) {
    if (!watcher || !events) return -1;

    struct kevent eventlist[128];
    int event_count = 0;
    long long last_event_time = 0;

    while (1) {
        /* Calculate timeout */
        struct timespec timeout;
        if (last_event_time > 0) {
            long long elapsed = current_time_ms() - last_event_time;
            if (elapsed >= DEBOUNCE_MS) {
                break;  /* Debounce period elapsed */
            }
            long long remaining = DEBOUNCE_MS - elapsed;
            timeout.tv_sec = remaining / 1000;
            timeout.tv_nsec = (remaining % 1000) * 1000000;
        } else {
            timeout.tv_sec = 0;
            timeout.tv_nsec = 0;
        }

        /* Wait for events */
        int nev = kevent(watcher->kq, NULL, 0, eventlist, 128,
                        last_event_time > 0 ? &timeout : NULL);

        if (nev == -1) {
            if (errno == EINTR) {
                break;  /* Interrupted by signal */
            }
            fprintf(stderr, "Error: kevent failed: %s\n", strerror(errno));
            return -1;
        }

        if (nev == 0 && last_event_time > 0) {
            break;  /* Timeout - debounce complete */
        }

        /* Process events */
        for (int i = 0; i < nev && event_count < max_events; i++) {
            struct kevent *kev = &eventlist[i];

            /* Find the file path for this fd */
            const char *filepath = NULL;
            for (int j = 0; j < watcher->fd_count; j++) {
                if (watcher->fds[j].fd == (int)kev->ident) {
                    filepath = watcher->fds[j].path;
                    break;
                }
            }

            if (!filepath) continue;

            /* Check if already in events (deduplicate) */
            int already_exists = 0;
            for (int j = 0; j < event_count; j++) {
                if (strcmp(events[j].filepath, filepath) == 0) {
                    already_exists = 1;
                    break;
                }
            }

            if (!already_exists) {
                strncpy(events[event_count].filepath, filepath, sizeof(events[0].filepath) - 1);
                events[event_count].filepath[sizeof(events[0].filepath) - 1] = '\0';

                /* Determine event type */
                if (kev->fflags & NOTE_DELETE) {
                    events[event_count].type = FILE_EVENT_DELETED;
                } else if (kev->fflags & (NOTE_WRITE | NOTE_EXTEND)) {
                    events[event_count].type = FILE_EVENT_MODIFIED;
                } else {
                    events[event_count].type = FILE_EVENT_MODIFIED;
                }

                event_count++;
                last_event_time = current_time_ms();
            }
        }
    }

    return event_count;
}

void file_watcher_free(FileWatcher *watcher) {
    if (!watcher) return;

    /* Close all file descriptors */
    for (int i = 0; i < watcher->fd_count; i++) {
        close(watcher->fds[i].fd);
    }

    free(watcher->fds);
    close(watcher->kq);
    free(watcher);
}

#else
/* Linux inotify implementation */
#include <sys/inotify.h>
#include <sys/select.h>

/* Watch descriptor mapping */
typedef struct {
    int wd;                /* inotify watch descriptor */
    char path[4096];       /* Directory path */
} WatchMapping;

/* File watcher structure */
struct FileWatcher {
    int fd;                                    /* inotify file descriptor */
    WatchMapping watches[MAX_WATCH_DIRS];      /* Watch descriptor mappings */
    int watch_count;                           /* Number of watches */
    char extensions[MAX_FILE_EXTENSIONS][FILE_EXTENSION_MAX_LENGTH];  /* Copied file extensions */
    int extension_count;                       /* Number of extensions */
};

/* Check if filename has valid extension */
static int has_valid_extension_array(const char *filename, char extensions[][FILE_EXTENSION_MAX_LENGTH], int count) {
    if (!filename) return 0;

    const char *dot = strrchr(filename, '.');
    if (!dot) return 0;

    for (int i = 0; i < count; i++) {
        if (strcmp(dot, extensions[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Add watch for a single directory */
static int add_watch(FileWatcher *watcher, const char *path) {
    if (watcher->watch_count >= MAX_WATCH_DIRS) {
        fprintf(stderr, "Error: Maximum watch limit reached (%d)\n", MAX_WATCH_DIRS);
        return -1;
    }

    int wd = inotify_add_watch(watcher->fd, path, IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_TO);
    if (wd == -1) {
        /* Silently skip directories we can't watch (permissions, etc.) */
        return 0;
    }

    watcher->watches[watcher->watch_count].wd = wd;
    strncpy(watcher->watches[watcher->watch_count].path, path, sizeof(watcher->watches[0].path) - 1);
    watcher->watch_count++;

    return 0;
}

/* Recursively add watches for directory and subdirectories */
static int add_watch_recursive(FileWatcher *watcher, const char *directory) {
    /* Add watch for this directory */
    if (add_watch(watcher, directory) != 0) {
        return -1;
    }

    /* Recursively add subdirectories */
    DIR *dir = opendir(directory);
    if (!dir) return 0;  /* Skip if can't open */

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        /* Build full path */
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

        /* Check if directory */
        if (entry->d_type == DT_DIR) {
            add_watch_recursive(watcher, path);
        }
    }

    closedir(dir);
    return 0;
}

/* Find directory path for watch descriptor */
static const char* find_watch_path(FileWatcher *watcher, int wd) {
    for (int i = 0; i < watcher->watch_count; i++) {
        if (watcher->watches[i].wd == wd) {
            return watcher->watches[i].path;
        }
    }
    return NULL;
}

/* Get current time in milliseconds */
static long long current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

FileWatcher* file_watcher_init(void) {
    FileWatcher *watcher = calloc(1, sizeof(FileWatcher));
    if (!watcher) {
        fprintf(stderr, "Error: Failed to allocate file watcher\n");
        return NULL;
    }

    watcher->fd = inotify_init1(IN_NONBLOCK);
    if (watcher->fd == -1) {
        fprintf(stderr, "Error: Failed to initialize inotify: %s\n", strerror(errno));
        free(watcher);
        return NULL;
    }

    return watcher;
}

int file_watcher_add_directory(FileWatcher *watcher, const char *directory,
                                const char **extensions, int extension_count) {
    if (!watcher || !directory) return -1;

    /* Copy extensions to avoid pointer issues */
    watcher->extension_count = extension_count < MAX_FILE_EXTENSIONS ? extension_count : MAX_FILE_EXTENSIONS;
    for (int i = 0; i < watcher->extension_count; i++) {
        if (extensions[i]) {
            snprintf(watcher->extensions[i], FILE_EXTENSION_MAX_LENGTH, "%s", extensions[i]);
        } else {
            watcher->extensions[i][0] = '\0';
        }
    }

    /* Add watches recursively */
    return add_watch_recursive(watcher, directory);
}

int file_watcher_wait(FileWatcher *watcher, FileEvent *events, int max_events) {
    if (!watcher || !events) return -1;

    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    int event_count = 0;
    long long last_event_time = 0;

    while (1) {
        /* Check if we should return (debounce complete) */
        if (last_event_time > 0) {
            long long elapsed = current_time_ms() - last_event_time;
            if (elapsed >= DEBOUNCE_MS) {
                break;  /* Debounce period elapsed, return events */
            }
        }

        /* Set timeout for select */
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = last_event_time > 0 ? DEBOUNCE_MS * 1000 : 0;  /* Wait for debounce or block */

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(watcher->fd, &fds);

        int ret = select(watcher->fd + 1, &fds, NULL, NULL, last_event_time > 0 ? &timeout : NULL);
        if (ret == -1) {
            /* EINTR is expected when interrupted by signals (e.g., SIGINT), so ignore it */
            if (errno == EINTR) {
                break;
            }
            fprintf(stderr, "Error: select failed: %s\n", strerror(errno));
            return -1;
        } else if (ret == 0) {
            /* Timeout - debounce complete */
            break;
        }

        /* Read events */
        ssize_t len = read(watcher->fd, buf, sizeof(buf));
        if (len == -1 && errno != EAGAIN) {
            fprintf(stderr, "Error: read failed: %s\n", strerror(errno));
            return -1;
        }
        if (len <= 0) continue;

        /* Process events */
        const struct inotify_event *event;
        for (char *ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
            event = (const struct inotify_event *)ptr;

            /* Skip directory events */
            if (event->mask & IN_ISDIR) continue;

            /* Skip if no name */
            if (event->len == 0) continue;

            /* Check extension */
            if (!has_valid_extension_array(event->name, watcher->extensions, watcher->extension_count)) {
                continue;
            }

            /* Find directory path */
            const char *dir = find_watch_path(watcher, event->wd);
            if (!dir) continue;

            /* Build full path */
            char filepath[4096];
            snprintf(filepath, sizeof(filepath), "%s/%s", dir, event->name);

            /* Check if we already have this file in the events array (deduplicate) */
            int already_exists = 0;
            for (int i = 0; i < event_count; i++) {
                if (strcmp(events[i].filepath, filepath) == 0) {
                    already_exists = 1;
                    break;
                }
            }

            if (!already_exists && event_count < max_events) {
                strncpy(events[event_count].filepath, filepath, sizeof(events[0].filepath) - 1);
                events[event_count].filepath[sizeof(events[0].filepath) - 1] = '\0';

                /* Determine event type */
                if (event->mask & (IN_MODIFY | IN_MOVED_TO)) {
                    events[event_count].type = FILE_EVENT_MODIFIED;
                } else if (event->mask & IN_CREATE) {
                    events[event_count].type = FILE_EVENT_CREATED;
                } else if (event->mask & IN_DELETE) {
                    events[event_count].type = FILE_EVENT_DELETED;
                }

                event_count++;
            }

            last_event_time = current_time_ms();
        }
    }

    return event_count;
}

void file_watcher_free(FileWatcher *watcher) {
    if (!watcher) return;

    /* Remove all watches */
    for (int i = 0; i < watcher->watch_count; i++) {
        inotify_rm_watch(watcher->fd, watcher->watches[i].wd);
    }

    close(watcher->fd);
    free(watcher);
}

#endif /* BSD vs Linux */
