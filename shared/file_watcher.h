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
#ifndef FILE_WATCHER_H
#define FILE_WATCHER_H

#include <stddef.h>

/* Maximum number of watched directories */
#define MAX_WATCH_DIRS 8192

/* Maximum number of file events to return in one batch */
#define MAX_FILE_EVENTS 128

/* Debounce time in milliseconds (wait after last event before returning) */
#define DEBOUNCE_MS 200

/* File watcher handle */
typedef struct FileWatcher FileWatcher;

/* File event types */
typedef enum {
    FILE_EVENT_MODIFIED,
    FILE_EVENT_CREATED,
    FILE_EVENT_DELETED
} FileEventType;

/* File event information */
typedef struct {
    char filepath[4096];  /* Full path to file */
    FileEventType type;
} FileEvent;

/**
 * Initialize file watcher
 * Returns: FileWatcher handle, or NULL on error
 */
FileWatcher* file_watcher_init(void);

/**
 * Add directory to watch recursively
 * @param watcher: FileWatcher handle
 * @param directory: Directory path to watch
 * @param extensions: Array of file extensions to watch (e.g., [".ts", ".tsx"])
 * @param extension_count: Number of extensions
 * Returns: 0 on success, -1 on error
 */
int file_watcher_add_directory(FileWatcher *watcher, const char *directory,
                                const char **extensions, int extension_count);

/**
 * Wait for file events with debouncing
 * Blocks until file changes detected, then waits DEBOUNCE_MS for more events
 * @param watcher: FileWatcher handle
 * @param events: Array to store file events (caller allocates)
 * @param max_events: Maximum events to return
 * Returns: Number of events, or -1 on error
 */
int file_watcher_wait(FileWatcher *watcher, FileEvent *events, int max_events);

/**
 * Free file watcher resources
 * @param watcher: FileWatcher handle
 */
void file_watcher_free(FileWatcher *watcher);

#endif /* FILE_WATCHER_H */
