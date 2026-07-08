# Fix function-valued-binding & import coverage in the TS-JS indexer

> Originally scoped as "CommonJS coverage," but probing NodeBB showed the root
> cause is broader and not CommonJS-specific (see Census). The unifying defect is:
> **function expressions bound to a name are not recognized as functions**, plus
> **imports/`require` are not modeled**. CommonJS just makes it impossible to miss.

## Context

The `typescript/` indexer (`index-ts-static`, used for `.ts/.tsx/.js/.jsx/.mjs`)
recognizes `function foo(){}` declarations and class methods as `FUNC`, but a
*function expression assigned to a name* (a variable, a member, or an object-literal
key) is recorded as the binding (`VAR`/`PROP`) **plus a separate anonymous
`<lambda>` `LAM`** — never a named `FUNC`. It also doesn't model `require`/default
imports, and skips generators and accessors entirely.

NodeBB (first heavily-CommonJS repo we've indexed) made the scale obvious.

### Census — the smoking gun (517 files, `tmp/nodebb/app/src`)

```
FUNC   771      <-- named functions found
LAM   3916      <-- anonymous <lambda>; ~84% of functions lost their names
IMP      0      <-- entire import graph invisible to `qi -i imp`
CLASS    0      <-- correct: NodeBB has no ES classes (detection works)
```

### The gap table (✅ works / ⛔ broken; confirmed by full gamut run — `TEST_COMMONJS_QI_QUERIES.md`)

| Idiom | Should be | Currently |
|---|---|---|
| `function foo(){}` | `FUNC` | ✅ `FUNC` |
| class method / `static` | `FUNC` (parent class) | ✅ `FUNC` |
| object shorthand method `{ m(){} }` | `FUNC` | ✅ `FUNC` |
| `import { name }` (plain named import) | `IMP` | ✅ `IMP` |
| genuine callback `arr.map(x => …)` | anonymous `LAM` | ✅ `LAM` (correct) |
| `const x = () => {}` / `= function(){}` | `FUNC` | ⛔ `VAR` + anon `LAM` |
| `const x = function named(){}` | `FUNC` (name kept) | ⛔ `VAR` + anon `LAM`; inner `named` lost |
| `obj.prop = function(){}` / `= () => {}` | `FUNC` (parent obj) | ⛔ `PROP` + anon `LAM` |
| object-literal `{ foo: () => {}, bar: function(){} }` | `FUNC` (parent obj) | ⛔ `PROP` |
| `function* g(){}` / `async function* g(){}` | `FUNC` | ⛔ **not indexed at all** |
| `const g = function*(){}` | `FUNC` | ⛔ `VAR` + (generator body silent) |
| class `get x()` / `set x(v)` | `FUNC` (accessor) | ✅ already works (test fixture used 1-char `x` which is filtered by `MIN_SYMBOL_LENGTH`) |
| `const x = require('…')` | `IMP` | ⛔ `VAR` (module path as `STR`) |
| `const { a, b } = require('…')` | `a`, `b` → `IMP` | ⛔ **not indexed at all** (not even `VAR`) |
| `import x from '…'` (default) | `IMP` | ⛔ *missing entirely* |
| `import { name as alias }` (aliased named import) | `alias` → `IMP` | ⛔ original `name` indexed; local `alias` missing |

**Downstream navigation breakage (consequence of PROP misclassification):**
- `qi --within <sym>` emits "Invalid source_location format" warnings and falls back to
  all occurrences of the symbol when the definition is `PROP` (PROP rows have no span).
  Fixing the classification (PROP→FUNC) also fixes `--within` automatically.

Consequences: `--toc` is empty for nearly every file; ~84% of functions are
unnamed `LAM`; `qi -i func` misses most functions; `qi -i imp` returns nothing.

## Settled decisions

- **No double-indexing.** A symbol is exactly one context. A function-valued
  binding is a `FUNC` (not VAR/PROP **and** LAM); a `require` binding is an `IMP`
  (not VAR). Emit **one** row and **suppress** the now-redundant anonymous `LAM`.
- **Only name function expressions that are bound to a name.** Promote to `FUNC`
  when the function/arrow is the RHS of an assignment, a variable initializer, or an
  object-literal property value. **Genuine anonymous callbacks** (`arr.map(x => …)`,
  promise `.then(…)`, etc.) **stay anonymous `LAM`.**
- Use the **binding name** as the `FUNC` symbol; set `parent_symbol` to the owning
  object where there is one (`UserEmail.exists` → parent `UserEmail`). The inner
  name of a named function expression (`function named(){}`) is optional/secondary.
- Imports: `require` binding → `IMP` only; each destructured name → `IMP`; module
  path stays a line-linked `STR`; dynamic `require(expr)` → binding still `IMP`.
- Implement and **verify each item separately** (see Verification). Each needs a
  rebuild (`make`) + re-index — ask the user; never build autonomously.

---

## Phase 1 — Handler fixes (no schema change)

### Group A — function-valued bindings → `FUNC`

- [ ] **A1. `const/let/var x = function/arrow` → `FUNC`** (the biggest; all modern
  JS/TS, not just CommonJS). In the variable-declaration path: if the initializer is
  a `function_expression`/`arrow_function`, emit the binding as `FUNC` (no VAR, no
  anon LAM). If the initializer is `require(...)`, emit `IMP` (see C1). Else `VAR`.
  - Verify: `const _validatePath = async () => {}` (`src/user/uploads.js:15`) → `FUNC`.
- [ ] **A2. `obj.prop = function/arrow` → `FUNC`** (parent = obj).
  `typescript/ts_language.c:handle_assignment_expression` (~L1932): when the `right`
  field is a function/arrow, emit the innermost LHS property as `CONTEXT_FUNCTION`
  (parent = object) instead of `PROP` at ~L2006; suppress the anon LAM. Non-function
  RHS (`obj.x = 5`) stays `PROP`.
  - Verify: `UserEmail.exists = async function(){}` (`src/user/email.js`) → `FUNC`
    parent `UserEmail`; `--toc` lists exists/available/remove/….
- [ ] **A3. object-literal function properties → `FUNC`** (parent = object).
  `{ foo: () => {}, bar: function(){} }` — the property value is a function/arrow.
  (Object-literal *method shorthand* `{ method(){} }` already works.)
  - Verify: `module.exports = { method: async function(){} }`
    (`src/upgrades/1.4.4/config_urls_update.js`) → `method` `FUNC`.
- [ ] **A4. named function expression keeps a name.** `const x = function named(){}`
  → `FUNC` named `x` (binding wins; inner `named` optional). No anon LAM.

### Group B — missing constructs

- [ ] **B1. generators** `function* g(){}`, `async function* g(){}` → `FUNC`
  (currently emit nothing).
- [x] **B2. class getters/setters** — already work. The test fixture used single-char
  `get x()` which `MIN_SYMBOL_LENGTH` filters. Verified: `get value()` indexes as
  `FUNC parent=ClassName`. No code change needed.

### Group C — imports

- [ ] **C1. `const x = require('…')` → `IMP`** (binding only). Detect initializer
  `call_expression` whose callee is `require`. Destructuring (`const { a, b } = require(…)`)
  → each destructured name `IMP` — gamut confirmed these are **not indexed at all**
  today, not even VAR. Dynamic arg → binding still `IMP`. Module path stays
  line-linked `STR`.
  - Verify: `qi % -i imp -f src/user/email.js` lists the 8 requires.
- [ ] **C2. ES default import binding → `IMP`.** `import x from '…'` currently drops
  `x`. `typescript/ts_language.c:handle_import_statement` (~L1077). Named/namespace
  already work (✅).
- [ ] **C3. ES aliased named import → index the local alias, not the original name.**
  `import { name as alias }` currently indexes `name` as `IMP`; `alias` (the symbol
  actually used in the file) is missing. Fix: emit `alias` as `IMP` (or both, but
  `alias` is the useful local binding). Same handler as C2.

### Group D — regression + roll-out

- [ ] Re-run `typescript/` fixtures / `tests/` goldens; regenerate the ones that
  shift (PROP→FUNC, VAR→IMP/FUNC) — don't gate on them ([[feedback_dont_gate_on_goldens]]).
- [ ] Re-index `tmp/nodebb/app/src`: FUNC should dominate, anon LAM drop to genuine
  callbacks, IMP in the hundreds, `--toc` populated.
- [ ] Re-index the NodeBB Pro instance(s) so live runs benefit.

### Note — module.exports export surface (Phase 3, not in scope now)

`module.exports = X` and `exports.foo = …` are the CJS export surface, but
qi has no way to answer "what does this module export?" — `exports` appears as
`PROP parent=module` but the exported value is not captured. Requires a new
context (`EXP`) or a column; deferred to Phase 2/3 with the resolved-source
column design.

---

## Phase 2 — General "resolved source" column (schema change, separate effort)

A function-valued / import binding is often renamed (`const db = require('../database')`
→ `db` really refers to `database`). Today binding name and source live on separate
rows (IMP/FUNC + STR), joined only by line proximity. A column on the row makes
`db ← ../database` first-class and reverse-queryable.

**Design (under discussion):**
- **General, not import-specific.** Same "local ← source" relation covers require,
  ES alias imports (`{readFile as rf}` → `rf ← readFile`), namespace imports
  (`* as React`). Name it generically (`import_source`/`source_name`/`origin`).
- **Reconcile with `QI_HINTS_AND_IMPORT_RESOLUTION_PLAN.md`'s proposed `full_target`
  column** — decide if these are the *same* column (don't add two).
- **Store the literal specifier** (`../database`, `lodash`) — lossless; derive the
  short basename at qi display time.
- Metadata on the single row — consistent with no-double-indexing.

**Blast radius (why it's its own phase):** schema migration (`shared/database.c`,
NULL-backward-compatible) · new `ExtColumns` field (`shared/parse_result.h`) ·
populate across all 8 indexers (or NULL) · qi display + filter flag + `--help` ·
full re-index · `-v` golden churn ([[project_schema_config_dependent]]).

---

## Verification

Run the **full gamut** in `TEST_COMMONJS_QI_QUERIES.md` (setup + categories A–F).
Three representative SQL probes cover most of the ground without enumerating every
query:

1. **Census (systemic gaps at a glance)** — context distribution + the key ratios:
   ```bash
   sqlite3 tmp/nodebb-full.db "SELECT context,count(*) FROM code_index GROUP BY context ORDER BY 2 DESC;"
   sqlite3 tmp/nodebb-full.db "SELECT 'FUNC',count(*) FROM code_index WHERE context='FUNC' UNION ALL SELECT 'anon LAM',count(*) FROM code_index WHERE context='LAM' AND symbol='<lambda>' UNION ALL SELECT 'IMP',count(*) FROM code_index WHERE context='IMP';"
   ```
   After the fixes: FUNC should dominate, anon LAM shrink to genuine callbacks, IMP > 0.

2. **Synthetic idiom truth-table (per-idiom classification, unambiguous)** — index
   `tools/sources/javascript/gaps.js` (canonical; one construct per block with
   inline expected/actual comments) and read the contexts back:
   ```bash
   rm -f tmp/gaps.db
   ./build/index-ts-static tools/sources/javascript/gaps.js --once -f tmp/gaps.db
   sqlite3 -column tmp/gaps.db "SELECT line,symbol,context,parent_symbol FROM code_index WHERE context IN ('FUNC','CLASS','LAM','PROP','VAR','IMP') ORDER BY line;"
   ```
   Each block maps to one row in the gap table above — the cleanest pass/fail per idiom.

3. **Targeted spot-check (a real symbol)** — confirm one known case before/after:
   ```bash
   sqlite3 -column tmp/nodebb-full.db "SELECT line,symbol,context,parent_symbol FROM code_index WHERE symbol='exists';"
   ```
   Expect `exists` `FUNC` parent `UserEmail` after A2 (today: `PROP` + anon `LAM`).

The pattern generalizes: **census** finds systemic regressions, the **synthetic
fixture** pins down each idiom, and a **targeted symbol query** verifies a specific
real-world case. Add `--db-file` qi queries (e.g. `qi -f … --toc`, `qi % -i imp`)
only where you want to confirm the *user-facing* behavior on top of the raw rows.

---

## References
- Full query gamut: `TEST_COMMONJS_QI_QUERIES.md`
- Canonical gap fixture: `tools/sources/javascript/gaps.js` — one construct per block,
  with inline `// expected:` / `// actual:` comments; use this for per-item verification
  after each fix (shorter / more targeted than the full NodeBB census).
- Live codebase fixture: `tmp/nodebb/app` (extract cmd in `TEST_COMMONJS_QI_QUERIES.md`)
- Synthetic idiom scratch file: `tmp/js-idioms.js` (ad-hoc; gaps.js is the canonical version)
- Prior import-resolution design: `QI_HINTS_AND_IMPORT_RESOLUTION_PLAN.md`
- TS indexer prior fixes: memory `project_qi_go_receiver_parent`
