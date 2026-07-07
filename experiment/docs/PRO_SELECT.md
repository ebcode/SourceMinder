# pro_select.py — Pro instance selector

Canonical CLI for choosing SWE-bench Pro experiment instances from
`experiment/data/swebench_pro/test.parquet` (731 instances). It centralizes the
one parsing gotcha that kept biting ad-hoc scripts — `fail_to_pass`/`pass_to_pass`
are **Python-repr strings, not JSON** (`ast.literal_eval`, not `json.loads`) —
and emits a filtered, ranked table plus the docker `image_name`s for launching.

Reads the parquet via pyarrow (`.venv_pro` has it). Counterpart to
`pro_batch_status.py`, which reports on batches already run.

```bash
PY=experiment/.venv_pro/bin/python
$PY experiment/analysis/pro_select.py --lang go --n-files 4-6 --limit 15
```

## Columns

| Column | Meaning |
|---|---|
| `n_files` | gold-patch file count (`diff --git` lines) — a **proxy** that overstates required scope |
| `patch_lines` | `+`/`-` body lines in the gold patch |
| `n_f2p` / `n_p2p` | count of fail-to-pass / pass-to-pass tests |
| `n_test_files` | distinct test files holding the **failing** tests (js: before ` \| `, py: before `::`; go has no file path → distinct top-level `Test*` funcs). Always `≤ n_f2p`; the cross-language-comparable "how much to understand" axis |
| `runs` | how many `results/pro_runs/*` batches already ran this instance |
| `image_name` | docker launch target (CSV only) |

## Flags

`--lang` (repeatable: go/python/js/ts) · `--repo SUBSTR` · `--n-files` / `--n-f2p`
ranges (`4-6`, `4`, `4-`, `-6`) · `--fresh` (only `runs == 0`) ·
`--rank {n_test_files,n_files,n_f2p,n_p2p,patch_lines,runs}` (default `n_test_files`) ·
`--asc` · `--limit N` (0 = all) · `--csv` (includes `image_name`).

Two honesty guardrails print as footnotes: ranking by `n_f2p` across languages
warns it isn't comparable (go = coarse top-level funcs, py/js = per-case); and
`n_files`/`patch_lines` are flagged as gold-patch proxies that overstate scope
(a 4-file instance was once solved touching 1 file).

## Use cases

### 1. Cheapest fresh instance — smoke-test the pipeline
```bash
$PY pro_select.py --lang go --n-files 1-2 --fresh --rank patch_lines --asc
```
Smallest unrun 1–2 file Go patches first — the minimal-cost instance to validate
the harness before spending budget. `--asc` floats the smallest patch to the top.

### 2. High-scope Python — where qi's multi-file edge should show
```bash
$PY pro_select.py --lang python --rank n_test_files --n-files 3-
```
Ranks by distinct failing-test files. The `n_f2p`→`n_test_files` gap (e.g.
785 cases → 8 files) is the per-case-vs-per-file normalization; high
`n_test_files` is the honest "agent must understand many areas" signal.

### 3. Audit one repo — what's already been run
```bash
$PY pro_select.py --repo qutebrowser --rank runs
```
Sorts by how many batches already ran each instance. `runs > 0` = covered,
`runs 0` = untouched. Add `--fresh` to see only what's left.

### 4. Cross-language ranking — the guardrail firing
```bash
$PY pro_select.py --rank n_f2p --n-files 2-5 --fresh
```
Ranking across languages by `n_f2p` is dominated by Python (per-case) and the
tool prints the not-comparable warning. Real ranking, but flagged as misleading
— rank by `n_test_files` or restrict to one `--lang` instead.

### 5. Export with launch targets
```bash
$PY pro_select.py --lang go --fresh --n-files 3-4 --rank n_test_files --limit 3 --csv
```
Machine-readable rows including `image_name` (the `jefzda/sweap-images:…` tag) —
the select→launch handoff without a second lookup.

## See Also

- `pro_batch_status.py` — status of batches already run.
- `PRO_ANALYZE.md` — per-batch pipeline that consumes selected instances.
- `STATISTICAL_METHODS.md` / `CROSS_INSTANCE.md` — analyzing the results.
- The honest scope axis (map each failing test to the files it exercises) is the
  separate "required scope computer" idea, not this tool.
</content>
