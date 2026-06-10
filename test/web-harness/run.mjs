/* run.mjs -- headless end-to-end harness for the qi web bridge.
 *
 * Loads the real qi-web.wasm under Node, opens the same code-index snapshot the
 * browser serves, and runs a battery of qi commands through the SHARED pipeline
 * (html/qi-pipeline.js) -- the identical code path the browser worker uses.
 * Each case asserts that expected fragments appear in (and, optionally, that
 * forbidden fragments are absent from) the formatted output.
 *
 * This closes the iteration loop: after `make web`, run this instead of a
 * browser round-trip to confirm every WASM export (qi_web_build, qi_web_format,
 * qi_web_format_files, qi_web_toc_format, qi_web_format_breakdown) still works.
 *
 * Usage:  node test/web-harness/run.mjs [-v]
 *   -v / --verbose   print the first chunk of each case's output
 */

import { fileURLToPath, pathToFileURL } from 'url';
import { dirname, join } from 'path';

import { loadQiModule } from './load-qi.mjs';
import { openDb } from './db.mjs';
import { makeSourceProvider } from './sources.mjs';
import { runUnitTests } from './unit.mjs';
import { loadProject, resolveProjectPath, requireDb } from './manifest.mjs';

var HERE = dirname(fileURLToPath(import.meta.url));
var REPO_ROOT = join(HERE, '..', '..');
var HTML_DIR = join(REPO_ROOT, 'html');

var VERBOSE = process.argv.includes('-v') || process.argv.includes('--verbose');

/* Optional --project <id>; defaults to the first entry of the preferred
 * manifest (test manifest if present, else html/projects.json -- see
 * manifest.mjs). */
function selectedProjectId() {
    var i = process.argv.indexOf('--project');
    return i >= 0 ? process.argv[i + 1] : null;
}

