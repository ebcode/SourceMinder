/* qi-pipeline.js -- the qi query pipeline (SQL build -> DB execute -> format),
 * shared verbatim by the browser worker (qi-worker.js) and the Node test
 * harness (test/web-harness/).  This is the single source of truth for the
 * bridge's query orchestration: extracting it means the harness exercises the
 * exact code the browser runs, so a Node test can no longer pass while the
 * browser silently breaks.
 *
 * It touches no browser- or Node-specific global.  Every external dependency is
 * injected via `ctx`:
 *
 *   ctx = {
 *     qiModule,   // an initialized QiWebModule (ccall / _malloc / _free / ...)
 *     db,         // { selectArrays(sql) -> rows[], selectValue(sql) -> scalar }
 *     getSources, // async (paths[]) -> [{ path, content }, ...]  (present files only)
 *     debug,      // optional truthy to enable diagnostic logging (default off)
 *     log,        // optional (...args) => void sink, used only when debug is on
 *   }
 *
 * The worker passes activeDb / SourceCache.getFiles / console.log; the harness
 * passes a sqlite-wasm connection / a local-file reader / a silent logger. */

/* Canonical filepath for a row, matching build_row_filepath() in
 * qi-web-entry.c exactly -- this string is the source-blob lookup key, so the
 * two sides MUST agree (see QI_WEB_FILE_PLAN.md). */
export function buildRowFilepath(dir, file) {
    if (dir && dir[dir.length - 1] !== '/') return dir + '/' + file;
    return dir + file;
}

/* Marshal present source files into one WASM heap buffer as NUL-framed
 * "<path>\0<content>\0" records (the format source_map_parse() walks in place).
 * Returns {ptr, len}; caller MUST qiModule._free(ptr) once the call returns.
 * stringToUTF8 writes straight into the heap, so the payload is copied exactly
 * once -- no big JS string, no ccall stack marshaling. */
