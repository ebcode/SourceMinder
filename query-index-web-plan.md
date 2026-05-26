# Query Index Web Extraction Plan

## Goal

- [ ] Extract a browser/WASM-capable query path from `query-index.c` incrementally, without a large refactor of the existing CLI implementation.
- [ ] Preserve `query-index.c` as the reference implementation for CLI behavior while building `query-index-web.c` as the web-targeted execution path.
- [ ] Make browser-safe versus host-dependent boundaries explicit in code so later extraction work is mechanical rather than speculative.

## Working Principles

- [ ] Prefer exact extraction over redesign. Copy behavior first; refactor later only when necessary.
- [ ] Keep `query-index.c` functional throughout the process.
- [ ] Avoid splitting `query-index.c` into multiple files as a prerequisite for the web target.
- [ ] Keep SQLite query execution local in WASM whenever the behavior is fully indexed-data-driven.
- [ ] Route only truly host-dependent behavior through an intermediary bridge.
- [ ] Minimize divergence between CLI and web behavior by preserving function logic where possible.

## Boundary Labels

- [x] Introduce consistent boundary comments above relevant functions.
- [x] Use `WEB_SAFE` for logic that can run unchanged in browser/WASM.
- [x] Use `HOST_ONLY` for logic tied to CLI/process/filesystem/environment/terminal behavior.
- [x] Use `HOST_BRIDGED` for logic the web version still needs, but which requires host assistance such as AJAX or JS callbacks.

### Comment Conventions

- [ ] Use short, explicit comments directly above functions.
- [ ] Keep the comments about runtime boundary and reason, not implementation detail.

Example forms:

```c
/* WEB_SAFE: pure query/filter logic over indexed SQLite data. */
```

```c
/* HOST_ONLY: reads CLI config from HOME and local filesystem. */
```

```c
/* HOST_BRIDGED: requires source file contents; CLI reads from disk, web must fetch via host bridge. */
```

## Initial Classification

### Likely `WEB_SAFE`

- [x] `process_file_pattern()`
- [x] `build_common_filters()`
- [x] `build_query_filters()`
- [x] `build_query_sql()`
- [x] `lookup_within_definitions()`
- [x] `execute_proximity_to_temp_table()`
- [ ] `count_distinct_files()`
- [ ] `count_pattern_matches()`
- [ ] `get_total_count()`
- [ ] `get_context_summary()`
- [ ] `get_total_file_count()`
- [x] `parse_source_location()`
- [ ] TOC query-building and DB-backed TOC assembly

### Likely `HOST_ONLY`

- [x] `main()`
- [x] `load_config_file()`
- [ ] help/version/list-types CLI output paths
- [ ] database path existence checks via `stat()`
- [x] extension/config discovery via installed paths or environment
- [ ] terminal-table rendering helpers when used only for CLI output

### Likely `HOST_BRIDGED`

- [x] `print_context_lines()`
- [x] `print_expansion_or_context()`
- [x] `print_lines_range()`
- [ ] any future path that requires source file contents outside the SQLite index

## Phase 1: Annotate Boundaries In Place

- [x] Add boundary comments above the relevant functions in `query-index.c`.
- [x] Add boundary comments above relevant helper functions in shared files that participate in web extraction.
- [ ] Focus first on functions at the runtime boundary rather than annotating every function in the file.
- [ ] Confirm each `HOST_BRIDGED` label corresponds to a real browser need, not just a current CLI implementation detail.

### Notes

- [x] Keep annotations lightweight to avoid comment noise.
- [ ] Prefer changing comments only in this phase; avoid logic edits unless needed for correctness.

## Phase 2: Create `query-index-web.c`

- [x] Create a new `query-index-web.c` file.
- [x] Start the file small and purpose-built; do not duplicate the entire `query-index.c` file.
- [x] Copy only the initial `WEB_SAFE` functions needed for a first executable browser query path.
- [x] Keep copied logic as close as possible to the source implementation.
- [x] Add brief comments noting that copied logic should remain behaviorally aligned with the CLI implementation.

### First Extraction Targets

- [x] pattern normalization helpers needed by the web path
- [x] file pattern processing
- [x] SQL filter construction
- [ ] within-definition lookup
- [ ] proximity query execution
- [ ] result counting helpers
- [ ] minimal result row extraction

### Non-Goals For The First Web File

- [ ] Do not move `main()`.
- [ ] Do not move full CLI help text.
- [ ] Do not move config-file loading.
- [ ] Do not move filesystem-based source expansion.
- [ ] Do not move terminal formatting unless required as a temporary debug aid.

## Phase 3: Define A Minimal Web Query Entry Point

