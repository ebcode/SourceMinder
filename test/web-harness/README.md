# Web bridge headless harness

Runs the real `qi-web.wasm` under Node and exercises qi queries end-to-end —
SQL build → DB execute → format — without a browser. Closes the iteration loop:
after `make web`, run this instead of reloading a browser tab to confirm every
WASM export still works.

## Run

```sh
make web          # rebuild qi-web.js / qi-web.wasm (after ./configure)
make web-test     # == node test/web-harness/run.mjs
```

Add `-v` for per-case output, or `--project <id>` to test a specific manifest
entry (defaults to the first, which is what the app auto-loads):

```sh
node test/web-harness/run.mjs -v
node test/web-harness/run.mjs --project jinja
```

Exit code is non-zero if any case fails.

The harness reads `html/projects.json` and resolves each project's `dbUrl` and
`sourceBase` exactly as the browser worker does — so it validates the same DB
snapshot and source tree the deployed site serves.

## How it stays honest

The harness does **not** reimplement the query pipeline. The orchestration lives
in [`html/qi-pipeline.js`](../../html/qi-pipeline.js) and is imported by *both*
the browser worker (`html/qi-worker.js`) and this harness. They differ only in
the injected `ctx`:

| dependency   | browser worker        | harness                     |
|--------------|-----------------------|-----------------------------|
| `qiModule`   | `QiWebModule()`       | same, loaded via `load-qi.mjs` |
| `db`         | `activeDb` (in-memory)| sqlite-wasm deserialize of `code-index.browser.db` |
| `getSources` | `SourceCache.getFiles`| local-file reads (`sources.mjs`) |
| `log`        | `console.log`         | no-op (or `console.log` with `-v`) |

So a passing harness run means the actual code path the browser uses works.

## Files

- `load-qi.mjs` — loads the Emscripten ES6 module under Node (shims the
  CommonJS `__dirname`/`require` globals it expects, passes the `.wasm` bytes in).
- `db.mjs` — opens the project's snapshot with `@sqlite.org/sqlite-wasm`
  (the same SQLite engine the browser ships) via `sqlite3_deserialize`.
- `sources.mjs` — Node analog of `SourceCache`: reads each row's file from the
  project's local source tree (`html/sources/<id>/`), returns present files as
  `{path, content}` records.
- `run.mjs` — reads `projects.json`, resolves `dbUrl`/`sourceBase`, then runs
  the case list + assertions.
- `unit.mjs` — fast unit tests for the pure helpers in `qi-pipeline.js` (e.g.
  `injectWhereClause` placement against nested subqueries / string literals).
  No WASM or DB needed. `make web-test` runs these before the integration suite.

## Prerequisites and caveats

- **`make web` first.** The harness tests the *built* artifact; a stale
  `qi-web.wasm` will (correctly) fail cases that depend on recent C changes.
- **`./configure` before `make web`** if `EXPORTED_FUNCTIONS` changed — the
  generated `Makefile` can lag behind `configure`.
- **The browser snapshot must be non-WAL.** A raw copy of a WAL-mode
  `code-index.db` cannot be deserialized (first query throws `SQLITE_CANTOPEN`)
  — in the browser worker *and* here, since both share the deserialize path.
  Always regenerate snapshots with `html/sources/browser-snapshot.sh`, which
  folds WAL out via `VACUUM INTO`:

  ```sh
  cd html/sources && ./browser-snapshot.sh --project <name>
  ```

  (See `html/sources/README.md` for the full add-a-project workflow.)

- **Source-expansion cases (`-e`/`-C`) need a *fresh* snapshot.** They read local
  files at the line numbers recorded in the DB, so a browser DB stale relative to
  the source tree renders the wrong lines. Re-index, then re-snapshot before
  validating these.
