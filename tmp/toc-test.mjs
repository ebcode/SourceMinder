import { pathToFileURL } from 'url';
import { join } from 'path';
import { createRequire } from 'module';

const HTML_DIR = '/home/eli/projects/awesome/SourceMinder-wasm2/html';

// Load the WASM module the same way run.mjs does
const qiWebUrl = pathToFileURL(join(HTML_DIR, 'qi-web.js')).href;
const initModule = (await import(qiWebUrl)).default;
const qiModule = await initModule({
    locateFile: (f) => join(HTML_DIR, f),
    printErr: () => {},
});

function build(cmd) {
    const raw = qiModule.ccall('qi_web_build', 'string', ['string'], [cmd]);
    const lines = {};
    for (const l of raw.split('\n')) {
        const p = l.indexOf('|');
        if (p >= 0) lines[l.slice(0, p)] = l.slice(p + 1);
    }
    return lines;
}

// Test 1: --toc with no pattern
const t1 = build('qi --toc -f negroni.go');
console.log('toc no pattern  -> MODE:', t1.MODE, '| ERROR:', t1.ERROR, '| TOC_SQL present:', !!t1.TOC_SQL);

// Test 2: --toc with % pattern (should be same as no pattern)
const t2 = build('qi % --toc -f negroni.go');
console.log('toc %           -> MODE:', t2.MODE, '| ERROR:', t2.ERROR, '| TOC_SQL present:', !!t2.TOC_SQL);

// Test 3: --toc with a real pattern
const t3 = build('qi Handler --toc -f negroni.go');
console.log('toc Handler     -> MODE:', t3.MODE, '| ERROR:', t3.ERROR, '| TOC_SQL present:', !!t3.TOC_SQL);

// Test 4: --toc with no -f (should error)
const t4 = build('qi --toc');
console.log('toc no -f       -> ERROR:', t4.ERROR);

// Test 5: bare qi (no args after qi) -- used to show help
const t5 = build('qi');
console.log('bare qi         -> ERROR:', t5.ERROR);
