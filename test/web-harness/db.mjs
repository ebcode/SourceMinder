/* db.mjs -- open a code-index snapshot under Node using the SAME SQLite engine
 * the browser ships (@sqlite.org/sqlite-wasm).  We read the .db bytes and
 * sqlite3_deserialize them into an in-memory connection, exactly mirroring
 * loadDbFromBytes() in qi-worker.js -- so the SQL qi_web_build() emits runs
 * against an identical engine to production, not a native libsqlite variant.
 *
 * The returned oo1.DB already exposes selectArrays(sql) / selectValue(sql),
 * which is precisely the `db` shape qi-pipeline.js expects. */

import { readFileSync } from 'fs';
import { pathToFileURL } from 'url';
import { join } from 'path';

export async function openDb(htmlDir, dbPath) {
    /* Resolve the package's Node build explicitly; the harness lives outside
     * html/ so bare-specifier resolution would not find html/node_modules. */
    var nodeEntry = pathToFileURL(join(htmlDir,
        'node_modules/@sqlite.org/sqlite-wasm/dist/node.mjs')).href;
    var sqlite3InitModule = (await import(nodeEntry)).default;

    var sqlite3 = await sqlite3InitModule({ print: function() {}, printErr: function() {} });

    /* Node's readFileSync returns a Buffer; allocFromTypedArray only accepts a
     * plain typed array, so wrap (no copy) as a Uint8Array. */
    var buf = readFileSync(dbPath);
    var bytes = new Uint8Array(buf.buffer, buf.byteOffset, buf.byteLength);
    var db = new sqlite3.oo1.DB();
    var ptr = sqlite3.wasm.allocFromTypedArray(bytes);
    var rc = sqlite3.capi.sqlite3_deserialize(
        db.pointer, 'main', ptr, bytes.byteLength, bytes.byteLength,
        sqlite3.capi.SQLITE_DESERIALIZE_FREEONCLOSE);
    db.checkRc(rc);
    return { sqlite3: sqlite3, db: db };
}
