# QI Session Summary

[This summary was written by ChatGPT v5.4 on 5/11/2026]

## Purpose of this summary

This summary captures how `qi` was used during this session across several real Perl-heavy repositories, where it helped, where it got in the way, and whether it feels strong enough to become part of a normal command-line toolbelt.

The focus here is practical rather than theoretical:

- What kinds of questions could `qi` answer quickly?
- What kinds of questions required awkward workarounds?
- Where did `qi` reveal product gaps versus indexing gaps?
- What changes would most improve its usefulness, excluding call-graph work?

The repositories and scenarios exercised here were a good stress test because they were not all the same kind of project:

- `exiftool`: large, real-world Perl codebase with a major module and extensionless scripts
- `FlameGraph`: Perl scripts with substantial top-level logic and parser-style heuristics
- `poe`: older framework-style Perl codebase with many modules and subsystem families
- `cloc`: triggered a tree-sitter-perl crash during indexing, which surfaced parser/grammar boundary issues

## High-level conclusion

`qi` was genuinely useful in this session.

It was especially effective for:

- rapid structural orientation
- identifying architectural centers of gravity
- finding definitions and usages with less noise than plain text search
- comparing modules and subsystems by shape
- testing whether a new language indexer is "good enough to get bearings"

It was less effective for:

- top-level script control flow
- variable-driven reasoning in long Perl scripts
- questions whose answer depends on guard conditions, branch behavior, or state mutation rather than on named definitions
- situations where the right query pattern was not already obvious

If I had access to this tool in a normal engineering environment, I would absolutely use it again.

I would probably add it to my toolbelt, with an important caveat:

- I would use it as a first-pass code navigation and orientation tool.
- I would not treat it as a replacement for careful code reading.

The best framing for `qi` is:

> better than grep for structural discovery, but best when paired with targeted reading

## How `qi` was used in this session

### 1. Initial help and workflow discovery

The first thing done was to run:

```bash
qi --help
```

This established the query model:

- symbol-oriented search
- context filters (`-i`, `-x`)
- file filters (`-f`)
- definition/usage separation (`--def`, `--usage`)
- scoped search (`--within`)
- file TOC (`--toc`)
- definition expansion (`-e`)
- context expansion (`-C`, `-A`, `-B`)

That was important because it changed the working approach. Instead of treating the codebase like text and using search + file reads, the codebase could be treated as a symbol index with structured metadata.

### 2. Using `qi` to evaluate Perl support on `exiftool`

This was one of the most important uses of `qi` in the session.

The goal was not just to find one symbol. The goal was to determine whether the new Perl indexer was good enough that someone could get their bearings in a large real Perl codebase using `qi`.

Questions that were explicitly tested:

- What are the main entry points?
- Where is symbol `X` defined?
- Who calls function `Y`?
- What functions are in this file?
- What symbols are used within function `Z`?

Examples of effective `qi` usage in `exiftool`:

- `qi '*' -f lib/Image/ExifTool.pm --toc`
- `qi ImageInfo -i func -e`
- `qi ImageInfo --usage -x noise`
- `qi % -w ImageInfo -x noise`
- `qi '*' -f exiftool --toc`
- `qi GetImageInfo -i func -e`
- `qi ImageInfo --within GetImageInfo -x noise`

This was a strong demonstration of `qi`'s strengths.

It answered the main orientation questions well once indexing problems were fixed.

In particular:

- `--toc` made huge modules understandable quickly.
- `--usage -x noise` was excellent for identifying real callsites rather than comment/string noise.
- `-e` made definition expansion useful enough that reading the whole file was not always necessary.
- once `--within` worked, scoped exploration became much more powerful.

### 3. Using `qi` to debug and validate tooling problems

The session was not just passive code exploration. `qi` was used to help locate SourceMinder bugs and validate fixes.

Examples:

#### Extensionless Perl files were not indexed

The `exiftool` script itself was not indexed because the Perl indexer only accepted `.pl`, `.pm`, and `.t` files.

`qi` was used to establish that:

