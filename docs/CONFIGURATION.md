# Configuration

## Config File Locations

- **Installed system-wide:** `/usr/local/share/sourceminder/<language>/config/`
- **Local development:** `<language>/config/` (in the project directory)
- **Built-in defaults:** compiled into every binary at build time

The search order is: `$INDEXER_DATA_DIR`, then the local directory, then the
system-wide directory, then the built-in defaults. A config file on disk
always overrides the built-in copy — the defaults only apply when no file is
found, so a bare binary works with no config files installed at all.

Run `index-<language> --show-config` to print the effective contents of every
config file along with its source — either the on-disk file being read (so you
know which file to edit) or `built-in` plus the path where an override can be
placed.

## File Extensions

**Location:** `<language>/config/file-extensions.txt`
**Format:** One extension per line starting with a dot:

```
.ts
.tsx
.js
.jsx
```

No recompilation needed after editing config files. But you will need to re-index.

## Ignored Folders and Files

**Location:** `<language>/config/ignore_files.txt`
**Format:** One folder per line:

```
node_modules
dist
build
.git
vendor/legacy
```

Folders are ignored at any level. Use `--exclude-dir` for per-run exclusions.

## Stopwords & Keywords

- **Stopwords** (shared): `shared/config/stopwords.txt`
- **Keywords** (per-language): `<language>/config/<language>-keywords.txt`
