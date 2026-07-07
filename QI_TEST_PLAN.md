# QI Test Plan

## Goal

Catch user-facing regressions in `./qi` before they reach normal use. The existing regression tests mostly validate indexing output. This plan adds a focused CLI UX layer for help text, TOC output, debug behavior, option compatibility, and documentation examples.

## Current Coverage

- `make test` builds and runs `tests/run-tests`.
- `tests/run-tests.c` indexes fixture files into temporary SQLite databases, runs `./qi % -v --db-file ...`, and diffs output against `expected.qi.output`.
- Current committed fixtures cover a small C case and several TypeScript cases.
- A C daemon delete smoke test verifies that deleting a watched file removes symbols from query results.
- `make lint` exists, but `clang-tidy` is non-blocking because the target ends with `|| true`.

## Gaps

- No CLI behavior snapshots for `./qi --help`.
- No `--toc` snapshot or semantic tests.
- No `--toc --debug` test.
- No test asserting `Result breakdown` counts match rendered section counts.
- No test for escaped flag-like searches such as `./qi '\--toc' '\--debug'`.
- No compatibility matrix for flags that should work in both normal query mode and `--toc` mode.
- No docs/example command validation.
- No CI workflow is currently present under `.github/workflows`.

## Recommended Approach

Use bare-bones C11 for the next test layer.

Reasons:

- The repository already has a portable C11 test runner.
- `./qi` is CLI-first, so most UX checks are command execution plus stdout/stderr assertions.
- Avoids adding a testing dependency for simple exit-code, string, and snapshot checks.
- Keeps tests usable on Linux, macOS, and MSYS-style environments.
- Matches the project preference for simple tooling with clear boundaries.

Add a new focused runner rather than expanding the indexer golden runner indefinitely:

- `tests/run-tests.c`: keep for indexer fixture regression tests.
- `tests/run-cli-tests.c`: add for command behavior, formatting invariants, and CLI UX checks.
- `make test`: run both runners.

## CLI UX Test Runner

`tests/run-cli-tests.c` should provide small helpers for:

- Running a command and capturing stdout/stderr.
- Asserting exit code.
- Asserting output contains required text.
- Asserting output does not contain forbidden text.
- Comparing output to a golden file when exact formatting is important.
- Counting output lines for help-size guardrails.
- Extracting numeric counts from output for semantic checks.

Example helper shape:

```c
assert_command_contains("./qi --help", "Usage: qi PATTERN");
assert_command_contains("./qi % -f query-index.c --toc --debug", "SQL: [TOC count query]");
assert_command_line_count_at_most("./qi --help", 80);
assert_toc_import_counts_match("./qi % -f query-index.c --toc");
```

## High-Value Initial Tests

### Help Output

- `./qi --help` exits 0.
- Help includes core sections: `Quick Start`, `Match`, `Filter`, `Display`, `Database`.
- Help includes important flags: `--toc`, `--debug`, `--raw`, `--columns`, `--db-file`.
- Help stays below a line-count threshold.
- Help does not include duplicate or stale option descriptions.

### TOC Output

- `./qi % -f query-index.c --toc` exits 0.
- Output includes `Result breakdown:`.
- Output includes `IMPORTS (N):`.
- `Result breakdown` `IMP (N)` equals `IMPORTS (N)`.
- Output includes expected sections such as `FUNCTIONS` and `TYPES`.

### TOC Debug

- `./qi % -f query-index.c --toc --debug` exits 0.
- Output includes `SQL: [TOC count query]`.
- Output includes `SQL: [TOC main query]`.
- Output still includes normal TOC output after debug lines.

### Escaped Flag Search

- `./qi '\--toc' '\--debug'` exits 0.
- Output searches for literal `--toc` and `--debug`, not option behavior.
- Output includes known matches in `query-index.c`.

### Option Compatibility

Smoke-test flags that should work in both normal mode and `--toc` mode:

- `-f` / `--file`
- `-i` / `--include-context`
- `--limit`
- `--limit-per-file`
- `--debug`
- `--db-file`

These tests should catch cases where a flag is parsed in `query-index.c` but not propagated into a submodule such as `shared/toc.c`.

## Snapshot Tests

Use snapshots sparingly for output where exact formatting matters:

- `tests/cli/help.expected`
- `tests/cli/toc-query-index.expected`
- `tests/cli/toc-debug-query-index.expected` if debug SQL is stable enough

Prefer semantic assertions when exact output would be too brittle, especially for indexed counts that change as the codebase changes.

## Documentation Command Checks

Add a later `make check-docs` target that runs a curated safe subset of documented commands from `README.md` and `docs/*.md`.

Start with explicitly listed commands rather than automatic extraction. Automatic extraction can come later once command examples follow a consistent format.

Initial candidates:

- `./qi --help`
- `./qi % --files`
- `./qi % -f query-index.c --toc`
- `./qi '\--help'`

## CI Recommendation

Add a minimal CI workflow that runs:

1. Build.
2. `make test`.
3. CLI UX tests.
4. Optional non-blocking lint.

Once lint noise is under control, make selected warnings blocking.

## Longer-Term Test Ideas

- Real-world corpus smoke tests for large projects.
- Cross-validation between tree-sitter parses and indexed database rows.
- Performance guardrails for common queries.
- Terminal-width checks for help and TOC readability.
- First-run diagnostics tests for missing database, missing config, and missing dependencies.
