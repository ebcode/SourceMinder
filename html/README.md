# HTML Test Harness

This folder contains the first browser-side SQLite/WASM harness for SourceMinder.

## Purpose

- Load a browser-safe snapshot of the repository index in the browser with SQLite's official WASM build
- Prove the browser can query the SourceMinder index without a server-side SQL layer
- Establish the place where a future compiled `qi` WASM module can plug in

## Run

From `html/`:

```bash
npm install
npm run serve
```

Then open:

```text
http://localhost:8000/index.html
```

## Current Behavior

- Initializes SQLite's official browser WASM module
- Fetches `code-index.browser.db`
- Deserializes it into an in-memory SQLite connection
- Runs a few validation queries
- Renders summary cards, context counts, and sample rows
- Exposes a tiny query form with a readonly `<textarea>` above an `<input>`

The input box now accepts a small browser-side subset of `qi` commands, including:

- positional patterns
- `-i` / `--include-context`
- `-x` / `--exclude-context`
- `-f` / `--file`
- `--def`
- `--usage`
- `--limit`

Example:

```text
qi % -i call -x noise --limit 20
```

## Snapshot File

`code-index.browser.db` is a browser-safe snapshot created from the live repository database.

Refresh it from the repository root with:

```bash
sqlite3 code-index.db ".backup 'html/code-index.browser.db'"
sqlite3 html/code-index.browser.db "PRAGMA journal_mode=DELETE; VACUUM;"
```

This avoids depending on WAL sidecar files in the browser and rewrites the snapshot into a browser-safe non-WAL database.

## Next Steps

- Replace summary/sample queries with a JS-facing `qi` request API
- Intercept `-e`, `-C`, `-A`, and `-B` flows and resolve source lines through an HTTP endpoint
- Decide whether persistence should remain in-memory or move to OPFS/IndexedDB-backed SQLite
