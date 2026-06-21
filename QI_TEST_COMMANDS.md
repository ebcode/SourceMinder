# QI Test Commands

Commands to exercise `./qi` as a repository exploration tool and evaluate user-facing output quality.

## Orientation

1. `./qi --help`
2. `./qi % --files --limit 20`
3. `./qi % -f query-index.c --toc`
4. `./qi % -f shared/toc.c --toc --debug`

## Symbol Lookup

5. `./qi build_toc --def -e --raw`
6. `./qi print_imports --def -e`
7. `./qi TocConfig --def -e`
8. `./qi debug -f query-index.c shared/toc.c --columns line,sym,ctx,d`

## Filtering And Structure

9. `./qi % -f shared/toc.c --toc -i func`
10. `./qi % -f shared/toc.c --toc -i imp`
11. `./qi % -f query-index.c -i imp --columns line,sym,ctx`
12. `./qi % -f query-index.c --toc --limit 10`

## Edge Cases

13. `./qi '\--toc' '\--debug'`
14. `./qi nonexistent_symbol_hopefully --def`
15. `./qi % -f no-such-file.c --toc`
