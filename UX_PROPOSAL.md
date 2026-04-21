`qi` UX Proposal

Goal
- Optimize `qi` for everyday code navigation by LLM coding agents.
- Keep the tool simple, deterministic, and low-noise.
- Avoid fuzzy matching or concept/intent search.

Current Strengths
- Deterministic symbol lookup is strong and works well for agent-driven exploration.
- File-scoped search with `-f` keeps exploration focused and avoids grep-style false positives.
- Context filtering with `-i` is expressive enough to ask precise questions without over-reading files.
- `--toc` is one of the strongest features for structural exploration.
- `-e` works well for jumping from a symbol to an expanded definition.
- In practice, `qi` supports a productive exploration loop:
  - `qi '*' -f file --toc`
  - `qi symbol -f file`
  - `qi symbol -f file -e`
  - `qi % -f file -i ... -v`

Main UX Gap
- When a symbol does not appear in results, it is not always obvious whether:
  - the symbol is truly absent from the index
  - the symbol was excluded by stopwords
  - the symbol was excluded by the language keyword filter
  - the symbol was excluded because it is too short to index
  - the current query flags filtered it out

- This ambiguity creates avoidable friction for both humans and coding agents.
- The highest-value improvement is better explanation for intentional exclusions.

Proposal
- When a query returns no results because the symbol is excluded, print the exact reason and source.
- Keep the behavior deterministic and explicit.
- Do not add fuzzy search, concept matching, or suggestion-heavy behavior.

Recommended Output Behavior

Case: excluded by shared stopwords
```text
Pattern 'any' matched 0 indexed occurrences.
Reason: excluded by global stopword filter
Source: shared/config/stopwords.txt
```

Case: excluded by language keyword filter
```text
Pattern 'printf' matched 0 indexed occurrences.
Reason: excluded by Perl keyword filter
Source: perl/config/keywords.txt
```

Case: excluded by minimum symbol length
```text
Pattern 'x' matched 0 indexed occurrences.
Reason: one-character symbols are not indexed
```

Case: symbol exists in source but current query flags exclude it
```text
Pattern 'foo' matched 0 indexed occurrences with the current filters.
Reason: filtered out by query constraints
Hint: retry without `-i` / `-x` / `-f` restrictions
```

Case: truly absent from the index
```text
Pattern 'missing_symbol' matched 0 indexed occurrences.
Reason: no indexed matches found
```

Priorities
- Priority 1: surface exact exclusion source for stopwords and keywords
- Priority 2: distinguish exclusion by indexing rules from exclusion by query flags
- Priority 3: make “truly absent” clearly different from “intentionally excluded”

Notes on Auditing
- A separate “index audit” mode may not be necessary.
- For parser/indexer validation, a practical audit path already exists:
  - `qi % -f file.pl -v`
- If verbose output remains stable and complete, that is probably sufficient for audit-style inspection.

Non-Goals
- Fuzzy matching
- Concept search
- Intent-based search
- Suggestion-heavy output

Summary
- `qi` is already effective for deterministic code navigation.
- The main UX improvement is explainability when symbols are intentionally absent from the index.
- The tool should tell the user exactly why a query is empty, and where that exclusion came from.
