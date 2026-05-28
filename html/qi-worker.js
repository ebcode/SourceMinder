/* qi-worker.js -- Web worker for qi.wasm + SQLite.  Keeps WASM off the main
 * thread so no query (SQL build, DB execute, format) blocks the terminal UI.
 *
 * Message protocol:
 *   Main -> Worker:
 *     { type: 'init' }                      -- load modules, open DB
 *     { type: 'query', cmd: '<qi command>' } -- run a qi query
 *
 *   Worker -> Main:
 *     { type: 'status',  message: '...' }    -- progress update
 *     { type: 'ready',   summary: [...], version: '...' } -- init complete
 *     { type: 'output',  text: '...' }       -- formatted query output
 *     { type: 'error',   message: '...' }    -- error from init or query
 */

import sqlite3InitModule from './node_modules/@sqlite.org/sqlite-wasm/dist/index.mjs';
import QiWebModule from './qi-web.js';

var activeDb = null;
var qiModule = null;
var sqlite3 = null;
var ready = false;

function expectSingleValue(db, sql) {
    var value = db.selectValue(sql);
    if (value === undefined) {
        throw new Error('Query returned no rows: ' + sql);
    }
    return value;
}

function runQuery(input) {
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

    var sql = buildLines.SQL;
    var limit = parseInt(buildLines.LIMIT || '25', 10);
    console.log('[worker] SQL:', sql);
    console.log('[worker] LIMIT:', limit);

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

    /* 4. Format qi output via WASM */
    var formatted = qiModule.ccall('qi_web_format', 'string',
        ['string', 'string', 'number', 'number'],
        [buildResult, rowsTsv, total, rows.length]);
    console.log('[worker] qi_web_format result (first 300 chars):\n' + formatted.substring(0, 300));

    return formatted;
}

async function init() {
    self.postMessage({ type: 'status', message: 'Initializing qi WASM module...' });
    qiModule = await QiWebModule();
    console.log('[worker] qiModule loaded, exported methods:', Object.keys(qiModule).filter(function(k) { return typeof qiModule[k] !== 'string'; }));

    self.postMessage({ type: 'status', message: 'Initializing SQLite WASM runtime...' });
    sqlite3 = await sqlite3InitModule({
        print: function() {},
        printErr: function() { /* quiet */ },
    });
    console.log('[worker] sqlite3 loaded');

    self.postMessage({ type: 'status', message: 'Fetching browser snapshot database...' });
    var response = await fetch('./code-index.browser.db');
    if (!response.ok) {
        throw new Error('Failed to fetch DB: ' + response.status + ' ' + response.statusText);
    }
    var bytes = new Uint8Array(await response.arrayBuffer());
    console.log('[worker] DB fetched, size:', bytes.byteLength);

    self.postMessage({ type: 'status', message: 'Loading database...' });
    var db = new sqlite3.oo1.DB();
    var ptr = sqlite3.wasm.allocFromTypedArray(bytes);
    var rc = sqlite3.capi.sqlite3_deserialize(
        db.pointer,
        'main',
        ptr,
        bytes.byteLength,
        bytes.byteLength,
        sqlite3.capi.SQLITE_DESERIALIZE_FREEONCLOSE
    );
    db.checkRc(rc);
    activeDb = db;

    /* Check table schema to verify column order */
    var schemaRows = db.selectArrays("SELECT sql FROM sqlite_master WHERE type='table' AND name='code_index'");
    if (schemaRows.length > 0) {
        console.log('[worker] code_index CREATE TABLE:', schemaRows[0][0]);
    }

    /* Summary stats for the cards */
    var totalRows       = expectSingleValue(db, 'SELECT COUNT(*) FROM code_index');
    var totalFiles      = expectSingleValue(db, 'SELECT COUNT(*) FROM (SELECT DISTINCT directory, filename FROM code_index)');
    var distinctSymbols = expectSingleValue(db, 'SELECT COUNT(DISTINCT symbol) FROM code_index');
    var definitions     = expectSingleValue(db, 'SELECT COUNT(*) FROM code_index WHERE is_definition = 1');

    console.log('[worker] summary:', { totalRows: totalRows, totalFiles: totalFiles, distinctSymbols: distinctSymbols, definitions: definitions });

    ready = true;

    self.postMessage({
        type: 'ready',
        summary: [
            { label: 'Indexed rows',      value: totalRows.toLocaleString() },
            { label: 'Distinct files',    value: totalFiles.toLocaleString() },
            { label: 'Distinct symbols',  value: distinctSymbols.toLocaleString() },
            { label: 'Definitions',       value: definitions.toLocaleString() },
        ],
        version: sqlite3.version.libVersion,
    });
}

self.onmessage = function(event) {
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
    } else if (msg.type === 'query') {
        if (!ready) {
            self.postMessage({ type: 'error', message: 'Not initialized yet.' });
            return;
        }
        try {
            var output = runQuery(msg.cmd);
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
