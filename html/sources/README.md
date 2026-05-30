# html/sources/ — per-project source trees

Each subdirectory here holds the source tree for one project in the dropdown.
The browser uses these files to render `-e` / `-C` / `-A` / `-B` source context:
when a query needs source, the worker fetches each matched file from the
project's `sourceBase` over HTTP (same origin).

```
html/
├── projects.json                       # the manifest (one entry per project)
├── code-index.browser.db               # sourceminder's snapshot (non-WAL)
├── code-index-jinja.browser.db         # another project's snapshot
└── sources/
    ├── README.md                       # this file (tracked)
    ├── browser-snapshot.sh             # snapshot tool (tracked)
    ├── sourceminder/                   # source tree, mirrors the indexed paths
    ├── jinja/
    │   └── code-index.db               # the project's index, built in place
    └── PHPMailer/
```

Everything in this directory **except `README.md` and `browser-snapshot.sh` is
gitignored** — source trees, in-place indexes, and `*.browser.db` snapshots are
local/generated assets, regenerated per machine rather than committed.

## Manifest schema (`projects.json`)

`projects.json` is an array of project objects. See `../projects.example.json`
for a multi-project example.

| field        | meaning                                                                 |
|--------------|-------------------------------------------------------------------------|
| `id`         | stable key (dropdown value, DB cache key). Lowercase, no spaces.        |
| `name`       | label shown in the dropdown.                                            |
| `version`    | bump to force a client DB re-download (cache-busts the snapshot).       |
| `dbUrl`      | URL of the browser DB snapshot, relative to `html/`.                    |
| `sourceBase` | URL of the source tree root, relative to `html/`. **Trailing slash.**   |
| `sizeBytes`  | snapshot size, for the download progress bar.                           |

`sourceBase` must resolve so that an indexed path like `./shared/foo.c`
(leading `./` stripped) appends cleanly: `sourceBase + "shared/foo.c"`. The
directory layout under `sources/<project>/` must therefore mirror the paths
recorded in that project's index — which is why the project is **indexed in
place** (see below).

## Adding a project

The `<name>` you pick is one identifier that drives everything: the source
folder (`sources/<name>/`), the `sourceBase` qi-web routes fetches to, and the
snapshot filename.

1. **Place the source tree** under `html/sources/<name>/`.

2. **Index it in place.** Run the indexer from inside `html/sources/<name>/` so
   the paths it records (`./...`) are relative to that folder. This produces
   `html/sources/<name>/code-index.db`. (Indexing in place is what makes the
   recorded paths line up with `sourceBase: "./sources/<name>/"` — index it
   anywhere else and `-e`/`-C` will look for files at the wrong paths.)

3. **Snapshot it** with the tool in this folder. It reads
   `sources/<name>/code-index.db` and writes a non-WAL, browser-safe copy one
   level up to `html/code-index-<name>.browser.db`:

   ```sh
   ./browser-snapshot.sh --project <name>
   ```

   It prints the `dbUrl` / `sizeBytes` to drop into the manifest.

4. **Add a manifest entry** to `../projects.json`:

   ```json
   {
     "id": "<name>",
     "name": "<label>",
     "version": "1",
     "dbUrl": "./code-index-<name>.browser.db",
     "sourceBase": "./sources/<name>/",
     "sizeBytes": <printed by the script>
   }
   ```

5. **Verify** end-to-end with the headless harness (reads `projects.json`,
   resolves `dbUrl` + `sourceBase` exactly like the browser):

   ```sh
   node test/web-harness/run.mjs --project <name>
   ```

## Invariants (why things break if you skip a step)

- **Index in place.** `-e`/`-C` read files at `sourceBase + recorded-path`. If
  the index was built somewhere other than `sources/<name>/`, recorded paths
  won't resolve and source rendering will miss (or show the wrong file).
- **Snapshots must be non-WAL.** A raw copy of a WAL-mode DB cannot be
  deserialized in the browser (or the harness) — it throws `SQLITE_CANTOPEN` on
  the first query. Always go through `browser-snapshot.sh` (it uses
  `VACUUM INTO`, which folds the WAL in).
- **Re-snapshot after re-indexing.** A snapshot stale relative to its source
  tree renders the wrong lines.

## The sourceminder (self) project

`sourceminder` is the one project not indexed under `sources/`: its index is the
dev's working `code-index.db` at the repo root, and the indexed files were
copied into `sources/sourceminder/` to mirror those paths. Its snapshot is the
plain `code-index.browser.db`. To fold it into the uniform flow above, re-index
inside `sources/sourceminder/`, run `./browser-snapshot.sh --project
sourceminder`, and update its `dbUrl` to `./code-index-sourceminder.browser.db`.
