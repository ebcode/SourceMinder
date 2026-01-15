# Building SourceMinder on Windows (MSYS2)

This guide explains how to build SourceMinder on Windows using MSYS2, which provides a Unix-like environment with the necessary tools and libraries.

## Quick Start (Python indexer only)

If you just need the Python indexer, follow these minimal steps:

```bash
# In MSYS2 UCRT64 terminal:
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-sqlite3 mingw-w64-ucrt-x86_64-tree-sitter mingw-w64-ucrt-x86_64-libsystre git make

cd /c/Projects/SourceMinder  # adjust path as needed

# Clone tree-sitter for headers (MSYS2 package doesn't include dev headers)
git clone --depth 1 https://github.com/tree-sitter/tree-sitter.git

# Clone the Python grammar
git clone https://github.com/tree-sitter/tree-sitter-python.git

./configure --enable-python
make
```

## Full Installation

### 1. Install MSYS2

Download and install MSYS2 from: https://www.msys2.org/

Run the installer and follow the prompts. Default installation path is `C:\msys64`.

### 2. Update MSYS2

Open "MSYS2 UCRT64" from the Start menu (important: use UCRT64, not MSYS or MINGW32).

Update the package database:
```bash
pacman -Syu
```

If prompted to close the terminal, do so and reopen "MSYS2 UCRT64", then run:
```bash
pacman -Su
```

### 3. Install Required Packages

```bash
pacman -S --needed \
    mingw-w64-ucrt-x86_64-gcc \
    mingw-w64-ucrt-x86_64-sqlite3 \
    mingw-w64-ucrt-x86_64-tree-sitter \
    mingw-w64-ucrt-x86_64-libsystre \
    git \
    make
```

## Building SourceMinder

### 1. Navigate to the Repository

In the MSYS2 UCRT64 terminal:
```bash
cd /c/Projects/SourceMinder  # adjust to your path
```

Note: In MSYS2, Windows paths use forward slashes and `C:` becomes `/c/`.

### 2. Clone Tree-Sitter and Grammars

First, clone the main tree-sitter repository for headers (the MSYS2 package only includes the runtime library, not development headers):

```bash
git clone --depth 1 https://github.com/tree-sitter/tree-sitter.git
```

Then clone the grammars you need:

```bash
# For Python only:
git clone https://github.com/tree-sitter/tree-sitter-python.git

# Or for all languages:
git clone https://github.com/tree-sitter/tree-sitter-c.git
git clone https://github.com/tree-sitter/tree-sitter-go.git
git clone https://github.com/tree-sitter/tree-sitter-php.git
git clone https://github.com/tree-sitter/tree-sitter-python.git
git clone https://github.com/tree-sitter/tree-sitter-typescript.git
```

### 3. Configure

```bash
# Python only (fastest build):
./configure --enable-python

# Or all languages:
./configure --enable-all

# Or specific combinations:
./configure --enable-python --enable-c
```

### 4. Build

```bash
make
```

The binaries will be created in the `build/` directory with symlinks in the project root.

## Usage

### Running from MSYS2 Terminal

```bash
# Index a directory
./index-c . --once --verbose

# Query the index
./qi test --limit 10
```

### Running from Windows Command Prompt or PowerShell

Add the MSYS2 bin directory to your PATH, or use full paths:

```cmd
C:\msys64\ucrt64\bin\bash.exe -c "cd /c/Projects/SourceMinder && ./qi test --limit 10"
```

Or copy the required DLLs alongside the executables for standalone use (see "Standalone Executables" below).

## Standalone Executables (Optional)

To run the executables outside of MSYS2, copy the required DLLs:

```bash
# From MSYS2 UCRT64 terminal
cp /ucrt64/bin/libsqlite3-0.dll build/
cp /ucrt64/bin/libtree-sitter.dll build/
cp /ucrt64/bin/libgcc_s_seh-1.dll build/
cp /ucrt64/bin/libwinpthread-1.dll build/
```

Then you can run from Windows Command Prompt:
```cmd
cd C:\Projects\SourceMinder\build
qi.exe test --limit 10
```

## Daemon Mode Note

The file watcher (daemon mode) uses Windows-specific APIs when built under MSYS2. If you encounter issues with `--daemon` mode, use `--once` mode instead:

```bash
# Instead of daemon mode:
./index-c . --once --verbose

# Re-run manually when files change, or use a watch script
```

## Troubleshooting

### "command not found: make"
Ensure you're using "MSYS2 UCRT64" terminal, not plain "MSYS2".

### "sqlite3.h: No such file"
Install the SQLite development package:
```bash
pacman -S mingw-w64-ucrt-x86_64-sqlite3
```

### "tree_sitter/api.h: No such file" or "cannot find -ltree-sitter"
The MSYS2 tree-sitter package only includes the CLI tool, not the library or headers. Clone the main tree-sitter repository:
```bash
git clone --depth 1 https://github.com/tree-sitter/tree-sitter.git
```
The configure script will find headers in `tree-sitter/lib/include/` and build the library from `tree-sitter/lib/src/lib.c`.

### "regex.h: No such file or directory"
Install the libsystre package which provides POSIX regex:
```bash
pacman -S mingw-w64-ucrt-x86_64-libsystre
```

### Linker errors about undefined references
Make sure you're using the UCRT64 environment consistently. Don't mix MSYS, MINGW32, MINGW64, and UCRT64.

### Path issues with backslashes
SourceMinder expects Unix-style paths (forward slashes). In MSYS2, Windows paths are automatically converted:
- `C:\Projects\SourceMinder` becomes `/c/Projects/SourceMinder`

## Environment Comparison

| Environment | Use Case |
|-------------|----------|
| MSYS2 UCRT64 | **Recommended** - Modern Windows C runtime |
| MSYS2 MINGW64 | Alternative - Older MinGW runtime |
| MSYS2 MSYS | Unix tools only - Don't use for building |

## See Also

- [README.md](README.md) - Main documentation
- [MACOS_SETUP.md](MACOS_SETUP.md) - macOS build instructions
