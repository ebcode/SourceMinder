# SourceMinder Changelog

## Version 0.3.3-beta (2026-07-28)

- Add check for .smconfig file in current folder
- Separate [ic] config sections into individual indexer sections

## Version 0.3.2-beta (2026-07-28)

- Fix slow db_init issue in query-index.c
- Add --exclude-file flag to indexers
- Add bug reporting email to README and --help
- Remove tree-sitter version from qi --version
- Add --version flag to WASM port
- Minor --help output changes

## Version 0.3.1-beta (2026-07-22)

- install.sh for one-line installation
- Release scripts and Alpine Dockerfile for building static binaries (x86_64 for now)
- Moved scratch Rust sources to tools/sources/rust/
- Added cross-instance eval results and logs
- --list-types flag ported to WASM build

## Version 0.3.0-beta (2026-07-06)

- WASM build
- New CLI flags: --parent-type, --show-config, --toc (WASM), -l (short for --limit)
- Built-in config for static binary
- MACRO context type (index macros in #ifdef blocks)
- Detect grep-style \| alternation in patterns
- Dotted qualified-name auto-retry (e.g. "Some.func")
- Unknown-flag detection
- Rust: trait signatures, struct expressions, enums, generics/lifetimes, closures/macros, FFI, match patterns, modules
- TypeScript: parent and namespace fixes
- Go: import resolution and parent navigation
- Python: parent and namespace fixes
- C: ARG type gets parent=<function>, macro definition locations
- Schema: NOCASE collation on idx_symbol for fast LIKE queries
- --exclude-dir honored in daemon mode
- shared/toc.c: fix unchecked buffer overflow, O(1) last-file cache, bsearch import dedup
- shared/file_watcher.c: handle deleted files across watcher backends
- MAX_EXPRESSION_DEPTH guard against stack overflow on misparsed C++ templates
- Docs: expanded ADANCED_USAGE, RUST_GUIDE rewrite

## Version 0.2.1-beta (2026-05-21)

- Rust language implementation (new)
- Perl language implementation (new)
- --raw flag for machine-readable output (Claude Code / Edit tool integration)
- PHP: index string_content in single-quoted strings, heredocs, and concatenation
- C: index fields of anonymous inline structs as PROP
- query-index.c: fix --within for perl, fix multi-component -f path matching
- qi help: add --def and --usage to Quick Start
