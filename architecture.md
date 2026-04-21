  ---
  SourceMinder Architecture

  High-Level Structure

  Indexers (one per language) → Shared runtime → SQLite database → qi query tool

  ---
  Indexers (index-c, index-go, index-php, index-python, index-ts)

  Each language has its own directory (c/, go/, php/, python/, typescript/) containing:
  - index-<lang>.c — thin entry point: registers 4 function pointers (parser_init, parser_parse_file, parser_free, parser_set_debug) and calls indexer_main()
  - <lang>_language.c — the AST walking logic (e.g. handle_function_definition, handle_declaration, visit_node, etc.), driven by tree-sitter

  The indexer entry points follow the same wrapper pattern: each builds an IndexerConfig with
  language-specific parser callbacks and delegates the shared runtime behavior to indexer_main().

  ---
  Shared Library (shared/)

  ┌────────────────────────────────┬──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
  │              File              │                                                                         Role                                                                         │
  ├────────────────────────────────┼──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ indexer_main.c                 │ Main loop: CLI parsing, daemon/--once mode, file watching, dispatch to language parsers                                                              │
  ├────────────────────────────────┼──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ file_walker.c                  │ Recursive directory traversal, glob matching, exclusion lists                                                                                        │
  ├────────────────────────────────┼──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ file_watcher.c                 │ inotify-based daemon that triggers re-indexing on file changes                                                                                       │
  ├────────────────────────────────┼──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ filter.c                       │ Loads stopwords, keywords, regex patterns; filter_should_index() decides per-symbol                                                                  │
  ├────────────────────────────────┼──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ parse_result.c                 │ ParseResult / IndexEntry — accumulator that parsers write into; add_entry() normalizes symbols (lowercase, strip $ for PHP, strip trailing           │
  │                                │ punctuation for comments/strings)                                                                                                                    │
  ├────────────────────────────────┼──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ database.c                     │ SQLite layer: db_init, db_insert, db_delete_by_file, WAL mode, prepared statements                                                                   │
  ├────────────────────────────────┼──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ sql_builder.c                  │ Dynamic SQL string builder used by qi                                                                                                                │
  ├────────────────────────────────┼──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ validation.c                   │ Preflight checks on config files (extensions, stopwords, etc.)                                                                                       │
  ├────────────────────────────────┼──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ string_utils.c, file_utils.c,  │ Low-level helpers                                                                                                                                    │
  │ paths.c                        │                                                                                                                                                      │
  ├────────────────────────────────┼──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ column_schema.def              │ X-Macro — single source of truth for all filterable columns (parent, scope, namespace, modifier, clue, type, is_definition). Adding a column here    │
  │                                │ auto-generates CLI flags, SQL WHERE clauses, and display columns.                                                                                    │
  └────────────────────────────────┴──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

  ---
  Query Tool (query-index.c)

  Large single-file tool (~3500 lines). Key stages:
  1. CLI parsing (scan_cli_flags, load_config_file for ~/.smconfig)
  2. Build SQL filters (build_common_filters, build_query_filters)
  3. --within scoped search via lookup_within_definitions (resolves to line ranges, then filters)
  4. --and proximity via temp table (execute_proximity_to_temp_table)
  5. Output: tabular (print_table_row), expanded (print_expansion_or_context), TOC (toc.c), or files-only

  ---
  Data Flow

  source file
      → tree-sitter AST
      → <lang>_language.c (visit_node → handle_* extraction)
      → filter_should_index() at extraction time (stopwords/keywords/regex)
      → add_entry()
      → ParseResult (array of accepted IndexEntry records)
      → db_insert() → SQLite (code-index.db)
      → qi queries it

  ---
  Notable Design Patterns

  - X-Macro (column_schema.def) for zero-boilerplate column extension
  - Wrapper functions (parser_init_wrapper etc.) in each index-<lang>.c allow indexer_main to call language parsers generically via function pointers
  - WAL mode enables concurrent multi-language indexing without locks
  - ~/.smconfig config file mirrors CLI flags for persistent defaults
