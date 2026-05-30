/* load-qi.mjs -- load the Emscripten qi-web.js module under Node.
 *
 * qi-web.js is built with -sEXPORT_ES6=1 but its Node branch references the
 * CommonJS globals `__dirname` and `require`, which do not exist in an ES
 * module scope.  We inject both as globals before importing, and pass the
 * .wasm bytes directly (Module.wasmBinary) so the module never has to locate
 * or read the file itself.  This is the only Node-vs-browser shim the harness
 * needs; everything past this point is the same module the worker loads. */

import { createRequire } from 'module';
import { readFileSync } from 'fs';
import { pathToFileURL } from 'url';
import { join } from 'path';

export async function loadQiModule(htmlDir) {
    /* Emscripten's Node path uses these CommonJS globals at factory time. */
    if (typeof globalThis.require === 'undefined') {
        globalThis.require = createRequire(import.meta.url);
    }
    globalThis.__dirname = htmlDir;

    var jsUrl = pathToFileURL(join(htmlDir, 'qi-web.js')).href;
    var wasmBinary = readFileSync(join(htmlDir, 'qi-web.wasm'));

    var mod = await import(jsUrl);
    var QiWebModule = mod.default;
    return QiWebModule({ wasmBinary: wasmBinary });
}
