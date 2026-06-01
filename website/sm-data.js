/* sm-data.js — seeded symbol indexes for the live qi demo.
   Plain JS (no Babel). Attaches window.SM_PROJECTS + helpers.
   Each project mimics what a SourceMinder language-indexer would emit. */
(function () {
  // kind tags used by qi output
  const F = 'FUNC', T = 'TYPE', V = 'VAR', C = 'CONST';

  const PROJECTS = {
    negroni: {
      key: 'negroni', name: 'negroni', version: 'v3.1.1', size: '3.1 MB',
      lang: 'Go', meta: 'idiomatic HTTP middleware for Go',
      stats: { rows: 8904, files: 64, symbols: 1180, defs: 742 },
      files: {
        './logger.go': {
          imports: ['bytes', 'log', 'net/http', 'os', 'text/template', 'time'],
          symbols: [
            { n: 'LoggerEntry', k: T, line: 13 },
            { n: 'ALogger', k: T, line: 30 },
            { n: 'Logger', k: T, line: 36 },
            { n: 'NewLogger', k: F, line: 44 },
            { n: 'SetFormat', k: F, line: 50 },
            { n: 'SetDateFormat', k: F, line: 54 },
            { n: 'ServeHTTP', k: F, line: 58 },
          ],
        },
        './negroni.go': {
          imports: ['log', 'net/http', 'os'],
          symbols: [
            { n: 'Handler', k: T, line: 18 },
            { n: 'HandlerFunc', k: T, line: 22 },
            { n: 'Negroni', k: T, line: 28 },
            { n: 'middleware', k: T, line: 40 },
            { n: 'New', k: F, line: 51 },
            { n: 'Classic', k: F, line: 64 },
            { n: 'Use', k: F, line: 75 },
            { n: 'UseHandler', k: F, line: 84 },
            { n: 'ServeHTTP', k: F, line: 101 },
            { n: 'Run', k: F, line: 118 },
          ],
        },
        './recovery.go': {
          imports: ['fmt', 'net/http', 'runtime', 'runtime/debug'],
          symbols: [
            { n: 'PanicInformation', k: T, line: 12 },
            { n: 'Recovery', k: T, line: 20 },
            { n: 'NewRecovery', k: F, line: 33 },
            { n: 'ServeHTTP', k: F, line: 41 },
          ],
        },
        './response_writer.go': {
          imports: ['bufio', 'net', 'net/http'],
          symbols: [
            { n: 'ResponseWriter', k: T, line: 12 },
            { n: 'responseWriter', k: T, line: 33 },
            { n: 'NewResponseWriter', k: F, line: 19 },
            { n: 'WriteHeader', k: F, line: 40 },
            { n: 'Write', k: F, line: 47 },
          ],
        },
      },
    },

    slim: {
      key: 'slim', name: 'Slim', version: 'v1', size: '4.2 MB',
      lang: 'TypeScript', meta: 'SQLite 3.53.0 in-browser',
      stats: { rows: 13882, files: 125, symbols: 2110, defs: 1302 },
      files: {
        './db/conn.ts': {
          imports: ['./vfs', './wasm', 'events'],
          symbols: [
            { n: 'ConnOptions', k: T, line: 9 },
            { n: 'Connection', k: T, line: 14 },
            { n: 'openDatabase', k: F, line: 22 },
            { n: 'closeDatabase', k: F, line: 51 },
            { n: 'execute', k: F, line: 68 },
            { n: 'prepare', k: F, line: 90 },
          ],
        },
        './query/parser.ts': {
          imports: ['./tokens', './ast'],
          symbols: [
            { n: 'QueryNode', k: T, line: 11 },
            { n: 'ParseError', k: T, line: 30 },
            { n: 'tokenize', k: F, line: 18 },
            { n: 'parseQuery', k: F, line: 40 },
            { n: 'parseUserClause', k: F, line: 77 },
          ],
        },
        './vfs/memfs.ts': {
          imports: ['buffer'],
          symbols: [
            { n: 'MemFile', k: T, line: 12 },
            { n: 'Block', k: T, line: 20 },
            { n: 'createUser', k: F, line: 33 },
            { n: 'getUserById', k: F, line: 51 },
            { n: 'readBlock', k: F, line: 70 },
            { n: 'writeBlock', k: F, line: 88 },
          ],
        },
        './api/users.ts': {
          imports: ['./db', './auth'],
          symbols: [
            { n: 'User', k: T, line: 12 },
            { n: 'UserRow', k: T, line: 26 },
            { n: 'getUserById', k: F, line: 42 },
            { n: 'createUser', k: F, line: 88 },
            { n: 'listUsers', k: F, line: 120 },
            { n: 'deleteUser', k: F, line: 140 },
          ],
        },
      },
    },

    acme: {
      key: 'acme', name: 'acme-api', version: 'v0.9', size: '8.1 MB',
      lang: 'Go + TS', meta: 'mixed Go/TS service monorepo',
      stats: { rows: 21408, files: 318, symbols: 4126, defs: 2530 },
      files: {
        './api/users.ts': {
          imports: ['./db', './auth'],
          symbols: [
            { n: 'User', k: T, line: 12 },
            { n: 'getUserById', k: F, line: 42 },
            { n: 'createUser', k: F, line: 88 },
            { n: 'listUsers', k: F, line: 120 },
          ],
        },
        './auth/session.go': {
          imports: ['context', 'time', 'crypto/rand'],
          symbols: [
            { n: 'Session', k: T, line: 12 },
            { n: 'Claims', k: T, line: 24 },
            { n: 'NewSession', k: F, line: 33 },
            { n: 'resolveUser', k: F, line: 17 },
            { n: 'UserFromCtx', k: F, line: 61 },
          ],
        },
        './db/pool.go': {
          imports: ['database/sql', 'sync', 'time'],
          symbols: [
            { n: 'Pool', k: T, line: 11 },
            { n: 'Connect', k: F, line: 20 },
            { n: 'Acquire', k: F, line: 44 },
            { n: 'Release', k: F, line: 67 },
          ],
        },
      },
    },
  };

  const ORDER = ['slim', 'negroni', 'acme'];

  // glob: supports * wildcards, case-insensitive, substring if no *
  function globToRe(pat) {
    const hasStar = pat.includes('*');
    let body = pat.replace(/[.+^${}()|[\]\\]/g, '\\$&').replace(/\*/g, '.*');
    if (!hasStar) body = '.*' + body + '.*';
    return new RegExp('^' + body + '$', 'i');
  }

  window.SM_PROJECTS = PROJECTS;
  window.SM_ORDER = ORDER;
  window.SM_globToRe = globToRe;
})();
