/* sources.mjs -- Node source provider for -e / -C / -A / -B queries.
 *
 * The browser's SourceCache fetches each row's file over HTTP from the
 * project's sourceBase; under Node we read the same files from the local source
 * tree.  Contract matches SourceCache.getFiles(): take row filepaths, return
 * the PRESENT ones as { path, content } records (missing files omitted, so the
 * C render twin emits its "could not read file" warning -- same as a 404).
 * The returned `path` is left verbatim because it is the source-blob lookup key
 * the C side matches against (see buildRowFilepath / source_map_get). */

import { readFileSync } from 'fs';
import { join } from 'path';

export function makeSourceProvider(sourceRoot) {
    return async function getSources(paths) {
        var uniq = Array.from(new Set(paths));
        var out = [];
        for (var i = 0; i < uniq.length; i++) {
            var rel = uniq[i].replace(/^\.\//, '');   /* strip leading ./ */
            try {
                var content = readFileSync(join(sourceRoot, rel), 'utf8');
                out.push({ path: uniq[i], content: content });
            } catch (e) {
                /* missing/unreadable -> omit, mirroring a 404 in the browser */
            }
        }
        return out;
    };
}
