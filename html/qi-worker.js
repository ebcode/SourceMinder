/* qi-worker.js -- Web worker for qi.wasm + SQLite.  Keeps WASM off the main
 * thread so no query (SQL build, DB execute, format) blocks the terminal UI.
 *
 * Message protocol:
 *   Main -> Worker:
 *     { type: 'init' }                       -- load WASM runtimes + manifest
 *     { type: 'load-project', project: {...} }-- switch to a manifest project
 *     { type: 'query', cmd: '<qi command>' }  -- run a qi query
 *
 *   Worker -> Main:
 *     { type: 'status',   message: '...' }    -- progress update
 *     { type: 'projects', projects: [...], version: '...' } -- manifest loaded
 *     { type: 'progress', projectId, loaded, total } -- DB download progress
 *     { type: 'ready',    projectId, projectName, summary: [...] } -- project loaded
 *     { type: 'output',   text: '...' }       -- formatted query output
 *     { type: 'error',    message: '...' }    -- error from init/load/query
 */

import sqlite3InitModule from './node_modules/@sqlite.org/sqlite-wasm/dist/index.mjs';

/* qi-web.js (+ its .wasm) and qi-pipeline.js are loaded dynamically in init()
 * for cache-busting: the WASM glue/binary are resolved through the build's
 * content-hashed asset-manifest.json, and the pipeline is busted with the same
 * ?t token this worker was loaded under.  Bound here, assigned in init(). */
var runQuery = null;
var expectSingleValue = null;

/* Flip to true to surface the pipeline's per-query diagnostics in the worker
 * console.  Off by default so production neither logs nor pays the cost of
 * building those log payloads (see qi-pipeline.js's debug gate). */
var DEBUG = false;

var activeDb = null;
var qiModule = null;
var sqlite3 = null;
var ready = false;

/* Operation queue: init / load-project / query are serialized onto a single
 * tail promise so they never interleave.  Without this, an async runQuery()
 * could yield at its source-fetch await and let a load-project swap activeDb
 * concurrently -- closing and replacing it out from under the in-flight
 * query (mixed-project results or "DB closed" errors).  enqueue() guarantees a
 * project switch waits for the in-flight query to finish against its own DB. */
var opQueue = Promise.resolve();
function enqueue(fn) {
    var run = opQueue.then(fn);
    opQueue = run.catch(function() { /* keep the chain alive past failures */ });
    return run;
}

/* --- SourceCache: read-only source fetch for -e / -C / -A / -B -----------
 *
 * Deep module: callers ask getFiles(paths) and receive the present files as
 * {path, content} records.  Dedup, a session cache, bounded-concurrency
 * fetching, and 404->omit are all internal.  The backend is just the static
 * origin -- swap fetchOne() for a batch API later without touching any other
 * code (QI_WEB_FILE_PLAN.md sections 2, 5). */
