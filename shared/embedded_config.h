/* SourceMinder
 * Copyright 2026 Eli Bird
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
#ifndef EMBEDDED_CONFIG_H
#define EMBEDDED_CONFIG_H

/**
 * Look up the built-in default contents of a config file.
 *
 * The embedded copies are the last resort in the config search order:
 * $INDEXER_DATA_DIR, current directory, and the system install directory
 * (resolve_data_file() in paths.c) are all consulted first — a file on
 * disk always overrides the built-in defaults.
 *
 * @param relative_path: config path as built by the loaders,
 *                       e.g. "c/config/keywords.txt" or
 *                       "shared/config/stopwords.txt"
 * Returns: NUL-terminated file contents, or NULL if not embedded
 *
 * Implementation is generated at build time by tools/embed-config.c
 * into shared/embedded_config.c (not checked in).
 */
const char *embedded_config_get(const char *relative_path);

#endif /* EMBEDDED_CONFIG_H */