- the main script was missing from the index
- the large module was present
- architecture-level orientation was incomplete without extensionless scripts

After fixing SourceMinder’s file-matching behavior, `qi` could successfully show:

- `qi '*' -f exiftool --toc`

That materially changed how usable the tool was on a real Perl repo.

#### Large POD blocks crashed the Perl parser

The `exiftool` script triggered a failure in `handle_pod` because a POD node exceeded the fixed extraction buffer.

While the actual fix required code changes and reading the parser source, `qi` helped verify whether the resulting codebase became navigable afterward.

#### `--within` was broken

This turned out to be a very important product issue.

The initial symptom was:

- `qi % -w ImageInfo -x noise`
  failed with `No definition found for symbol 'ImageInfo'`

`qi` itself was then used as part of debugging:

- confirm the definition row existed
- compare successful lowercase lookups against failing mixed-case lookups
- validate the repaired behavior on both module functions and script functions

The root cause was that `--within` lookup used raw symbol text while the database stored normalized lowercase symbols.

After fixing that, the same `qi` workflows worked correctly.

This was a good example of using the tool both as a user-facing feature and as a self-test surface.

### 4. Using `qi` to explore FlameGraph

FlameGraph was probably the best stress test of where `qi` is useful and where it struggles.

FlameGraph is not primarily a deep module hierarchy. It is a script-heavy Perl codebase where important behavior often lives in top-level code rather than in lots of named functions.

That exposed both the strengths and weaknesses of `qi` very clearly.

#### What `qi` did well in FlameGraph

It was very good at quickly identifying the major code boundaries:

- `flamegraph.pl` is the main renderer
- `stackcollapse-*` scripts are parser/normalizer/aggregator front-ends
- `difffolded.pl` is structurally small and simple

Useful queries included:

- `qi '*' -f FlameGraph/flamegraph.pl --toc`
- `qi '*' -f FlameGraph/stackcollapse-perf.pl --toc`
- `qi '*' -f FlameGraph/difffolded.pl --toc`
- `qi svg -x noise --files`
- `qi color -f FlameGraph/flamegraph.pl -C 4`
- `qi flow -i func -f FlameGraph/flamegraph.pl -e`
- `qi GetOptions -x noise --limit 20`

This was enough to answer questions like:

- Where is SVG generation implemented?
- Where are colors chosen?
- Which script is the renderer and which are parsers?
- Which stackcollapse scripts are structurally closest?
- Where is the real boundary between parser scripts and renderer logic?

That is exactly the kind of "come up to speed quickly" work where `qi` shines.

#### Where `qi` struggled in FlameGraph

The harder questions in FlameGraph were not about named API structure. They were about behavior encoded in top-level script flow and variable-dependent branches.

Examples:

- Why does removing `comm` from `perf script` make the graph empty?
- Why do user-defined Rust functions not appear in a flame graph?
- Why is `stackcollapse-perf.pl` memory-hungry?
- What parts of FlameGraph knowingly trade correctness for practicality?

`qi` could answer these, but less directly.

The difficulty was not symbol extraction quality. The difficulty was that the most important facts were things like:

- `$pname` must exist before a sample is emitted
- `include_pname` controls whether process names are prepended
- unknown symbols are rewritten for best-effort output
- event types are default-filtered to the first encountered type
- narrow frames are pruned below `minwidth`

These are not naturally "definition/usage" questions.

They are closer to:

- where is this variable assigned?
- where is it checked?
- what branch causes data to be dropped?
- what policy choice is encoded in this guard?

`qi` could help by searching for the variable names and showing context, but it did not provide a first-class way to ask those questions.

That is the single biggest product gap that FlameGraph exposed.

### 5. Using `qi` to reason about POE

POE was a different kind of test: a framework-style module hierarchy rather than a script-heavy utility repo.

Here, `qi` felt especially strong.

The README and file layout suggested a vocabulary:

- kernel
- session
- wheels
- filters
- drivers
- loops

Using TOCs on core modules immediately paid off:

