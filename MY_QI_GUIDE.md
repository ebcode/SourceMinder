# MY_QI_GUIDE — notes to future self on using `qi`

`qi` is the code-search tool to reach for in this repo (prefer it over grep/find/Read
for code navigation). This is hard-won operational knowledge — read it before assuming
how the tool behaves. Start a session with `qi --help` and `qi --list-types`.

## Mental model (internalize this first)

`qi` is a **tree-sitter → SQLite symbol index**, not a text searcher. One flat
`code_index` table queried with `symbol LIKE ? ESCAPE '\'`. It indexes **identifiers +
structured metadata** (parent, scope/visibility, namespace, modifier, return/field type,
context-kind, clue/attributes) plus **word-tokenized comments and strings**. It does NOT
store raw file text. `qi --debug <query>` shows the actual SQL and the column set.

So: it answers *"where is this symbol, what kind is it, what's related to it"* — not
*"what lines contain this text."*

## When to fall back to grep / ripgrep (qi can't do these)

- **Literal phrases** — `qi 'page not found'` is refused (word boundaries). `--and` only
  does *unordered same-line (or within-N-lines) co-occurrence*, not an ordered phrase.
- **Regex / true substring across word boundaries** — not supported.
- **Multiline patterns** — not supported.
- **Case-sensitive matching** — qi is case-insensitive only (`genome` == `Genome`).
- **Comment/string *content* beyond a single word** — only individual words are indexed.
- **Non-`.rs` files** — this DB indexes Rust only. No `.toml` / `.md` / `.json`.

## Power patterns (qi's real strengths — use these)

| Goal | Command |
|------|---------|
| Jump to definition | `qi NAME --def -e --raw` (`--raw` = clean source for Edit anchors) |
| Find references / call sites | `qi NAME --usage` |
| A type's whole API (fields + methods) | `qi '*' -p TypeName` |
| One method of a type | `qi method -p TypeName` |
| Fields/vars by type annotation | `qi '*' -t 'Vec<*>' -i prop` |
| Search inside a definition's span | `qi '*' -w SYMBOL -i call` |
| File structure / outline | `qi % -f path/to/file.rs --toc` |
| Restrict to a kind | `-i func class enum prop call com str` (see `--list-types`) |
| Drop comments+strings | `-x noise` |
| Co-occurrence on a line | `qi word1 word2 --and` (add a range: `--and 3`) |
| All metadata columns | `-v` (or pick: `--columns line sym par type scope mod clue`) |

Multiple patterns = **OR** (`qi mutate crossover` returns both).
Filters AND together (context + parent + type + file + modifier).

## Quirks / gotchas to remember

- **Case-insensitive.** Disambiguate a type from a same-named var/module with `-i`
  (e.g. `-i class` for the type, `-i var` for the binding).
- **`_` and `.` are single-char wildcards** (so are `%` and `*` for "any"). Nearly every
  Rust identifier has `_`, so `run_fst` ≡ `run.fst`. Usually a harmless superset, but it
  can pull false positives; there's no clean way to force a literal `_`.
- **Auto-fallback:** an exact match with no hits silently retries as a `*substring*`
  search ("Retrying with partial matches…").
- **Visibility is `-s pub`, not `-s public`** (the `--help` example is C/C++-centric).
  For functions, `pub` lives in the `scope` column; for `mod` declarations it lands in
  `modifier` instead — inconsistent, so check both with `-v` if filtering by visibility.
- **`--parent-type` is unreliable on Rust** (returned 0 on real impls). Use `-p NAME`
  (parent *symbol* name) instead to enumerate a type's members.
- **Speed:** ~0.35s flat (startup-dominated at this scale). Prefer prefix patterns
  (`get*`) over leading-wildcard (`*get*`) on bigger indexes — leading `%` can't use an
  index.
- Config defaults live in `~/.smconfig` `[qi]` (this repo defaults to `-q`, quiet chrome).

## TL;DR decision rule

Use **qi** for: jump-to-def, find-refs, "what's the API of X", pattern/type-filtered
symbol search, file outlines, attribute discovery, and pulling clean source for edits.
Use **grep/ripgrep** for: literal phrases, regex, case-sensitive, multiline, string/comment
content, and any non-`.rs` file.