- [ ] Introduce a browser-oriented entry point in `query-index-web.c`.
- [ ] Accept structured inputs rather than raw CLI `argv` where practical.
- [ ] Keep the first API small and focused on the currently supported browser query subset.
- [ ] Return structured results rather than printing terminal output.

### Early API Shape

- [ ] query request struct for patterns, filters, limits, and mode flags
- [ ] query response struct for rows, file groups, counts, and diagnostics
- [ ] explicit handling for DB-backed modes such as standard row search, files-only, and TOC

### Notes

- [ ] The initial API does not need to represent all CLI flags.
- [ ] The API should be designed so unsupported features fail explicitly rather than silently degrading.

## Phase 4: Keep DB-Backed Behavior Inside WASM

- [ ] Execute SQLite filtering and query logic locally in WASM.
- [ ] Keep `--and` and proximity search in the WASM layer.
- [ ] Keep `--within` definition lookup in the WASM layer.
- [ ] Keep files-only and TOC queries in the WASM layer if they remain purely index-driven.
- [ ] Measure and verify browser behavior against the CLI for representative queries.

### Verification Targets

- [ ] single-pattern queries
- [ ] multi-pattern OR queries
- [ ] `--and` same-line queries
- [ ] proximity queries
- [ ] file pattern filtering
- [ ] `--within` scoped queries
- [ ] TOC generation from indexed metadata

## Phase 5: Introduce A Host Bridge For Source-Dependent Features

- [ ] Identify the smallest host bridge interface that supports source expansion and context rendering.
- [ ] Keep low-level query functions unaware of AJAX, HTTP, or browser transport details.
- [ ] Represent source needs as requests for file content or line ranges, not as terminal printing operations.

### First Bridge Responsibilities

- [ ] fetch file text by indexed path
- [ ] fetch line ranges for context display
- [ ] fetch full definition ranges for `-e`

### CLI vs Browser Responsibility Split

- [ ] CLI path fulfills bridge requests from local filesystem.
- [ ] Browser path fulfills bridge requests through JS/AJAX.
- [ ] Core query logic computes the requested ranges from indexed metadata.

### Notes

- [ ] `parse_source_location()` should remain local and reusable in the web path.
- [ ] Source fetching should be decoupled from query execution so the same query results can be rendered in multiple environments.

## Phase 6: Route Existing Source-Expansion Features Through The Bridge

- [ ] Rework `-e` behavior so query logic identifies the source span and host code retrieves content.
- [ ] Rework `-C`, `-A`, and `-B` behavior similarly.
- [ ] Preserve CLI behavior while making the source-fetch step replaceable.
- [ ] Ensure errors remain explicit when source content cannot be retrieved.

### Risks To Watch

- [ ] mismatch between indexed path and served path
- [ ] moved or missing files
- [ ] partial content fetches that break line-number assumptions
- [ ] browser latency or batching issues for many context fetches

## Phase 7: Expand Web Feature Coverage Gradually

- [ ] Add more CLI-equivalent filters and output modes as needed.
- [ ] Align browser behavior with the real query engine rather than re-implementing behavior in JavaScript.
- [ ] Keep unsupported flags explicit until they have a well-defined web execution path.

### Broad Later Targets

- [ ] richer result formatting in the browser
- [ ] broader flag coverage
- [ ] stronger parity tests between CLI and web results
- [ ] possible Emscripten bindings cleanup once the extracted surface stabilizes

## Divergence Control

- [ ] When copying logic from `query-index.c`, preserve behavior exactly unless a boundary requires adaptation.
- [ ] Record meaningful intentional divergences in notes below.
- [ ] Prefer small sync steps from `query-index.c` into `query-index-web.c` over speculative redesign.

## Open Questions

- [ ] What is the minimal first structured API needed by the browser harness?
- [ ] Should TOC ship in the first extracted web path or follow after standard row queries?
- [ ] What is the best browser host-bridge transport for source fetching: direct static-file fetch, a small HTTP API, or both?
- [ ] How much CLI argument parsing, if any, should be preserved in the web-targeted code?

## Notes

- [x] Add implementation notes and discoveries here as the work progresses.
- [ ] Record any functions whose classification changes after closer inspection.
- [ ] Record any browser-specific constraints discovered during Emscripten integration.

### Current Notes

- [x] Boundary annotations are now in `query-index.c`, `shared/file_utils.c`, `shared/extensions.c`, `shared/paths.c`, and `shared/toc.c`.
- [x] `query-index-web.c` has been created with an exact extraction of the wildcard and file-pattern normalization helpers as the first `WEB_SAFE` slice.
- [x] `query-index-web.c` now also contains exact extractions of the private query-side filter types plus the `build_common_filters` and `build_query_filters` SQL helpers.
- [x] `make qi` passes after the annotation pass and initial extraction scaffold.
