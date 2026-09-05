# sm-golden

Catches unintended changes to indexer output. Indexes each file into a scratch
database, reads the rows back through `qi`, and compares them to a committed
snapshot.

## Build

    make test/sm-golden

## Verify

    ./test/sm-golden tools/sources/ruby/*.rb

PASS or FAIL per file, with a count at the end. Each FAIL prints a diff of
`test/golden/<path>.snapshot` against the current output: a `-` line is a row
the golden has and this build no longer produces, a `+` line is a new one.

## Bless

    ./test/sm-golden --update tools/sources/ruby/*.rb

Read the diff before committing a re-bless. Every changed line is a row the
indexer now reports differently, and that is the point of the tool.

Bless only files under `tools/sources/`. Real corpora change underneath you, so
their snapshots are unreviewable.

## Snapshots

A snapshot is what one qi command prints, byte for byte:

    ./qi % --no-config -q -v --db-file <scratch>

Nothing is trimmed, sorted, or dropped, so you can `cat` a golden and read it
next to the terminal with nothing in between.

Expect whole-file diffs. qi sizes each column to the widest value it prints, so
one new row repads every line. That is fine here because the files under
`tools/sources/` are kept short.

Two things follow from storing display text:

- Most lines end in spaces. `.gitattributes` keeps git from flagging or fixing
  them. Do not let an editor strip trailing whitespace in `test/golden/`.
- A snapshot is not data. Values can contain the `|` separator, so do not write
  anything that parses one back into columns.

Both the indexer and `qi` run with `--no-config`, so a `.smconfig` won't move
rows. Scratch files go in `./tmp`.
