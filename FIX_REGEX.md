Named capture declarations in Perl regexes are not being indexed at the declaration site.

Repro file:
- `./tools/sources/perl/regex.pl`

Expected:
- The capture names in `(?<year>\d{4})-(?<month>\d{2})-(?<day>\d{2})` on line 20 should be indexed.

Actual:
- `qi year -f ./tools/sources/perl/regex.pl`
- `qi month -f ./tools/sources/perl/regex.pl`
- `qi day -f ./tools/sources/perl/regex.pl`

Only return matches on line 21 from `$+{year}`, `$+{month}`, and `$+{day}`.
There are no indexed matches at the declaration site on line 20.

Implementation findings in `perl/perl_language.c`:
- `visit_node()` has no regex-specific dispatch path. It handles declarations, subscripts, calls,
  strings, comments, POD, labels, and similar node types, but nothing for regex constructs.
- `init_perl_symbols()` does not register any regex-specific tree-sitter symbol names, which matches
  the absence of a regex handler.
- `handle_subscript_expression()` explains why `$+{year}` is indexed: it treats autoquoted hash keys
  inside subscript expressions as `CONTEXT_PROPERTY`.

Likely fix area:
- Add regex-related symbol registration in `init_perl_symbols()`.
- Add a regex-specific handler and dispatch to it from `visit_node()`.
- That handler should index named capture declarations such as `(?<year>...)`, `(?<month>...)`,
  and `(?<day>...)` at the declaration site.
