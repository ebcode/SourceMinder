# Configuration

## Config File Locations

- **Installed system-wide:** `/usr/local/share/sourceminder/<language>/config/`
- **Local development:** `<language>/config/` (in the project directory)

The indexer checks both locations (local takes precedence).

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
