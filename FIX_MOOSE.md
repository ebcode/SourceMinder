Moose attribute declarations in Perl are not being indexed at the declaration site.

Repro file:
- `./tools/sources/perl/oop-moose.pl`

Expected:
- Attribute names declared with `has 'name'`, `has 'age'`, `has 'email'`, `has 'company'`, and `has 'salary'`
  should be indexed as declarations.

Actual:
- The declaration sites index `has` as `CALL`.
- The attribute names appear as `STR` at declaration time or as `CALL` later when accessed as methods.

Examples:
- `qi has_email -f ./tools/sources/perl/oop-moose.pl` returns only `STR` on line 22.
- `qi company -f ./tools/sources/perl/oop-moose.pl` returns `STR` on line 45 and `CALL` on line 59.
- `qi '*' -f ./tools/sources/perl/oop-moose.pl -i ns imp func arg var call prop --limit 500`
  shows the `has` calls and option keys, but not attribute declarations for the declared field names.

Implementation findings in `perl/perl_language.c`:
- `handle_function_call()` indexes the callee name as `CONTEXT_CALL` and then recurses, which explains
  why `has` is recorded as a call but the declared Moose attribute name is not recorded as a definition.
- `visit_node()` has no Moose-specific dispatch or declaration handling path for `has 'attr' => (...)`.
- `handle_autoquoted_bareword()` indexes barewords generically as `CONTEXT_PROPERTY`, which is not
  sufficient for Moose attribute declarations because it does not mark them as definitions.
- `index_constant_names()` is a useful reference pattern: it walks syntax beneath a higher-level form
  and emits declaration entries for discovered names.

Likely fix area:
- Add a Moose-specific special case for `has 'attr' => (...)`, likely in `handle_function_call()` or
  in a new helper it calls.
- When the callee is `has`, index the attribute name as a declaration instead of leaving it only as
  string content or later method usage.