var SourceCache = (function() {
    /* Per-project base URL under which that project's source tree is served,
     * same origin.  Set via setBase() on each project switch; paths from the
     * index (e.g. "./shared/foo.c") resolve against it. */
    var base = './';
    /* Client-side politeness cap; the real abuse boundary is nginx limit_req. */
    var MAX_CONCURRENT = 5;

    /* path -> file content (string), or null once known-missing (404/error). */
    var cache = new Map();

    function pathToUrl(path) {
        return base + path.replace(/^\.\//, '');  /* strip leading ./ */
    }

    async function fetchOne(path) {
        try {
            var resp = await fetch(pathToUrl(path));
            if (resp.ok) {
                cache.set(path, await resp.text());     /* hit -> cache content */
            } else if (resp.status === 404 || resp.status === 410) {
                cache.set(path, null);                   /* definite miss -> cache null */
            }
            /* Other statuses (5xx, 429, ...) are transient: leave uncached so a
             * later query retries.  This file is simply omitted from this query's
             * blob (cache.get -> undefined == null), surfacing the CLI's
             * "could not read file" for this run only. */
        } catch (e) {
            /* Network error: also transient -- do not poison the cache. */
        }
    }

    /* Fetch every not-yet-cached path, at most MAX_CONCURRENT in flight. */
    async function fetchMissing(paths) {
        var todo = paths.filter(function(p) { return !cache.has(p); });
        var idx = 0;
        async function lane() {
            while (idx < todo.length) await fetchOne(todo[idx++]);
        }
        var lanes = [];
        for (var i = 0; i < MAX_CONCURRENT && i < todo.length; i++) lanes.push(lane());
        await Promise.all(lanes);
    }

    return {
        /* Point at a project's source root, dropping the previous project's
         * cached files (an identical relative path is a different file there). */
        setBase: function(b) { base = b || './'; cache.clear(); },
        /* Fetch the requested paths and return the present ones as
         * {path, content} records.  Missing files are omitted, so the C lookup
         * misses and the render twin emits the CLI's "could not read" warning.
         * Marshaling into the WASM heap is the caller's job (marshalSources) so
         * this stays backend-agnostic and avoids a big intermediate string. */
        getFiles: async function(paths) {
            var uniq = Array.from(new Set(paths));
            await fetchMissing(uniq);
            var out = [];
            for (var i = 0; i < uniq.length; i++) {
                var content = cache.get(uniq[i]);
                if (content == null) continue;
                out.push({ path: uniq[i], content: content });
            }
            return out;
        }
    };
})();

/* The query pipeline (build -> execute -> format), including marshalSources,
 * injectWhereClause, and runQuery, lives in qi-pipeline.js so the Node test
 * harness runs the identical code.  This worker supplies the browser-specific
 * dependencies (activeDb, SourceCache, console) via the ctx object below. */

/* -- Project DB management --------------------------------------------- */

/* Deserialize DB bytes into a fresh in-memory connection and return it.  Does
 * NOT touch activeDb -- loadProject() swaps only after the new DB is fully
 * validated, so a failed load never strands the worker without its last-good
 * project (see loadProject). */
function deserializeDb(bytes) {
    var db = new sqlite3.oo1.DB();
    var ptr = sqlite3.wasm.allocFromTypedArray(bytes);
    var rc = sqlite3.capi.sqlite3_deserialize(
        db.pointer, 'main', ptr, bytes.byteLength, bytes.byteLength,
        sqlite3.capi.SQLITE_DESERIALIZE_FREEONCLOSE);
    db.checkRc(rc);
    return db;
}

/* Per-project summary cards from the loaded DB. */
function computeSummary(db) {
    return [
        { label: 'Indexed rows',     value: expectSingleValue(db, 'SELECT COUNT(*) FROM code_index').toLocaleString() },
        { label: 'Distinct files',   value: expectSingleValue(db, 'SELECT COUNT(*) FROM (SELECT DISTINCT directory, filename FROM code_index)').toLocaleString() },
        { label: 'Distinct symbols', value: expectSingleValue(db, 'SELECT COUNT(DISTINCT symbol) FROM code_index').toLocaleString() },
        { label: 'Definitions',      value: expectSingleValue(db, 'SELECT COUNT(*) FROM code_index WHERE is_definition = 1').toLocaleString() },
    ];
}

/* Acquire a project's DB bytes: from Cache Storage if already downloaded, else
 * streamed from the server with progress events.  The cache key includes the
 * manifest version, so bumping `version` forces a re-download (dev refresh). */
async function getDbBytes(project) {
    var cacheKey = project.dbUrl + '@' + (project.version || project.sizeBytes || '');
    var store = null;
    try { store = await caches.open('qi-db-cache'); } catch (e) { /* Cache API absent */ }

    if (store) {
        var hit = await store.match(cacheKey);
        if (hit) {
            console.log('[worker] DB cache hit:', cacheKey);
            return new Uint8Array(await hit.arrayBuffer());
        }
    }

    var resp = await fetch(project.dbUrl);
    if (!resp.ok) throw new Error('Failed to fetch DB: ' + resp.status + ' ' + resp.statusText);

    var total = parseInt(resp.headers.get('Content-Length') || '', 10) || project.sizeBytes || 0;

    var bytes;
    if (resp.body && typeof resp.body.getReader === 'function') {
        /* Stream the body so we can report download progress. */
        var reader = resp.body.getReader();
        var chunks = [];
        var loaded = 0;
        for (;;) {
            var r = await reader.read();
            if (r.done) break;
            chunks.push(r.value);
            loaded += r.value.length;
            self.postMessage({ type: 'progress', projectId: project.id, loaded: loaded, total: total });
        }
        bytes = new Uint8Array(loaded);
        var off = 0;
        for (var i = 0; i < chunks.length; i++) { bytes.set(chunks[i], off); off += chunks[i].length; }
    } else {
        /* Some environments expose no readable body even on a 200 (resp.body
         * null / no getReader).  Fall back to a one-shot read -- no incremental
         * progress, but emit a single completion tick so the UI bar finishes. */
        bytes = new Uint8Array(await resp.arrayBuffer());
        self.postMessage({ type: 'progress', projectId: project.id, loaded: bytes.length, total: total || bytes.length });
    }

    if (store) {
        try { await store.put(cacheKey, new Response(bytes)); }
        catch (e) { console.warn('[worker] DB cache put failed:', e); }
    }
    return bytes;
}

/* Load a project: acquire its DB, point SourceCache at its files, publish its
 * summary.  Blocks queries (ready=false) until complete. */
async function loadProject(project) {
    ready = false;
    self.postMessage({ type: 'status', message: 'Loading ' + project.name + '...' });

    var bytes = await getDbBytes(project);
    console.log('[worker] DB bytes for', project.id + ':', bytes.byteLength);

    /* Build and validate the new DB fully BEFORE swapping it in.  computeSummary
     * doubles as validation (it queries code_index); if deserialize or any of
     * those queries throws, close the half-built DB and leave activeDb on the
     * last-good project -- so the load-project catch's `ready = !!activeDb`
     * never re-enables queries against a failed replacement. */
    var newDb = deserializeDb(bytes);
    var summary;
    try {
        summary = computeSummary(newDb);
    } catch (e) {
        try { newDb.close(); } catch (e2) { /* ignore */ }
        throw e;
    }

    /* Commit: from here on nothing fallible runs, so the swap is atomic from the
     * catch handler's point of view. */
    if (activeDb) { try { activeDb.close(); } catch (e) { /* ignore */ } }
    activeDb = newDb;
    SourceCache.setBase(project.sourceBase);

    ready = true;
    self.postMessage({
        type: 'ready',
        projectId: project.id,
        projectName: project.name,
        projectVersion: project.version,
        summary: summary,
    });
}

async function init() {
    /* Pipeline: bust with the same ?t token the worker was loaded under so a
     * fresh worker always pulls the matching qi-pipeline.js. */
    var pipelineUrl = new URL('./qi-pipeline.js' + self.location.search, self.location.href).href;
    var pipeline = await import(pipelineUrl);
    runQuery = pipeline.runQuery;
    expectSingleValue = pipeline.expectSingleValue;

    self.postMessage({ type: 'status', message: 'Initializing qi WASM module...' });
    /* WASM glue + binary: resolve content-hashed names from the build manifest,
     * falling back to the plain names when no manifest is present. */
    var assets = null;
    try {
        var manifestResp = await fetch('./asset-manifest.json', { cache: 'no-store' });
        if (manifestResp.ok) assets = await manifestResp.json();
    } catch (e) { /* fall back to plain names below */ }
    var qiJsName   = assets ? assets.qiWebJs   : 'qi-web.js';
    var qiWasmName = assets ? assets.qiWebWasm : 'qi-web.wasm';
    var qiJsUrl   = new URL('./' + qiJsName,   self.location.href).href;
    var qiWasmUrl = new URL('./' + qiWasmName, self.location.href).href;

    var QiWebModule = (await import(qiJsUrl)).default;
    qiModule = await QiWebModule({
        locateFile: function(path) {
            return path.endsWith('.wasm') ? qiWasmUrl : path;
        },
    });

    self.postMessage({ type: 'status', message: 'Initializing SQLite WASM runtime...' });
    sqlite3 = await sqlite3InitModule({ print: function() {}, printErr: function() {} });
    console.log('[worker] sqlite3 loaded, version', sqlite3.version.libVersion);

    self.postMessage({ type: 'status', message: 'Loading project list...' });
    var resp = await fetch('./projects.json');
    if (!resp.ok) throw new Error('Failed to fetch projects.json: ' + resp.status);
    var projects = await resp.json();
    if (!Array.isArray(projects) || projects.length === 0) {
        throw new Error('projects.json is empty or malformed.');
    }

    self.postMessage({ type: 'projects', projects: projects, version: sqlite3.version.libVersion });

    /* Auto-load the first project. */
    await loadProject(projects[0]);
}

self.onmessage = function(event) {
    var msg = event.data;
    console.log('[worker] onmessage type:', msg.type);

    if (msg.type === 'init') {
        enqueue(function() {
            return init().catch(function(error) {
                console.error('[worker] init error:', error);
                self.postMessage({
                    type: 'error',
                    phase: 'init',
                    message: error instanceof Error ? error.message : String(error),
                });
            });
        });
    } else if (msg.type === 'load-project') {
        enqueue(function() {
            return loadProject(msg.project).catch(function(error) {
                console.error('[worker] load-project error:', error);
                /* loadProject() cleared ready before the failure but swaps
                 * activeDb only after the new DB is fully validated, so on any
                 * failure activeDb is still the last-good project (or null if
                 * none ever loaded).  Restoring ready = !!activeDb therefore
                 * re-enables queries only against a genuinely good DB. */
                ready = !!activeDb;
                self.postMessage({
                    type: 'error',
                    phase: 'load',
                    message: error instanceof Error ? error.message : String(error),
                });
            });
        });
    } else if (msg.type === 'query') {
        enqueue(function() {
            /* Re-check ready inside the queued turn: a load-project ahead of us
             * may still be settling, or may have failed. */
            if (!ready) {
                self.postMessage({ type: 'error', phase: 'query', message: 'Not initialized yet.' });
                return;
            }
            /* Build the pipeline ctx from this worker's browser deps.  activeDb
             * is read here (not closed over) so each query binds to the DB that
             * is current when its queued turn runs. */
            var ctx = {
                qiModule: qiModule,
                db: activeDb,
                getSources: function(paths) { return SourceCache.getFiles(paths); },
                debug: DEBUG,
                log: console.log.bind(console),
            };
            return runQuery(ctx, msg.cmd).then(function(output) {
                console.log('[worker] posting output, length:', output.length);
                self.postMessage({ type: 'output', text: output });
            }, function(error) {
                console.error('[worker] query error:', error);
                self.postMessage({
                    type: 'error',
                    phase: 'query',
                    message: error instanceof Error ? error.message : String(error),
                });
            });
        });
    }
};
