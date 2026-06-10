/* manifest.mjs -- harness project selection, shared by run.mjs and parity.mjs.
 *
 * Prefers the test manifest (test/web-harness/projects.test.json, paths
 * relative to the REPO ROOT; its db artifacts come from `make test-web-db`)
 * and falls back to the deployment manifest (html/projects.json, paths
 * relative to html/).  Keeping the two separate lets html/projects.json change
 * freely for releases without breaking the harnesses.
 *
 * --project <id> searches the test manifest first, then deployment, so e.g.
 * `--project negroni` still reaches deployed demo projects while the test
 * manifest exists. */

import { readFileSync, existsSync } from 'fs';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

var HERE = dirname(fileURLToPath(import.meta.url));
var REPO_ROOT = join(HERE, '..', '..');
var HTML_DIR = join(REPO_ROOT, 'html');
var TEST_MANIFEST = join(HERE, 'projects.test.json');
var DEPLOY_MANIFEST = join(HTML_DIR, 'projects.json');

function readManifest(path, baseDir) {
    var list = JSON.parse(readFileSync(path, 'utf8'));
    if (!Array.isArray(list)) throw new Error(path + ' is malformed (expected an array)');
    list.forEach(function(p) { p.baseDir = baseDir; p.manifestPath = path; });
    return list;
}

export function loadProject(wantId) {
    var entries = [];
    if (existsSync(TEST_MANIFEST)) entries = entries.concat(readManifest(TEST_MANIFEST, REPO_ROOT));
    if (existsSync(DEPLOY_MANIFEST)) entries = entries.concat(readManifest(DEPLOY_MANIFEST, HTML_DIR));
    if (entries.length === 0) {
        throw new Error('no project manifest found (' + TEST_MANIFEST + ' or ' + DEPLOY_MANIFEST + ')');
    }
    var p = wantId
        ? entries.find(function(x) { return x.id === wantId; })
        : entries[0];
    if (!p) throw new Error('no project with id "' + wantId + '" in any manifest');
    return p;
}

/* Resolve a manifest-relative URL against the manifest's own base dir
 * (repo root for the test manifest, html/ for the deployment manifest). */
export function resolveProjectPath(project, url) {
    return join(project.baseDir, String(url).replace(/^\.\//, ''));
}

/* Fail early with an actionable message when the project's db snapshot is
 * missing -- for the generated test project that means `make test-web-db`. */
export function requireDb(project, dbPath) {
    if (!existsSync(dbPath)) {
        throw new Error('db snapshot not found: ' + dbPath +
            (project.baseDir === HTML_DIR ? '' : '\n  run `make test-web-db` first'));
    }
}