- `qi '*' -f poe/poe/lib/POE.pm --toc`
- `qi '*' -f poe/poe/lib/POE/Kernel.pm --toc`
- `qi '*' -f poe/poe/lib/POE/Session.pm --toc`
- `qi '*' -f poe/poe/lib/POE/Wheel/ --toc`
- `qi '*' -f poe/poe/lib/POE/Filter/ --toc`

That made it possible to answer reasonably deep architectural questions using `qi` alone:

- how event loop integration is structured
- where timers and I/O multiplexing live
- how sessions dispatch handlers

POE was a strong example of when `qi` is more efficient than reading files blindly.

The module boundaries were clean enough that `--toc`, `-e`, and targeted function expansion answered a lot without needing broad file reads.

### 6. Handling a tree-sitter-perl crash while indexing `cloc`

The `cloc` indexing attempt surfaced another useful distinction.

The crash:

```text
ts_parser__external_scanner_serialize: Assertion `length <= 1024' failed.
```

This was not a `qi` usability problem at all.

It was a parser implementation problem in upstream `tree-sitter-perl`.

Still, the broader SourceMinder workflow mattered here:

- identify whether the bug was in SourceMinder or the grammar
- inspect the grammar’s external scanner code
- reason about how serialization exceeded tree-sitter’s 1024-byte limit
- record a concrete upstream fix in `FIX_TREESITTER_PERL.md`

This reinforced a useful boundary:

- `qi` helps once code is indexed
- but parser/indexing crashes are below `qi`’s level and need direct implementation work

## Where `qi` was especially useful

### A. Table of contents discovery

`--toc` was probably the single most valuable feature in the session.

It made large code files legible quickly.

This was especially effective in:

- `lib/Image/ExifTool.pm`
- `FlameGraph/flamegraph.pl`
- `POE::Kernel`
- `POE::Session`
- the `POE::Wheel` and `POE::Filter` families

Without `--toc`, the workflow would have involved much more blind opening of files and scrolling.

### B. Definition and usage separation

`--def` and `--usage` were very useful.

Examples:

- locating `ImageInfo`'s definition in `exiftool`
- seeing where `ImageInfo` was used inside the CLI scripts
- checking who calls `ExtractInfo`
- seeing where `GetOptions` was used across FlameGraph scripts

This is far cleaner than plain grep when a symbol name appears in comments, strings, documentation, or other contexts that are not behaviorally meaningful.

### C. Noise control with `-x noise`

This was consistently helpful.

It reduced false positives a lot, especially in Perl code where comments and string literals frequently contain symbol-like words.

### D. Scoped search with `--within`

Once fixed, `--within` was very valuable.

It materially improved the `exiftool` validation story because it enabled:

- exploring local symbol usage in `ImageInfo`
- exploring where `ImageInfo` was called within `GetImageInfo`

Without `--within`, the tool would still have been useful, but much less compelling as a code-navigation system.

### E. Cross-file family comparison

This was especially useful in FlameGraph and POE.

Examples:

- comparing `stackcollapse-*` scripts by structural shape
- seeing that `POE::Filter` modules share a stable interface pattern
- seeing that `POE::Wheel` modules vary widely in complexity and responsibilities

This kind of structural comparison is painful with plain grep and ad hoc reading.

## Where `qi` fell short

### A. Top-level script logic

This is the biggest weakness that showed up.

FlameGraph is full of important behavior in top-level loops and script state.

Examples of hard questions:

- Why is a sample dropped?
- What variable gates emission?
- What happens if `comm` is removed?
- What fallback is applied when a function is `[unknown]`?

`qi` can find the relevant variable names and nearby context, but it does not treat top-level program structure as a first-class navigable object.

That makes script-heavy repos harder than module-heavy repos.

### B. Variable lifecycle reasoning

The FlameGraph issues often reduced to variable lifecycle questions.

Examples:

- where is `$pname` set?
- where is `$pname` required?
- where is `$pname` cleared?
- what options affect `$pname`?

`qi` can approximate this with repeated symbol searches and context, but there is no direct mode for it.

This is a major opportunity for improvement.

### C. Query intent had to be supplied by the user/operator

`qi` works best when the user already has a pretty good guess about what symbol, variable, or subsystem to search for.

If the right concept name is not obvious, `qi` is less helpful.

This is where TOC helps, but only if the file boundaries are already known.

### D. Raw namespace discovery was sometimes weak

In POE, trying to discover things through raw namespace searches like:

- `qi Wheel -i ns`
- `qi Filter -i ns`

was less helpful than using TOC over known directory structures.

That is not necessarily wrong, but it shows that not all indexed metadata is equally useful for practical orientation.

### E. Some questions remained inferential without careful reading

For some FlameGraph issues, `qi` got me to a very plausible answer, but not always to the level of absolute certainty one gets from carefully reading the relevant file end-to-end.

In those cases, the weak point was not that `qi` was wrong. It was that the answer depended on control flow and ordering rather than just symbol presence.

## Would I use `qi` again?

Yes.

Unequivocally yes.

This session was enough to convince me that `qi` is genuinely useful.

It is not a gimmick. It meaningfully changes the first 10-30 minutes of codebase orientation.

It is especially effective for:

- inherited codebases
- large modules
- unfamiliar subsystem comparisons
- language-indexer validation
- narrowing reading targets before opening files

## Would I add it to my own command-line toolbelt?

Yes, with qualification.

I would not replace grep with `qi` outright.

I would use them for different jobs:

- `qi` for code-structure navigation and subsystem discovery
- grep/ripgrep for raw text, messages, prose, or regex-heavy search
- direct reading once the right file or function has been identified

If I were doing day-to-day engineering on polyglot codebases, I would want `qi` available.

The biggest factor in that judgment is not just that it found symbols.

It is that it repeatedly reduced the amount of irrelevant material I had to open.

That is a real productivity win.

## How `qi` compares to working without it

Without `qi`, the fallback workflow would have been:

- list directories
- search for strings or file names
- read likely files
- refine the search
- keep repeating

That would have worked.

But it would have been slower and noisier.

Compared to that workflow, `qi` was better at:

- surfacing the true core modules quickly
- reducing false positives
- distinguishing definitions from usages
- comparing code shape across related files
- giving a fast file-level map via `--toc`

Direct reading is still better for detailed semantics.

So the best workflow is:

1. use `qi` for discovery and narrowing
2. then read precisely

That combination is stronger than either one alone.

## Feature ideas suggested by the session

The following ideas came directly out of the gaps exposed during `exiftool`, FlameGraph, and POE work.

Call graph work is intentionally excluded.

### 1. Synthetic top-level scope for script files

This is the single most compelling feature idea from the session.

Problem:

- many Perl scripts keep important logic at top level rather than in functions
- `--within` works great for named subs, but not for top-level control flow

Possible feature:

- introduce a synthetic scope such as `__top_level__` or `__main__`
- let users run queries like:

```bash
qi pname --within __top_level__ -f FlameGraph/stackcollapse-perf.pl
```

Why this matters:

- it would make script-heavy codebases dramatically easier to navigate
- it would turn top-level logic into a first-class exploration target

### 2. Top-level TOC sections for scripts

Related but even lighter-weight.

Possible feature:

- teach `--toc` to include synthetic sections in script files, such as:
  - option parsing
  - main input loop
  - output/flush section
  - cleanup/finalization section

Even just a single `TOP-LEVEL LOGIC` range would help.

This seems lower-effort than full top-level scope support and would still provide a big usability win.

### 3. Variable lifecycle query mode

Problem:

- many difficult real-world questions are about stateful variables, not just named functions

Desired capability:

- show assignments, guard checks, and major uses of a variable within a file or scope

Example desired experience:

```bash
qi --var-flow pname -f FlameGraph/stackcollapse-perf.pl
```

Expected output conceptually:

- where `pname` is assigned
- where it is checked (`next if not $pname`)
- where it is prepended into a stack
- where it is reset

This would have made several FlameGraph issue answers much more direct.

### 4. Guard/branch-oriented search mode

Problem:

- some of the most important logic in scripts is in branch guards and early exits

Desired capability:

- highlight control-affecting uses of a symbol, such as:
  - `if (...)`
  - `unless (...)`
  - `next if ...`
  - `return if ...`
  - `die/warn` paths

This would be especially useful in issue triage and failure analysis.

### 5. Better docs for script-heavy workflows

This is one of the easiest wins.

The FlameGraph work showed that `qi` already has enough capability to do better than I initially used it for, particularly with:

- `--toc`
- `-e`
- `-C`, `-A`, `-B`

What is missing is a clearly documented workflow for script-heavy repos.

Suggested docs section:

- "Using `qi` on big Perl scripts"
- show how to use:
  - `--toc` for structure
  - `-e` on helper functions
  - generous `-C` around important state variables
  - `--within` once a relevant function is identified

This is likely a high-impact, low-effort improvement.

### 6. Better "why no results?" guidance

When a query fails, especially a structurally meaningful one like `--within`, the tool could be more helpful.

Examples:

- suggest trying lowercase normalization when appropriate
- suggest nearby files or definitions if a symbol exists under normalized form
- distinguish between:
  - symbol not indexed
  - symbol indexed only as usage
  - symbol indexed but lacking definition range

This would have made the earlier `--within` debugging path shorter.

### 7. More useful namespace/family discovery

In POE, namespace-oriented searches were less useful than TOC over known directory structure.

Possible feature:

- a file-family or directory-family TOC summary mode
- something like:

```bash
qi --toc -f poe/poe/lib/POE/Wheel/
```

but grouped into a more compact comparative summary:

- file name
- number of functions
- maybe top few exported or defining symbols

This would make subsystem comparison easier.

### 8. More explicit support for policy-decision discovery

The FlameGraph analysis repeatedly found important design decisions encoded in:

- default values
- booleans
- guard branches
- fallback rewrites

Possible feature:

- a mode for surfacing variables that:
  - are initialized near the top of the file
  - are referenced in branches later
  - are set via CLI flags or config

That would make "find hidden toggles" much easier.

### 9. Better help/examples for debugging by subsystem contract

POE showed a very successful pattern:

- inspect TOC of core module
- inspect TOC of adapter family
- expand the bridging functions between them

This pattern could be codified in examples.

For instance, docs could include examples like:

- "Find the boundary between API layer and backend implementation"
- "Find the runtime core and its adapters"
- "Compare sibling modules by shape"

That would teach users how to use `qi` in a way that goes beyond symbol lookup.

## What I would change about my own `qi` usage next time

This session also highlighted places where I could use the tool better.

### 1. Use `-e` more aggressively once the right function is known

I underused definition expansion in some of the script-heavy investigations.

Once a relevant helper was identified, expanding it would often have been better than doing more scattered symbol queries.

### 2. Use wider `-C` around state variables in scripts

For FlameGraph-like repos, a better workflow is:

- locate the key variable
- use large context windows
- treat that as a structured read of the relevant logic region

### 3. Prefer TOC over namespace search for framework-style repos

POE made it clear that file-level TOC exploration is often more useful than naive namespace searches.

### 4. Treat `qi` as a structured reader, not just a search tool

The more successful parts of this session used `qi` to answer structural questions.

The less successful parts came from treating it too much like a fancy grep.

## Final judgment

Would I use `qi` again?

Yes.

Would I add it to my normal toolbelt?

Yes.

Would I trust it as a sole source of understanding?

No.

Would I trust it as a first-pass structural navigation system that makes later reading much more efficient?

Absolutely.

That is the right way to think about it.

The strongest evidence from this session is that `qi` repeatedly shortened the path to the relevant code.

That is enough to justify having it installed and reaching for it early.

The biggest product opportunity is to make it better at script-level reasoning and variable-driven behavior, because that is where it most obviously fell short during real issue investigation.

If those gaps were addressed, `qi` would become substantially more powerful for exactly the kinds of practical debugging and code-orientation tasks that came up in this session.
