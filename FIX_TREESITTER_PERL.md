# tree-sitter-perl serialization crash

## Symptom

Indexing `cloc` with `index-perl` crashes while parsing `./cloc/sqlite_formatter`:

```text
index-perl: lib/src/parser.c:409: ts_parser__external_scanner_serialize:
Assertion `length <= 1024' failed.
Aborted (core dumped)
```

## Likely root cause

This appears to be in `tree-sitter-perl/src/scanner.c`, not in SourceMinder's Perl indexer.

The scanner serializes:

- a quote stack: `state->quotes`
- heredoc flags/state
- `TSPString heredoc_delim`

Current serialization logic caps `quote_count` at `UINT8_MAX`, but **does not cap total serialized size to tree-sitter's fixed serialization buffer size**:

- `TREE_SITTER_SERIALIZATION_BUFFER_SIZE == 1024`

In `tree-sitter-perl/src/scanner.c`:

```c
unsigned int tree_sitter_perl_external_scanner_serialize(void *payload, char *buffer) {
  LexerState *state = payload;
  size_t size = 0;

  size_t quote_count = state->quotes.size;
  if (quote_count > UINT8_MAX) {
    quote_count = UINT8_MAX;
  }
  buffer[size++] = (char)quote_count;

  if (quote_count > 0) {
    memcpy(&buffer[size], state->quotes.contents, quote_count * sizeof(TSPQuote));
  }
  size += quote_count * sizeof(TSPQuote);

  buffer[size++] = (char)state->heredoc_interpolates;
  buffer[size++] = (char)state->heredoc_indents;
  buffer[size++] = (char)state->heredoc_state;
  memcpy(&buffer[size], &state->heredoc_delim, sizeof(TSPString));
  size += sizeof(TSPString);

  return size;
}
```

So if `state->quotes.size` gets large enough, the scanner returns a serialized state larger than 1024 bytes, which triggers tree-sitter's assertion.

## Suggested upstream fix

Cap the number of serialized quotes based on the actual remaining serialization budget, not just `UINT8_MAX`.

Also, when truncating, preserve the **most recent / innermost** quote frames, because active parsing behavior depends on the tail of the quote stack.

Suggested patch shape:

```c
unsigned int tree_sitter_perl_external_scanner_serialize(void *payload, char *buffer) {
  LexerState *state = payload;
  size_t size = 0;
  const size_t fixed_size = 1 + 3 + sizeof(TSPString);
  const size_t max_quote_count =
      fixed_size < TREE_SITTER_SERIALIZATION_BUFFER_SIZE
          ? (TREE_SITTER_SERIALIZATION_BUFFER_SIZE - fixed_size) / sizeof(TSPQuote)
          : 0;

  size_t quote_count = state->quotes.size;
  if (quote_count > UINT8_MAX) {
    quote_count = UINT8_MAX;
  }
  if (quote_count > max_quote_count) {
    quote_count = max_quote_count;
  }

  buffer[size++] = (char)quote_count;

  if (quote_count > 0) {
    size_t quote_offset = state->quotes.size - quote_count;
    memcpy(&buffer[size], state->quotes.contents + quote_offset,
           quote_count * sizeof(TSPQuote));
  }
  size += quote_count * sizeof(TSPQuote);

  buffer[size++] = (char)state->heredoc_interpolates;
  buffer[size++] = (char)state->heredoc_indents;
  buffer[size++] = (char)state->heredoc_state;
  memcpy(&buffer[size], &state->heredoc_delim, sizeof(TSPString));
  size += sizeof(TSPString);

  return size;
}
```

## Why preserve the tail of the quote stack?

The scanner checks quote openers/closers by walking the quote stack from the end backward, so the innermost / most recent quote state is the most important parsing state to preserve when truncation is necessary.

## Scope

This note is for later review only.

I did **not** patch the vendored `tree-sitter-perl` source in this repo because the underlying issue is in the upstream grammar implementation.
