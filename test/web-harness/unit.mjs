/* unit.mjs -- fast unit tests for the pure helpers in html/qi-pipeline.js.
 * No WASM module or DB needed; these exercise the string/SQL logic directly.
 *
 * Run standalone:           node test/web-harness/unit.mjs
 * Or folded into run.mjs's combined total (what `make web-test` does).
 */

import { fileURLToPath, pathToFileURL } from 'url';
import { dirname, join } from 'path';

var HERE = dirname(fileURLToPath(import.meta.url));
var HTML_DIR = join(HERE, '..', '..', 'html');

/* Run the unit checks; returns { pass, fail }.  Prints a line per failure. */
export async function runUnitTests() {
    var pipeline = await import(pathToFileURL(join(HTML_DIR, 'qi-pipeline.js')).href);
    var injectWhereClause = pipeline.injectWhereClause;

    var pass = 0, fail = 0;
    /* Compare with runs of whitespace collapsed: injection may leave a harmless
     * double space, but only clause PLACEMENT (relative to keywords/subqueries)
     * is under test, and whitespace runs are insignificant in SQL. */
    function norm(s) { return s.replace(/\s+/g, ' ').trim(); }
    function check(name, got, want) {
        if (norm(got) === norm(want)) { pass++; return; }
        fail++;
        process.stdout.write('FAIL  ' + name + '\n');
        process.stdout.write('        got:  ' + JSON.stringify(got) + '\n');
        process.stdout.write('        want: ' + JSON.stringify(want) + '\n');
    }

    var W = " AND (x = 1)";

    /* No-op when clause is empty. */
    check('empty clause is a no-op',
        injectWhereClause('SELECT * FROM t ORDER BY a', ''),
        'SELECT * FROM t ORDER BY a');

    /* Insert before a plain outer ORDER BY. */
    check('before outer ORDER BY',
        injectWhereClause('SELECT * FROM t WHERE a=1 ORDER BY a', W),
        'SELECT * FROM t WHERE a=1' + W + ' ORDER BY a');

    /* Append when there is no tail clause (the COUNT_SQL shape). */
    check('append when no tail clause',
        injectWhereClause('SELECT COUNT(*) FROM t WHERE a=1 ', W),
        'SELECT COUNT(*) FROM t WHERE a=1 ' + W);

    /* Earliest outer tail clause wins (GROUP BY before ORDER BY). */
    check('before outer GROUP BY (not the later ORDER BY)',
        injectWhereClause('SELECT c, COUNT(*) FROM t WHERE a=1 GROUP BY c ORDER BY 2 DESC', W),
        'SELECT c, COUNT(*) FROM t WHERE a=1' + W + ' GROUP BY c ORDER BY 2 DESC');

    /* THE FIX: a subquery containing ORDER BY/LIMIT must NOT capture the
     * injection; it lands before the OUTER ORDER BY (proximity EXISTS subquery). */
    var nested = "SELECT * FROM code_index ci WHERE (ci.symbol LIKE 'malloc') " +
        "AND EXISTS (SELECT 1 FROM code_index WHERE symbol LIKE 'free' ORDER BY line LIMIT 1) " +
        "ORDER BY ci.directory, ci.filename, ci.line";
    var nestedWant = "SELECT * FROM code_index ci WHERE (ci.symbol LIKE 'malloc') " +
        "AND EXISTS (SELECT 1 FROM code_index WHERE symbol LIKE 'free' ORDER BY line LIMIT 1)" +
        W + " ORDER BY ci.directory, ci.filename, ci.line";
    check('subquery ORDER BY/LIMIT is skipped; injects before outer ORDER BY',
        injectWhereClause(nested, W), nestedWant);

    /* THE FIX: a keyword inside a string literal must NOT capture the injection. */
    check('keyword inside string literal is skipped',
        injectWhereClause("SELECT * FROM t WHERE symbol LIKE 'order by' ORDER BY a", W),
        "SELECT * FROM t WHERE symbol LIKE 'order by'" + W + " ORDER BY a");

    /* Escaped quote ('') inside a literal does not prematurely end the string. */
    check("escaped quote in literal handled",
        injectWhereClause("SELECT * FROM t WHERE s LIKE 'a''b LIMIT 1' ORDER BY a", W),
        "SELECT * FROM t WHERE s LIKE 'a''b LIMIT 1'" + W + " ORDER BY a");

    /* No tail clause at all, only inside a subquery -> append (don't inject into sub). */
    check('only subquery has tail clause -> append at end',
        injectWhereClause("SELECT * FROM t WHERE EXISTS (SELECT 1 FROM u ORDER BY x)", W),
        "SELECT * FROM t WHERE EXISTS (SELECT 1 FROM u ORDER BY x)" + W);

    return { pass: pass, fail: fail };
}

/* Standalone: run and exit. (When imported by run.mjs, this block is skipped.) */
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    var r = await runUnitTests();
    process.stdout.write('\n' + r.pass + ' passed, ' + r.fail + ' failed, ' +
        (r.pass + r.fail) + ' total\n');
    process.exit(r.fail === 0 ? 0 : 1);
}