function stripAnsi(s) {
    /* eslint-disable-next-line no-control-regex */
    return s.replace(/\x1b\[[0-9;]*m/g, '');
}

/* Each case: cmd, and fragments that must appear (expect) / must not (absent).
 * Fragments are matched against ANSI-stripped output, substring-wise, so they
 * survive coloring and CRLF.  Cases are intentionally loose on exact wording
 * (which is the C formatter's job to own) but tight on the structural markers
 * that prove each export ran and wired up correctly. */
var CASES = [
    {
        /* --help and -h must return the help text without hitting the DB. */
        name: '--help returns help text (qi_web_help)',
        cmd: 'qi --help',
        expect: ['Usage: qi PATTERN', 'Quick Start:', 'Display:', '--def'],
        absent: ['Error:', '--db-file'],
    },
    {
        name: 'plain query (qi_web_build/qi_web_format)',
        cmd: 'qi qi_web_build -i func --limit 5',
        expect: ['qi_web_build', 'qi-web-entry'],
    },
    {
        name: 'verbose columns',
        cmd: 'qi malloc -i func --limit 3',
        expect: ['malloc'],
    },
    {
        name: 'files mode (qi_web_format_files)',
        cmd: 'qi malloc --files --limit 20',
        expect: ['.c'],
    },
    {
        name: 'toc mode (qi_web_toc_format)',
        cmd: "qi '*' -f qi-web-entry.c --toc",
        expect: ['FUNCTIONS', 'IMPORTS', './qi-web-entry.c'],
    },
    {
        /* --toc with no pattern must work: patterns are optional in TOC mode. */
        name: 'toc mode, no pattern',
        cmd: 'qi --toc -f query-index-web.c',
        expect: ['query-index-web.c', 'FUNCTIONS'],
        absent: ['Error:'],
    },
    {
        name: 'breakdown on truncation (qi_web_format_breakdown)',
        cmd: "qi '*' -i func --limit 3",
        expect: ['Result breakdown:'],
    },
    {
        /* --parent-type resolves the parent symbol to its same-file definition
         * and matches that definition's declared type.  In sourceminder,
         * c/index-c.c populates `config`, a VAR of type IndexerConfig, via
         * designated initializers (config.parser_init = ...). */
        name: 'parent-type filter (--parent-type)',
        cmd: "qi '*' -i prop --parent-type IndexerConfig",
        expect: ['Filtering by parent type: IndexerConfig', 'parser_init', 'index-c.c'],
        absent: ['Error:'],
        needsBuild: true,
    },
    {
        name: 'within scope (--within)',
        cmd: 'qi wo_init --within qi_web_build',
        expect: ['wo_init'],
        needsBuild: true,
    },
    {
        /* --debug shows runnable, labeled SQL (inlined values, not native's '?'
         * placeholders) at each execution point.  -f shared/ + --limit forces a
         * file-filter count, a truncated total (breakdown), and a context summary. */
        name: 'debug emits runnable labeled SQL (qi_web_format --debug)',
        cmd: "qi malloc -f shared/ --debug --limit 3",
        expect: [
            'SQL: [File filter count]',
            'SQL: [Main query]',
            'SQL: [Get total count]',
            'SQL: [Get context summary]',
            "symbol LIKE 'malloc'",   // inlined value, runnable as-is
        ],
        absent: ['LIKE ? ESCAPE'],    // not native's bound-parameter form
        needsBuild: true,
    },
    {
        name: 'source expansion (-e, needs rebuilt ABI)',
        cmd: 'qi qi_web_build -i func -e --limit 1',
        expect: ['char *qi_web_build'],
        needsBuild: true,
    },
];

async function main() {
    /* Pure-helper unit tests first (no build needed), so they always report
     * even if the WASM module or snapshot fails to load below.  Their counts
     * fold into the single combined total at the end. */
    process.stdout.write('=== unit (pure helpers) ===\n');
    var unit = await runUnitTests();
    process.stdout.write(unit.pass + ' passed, ' + unit.fail + ' failed\n\n');

    process.stdout.write('=== integration (wasm + db) ===\n');
    var project = loadProject(selectedProjectId());
    var dbPath = resolveProjectPath(project, project.dbUrl);
    var sourceRoot = resolveProjectPath(project, project.sourceBase);
    requireDb(project, dbPath);

    process.stdout.write('Project: ' + project.id + '  (db: ' + project.dbUrl +
        ', sources: ' + project.sourceBase + ')\n');
    process.stdout.write('Loading qi-web.wasm + snapshot...\n');
    var qiModule = await loadQiModule(HTML_DIR);
    var opened = await openDb(HTML_DIR, dbPath);
    var getSources = makeSourceProvider(sourceRoot);

    var pipeline = await import(pathToFileURL(join(HTML_DIR, 'qi-pipeline.js')).href);

    var ctx = {
        qiModule: qiModule,
        db: opened.db,
        getSources: getSources,
        debug: VERBOSE,
        log: console.log.bind(console),
    };

    var pass = 0, fail = 0;
    for (var i = 0; i < CASES.length; i++) {
        var c = CASES[i];
        var out, err = null;
        try {
            out = await pipeline.runQuery(ctx, c.cmd);
        } catch (e) {
            err = e;
        }

        if (err) {
            fail++;
            report(false, c, 'threw: ' + (err && err.message ? err.message : String(err)));
            continue;
        }

        var clean = stripAnsi(out);
        var missing = (c.expect || []).filter(function(f) { return clean.indexOf(f) < 0; });
        var present = (c.absent || []).filter(function(f) { return clean.indexOf(f) >= 0; });

        if (missing.length === 0 && present.length === 0) {
            pass++;
            report(true, c, '');
        } else {
            fail++;
            var why = [];
            if (missing.length) why.push('missing: ' + JSON.stringify(missing));
            if (present.length) why.push('unexpected: ' + JSON.stringify(present));
            report(false, c, why.join('; '));
        }

        if (VERBOSE) {
            process.stdout.write('--- output (first 500 chars) ---\n');
            process.stdout.write(clean.slice(0, 500) + '\n--- end ---\n');
        }
    }

    opened.db.close();

    var totalPass = unit.pass + pass;
    var totalFail = unit.fail + fail;
    process.stdout.write('\n=== total ===\n' +
        totalPass + ' passed, ' + totalFail + ' failed, ' +
        (totalPass + totalFail) + ' total\n');
    process.exit(totalFail === 0 ? 0 : 1);
}

function report(ok, c, detail) {
    var tag = ok ? 'PASS' : 'FAIL';
    process.stdout.write(tag + '  ' + c.name + '  [' + c.cmd + ']' +
        (detail ? '\n        ' + detail : '') + '\n');
}

main().catch(function(e) {
    process.stderr.write('harness error: ' + (e && e.stack ? e.stack : String(e)) + '\n');
    process.exit(2);
});
