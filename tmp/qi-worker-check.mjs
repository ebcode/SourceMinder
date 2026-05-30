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
import QiWebModule from './qi-web.js';

var activeDb = null;
var qiModule = null;
var sqlite3 = null;
var ready = false;

/* Canonical filepath for a row, matching build_row_filepath() in
 * qi-web-entry.c exactly -- this string is the source-blob lookup key, so the
 * two sides MUST agree (see QI_WEB_FILE_PLAN.md). */
function buildRowFilepath(dir, file) {
    if (dir && dir[dir.length - 1] !== '/') return dir + '/' + file;
    return dir + file;
}

/* --- SourceCache: read-only source fetch for -e / -C / -A / -B -----------
 *
 * Deep module: callers ask getFilesBlob(paths) and receive the length-framed
 * blob qi_web_format() expects.  Dedup, a session cache, bounded-concurrency
 * fetching, 404->omit, and framing are all internal.  The backend is just the
 * static origin -- swap fetchOne() for a batch API later without touching any
 * other code (QI_WEB_FILE_PLAN.md sections 2, 5). */
var SourceCache = (function() {
    /* Per-project base URL under which that project's source tree is served,
     * same origin.  Set via setBase() on each project switch; paths from the
     * index (e.g. "./shared/foo.c") resolve against it. */
    var base = './';
    /* Client-side politeness cap; the real abuse boundary is nginx limit_req. */
    var MAX_CONCURRENT = 5;

    /* path -> file content (string), or null once known-missing (404/error). */
    var cache = new Map();
    var encoder = new TextEncoder();

    function pathToUrl(path) {
        return base + path.replace(/^\.\//, '');  /* strip leading ./ */
    }

    async function fetchOne(path) {
        try {
            var resp = await fetch(pathToUrl(path));
            cache.set(path, resp.ok ? await resp.text() : null);
        } catch (e) {
            cache.set(path, null);  /* network error -> treat as missing */
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

    /* Length-prefixed framing: "<path>\n<utf8_byte_len>\n<bytes>" per file.
     * byte length (not char length) so the C side reads exactly the bytes the
     * ccall string boundary writes as UTF-8.  Missing files are omitted, which
     * makes the C lookup miss and emit the CLI's "could not read" warning. */
    function buildBlob(paths) {
        var parts = [];
        for (var i = 0; i < paths.length; i++) {
            var content = cache.get(paths[i]);
            if (content == null) continue;
            var byteLen = encoder.encode(content).length;
            parts.push(paths[i] + '\n' + byteLen + '\n' + content);
        }
        return parts.join('');
    }

    return {
        /* Point at a project's source root, dropping the previous project's
         * cached files (an identical relative path is a different file there). */
        setBase: function(b) { base = b || './'; cache.clear(); },
        getFilesBlob: async function(paths) {
            var uniq = Array.from(new Set(paths));
            await fetchMissing(uniq);
            return buildBlob(uniq);
        }
    };
})();

function expectSingleValue(db, sql) {
    var value = db.selectValue(sql);
    if (value === undefined) {
        throw new Error('Query returned no rows: ' + sql);
    }
    return value;
}

async function runQuery(input) {
    console.log('[worker] runQuery called, cmd:', input);

    /* 1. Build SQL via WASM */
    var buildResult = qiModule.ccall('qi_web_build', 'string', ['string'], [input]);
    console.log('[worker] qi_web_build raw result:\n' + buildResult);

    /* Parse build result: lines like "PATTERNS|p1 p2", "SQL|SELECT ..." */
    var buildLines = {};
    var lines = buildResult.split('\n');
    for (var i = 0; i < lines.length; i++) {
        var pipe = lines[i].indexOf('|');
        if (pipe >= 0) {
            buildLines[lines[i].slice(0, pipe)] = lines[i].slice(pipe + 1);
        }
    }
    console.log('[worker] parsed buildLines keys:', Object.keys(buildLines));

    if (buildLines.ERROR && buildLines.ERROR !== 'OK') {
        console.log('[worker] build error:', buildLines.ERROR);
        return 'Error: ' + buildLines.ERROR + '\r\n';
    }

    var limit = parseInt(buildLines.LIMIT || '25', 10);
    console.log('[worker] LIMIT:', limit);

    /* TOC mode: different SQL, format, and output pipeline */
    if (buildLines.MODE === 'toc') {
        var tocSql = buildLines.TOC_SQL;
        if (!tocSql) return 'Error: No TOC SQL built.\r\n';

        /* Count breakdown by context type */
        var contextCounts = '';
        var countSql = buildLines.TOC_COUNT_SQL;
        if (countSql) {
            var t0 = performance.now();
            try {
                var countRows = activeDb.selectArrays(countSql);
                var t1 = performance.now();
                console.log('[worker] TOC count query:', (t1 - t0).toFixed(2) + 'ms', 'rows:', countRows.length);
                var parts = [];
                for (var ci = 0; ci < countRows.length; ci++) {
                    parts.push(String(countRows[ci][0]) + ':' + String(countRows[ci][1]));
                }
                contextCounts = parts.join('\n');
            } catch (e) {
                console.error('[worker] TOC count query error:', e);
                contextCounts = '';
            }
        }

        /* Execute TOC SQL */
        t0 = performance.now();
        var tocQuery = limit > 0 ? (tocSql + ' LIMIT ' + limit) : tocSql;
        var tocRows = activeDb.selectArrays(tocQuery);
        t1 = performance.now();
        console.log('[worker] TOC query:', (t1 - t0).toFixed(2) + 'ms', 'rows:', tocRows.length);

        /* Count total available (without limit) */
        var totalAvailable = tocRows.length;
        if (limit > 0 && tocRows.length >= limit) {
            try {
                totalAvailable = expectSingleValue(activeDb,
                    'SELECT COUNT(*) FROM (' + tocSql + ')');
            } catch (e) {
                totalAvailable = tocRows.length;
            }
        }

        /* Marshal TOC rows: 6-column TSV (symbol, line, source_location, context, dir, file) */
        var tsvLines = tocRows.map(function(row) {
            return row.map(function(v) { return v != null ? String(v) : ''; }).join('\t');
        });
        var rowsTsv = tsvLines.join('\n');

        /* Format via WASM */
        var tocOutput = qiModule.ccall('qi_web_toc_format', 'string',
            ['string', 'string', 'number', 'number', 'string'],
            [buildResult, rowsTsv, tocRows.length, totalAvailable, contextCounts]);
        console.log('[worker] qi_web_toc_format length:', tocOutput.length);
        return tocOutput;
    }

    var sql = buildLines.SQL;
    console.log('[worker] SQL:', sql);

    if (!sql) {
        return 'Error: No SQL built for query.\r\n';
    }

    /* Handle --within: resolve definition locations and inject WHERE clauses */
    if (buildLines.WITHIN_SQL) {
        var withinSql = buildLines.WITHIN_SQL;
        console.log('[worker] WITHIN_SQL:', withinSql);

        var t0 = performance.now();
        var withinRows = activeDb.selectArrays(withinSql);
        var t1 = performance.now();
        console.log('[worker] within lookup rows:', withinRows.length, 'time:', (t1 - t0).toFixed(2) + 'ms');

        if (withinRows.length === 0) {
            var syms = buildLines.WITHIN_SYMBOLS || '(unknown)';
            return 'Error: No definition found for symbol ' + syms + '\r\n';
        }

        /* Verify each requested symbol was actually found */
        var foundSyms = {};
        for (var ri = 0; ri < withinRows.length; ri++) {
            var matchedSym = String(withinRows[ri][3] || '').toLowerCase();
            foundSyms[matchedSym] = true;
        }
        var withinSyms = (buildLines.WITHIN_SYMBOLS || '').split(/\s+/);
        for (var si = 0; si < withinSyms.length; si++) {
            var sym = withinSyms[si].toLowerCase();
            if (sym && !foundSyms[sym]) {
                return "Error: No definition found for symbol '" + sym + "'\r\n";
            }
        }

        /* Determine column prefix based on whether query uses self-join alias */
        var colPrefix = (sql.indexOf('code_index ci') >= 0) ? 'ci.' : '';

        /* Build within WHERE clause from lookup results */
        var withinClauses = [];
        for (var ri = 0; ri < withinRows.length; ri++) {
            var row = withinRows[ri];
            var dir = String(row[0] || '');
            var file = String(row[1] || '');
            var srcloc = String(row[2] || '');

            /* Parse source_location: "2150:1-2166:1" -> start=2150, end=2166 */
            var startLine = 0, endLine = 0;
            var dash = srcloc.indexOf('-');
            if (dash >= 0) {
                var startPart = srcloc.substring(0, dash);
                var endPart = srcloc.substring(dash + 1);
                var colon1 = startPart.indexOf(':');
                var colon2 = endPart.indexOf(':');
                startLine = parseInt(colon1 >= 0 ? startPart.substring(0, colon1) : startPart, 10);
                endLine = parseInt(colon2 >= 0 ? endPart.substring(0, colon2) : endPart, 10);
            }

            if (startLine > 0 && endLine > 0) {
                var d = dir.replace(/'/g, "''");
                var f = file.replace(/'/g, "''");
                withinClauses.push(
                    '(' + colPrefix + "directory = '" + d + "'" +
                    ' AND ' + colPrefix + "filename = '" + f + "'" +
                    ' AND ' + colPrefix + 'line BETWEEN ' + startLine + ' AND ' + endLine + ')'
                );
            }
        }

        if (withinClauses.length > 0) {
            var withinWhere = ' AND (' + withinClauses.join(' OR ') + ')';

            /* Inject before ORDER BY */
            var orderIdx = sql.indexOf('ORDER BY');
            if (orderIdx >= 0) {
                sql = sql.substring(0, orderIdx) + withinWhere + ' ' + sql.substring(orderIdx);
            } else {
                sql = sql + withinWhere;
            }
            console.log('[worker] SQL with within injected (first 400 chars):\n' + sql.substring(0, 400));
        }
    }

    /* 2. Execute SQL against the in-memory DB */
    var countSql = buildLines.COUNT_SQL || ('SELECT COUNT(*) FROM (' + sql + ' LIMIT -1)');
    var t0 = performance.now();
    var total = expectSingleValue(activeDb, countSql);
    var t1 = performance.now();
    console.log('[worker] COUNT query:', (t1 - t0).toFixed(2) + 'ms', 'total matches:', total);

    t0 = performance.now();
    var rows = activeDb.selectArrays(sql + ' LIMIT ' + limit);
    t1 = performance.now();
    console.log('[worker] main query:', (t1 - t0).toFixed(2) + 'ms', 'row count:', rows.length);

    if (rows.length > 0) {
        console.log('[worker] first row (all 14 columns):', JSON.stringify(rows[0]));
    }

    /* 3. Marshal rows as TSV (all 14 columns, canonical SELECT * order) */
    var tsvLines = rows.map(function(row) {
        return row.map(function(v) { return v != null ? String(v) : ''; }).join('\t');
    });
    var rowsTsv = tsvLines.join('\n');
    console.log('[worker] rowsTsv (first 300 chars):\n' + rowsTsv.substring(0, 300));
    console.log('[worker] TSV fields per row:', (tsvLines[0] || '').split('\t').length);

    /* 4. Fetch source for -e/-C/-A/-B (superset = every displayed file; the C
     * side decides per row what to actually render). */
    var sourcesBlob = '';
    if (buildLines.NEEDS_SOURCE === '1') {
        var paths = rows.map(function(row) {
            return buildRowFilepath(
                String(row[1] != null ? row[1] : ''),
                String(row[2] != null ? row[2] : ''));
        });
        var ts = performance.now();
        sourcesBlob = await SourceCache.getFilesBlob(paths);
        console.log('[worker] source fetch:', (performance.now() - ts).toFixed(2) + 'ms',
            'blob bytes:', sourcesBlob.length);
    }

    /* 5. Format qi output via WASM. */
    var formatted = qiModule.ccall('qi_web_format', 'string',
        ['string', 'string', 'number', 'number', 'string'],
        [buildResult, rowsTsv, total, rows.length, sourcesBlob]);
    console.log('[worker] qi_web_format result (first 300 chars):\n' + formatted.substring(0, 300));

    return formatted;
}

/* -- Project DB management --------------------------------------------- */

/* Deserialize DB bytes into a fresh in-memory connection, closing any prior. */
function loadDbFromBytes(bytes) {
    if (activeDb) { try { activeDb.close(); } catch (e) { /* ignore */ } activeDb = null; }
    var db = new sqlite3.oo1.DB();
    var ptr = sqlite3.wasm.allocFromTypedArray(bytes);
    var rc = sqlite3.capi.sqlite3_deserialize(
        db.pointer, 'main', ptr, bytes.byteLength, bytes.byteLength,
        sqlite3.capi.SQLITE_DESERIALIZE_FREEONCLOSE);
    db.checkRc(rc);
    activeDb = db;
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

    /* Stream the body so we can report download progress. */
    var total = parseInt(resp.headers.get('Content-Length') || '', 10) || project.sizeBytes || 0;
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

    var bytes = new Uint8Array(loaded);
    var off = 0;
    for (var i = 0; i < chunks.length; i++) { bytes.set(chunks[i], off); off += chunks[i].length; }

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

    loadDbFromBytes(bytes);
    SourceCache.setBase(project.sourceBase);

    ready = true;
    self.postMessage({
        type: 'ready',
        projectId: project.id,
        projectName: project.name,
        summary: computeSummary(activeDb),
    });
}

async function init() {
    self.postMessage({ type: 'status', message: 'Initializing qi WASM module...' });
    qiModule = await QiWebModule();

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

self.onmessage = async function(event) {
    var msg = event.data;
    console.log('[worker] onmessage type:', msg.type);

    if (msg.type === 'init') {
        init().catch(function(error) {
            console.error('[worker] init error:', error);
            self.postMessage({
                type: 'error',
                message: error instanceof Error ? error.message : String(error),
            });
        });
    } else if (msg.type === 'load-project') {
        loadProject(msg.project).catch(function(error) {
            console.error('[worker] load-project error:', error);
            self.postMessage({
                type: 'error',
                message: error instanceof Error ? error.message : String(error),
            });
        });
    } else if (msg.type === 'query') {
        if (!ready) {
            self.postMessage({ type: 'error', message: 'Not initialized yet.' });
            return;
        }
        try {
            var output = await runQuery(msg.cmd);
            console.log('[worker] posting output, length:', output.length);
            self.postMessage({ type: 'output', text: output });
        } catch (error) {
            console.error('[worker] query error:', error);
            self.postMessage({
                type: 'error',
                message: error instanceof Error ? error.message : String(error),
            });
        }
    }
};
