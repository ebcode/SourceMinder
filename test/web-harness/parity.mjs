/* parity.mjs -- side-by-side diff of native `qi` vs web `qi` (Phase 9).
 *
 * For a given qi command this runs it TWO ways against the SAME data and diffs
 * the (ANSI-stripped, trailing-whitespace-normalized) output:
 *
 *   native:  the installed `qi` binary, invoked with cwd = the project's local
 *            source tree and --db-file code-index.db, so recorded relative
 *            paths resolve to real source for -e / -C / -A / -B.
 *   web:     the real qi-web.wasm + qi-pipeline.runQuery -- the identical code
 *            path the browser worker uses (same module run.mjs exercises).
 *
 * Both sides read the per-project DB and source tree under html/sources/<id>/,
 * so any divergence is a genuine native/web parity bug (ORDER BY drift,
 * formatting mismatch, off-by-one in the source render twin, ...), not a
 * data-skew artifact.
 *
 * Usage:
 *   node test/web-harness/parity.mjs "qi New -i func -C 3"   # diff one command
 *   node test/web-harness/parity.mjs --batch                 # canonical suite
 *   node test/web-harness/parity.mjs --project htop "qi % -i func --toc"
 *     -v / --verbose   print both outputs in full, even on an exact match
 *
 * Exit status: 0 if every command matched, 1 if any diverged, 2 on harness
 * error.  Mirrors run.mjs so it slots into the same `make web` loop.
 */

import { readFileSync } from 'fs';
import { spawnSync } from 'child_process';
import { fileURLToPath, pathToFileURL } from 'url';
import { dirname, join } from 'path';

import { loadQiModule } from './load-qi.mjs';
import { openDb } from './db.mjs';
import { makeSourceProvider } from './sources.mjs';

var HERE = dirname(fileURLToPath(import.meta.url));
var REPO_ROOT = join(HERE, '..', '..');
var HTML_DIR = join(REPO_ROOT, 'html');

/* The native DB lives beside the source it indexes (html/sources/<id>/), so
 * running qi from that cwd makes recorded relative paths resolve to real files.
 * This is the per-project native DB the .browser.db snapshot is VACUUM'd from,
 * so native and web read byte-identical index data. */
var NATIVE_DB = 'code-index.db';

/* Canonical parity suite -- §8.7 of QI_WEB_FILE_PLAN.md, made project-agnostic
 * with wildcard+limit queries so they exercise the source-render twin on any
 * language.  --limit makes ORDER BY drift between query-index.c and
 * query-index-web.c surface immediately (different rows -> a loud diff). */
var BATCH = [
    "qi % -i func -C 3 --limit 5",       // -C context window
    "qi % -i func --def -e --limit 3",   // -e expand definitions
    "qi % -i func -e --raw --limit 3",   // -e under --raw (bare source)
    "qi % -i func -A 2 --limit 3",       // after-context only (asymmetric)
    "qi % -i func -B 2 --limit 3",       // before-context only (asymmetric)
    "qi % -f % --toc --limit 0",         // TOC mode (-f required; --limit 0 = unlimited)
    "qi % --files --limit 20",           // files-only mode
    "qi zzqzxnomatchzz",                 // zero results: "matched 0 / no partial matches"
    "qi a",                              // zero results: short pattern (<2 chars), no retry
    "qi COLUMN -i func --limit-per-file 2 --limit 5",  // per-file display limit (2 per file, spans 3 files)
    "qi hello -i com",                   // single match: "Found 1 match" (singular pluralization)
    "qi % -f config.h --toc",            // TOC MACROS section + IMP-distinct breakdown count
    "qi % -f config.h --toc -i macro",   // MACRO is an allowed TOC context (-i)
    "qi % -f config.h --toc -x macro",   // TOC exclude-context (-x) removes the MACROS section
    "qi % -f query-index-web.c --toc --debug",  // --toc --debug: prints count + main SQL
    "qi -f query-index-web.c --toc --debug",    // bare "%" swallowed: identical to the line above
    "qi malloc -f shared/",              // "Filtering by file: N files matched" (count excludes pattern)
    "qi % -f yoyo",                      // 0 files matched: suggestion block
    "qi malloc --files -f shared/",      // file-filter header in files mode
    "qi % --within lookup_within_definitions --limit 3",  // --within header: singular symbol + singular instance
    "qi % --within build_query_sql lookup_within_definitions --limit 3",  // --within header: plural symbols + plural instances
    "qi lookup_within_definitions --full --limit 6",  // --full: full headers (SYMBOL/CONTEXT) + full context names (FUNCTION), incl. overflow
    "qi % -i func --full --limit 5",  // --full: full context-type header (FUNCTION) + full column headers
];

