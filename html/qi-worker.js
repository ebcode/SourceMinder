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

/* Operation queue: init / load-project / query are serialized onto a single
 * tail promise so they never interleave.  Without this, an async runQuery()
 * could yield at its source-fetch await and let a load-project run loadDbFromBytes()
 * concurrently -- closing and replacing activeDb out from under the in-flight
 * query (mixed-project results or "DB closed" errors).  enqueue() guarantees a
 * project switch waits for the in-flight query to finish against its own DB. */
var opQueue = Promise.resolve();
function enqueue(fn) {
    var run = opQueue.then(fn);
    opQueue = run.catch(function() { /* keep the chain alive past failures */ });
    return run;
}

/* Canonical filepath for a row, matching build_row_filepath() in
 * qi-web-entry.c exactly -- this string is the source-blob lookup key, so the
 * two sides MUST agree (see QI_WEB_FILE_PLAN.md). */
function buildRowFilepath(dir, file) {
    if (dir && dir[dir.length - 1] !== '/') return dir + '/' + file;
    return dir + file;
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

/* Marshal present source files into one WASM heap buffer as NUL-framed
 * "<path>\0<content>\0" records (the format source_map_parse() walks in place).
 * Returns {ptr, len}; caller MUST qiModule._free(ptr) once the call returns.
 * stringToUTF8 writes straight into the heap, so the payload is copied exactly
 * once -- no big JS string, no ccall stack marshaling. */
function marshalSources(files) {
    var sizes = [];
    var total = 0;
    for (var i = 0; i < files.length; i++) {
        var pl = qiModule.lengthBytesUTF8(files[i].path);
        var cl = qiModule.lengthBytesUTF8(files[i].content);
        sizes.push([pl, cl]);
        total += pl + 1 + cl + 1;   /* +1 each for the NUL terminators */
    }
    if (total === 0) return { ptr: 0, len: 0 };

    var ptr = qiModule._malloc(total);
    if (!ptr) throw new Error('Out of WASM memory for source blob (' + total + ' bytes)');

    var off = ptr;
    for (var j = 0; j < files.length; j++) {
        qiModule.stringToUTF8(files[j].path, off, sizes[j][0] + 1);
        off += sizes[j][0] + 1;
        qiModule.stringToUTF8(files[j].content, off, sizes[j][1] + 1);
        off += sizes[j][1] + 1;
    }
    return { ptr: ptr, len: total };
}

function expectSingleValue(db, sql) {
    var value = db.selectValue(sql);
    if (value === undefined) {
        throw new Error('Query returned no rows: ' + sql);
    }
    return value;
}

/* Insert an extra " AND (...)" clause ahead of any trailing GROUP BY / ORDER BY
 * / LIMIT, or append it if there is none.  Used to push the --within scope into
 * the main SQL *and* into COUNT_SQL / BREAKDOWN_SQL, which qi_web_build()
 * precomputes from the base SQL before any WITHIN post-processing -- without
 * this, totals and the breakdown would reflect the unscoped query. */
function injectWhereClause(sqlStr, clause) {
    if (!clause) return sqlStr;
    var cut = -1;
    var markers = ['GROUP BY', 'ORDER BY', 'LIMIT'];
    for (var i = 0; i < markers.length; i++) {
        var idx = sqlStr.indexOf(markers[i]);
        if (idx >= 0 && (cut < 0 || idx < cut)) cut = idx;
    }
    if (cut >= 0) return sqlStr.slice(0, cut) + clause + ' ' + sqlStr.slice(cut);
    return sqlStr + clause;
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

    /* Files mode: show only distinct file paths */
    if (buildLines.MODE === 'files') {
        var filesSql = buildLines.FILES_SQL;
        if (!filesSql) return 'Error: No FILES SQL built.\r\n';

        var t0 = performance.now();
        var fileQuery = limit > 0 ? (filesSql + ' LIMIT ' + limit) : filesSql;
        var fileRows = activeDb.selectArrays(fileQuery);
        var t1 = performance.now();
        console.log('[worker] files query:', (t1 - t0).toFixed(2) + 'ms', 'rows:', fileRows.length);

        /* Count total available (without limit) if limit was hit */
        var totalFiles = fileRows.length;
        if (limit > 0 && fileRows.length >= limit) {
            try {
                totalFiles = expectSingleValue(activeDb,
                    'SELECT COUNT(*) FROM (' + filesSql + ')');
            } catch (e) {
                totalFiles = fileRows.length;
            }
        }

        /* Marshal rows as 2-column TSV: directory\tfilename */
        var filesTsv = fileRows.map(function(r) {
            return (r[0] != null ? String(r[0]) : '') + '\t' + (r[1] != null ? String(r[1]) : '');
        }).join('\n');

        return qiModule.ccall('qi_web_format_files', 'string',
            ['string', 'number', 'number'],
            [filesTsv, fileRows.length, totalFiles]);
    }

    var sql = buildLines.SQL;
    console.log('[worker] SQL:', sql);

    if (!sql) {
        return 'Error: No SQL built for query.\r\n';
    }

    /* --within scope clause, also applied to COUNT_SQL/BREAKDOWN_SQL below. */
    var withinWhere = '';

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
            withinWhere = ' AND (' + withinClauses.join(' OR ') + ')';
            sql = injectWhereClause(sql, withinWhere);
            console.log('[worker] SQL with within injected (first 400 chars):\n' + sql.substring(0, 400));
        }
    }

    /* 2. Execute SQL against the in-memory DB.  COUNT_SQL was precomputed from
     * the base SQL, so re-apply the --within scope to it (withinWhere is '' when
     * --within is absent).  The fallback wraps the already-scoped `sql`. */
    var countSql = buildLines.COUNT_SQL
        ? injectWhereClause(buildLines.COUNT_SQL, withinWhere)
        : ('SELECT COUNT(*) FROM (' + sql + ' LIMIT -1)');
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
     * side decides per row what to actually render).  Marshal straight into the
     * WASM heap and hand qi_web_format a pointer+length -- the source payload is
     * never concatenated into a JS string nor pushed through the ccall stack. */
    var srcPtr = 0, srcLen = 0;
    if (buildLines.NEEDS_SOURCE === '1') {
        var paths = rows.map(function(row) {
            return buildRowFilepath(
                String(row[1] != null ? row[1] : ''),
                String(row[2] != null ? row[2] : ''));
        });
        var ts = performance.now();
        var files = await SourceCache.getFiles(paths);
        var marshalled = marshalSources(files);
        srcPtr = marshalled.ptr;
        srcLen = marshalled.len;
        console.log('[worker] source fetch:', (performance.now() - ts).toFixed(2) + 'ms',
            'files:', files.length, 'heap bytes:', srcLen);
    }

    /* 5. Format qi output via WASM.  Free the source buffer no matter what. */
    var formatted;
    try {
        formatted = qiModule.ccall('qi_web_format', 'string',
            ['string', 'string', 'number', 'number', 'number', 'number'],
            [buildResult, rowsTsv, total, rows.length, srcPtr, srcLen]);
    } finally {
        if (srcPtr) qiModule._free(srcPtr);
    }
    console.log('[worker] qi_web_format result (first 300 chars):\n' + formatted.substring(0, 300));

    /* 6. Append breakdown when results are truncated (mirrors CLI get_context_summary). */
    if (total > rows.length && buildLines.BREAKDOWN_SQL) {
        try {
            /* Same --within scoping as COUNT_SQL: BREAKDOWN_SQL was precomputed
             * before WITHIN post-processing; inject ahead of its GROUP BY. */
            var bdRows = activeDb.selectArrays(injectWhereClause(buildLines.BREAKDOWN_SQL, withinWhere));
            var bdTsv = bdRows.map(function(r) { return r[0] + '\t' + r[1]; }).join('\n');
            formatted += qiModule.ccall('qi_web_format_breakdown', 'string',
                ['string'], [bdTsv]);
        } catch (e) {
            /* breakdown is cosmetic; swallow errors silently */
        }
    }

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
                /* loadProject() cleared ready before the failure.  If the prior
                 * DB survived (failure before loadDbFromBytes), it is still
                 * usable -- restore readiness so queries are not blocked. */
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
            return runQuery(msg.cmd).then(function(output) {
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
