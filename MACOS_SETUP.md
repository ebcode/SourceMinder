# macOS Setup Guide for indexer-c

This document outlines the steps needed to compile the indexer-c project on a fresh macOS installation.

## Prerequisites

### 1. Install Homebrew

Homebrew is required for installing dependencies on macOS.

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

After installation, follow the instructions to add Homebrew to your PATH (typically adding to `~/.zshrc`):

```bash
# For Apple Silicon Macs
echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> ~/.zshrc
source ~/.zshrc

# For Intel Macs
echo 'eval "$(/usr/local/bin/brew shellenv)"' >> ~/.zshrc
source ~/.zshrc
```

### 2. Install Dependencies

```bash
# Install tree-sitter library (for parsing)
brew install tree-sitter

# Install sqlite (for database support, including development headers)
brew install sqlite

# Optional: Install tree-sitter CLI tool (if you need to regenerate parsers)
brew install tree-sitter-cli
```

## Tree-Sitter Grammar Repositories

The project requires tree-sitter grammar repositories for each supported language. These should be cloned in the project root directory:

```bash
cd /path/to/indexer-c

# Clone required grammar repositories
git clone https://github.com/tree-sitter/tree-sitter-c.git
git clone https://github.com/tree-sitter/tree-sitter-go.git
git clone https://github.com/tree-sitter/tree-sitter-php.git
git clone https://github.com/tree-sitter/tree-sitter-typescript.git
```

**Note:** The Makefile is configured to reference these external repositories directly, so you don't need to copy `parser.c` files into the language directories. This makes updates easier.

### Grammar Repository Structure

After cloning, your directory should look like:
```
indexer-c/
├── tree-sitter-c/
│   └── src/parser.c
├── tree-sitter-go/
│   └── src/parser.c
├── tree-sitter-php/
│   └── php/src/parser.c
│   └── php/src/scanner.c
└── tree-sitter-typescript/
    └── typescript/src/parser.c
    └── typescript/src/scanner.c
```

## Compilation

### Configure the Build

First, run the configure script to select which languages to build:

```bash
./configure --enable-all                              # All languages
./configure --enable-c --enable-typescript --enable-php  # Specific languages
./configure --enable-all --disable-php                   # All but PHP
CC=clang ./configure --enable-c                       # Custom compiler
```

**Note:** All languages are disabled by default. You must enable at least one language before building.

**What configure does:**
- Generates `config.h` with enabled language flags
- Extracts grammar versions from tree-sitter `package.json` files
- Creates `<language>/grammar_version.h` headers automatically (cross-platform)
- Generates the `Makefile` with appropriate targets

### Build the Project

```bash
make
```

This will:
- Compile the selected language indexers
- Build the query-index tool (qi)
- Create convenience symlinks in the project root

### Build Output

Binaries are created in the `build/` directory:
- `build/index-ts` - TypeScript indexer (if enabled)
- `build/index-c` - C indexer (if enabled)
- `build/index-php` - PHP indexer (if enabled)
- `build/index-go` - Go indexer (if enabled)
- `build/index-python` - Python indexer (if enabled)
- `build/qi` - Query tool (always built)

Symlinks in the project root (for enabled languages):
- `index-ts` → `build/index-ts`
- `index-c` → `build/index-c`
- `index-php` → `build/index-php`
- `index-go` → `build/index-go`
- `index-python` → `build/index-python`
- `qi` → `build/qi`

### Clean Build

To clean and rebuild from scratch:

```bash
make clean
./configure --enable-all  # Or your preferred language selection
make
```

## Global Installation

The indexers can be installed globally on the system, making them available from any directory.

### Installation Command

```bash
sudo make install
```

This will:
1. Install binaries to `/usr/local/bin/`:
   - `qi` (always)
   - `index-c` (if enabled)
   - `index-ts` (if enabled)
   - `index-php` (if enabled)
   - `index-go` (if enabled)
   - `index-python` (if enabled)

2. Install config files to `/usr/local/share/code-indexer/`:
   - `shared/config/` - Shared filter configuration (stopwords, exclusion patterns)
   - `c/config/` - C language configuration (if enabled)
   - `typescript/config/` - TypeScript language configuration (if enabled)
   - `php/config/` - PHP language configuration (if enabled)
   - `go/config/` - Go language configuration (if enabled)
   - `python/config/` - Python language configuration (if enabled)

### How Path Resolution Works

The indexers use a smart path resolution system with the following search order:

1. **Current directory** (development mode): Looks for `./c/config`, `./shared/config`, etc.
2. **System installation** (macOS): `/usr/local/share/code-indexer/`
3. **System installation** (Linux): `/usr/share/code-indexer/`

This means:
- **Development**: Run from the project directory without installation
- **Global installation**: Run from anywhere after `sudo make install`

### Using After Installation

After installation, you can use the indexers from any directory:

```bash
# Index any project
cd ~/my-project
index-c ./src

# Query the index
qi "function_name" --limit 10
```

### Uninstallation

To remove globally installed files:

```bash
sudo make uninstall
```

This removes:
- All binaries from `/usr/local/bin/`
- All config files from `/usr/local/share/code-indexer/`

## macOS-Specific Adaptations

### 1. File Watching (kqueue)