export function marshalSources(qiModule, files) {
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

export function expectSingleValue(db, sql) {
    var value = db.selectValue(sql);
    if (value === undefined) {
        throw new Error('Query returned no rows: ' + sql);
    }
    return value;
}

/* Offset of the first trailing clause keyword (GROUP BY / ORDER BY / LIMIT) that
 * belongs to the OUTER query: at parenthesis depth 0 and outside any string
 * literal.  A plain indexOf() would also match those keywords inside a subquery
 * (e.g. a proximity EXISTS(...)) or inside a user-supplied string literal (e.g.
 * symbol LIKE 'order by'), landing the injected clause in the wrong place.  We
 * track quote state ('' is SQLite's escaped quote) and paren depth so only
 * top-level keywords count.  Returns -1 if there is no outer tail clause. */
function findOuterTailClause(sql) {
    var markers = ['GROUP BY', 'ORDER BY', 'LIMIT'];
    var depth = 0;
    var inStr = false;
    for (var i = 0; i < sql.length; i++) {
        var c = sql[i];
        if (inStr) {
            if (c === "'") {
                if (sql[i + 1] === "'") i++;   /* '' -> escaped quote, stay in string */
                else inStr = false;
            }
            continue;
        }
        if (c === "'") { inStr = true; continue; }
        if (c === '(') { depth++; continue; }
        if (c === ')') { if (depth > 0) depth--; continue; }
        if (depth !== 0) continue;
        for (var m = 0; m < markers.length; m++) {
            if (sql.startsWith(markers[m], i)) return i;
        }
    }
    return -1;
}

/* Insert an extra " AND (...)" clause ahead of the outer query's trailing
 * GROUP BY / ORDER BY / LIMIT, or append it if there is none.  Used to push the
 * --within scope into the main SQL *and* into COUNT_SQL / BREAKDOWN_SQL, which
 * qi_web_build() precomputes from the base SQL before any WITHIN post-processing
 * -- without this, totals and the breakdown would reflect the unscoped query. */
export function injectWhereClause(sqlStr, clause) {
    if (!clause) return sqlStr;
    var cut = findOuterTailClause(sqlStr);
    if (cut >= 0) return sqlStr.slice(0, cut) + clause + ' ' + sqlStr.slice(cut);
    return sqlStr + clause;
}

/* Run one qi command end-to-end and return the formatted output string.
 * `ctx` supplies the WASM module, DB connection, source provider, and logger
 * (see file header). */
export async function runQuery(ctx, input, opts) {
    opts = opts || {};
    /* suppressHeader: drop the "Searching for:" header on this run.  Set when
     * re-running for the no-results partial-match retry, so the header (already
     * printed by the zero-row pass) is not repeated -- mirrors native's
     * goto retry_query. */
    var suppressHeader = !!opts.suppressHeader;
    var qiModule = ctx.qiModule;
    var db = ctx.db;
    /* Logging is gated on ctx.debug: when off, `log` is a no-op AND the
     * expensive payloads (JSON.stringify, substring, full-result concat) are
     * skipped via `if (debug)` guards -- JS evaluates call arguments eagerly, so
     * a no-op logger alone would still do that work on every query. */
    var debug = !!ctx.debug;
    var log = (debug && ctx.log) ? ctx.log : function() {};

    log('[pipeline] runQuery called, cmd:', input);

    /* 1. Build SQL via WASM */
    var buildResult = qiModule.ccall('qi_web_build', 'string', ['string'], [input]);
    if (debug) log('[pipeline] qi_web_build raw result:\n' + buildResult);

    /* Parse build result: lines like "PATTERNS|p1 p2", "SQL|SELECT ..." */
    var buildLines = {};
    var lines = buildResult.split('\n');
    for (var i = 0; i < lines.length; i++) {
        var pipe = lines[i].indexOf('|');
        if (pipe >= 0) {
            buildLines[lines[i].slice(0, pipe)] = lines[i].slice(pipe + 1);
        }
    }
    if (debug) log('[pipeline] parsed buildLines keys:', Object.keys(buildLines));

    if (buildLines.ERROR && buildLines.ERROR !== 'OK') {
        log('[pipeline] build error:', buildLines.ERROR);
        return 'Error: ' + buildLines.ERROR + '\r\n';
    }

    var limit = parseInt(buildLines.LIMIT || '0', 10);   /* 0 = unlimited (matches CLI) */
    log('[pipeline] LIMIT:', limit);

    /* File-filter count for the "Filtering by file: N file(s) matched" header.
     * The C side emits a FILE_FILTER_COUNT_SQL query + an HDR sentinel; we run
     * the count here (the worker owns the DB) and append FILE_FILTER_COUNT back
     * into build_info so print_hdr_lines can expand the sentinel.  (Not set in
     * TOC mode -- native doesn't print this header for --toc.) */
    if (buildLines.FILE_FILTER_COUNT_SQL) {
        var ffCount = 0;
        try {
            ffCount = expectSingleValue(db, buildLines.FILE_FILTER_COUNT_SQL);
        } catch (e) {
            log('[pipeline] file-filter count query error:', e);
        }
        buildResult += '\nFILE_FILTER_COUNT|' + ffCount;
    }

    /* TOC mode: different SQL, format, and output pipeline */
    if (buildLines.MODE === 'toc') {
        var tocSql = buildLines.TOC_SQL;
        if (!tocSql) return 'Error: No TOC SQL built.\r\n';

        /* Count breakdown by context type */
        var contextCounts = '';
        var countSql = buildLines.TOC_COUNT_SQL;
        if (countSql) {
            try {
                var countRows = db.selectArrays(countSql);
                log('[pipeline] TOC count query rows:', countRows.length);
                var parts = [];
                for (var ci = 0; ci < countRows.length; ci++) {
                    parts.push(String(countRows[ci][0]) + ':' + String(countRows[ci][1]));
                }
                contextCounts = parts.join('\n');
            } catch (e) {
                log('[pipeline] TOC count query error:', e);
                contextCounts = '';
            }
        }

        /* Execute TOC SQL */
        var tocQuery = limit > 0 ? (tocSql + ' LIMIT ' + limit) : tocSql;
        var tocRows = db.selectArrays(tocQuery);
        log('[pipeline] TOC query rows:', tocRows.length);

        /* Count total available (without limit) */
        var totalAvailable = tocRows.length;
        if (limit > 0 && tocRows.length >= limit) {
            try {
                totalAvailable = expectSingleValue(db,
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
        log('[pipeline] qi_web_toc_format length:', tocOutput.length);
        return tocOutput;
    }

    /* Files mode: show only distinct file paths */
    if (buildLines.MODE === 'files') {
        var filesSql = buildLines.FILES_SQL;
        if (!filesSql) return 'Error: No FILES SQL built.\r\n';

        var fileQuery = limit > 0 ? (filesSql + ' LIMIT ' + limit) : filesSql;
        var fileRows = db.selectArrays(fileQuery);
        log('[pipeline] files query rows:', fileRows.length);

        /* Count total available (without limit) if limit was hit */
        var totalFiles = fileRows.length;
        if (limit > 0 && fileRows.length >= limit) {
            try {
                totalFiles = expectSingleValue(db,
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
            ['string', 'string', 'number', 'number'],
            [buildResult, filesTsv, fileRows.length, totalFiles]);
    }

    /* Help mode: no DB query; delegate entirely to qi_web_help() */
    if (buildLines.MODE === 'help') {
        return qiModule.ccall('qi_web_help', 'string', [], []);
    }

    /* --list-types: no DB query; delegate to qi_web_list_types() */
    if (buildLines.MODE === 'list-types') {
        return qiModule.ccall('qi_web_list_types', 'string', [], []);
    }

    var sql = buildLines.SQL;
    log('[pipeline] SQL:', sql);

    if (!sql) {
        return 'Error: No SQL built for query.\r\n';
    }

    /* --within scope clause, also applied to COUNT_SQL/BREAKDOWN_SQL below. */
    var withinWhere = '';

    /* Handle --within: resolve definition locations and inject WHERE clauses */
    if (buildLines.WITHIN_SQL) {
        var withinSql = buildLines.WITHIN_SQL;
        log('[pipeline] WITHIN_SQL:', withinSql);

        var withinRows = db.selectArrays(withinSql);
        log('[pipeline] within lookup rows:', withinRows.length);

        /* Round-trip the instance count for the "Within symbol(s): ... (N
         * instances)" header (HDR|\x02 sentinel), mirroring FILE_FILTER_COUNT.
         * print_hdr_lines reads WITHIN_COUNT from build_info to pluralize. */
        buildResult += '\nWITHIN_COUNT|' + withinRows.length;

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
        for (var ri2 = 0; ri2 < withinRows.length; ri2++) {
            var row = withinRows[ri2];
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
            if (debug) log('[pipeline] SQL with within injected (first 400 chars):\n' + sql.substring(0, 400));
        }
    }

    /* 2. Execute SQL against the DB.  COUNT_SQL was precomputed from the base
     * SQL, so re-apply the --within scope to it (withinWhere is '' when --within
     * is absent).  The fallback wraps the already-scoped `sql`. */
    var mainCountSql = buildLines.COUNT_SQL
        ? injectWhereClause(buildLines.COUNT_SQL, withinWhere)
        : ('SELECT COUNT(*) FROM (' + sql + ' LIMIT -1)');
    var total = expectSingleValue(db, mainCountSql);
    log('[pipeline] total matches:', total);

    /* limit <= 0 means "no limit" (matches TOC/files modes); appending LIMIT 0
     * would suppress every row while total stays positive. */
    var mainQuery = limit > 0 ? (sql + ' LIMIT ' + limit) : sql;
    var rows = db.selectArrays(mainQuery);
    log('[pipeline] row count:', rows.length);

    if (debug && rows.length > 0) {
        log('[pipeline] first row (all 14 columns):', JSON.stringify(rows[0]));
    }

    /* 3. Marshal rows as TSV (all 14 columns, canonical SELECT * order) */
    var rowTsvLines = rows.map(function(row) {
        return row.map(function(v) { return v != null ? String(v) : ''; }).join('\t');
    });
    var rowsTsv2 = rowTsvLines.join('\n');
    if (debug) {
        log('[pipeline] rowsTsv (first 300 chars):\n' + rowsTsv2.substring(0, 300));
        log('[pipeline] TSV fields per row:', (rowTsvLines[0] || '').split('\t').length);
    }

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
        var files = await ctx.getSources(paths);
        var marshalled = marshalSources(qiModule, files);
        srcPtr = marshalled.ptr;
        srcLen = marshalled.len;
        log('[pipeline] source files:', files.length, 'heap bytes:', srcLen);
    }

    /* --debug: hand the C formatter the *runnable* SQL we actually executed
     * (LIMIT and --within scope already applied), so each "SQL: [...]" line can
     * be pasted into sqlite3 against the downloaded .db to reproduce what is
     * shown above it.  Gated on the command's --debug (buildLines.DEBUG), NOT
     * the pipeline's `debug` logger toggle, and kept off otherwise to stay lean. */
    if (buildLines.DEBUG === '1') {
        buildResult += '\nDEBUG_MAIN_SQL|' + mainQuery;
        buildResult += '\nDEBUG_COUNT_SQL|' + mainCountSql;
        if (buildLines.WITHIN_SQL)
            buildResult += '\nDEBUG_WITHIN_SQL|' + buildLines.WITHIN_SQL;
        if (buildLines.FILE_FILTER_COUNT_SQL)
            buildResult += '\nDEBUG_FILE_FILTER_SQL|' + buildLines.FILE_FILTER_COUNT_SQL;
    }

    /* 5. Format qi output via WASM.  Free the source buffer no matter what. */
    var formatted;
    try {
        formatted = qiModule.ccall('qi_web_format', 'string',
            ['string', 'string', 'number', 'number', 'number', 'number', 'number'],
            [buildResult, rowsTsv2, total, rows.length, srcPtr, srcLen, suppressHeader ? 1 : 0]);
    } finally {
        if (srcPtr) qiModule._free(srcPtr);
    }
    if (debug) log('[pipeline] qi_web_format result (first 300 chars):\n' + formatted.substring(0, 300));

    /* 6. Append breakdown when results are truncated (mirrors CLI
     * get_context_summary).  --raw suppresses all non-source output, so skip it
     * entirely -- native gates the equivalent print_summary_stats behind
     * `if (!raw)`; the breakdown export also carries the trailing Tip line.
     * -q drops the breakdown too (footer chrome): native quiet returns from
     * print_summary_stats before the breakdown prints. */
    if (total > rows.length && buildLines.BREAKDOWN_SQL && buildLines.RAW !== '1' &&
        buildLines.QUIET !== '1') {
        try {
            /* Same --within scoping as COUNT_SQL: BREAKDOWN_SQL was precomputed
             * before WITHIN post-processing; inject ahead of its GROUP BY. */
            var bdSql = injectWhereClause(buildLines.BREAKDOWN_SQL, withinWhere);
            var bdRows = db.selectArrays(bdSql);
            var bdTsv = bdRows.map(function(r) { return r[0] + '\t' + r[1]; }).join('\n');
            formatted += qiModule.ccall('qi_web_format_breakdown', 'string',
                ['string', 'string'], [bdTsv, buildLines.DEBUG === '1' ? bdSql : '']);
        } catch (e) {
            /* breakdown is cosmetic; swallow errors silently */
        }
    }

    /* 7. Zero-results diagnostics + partial-match retry (mirrors query-index.c's
     * no-match path).  Skipped under --raw, and on a suppressed-header run so the
     * retry can't recurse.  C decides the wording and whether to retry; JS runs
     * the filter-free counts and, if asked, re-runs the query wildcarded. */
    if (total === 0 && buildLines.RAW !== '1' && !suppressHeader) {
        var nrPatterns = (buildLines.NR_PATTERNS || '').split(' ').filter(Boolean);
        var countLines = [];
        for (var pi = 0; pi < nrPatterns.length; pi++) {
            var exactSql = buildLines['NR_EXACT_' + pi];
            var wildSql = buildLines['NR_WILD_' + pi];   /* absent if pattern has '%' */
            var exactCount = 0, wildCount = -1;
            try { if (exactSql) exactCount = expectSingleValue(db, exactSql); } catch (e) { exactCount = 0; }
            try { if (wildSql !== undefined) wildCount = expectSingleValue(db, wildSql); } catch (e) { wildCount = -1; }
            countLines.push(exactCount + '\t' + wildCount);
        }

        /* Filter-exclusion diagnostics: run each NRD_SQL_<flag> probe (the
         * query with that one filter cleared) and hand the counts back as
         * NRD_CNT_<flag>, so the C side can name the culprit filter.  The
         * file hint additionally reports WHERE the matches live when -f was
         * the culprit (paths joined with tabs; native prints dir+file
         * concatenated, so no separator is inserted). */
        for (var nk in buildLines) {
            if (nk.indexOf('NRD_SQL_') === 0) {
                var nrdCount = 0;
                try { nrdCount = expectSingleValue(db, buildLines[nk]); } catch (e) { nrdCount = 0; }
                buildResult += '\nNRD_CNT_' + nk.slice('NRD_SQL_'.length) + '|' + nrdCount;
            }
        }
        if (buildLines.NRD_FILEHINT_COUNT_SQL) {
            var fhTotal = 0;
            try { fhTotal = expectSingleValue(db, buildLines.NRD_FILEHINT_COUNT_SQL); } catch (e) { fhTotal = 0; }
            buildResult += '\nNRD_FILEHINT_TOTAL|' + fhTotal;
            if (fhTotal > 0 && buildLines.NRD_FILEHINT_SQL) {
                try {
                    var fhRows = db.selectArrays(buildLines.NRD_FILEHINT_SQL);
                    var fhPaths = fhRows.map(function(r) {
                        return String(r[0] != null ? r[0] : '') + String(r[1] != null ? r[1] : '');
                    });
                    buildResult += '\nNRD_FILEHINT_ROWS|' + fhPaths.join('\t');
                } catch (e) { /* hint is cosmetic; the count line alone is ignored */ }
            }
        }

        var nrOut = qiModule.ccall('qi_web_format_no_results', 'string',
            ['string', 'string'], [buildResult, countLines.join('\n')]);
        var nlPos = nrOut.indexOf('\n');
        var ctrl = nlPos >= 0 ? nrOut.slice(0, nlPos) : nrOut;
        formatted += nlPos >= 0 ? nrOut.slice(nlPos + 1) : '';

        /* Control line "RETRY|<idx>": re-run the query with that pattern
         * wildcarded (NR_RETRY_SQL was built for the single-pattern case), with
         * the header suppressed so only the partial-match results follow. */
        var rm = /^RETRY\|(-?\d+)/.exec(ctrl);
        if (rm && parseInt(rm[1], 10) >= 0 && buildLines.NR_RETRY_SQL) {
            var retryPattern = nrPatterns[parseInt(rm[1], 10)];
            formatted += await runQuery(ctx, wildcardPattern(input, retryPattern),
                                        { suppressHeader: true });
        }
    }

    return formatted;
}

/* Wrap the whole-word occurrence of `pattern` in the command string with '*' so
 * a re-run does a substring (LIKE %pattern%) search -- the JS analogue of native
 * swapping patterns->patterns[i] for "%X%".  Only reached for plain (no-wildcard)
 * single patterns, so a word-boundary replace is unambiguous. */
function wildcardPattern(input, pattern) {
    if (!pattern) return input;
    var esc = pattern.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    return input.replace(new RegExp('(^|\\s)' + esc + '(\\s|$)'), '$1*' + pattern + '*$2');
}