function arg(name) {
    var i = process.argv.indexOf(name);
    return i >= 0 ? process.argv[i + 1] : null;
}
var VERBOSE = process.argv.includes('-v') || process.argv.includes('--verbose');
var BATCH_MODE = process.argv.includes('--batch');

function resolveFromHtml(url) {
    return join(HTML_DIR, String(url).replace(/^\.\//, ''));
}

function loadProject() {
    var manifest = JSON.parse(readFileSync(join(HTML_DIR, 'projects.json'), 'utf8'));
    if (!Array.isArray(manifest) || manifest.length === 0) {
        throw new Error('projects.json is empty or malformed');
    }
    var wantId = arg('--project');
    var p = wantId
        ? manifest.find(function(x) { return x.id === wantId; })
        : manifest[0];
    if (!p) throw new Error('no project with id "' + wantId + '" in projects.json');
    return p;
}

/* The positional command string: the first argv item that is not a known
 * option and not an option's value.  Lets `--project X "qi ..."` work in any
 * order. */
function commandArg() {
    var skip = { '--project': true };          // options that consume a value
    var argv = process.argv.slice(2);
    for (var i = 0; i < argv.length; i++) {
        var a = argv[i];
        if (skip[a]) { i++; continue; }         // skip the value too
        if (a[0] === '-') continue;             // a bare flag (-v, --batch)
        return a;
    }
    return null;
}

/* Split a command line into argv, honoring single/double quotes so patterns
 * like  qi '*' -f foo.c  survive.  Mirrors how a shell would hand argv to the
 * native binary; the web side gets the raw string (runQuery tokenizes itself). */
function tokenize(line) {
    var out = [], cur = '', q = null, has = false;
    for (var i = 0; i < line.length; i++) {
        var ch = line[i];
        if (q) {
            if (ch === q) q = null;
            else cur += ch;
        } else if (ch === '"' || ch === "'") {
            q = ch; has = true;
        } else if (ch === ' ' || ch === '\t') {
            if (has) { out.push(cur); cur = ''; has = false; }
        } else {
            cur += ch; has = true;
        }
    }
    if (has) out.push(cur);
    return out;
}

function stripAnsi(s) {
    /* eslint-disable-next-line no-control-regex */
    return s.replace(/\x1b\[[0-9;]*m/g, '');
}

/* Symmetric normalization applied to BOTH sides: strip ANSI, rstrip each line
 * (terminal trailing space is not meaningful), and drop trailing blank lines.
 * Anything left is a real content difference. */
function normalize(s) {
    var lines = stripAnsi(s).split('\n').map(function(l) {
        return l.replace(/[ \t]+$/, '');
    });
    while (lines.length && lines[lines.length - 1] === '') lines.pop();
    return lines;
}

/* Run the native qi binary for a command string.  Strips the leading `qi`
 * token (the web side keeps it; runQuery consumes it itself), appends
 * --db-file, and runs from the project source root. */
function runNative(cmd, sourceRoot) {
    var argv = tokenize(cmd);
    if (argv.length && argv[0] === 'qi') argv = argv.slice(1);
    argv = argv.concat(['--db-file', NATIVE_DB]);
    var r = spawnSync('qi', argv, {
        cwd: sourceRoot, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024,
    });
    if (r.error) throw new Error('native qi failed to spawn: ' + r.error.message);
    /* qi prints diagnostics to stderr but results to stdout; for parity we
     * compare stdout only (the web side has no stderr channel). */
    return r.stdout || '';
}

/* Minimal LCS line diff -> unified-style rows ('  ' / '- ' / '+ ').  Good
 * enough to eyeball a divergence without pulling in a diff dependency. */
function diffLines(a, b) {
    var n = a.length, m = b.length;
    var lcs = [];
    for (var i = 0; i <= n; i++) lcs.push(new Array(m + 1).fill(0));
    for (i = n - 1; i >= 0; i--) {
        for (var j = m - 1; j >= 0; j--) {
            lcs[i][j] = a[i] === b[j]
                ? lcs[i + 1][j + 1] + 1
                : Math.max(lcs[i + 1][j], lcs[i][j + 1]);
        }
    }
    var rows = [];
    i = 0; j = 0;
    while (i < n && j < m) {
        if (a[i] === b[j]) { rows.push('  ' + a[i]); i++; j++; }
        else if (lcs[i + 1][j] >= lcs[i][j + 1]) { rows.push('- ' + a[i]); i++; }
        else { rows.push('+ ' + b[j]); j++; }
    }
    while (i < n) rows.push('- ' + a[i++]);
    while (j < m) rows.push('+ ' + b[j++]);
    return rows;
}

async function main() {
    var project = loadProject();
    var sourceRoot = resolveFromHtml(project.sourceBase);
    var dbPath = resolveFromHtml(project.dbUrl);

    var cmds;
    if (BATCH_MODE) {
        cmds = BATCH;
    } else {
        var one = commandArg();
        if (!one) {
            process.stderr.write(
                'usage: node test/web-harness/parity.mjs "qi <args>" | --batch ' +
                '[--project <id>] [-v]\n');
            process.exit(2);
        }
        cmds = [one];
    }

    process.stdout.write('Project: ' + project.id + '  (db: ' + project.dbUrl +
        ', sources: ' + project.sourceBase + ')\n');
    process.stdout.write('Loading qi-web.wasm + snapshot...\n\n');

    var qiModule = await loadQiModule(HTML_DIR);
    var opened = await openDb(HTML_DIR, dbPath);
    var getSources = makeSourceProvider(sourceRoot);
    var pipeline = await import(pathToFileURL(join(HTML_DIR, 'qi-pipeline.js')).href);

    var ctx = {
        qiModule: qiModule,
        db: opened.db,
        getSources: getSources,
        debug: false,
        log: console.log.bind(console),
    };

    var matched = 0, diverged = 0;
    for (var k = 0; k < cmds.length; k++) {
        var cmd = cmds[k];
        var nativeOut, webOut, err = null;
        try {
            nativeOut = runNative(cmd, sourceRoot);
            webOut = await pipeline.runQuery(ctx, cmd);
        } catch (e) {
            err = e;
        }

        if (err) {
            diverged++;
            process.stdout.write('ERROR  [' + cmd + ']\n        ' +
                (err.message || String(err)) + '\n\n');
            continue;
        }

        var nLines = normalize(nativeOut);
        var wLines = normalize(webOut);
        var same = nLines.length === wLines.length &&
            nLines.every(function(l, idx) { return l === wLines[idx]; });

        if (same) {
            matched++;
            process.stdout.write('MATCH  [' + cmd + ']  (' + nLines.length + ' lines)\n');
            if (VERBOSE) {
                process.stdout.write('--- output ---\n' + nLines.join('\n') +
                    '\n--- end ---\n');
            }
        } else {
            diverged++;
            process.stdout.write('DIFF   [' + cmd + ']  ' +
                '(native ' + nLines.length + ' / web ' + wLines.length + ' lines)\n');
            process.stdout.write('  (- native, + web)\n');
            var rows = diffLines(nLines, wLines);
            for (var r = 0; r < rows.length; r++) {
                process.stdout.write('  ' + rows[r] + '\n');
            }
            process.stdout.write('\n');
        }
    }

    opened.db.close();

    process.stdout.write('\n=== parity ===\n' + matched + ' matched, ' +
        diverged + ' diverged, ' + (matched + diverged) + ' total\n');
    process.exit(diverged === 0 ? 0 : 1);
}

main().catch(function(e) {
    process.stderr.write('harness error: ' + (e && e.stack ? e.stack : String(e)) + '\n');
    process.exit(2);
});
