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

    /* 2. Execute SQL against the in-memory DB */
    var countSql = 'SELECT COUNT(*) FROM (' + sql + ' LIMIT -1)';
    var total = expectSingleValue(activeDb, countSql);
    console.log('[worker] total matches:', total);

    var rows = activeDb.selectArrays(sql + ' LIMIT ' + limit);
    console.log('[worker] row count:', rows.length);

    if (rows.length > 0) {
        console.log('[worker] first row (all columns):', JSON.stringify(rows[0]));
        console.log('[worker]   row[0] (expect symbol?):', JSON.stringify(rows[0][0]));
        console.log('[worker]   row[1] (expect dir?):   ', JSON.stringify(rows[0][1]));
        console.log('[worker]   row[2] (expect file?):  ', JSON.stringify(rows[0][2]));
        console.log('[worker]   row[3] (expect line?):  ', JSON.stringify(rows[0][3]));
        console.log('[worker]   row[4] (expect ctx?):   ', JSON.stringify(rows[0][4]));
        console.log('[worker]   row[5]:', JSON.stringify(rows[0][5]));
        console.log('[worker]   row[6]:', JSON.stringify(rows[0][6]));
    }

    /* 3. Marshal rows as TSV (line, context, symbol, directory, filename) */
    var tsvLines = rows.map(function(row) {
        return [
            row[3] != null ? row[3] : '',
            row[4] != null ? row[4] : '',
            row[0] != null ? row[0] : '',
            row[1] != null ? row[1] : '',
            row[2] != null ? row[2] : ''
        ].join('\t');
    });
    var rowsTsv = tsvLines.join('\n');
    console.log('[worker] rowsTsv (first 300 chars):\n' + rowsTsv.substring(0, 300));

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
    var totalFiles      = expectSingleValue(db, 'SELECT COUNT(DISTINCT directory || filename) FROM code_index');
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