The file watcher has been implemented using **kqueue** for BSD/macOS compatibility (instead of Linux's inotify). This provides:
- Native BSD/macOS support
- Portability to FreeBSD, OpenBSD, NetBSD, DragonFly BSD
- Daemon mode for monitoring file changes

The implementation watches individual files rather than directories, which is the recommended approach for kqueue.

### 2. Homebrew Paths

The Makefile automatically detects Homebrew installation:
- **Apple Silicon (M1/M2/M3/M4)**: `/opt/homebrew`
- **Intel Macs**: `/usr/local`

Library paths are automatically configured for:
- tree-sitter headers and libraries
- SQLite headers and libraries (from Homebrew's keg-only installation)
- Tree-sitter grammar source directories

### 3. Compiler Compatibility

The project uses clang on macOS (aliased as `gcc`). Key compatibility fixes:
- Added `#include <strings.h>` for `strcasecmp`/`strncasecmp` functions (POSIX functions require explicit include on macOS)
- Refactored X-macro patterns to avoid embedding `#include` directives in function arguments (clang strictly enforces this)
- Disabled `-Wembedded-directive` warnings for clang

### 4. Expected Warnings

Some warnings are expected and harmless on macOS:
- **`-fprefetch-loop-arrays` not supported**: This is a GCC-specific optimization flag not available in clang
- **`_Static_assert` incompatible with C standards before C11**: These are false warnings; the code uses C11
- **`implicit conversion` warnings**: Code uses `-Weverything` for thorough checking

## Testing the Build

### Test the query tool:

```bash
./qi --help
```

### Index a directory:

```bash
# Index the project's source files
./index-c ./shared

# Or use any of the language-specific indexers
./index-ts ./typescript
./index-php ./php
./index-go ./go
```

### Query the index:

```bash
# Basic search
./qi "file_watcher" --limit 5

# Search with wildcards
./qi "%watcher%" --limit 10

# Exclude noise (comments and strings)
./qi "cursor" -x noise

# Search specific context types
./qi "FileWatcher" -i type

# Search for function calls
./qi "kevent" -i call
```

## Troubleshooting

### Missing tree-sitter headers

If you see errors about missing `tree_sitter/parser.h`:
- Verify tree-sitter grammar repos are cloned in the project root
- Check that the grammar directories contain `src/parser.c` files
- Ensure directory names match exactly (case-sensitive)

### Library linking errors

If you see errors about missing libraries:
- Verify Homebrew packages are installed: `brew list tree-sitter sqlite`
- Check Homebrew prefix: `brew --prefix` (should be `/opt/homebrew` on Apple Silicon)
- Ensure Homebrew is in your PATH

### Compiler warnings about optimization flags

The warning about `-fprefetch-loop-arrays` is expected on macOS (clang doesn't support this GCC-specific flag). This is harmless and can be ignored.

### "brew: command not found" during make

If you see this warning during `make clean`, it means Homebrew isn't in the PATH for the make shell. This is harmless - the Makefile automatically falls back to detecting Homebrew directories by checking standard locations.

## File Structure

```
indexer-c/
├── tree-sitter-c/          # C grammar (external repo)
├── tree-sitter-go/         # Go grammar (external repo)
├── tree-sitter-php/        # PHP grammar (external repo)
├── tree-sitter-typescript/ # TypeScript grammar (external repo)
├── c/                      # C indexer code
│   ├── c_language.c        # Language-specific implementation
│   ├── index-c.c           # Main entry point
│   └── data/               # Config files (extensions, keywords, etc.)
├── go/                     # Go indexer code
├── php/                    # PHP indexer code
├── typescript/             # TypeScript indexer code
├── shared/                 # Shared code (database, filters, etc.)
│   ├── file_watcher.c      # File watching (kqueue on BSD/macOS)
│   ├── database.c          # SQLite database layer
│   ├── filter.c            # Symbol filtering
│   └── ...
├── build/                  # Compiled binaries
├── Makefile                # Build configuration
├── query-index.c           # Query tool source
└── MACOS_SETUP.md          # This file
```

## Quick Reference

**Fresh macOS installation steps:**

1. Install Homebrew
   ```bash
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   ```

2. Install dependencies
   ```bash
   brew install tree-sitter sqlite
   ```

3. Clone tree-sitter grammar repositories
   ```bash
   cd /path/to/indexer-c
   git clone https://github.com/tree-sitter/tree-sitter-c.git
   git clone https://github.com/tree-sitter/tree-sitter-go.git
   git clone https://github.com/tree-sitter/tree-sitter-php.git
   git clone https://github.com/tree-sitter/tree-sitter-typescript.git
   git clone https://github.com/tree-sitter/tree-sitter-python.git
   ```

4. Configure and compile
   ```bash
   ./configure --enable-all
   make
   ```

5. Test
   ```bash
   ./qi --help
   ./index-c ./shared
   ./qi "file_watcher" --limit 5
   ```

## Notes for Future Builds

- **No file copying needed**: The Makefile references tree-sitter grammar repositories directly from their external directories
- **parser.c files are in .gitignore**: These files are large and come from external repos, so they're not checked into version control
- **grammar_version.h files are auto-generated**: The configure script extracts versions from tree-sitter `package.json` files using grep/sed (cross-platform compatible)
- **File watching uses kqueue**: BSD/macOS implementation differs from Linux's inotify, but provides the same functionality
- **Compiler warnings are normal**: The project uses `-Weverything` for comprehensive checking; most warnings are informational
- **Grammar updates**: To update grammars, just `git pull` in the respective tree-sitter-* directories, run `./configure` again, and recompile

## Differences from Linux Build

1. **File watching API**: Uses kqueue (BSD) instead of inotify (Linux)
2. **Library paths**: Homebrew locations differ from standard Linux paths
3. **Compiler**: Uses clang (as `gcc`) instead of actual GCC
4. **POSIX functions**: Require explicit `#include <strings.h>` on macOS
5. **X-macro handling**: Stricter enforcement in clang required code refactoring

## Additional Resources

- **Homebrew documentation**: https://brew.sh
- **Tree-sitter**: https://tree-sitter.github.io/tree-sitter/
- **kqueue manual**: `man kqueue` or `man 2 kqueue`
- **Project README**: See `README.md` for usage instructions and examples
