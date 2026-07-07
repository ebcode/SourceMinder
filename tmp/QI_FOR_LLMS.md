# qi for LLM Agents

## Why qi?

grep searches lines of text. qi searches code *structure*. It knows about
functions, arguments, types, parents, and calls — so you can ask questions
grep can't answer, in one command.

## When to use qi

| Task | Command |
|------|---------|
| List all functions/types/imports in a file | `qi -f <file> --toc` |
| Find a definition anywhere in the codebase | `qi <name> --def -e` |
| See all callers of a function | `qi <name> --usage` |
| Scope a search to one function body | `qi '*' -x noise --within <func> -v` |
| Find lines where two symbols co-occur | `qi <a> <b> --and` |
| Filter by symbol type | `qi <pat> -i FUNC` |
| See symbols on lines N-M of a file | `qi '*' -f <file> --lines N-M -v` |

Tip: add `-v` to any query to see parent, scope, type, and modifier columns.

## When NOT to use qi

qi indexes *symbols*, not arbitrary text. For searching within file contents
(text that isn't an identifier), use grep or sed.
