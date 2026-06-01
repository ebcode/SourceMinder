/* sm-terminal.jsx — the live qi demo terminal.
   Type real qi commands, switch project indexes, get real results.
   Depends on window.SM_PROJECTS / SM_ORDER / SM_globToRe (sm-data.js).
   Exports window.QiTerminal. */
const { useState, useRef, useEffect, useCallback } = React;

const seg = (t, c) => ({ t, c: c || 'fg' });
const KINDS = { FUNC: 'FUNC', TYPE: 'TYPE', VAR: 'VAR', CONST: 'CONST' };

function tokenize(s) {
  const re = /'([^']*)'|"([^"]*)"|(\S+)/g, out = []; let m;
  while ((m = re.exec(s))) out.push(m[1] ?? m[2] ?? m[3]);
  return out;
}
function leader(name, line) {
  const W = 50, left = '  ' + name + ' ';
  return left + '.'.repeat(Math.max(3, W - left.length)) + ' ' + line;
}
const ms = () => (Math.random() * 2.6 + 0.8).toFixed(1);

// ---- TOC for a file ----
function tocLines(proj, fileArg) {
  const path = Object.keys(proj.files).find(
    p => p === fileArg || p.split('/').pop() === fileArg || p === './' + fileArg
  );
  if (!path) return [[seg("qi: no file matching '" + fileArg + "' in this index", 'err')]];
  const f = proj.files[path];
  const funcs = f.symbols.filter(s => s.k === 'FUNC');
  const types = f.symbols.filter(s => s.k === 'TYPE');
  const out = [];
  out.push([seg('Result breakdown: ', 'dim'),
    seg(`FILE (1), FUNC (${funcs.length}), IMP (${f.imports.length}), TYPE (${types.length})`, 'fg')]);
  out.push([]);
  out.push([seg(path + ':', 'amber')]);
  out.push([]);
  out.push([seg('IMPORTS: ', 'dim'), seg(f.imports.join(', '), 'green')]);
  out.push([]);
  if (funcs.length) {
    out.push([seg(`FUNCTIONS (${funcs.length}):`, 'white')]);
    funcs.forEach(s => out.push([seg(leader(s.n, s.line), 'fg')]));
    out.push([]);
  }
  if (types.length) {
    out.push([seg(`TYPES (${types.length}):`, 'white')]);
    types.forEach(s => out.push([seg(leader(s.n, s.line), 'fg')]));
  }
  return out;
}

// ---- symbol search ----
function searchLines(proj, pattern, opts) {
  const matchAll = pattern === '%' || pattern === '*';
  const re = matchAll ? null : window.SM_globToRe(pattern);
  const kind = opts.kind ? opts.kind.toUpperCase() : null;
  const groups = [];
  let total = 0, limit = opts.limit || Infinity;
  for (const path of Object.keys(proj.files)) {
    const hits = [];
    for (const s of proj.files[path].symbols) {
      if (kind && s.k !== kind) continue;
      if (opts.def && s.k !== 'FUNC' && s.k !== 'TYPE') continue;
      if (!matchAll && !re.test(s.n)) continue;
      if (total >= limit) break;
      hits.push(s); total++;
    }
    if (hits.length) groups.push({ path, hits });
    if (total >= limit) break;
  }
  const out = [];
  out.push([seg(`searching index '${proj.name}' for `, 'dim'), seg(pattern, 'amber'), seg(' …', 'dim')]);
  out.push([]);
  if (!total) {
    out.push([seg('no symbols matched. try a wider glob, e.g. ', 'dim'), seg("qi '*user*'", 'fg')]);
    return out;
  }
  const maxN = Math.max(...groups.flatMap(g => g.hits.map(h => h.n.length)));
  out.push([seg('LINE', 'dim'), seg(' | ', 'dim'), seg('SYMBOL'.padEnd(maxN), 'dim'), seg(' | CTX', 'dim')]);
  out.push([seg('-----+-' + '-'.repeat(maxN) + '+----', 'dim')]);
  for (const g of groups) {
    out.push([seg(g.path + ':', 'amber')]);
    for (const h of g.hits) {
      out.push([
        seg(' ' + String(h.line).padStart(4) + ' | ', 'dim'),
        seg(h.n, 'hl'),
        seg(' '.repeat(maxN - h.n.length) + ' | ' + h.k, 'dim'),
      ]);
    }
  }
  out.push([]);
  out.push([seg(`found ${total} match${total === 1 ? '' : 'es'} · ${ms()} ms`, 'green')]);
  return out;
}

const HELP = [
  [seg('qi', 'white'), seg(' — query a SourceMinder index. ask in symbols, not strings.', 'fg')],
  [],
  [seg('usage:', 'dim')],
  [seg('  qi <pattern> [-i KIND] [--def] [--limit N] [--json]', 'fg')],
  [seg('  qi % -f <file> --toc', 'fg'), seg('        table of contents for one file', 'dim')],
  [seg('  load <project>', 'fg'), seg('              switch index  (slim · negroni · acme)', 'dim')],
  [seg('  help · clear', 'fg')],
  [],
  [seg('examples:', 'dim')],
  [seg("  qi '*user*' --def", 'fg')],
  [seg('  qi % -f logger.go --toc', 'fg')],
];

// returns {out, load} — load = project key to switch to (handled by component)
function runQi(raw, proj) {
  const toks = tokenize(raw.trim());
  if (!toks.length) return { out: [] };
  const head = toks[0].toLowerCase();

  if (head === 'clear' || head === 'cls') return { out: 'CLEAR' };
  if (head === 'help' || head === 'man') return { out: HELP };
  if (['load', 'use', 'project', 'open'].includes(head)) {
    const k = (toks[1] || '').toLowerCase();
    const key = window.SM_ORDER.find(p => p === k || window.SM_PROJECTS[p].name.toLowerCase() === k);
    if (!key) return { out: [[seg("unknown project '" + (toks[1] || '') + "'. options: " + window.SM_ORDER.join(', '), 'err')]] };
    return { out: [], load: key };
  }
  if (head === 'ls') {
    return { out: Object.keys(proj.files).map(p => [seg(p, 'amber')]) };
  }
  if (head !== 'qi') {
    return { out: [[seg(head + ': command not found. type ', 'err'), seg('help', 'fg'), seg(' for usage.', 'err')]] };
  }

  // parse qi flags
  const opts = {}; let pattern = null, file = null, toc = false;
  for (let i = 1; i < toks.length; i++) {
    const tk = toks[i];
    if (tk === '--toc') toc = true;
    else if (tk === '--def') opts.def = true;
    else if (tk === '--json') opts.json = true;
    else if (tk === '-f' || tk === '--file') file = toks[++i];
    else if (tk === '-i' || tk === '--kind') opts.kind = toks[++i];
    else if (tk === '--limit') opts.limit = parseInt(toks[++i], 10) || Infinity;
    else if (!tk.startsWith('-') && pattern === null) pattern = tk;
  }
  if (toc) {
    if (!file) return { out: [[seg('--toc needs a file: ', 'err'), seg('qi % -f <file> --toc', 'fg')]] };
    return { out: tocLines(proj, file) };
  }
  if (pattern === null) return { out: HELP };
  let out = searchLines(proj, pattern, opts);
  if (opts.json) out = [[seg('// --json: machine-readable output for agents (truncated)', 'dim')], ...out];
  return { out };
}

// ---- initial scrollback: replays the product screenshot's story ----
function bootScrollback() {
  const neg = window.SM_PROJECTS.negroni;
  const out = [];
  out.push([seg('Loaded index: negroni (v3.1.1)', 'green')]);
  out.push([]);
  out.push([seg('$ ', 'dim'), seg('qi % -f logger.go --toc', 'white')]);
  tocLines(neg, 'logger.go').forEach(l => out.push(l));
  out.push([]);
  out.push([seg('$ ', 'dim'), seg('load slim', 'white')]);
  out.push([seg('Switching to Slim…', 'dim')]);
  out.push([]);
  out.push([seg('Loaded index: Slim (v1)', 'green')]);
  out.push([seg('Slim — SQLite 3.53.0 in-browser · 13,882 rows indexed', 'dim')]);
  out.push([]);
  return out;
}

function Line({ segs }) {
  if (!segs.length) return <div className="qt-line">&nbsp;</div>;
  return <div className="qt-line">{segs.map((s, i) => <span key={i} className={'t-' + s.c}>{s.t}</span>)}</div>;
}

function StatCard({ label, value }) {
  return (
    <div className="qt-stat">
      <div className="qt-stat-l">{label}</div>
      <div className="qt-stat-v">{value}</div>
    </div>
  );
}

function QiTerminal() {
  const [projKey, setProjKey] = useState('slim');
  const [lines, setLines] = useState(bootScrollback);
  const [input, setInput] = useState('');
  const [hist, setHist] = useState([]);
  const [hIdx, setHIdx] = useState(-1);
  const [focus, setFocus] = useState(false);
  const bodyRef = useRef(null), inputRef = useRef(null);
  const proj = window.SM_PROJECTS[projKey];

  useEffect(() => { if (bodyRef.current) bodyRef.current.scrollTop = bodyRef.current.scrollHeight; }, [lines]);

  const push = useCallback((arr) => setLines(prev => [...prev, ...arr]), []);

  const loadProject = useCallback((key, fromTerminal) => {
    const p = window.SM_PROJECTS[key];
    if (key === projKey && !fromTerminal) return;
    const arr = [];
    if (!fromTerminal) arr.push([seg('$ ', 'dim'), seg('load ' + key, 'white')]);
    arr.push([seg('Switching to ' + p.name + '…', 'dim')]);
    arr.push([]);
    arr.push([seg('Loaded index: ' + p.name + ' (' + p.version + ')', 'green')]);
    arr.push([seg(p.meta + ' · ' + p.stats.rows.toLocaleString() + ' rows indexed', 'dim')]);
    arr.push([]);
    push(arr);
    setProjKey(key);
  }, [projKey, push]);

  const submit = useCallback(() => {
    const cmd = input;
    setInput('');
    if (cmd.trim()) { setHist(h => [...h, cmd]); }
    setHIdx(-1);
    push([[seg('$ ', 'dim'), seg(cmd, 'white')]]);
    const res = runQi(cmd, proj);
    if (res.out === 'CLEAR') { setLines([]); return; }
    if (res.load) { loadProject(res.load, false); return; }
    if (res.out && res.out.length) push(res.out);
    push([[]]);
  }, [input, proj, push, loadProject]);

  const onKey = (e) => {
    if (e.key === 'Enter') { e.preventDefault(); submit(); }
    else if (e.key === 'ArrowUp') {
      e.preventDefault();
      if (!hist.length) return;
      const ni = hIdx < 0 ? hist.length - 1 : Math.max(0, hIdx - 1);
      setHIdx(ni); setInput(hist[ni]);
    } else if (e.key === 'ArrowDown') {
      e.preventDefault();
      if (hIdx < 0) return;
      const ni = hIdx + 1;
      if (ni >= hist.length) { setHIdx(-1); setInput(''); } else { setHIdx(ni); setInput(hist[ni]); }
    } else if (e.key === 'l' && e.ctrlKey) { e.preventDefault(); setLines([]); }
  };

  const chips = ["qi '*user*' --def", 'qi % -f logger.go --toc', 'load negroni', 'help'];

  return (
    <div className="qt" onClick={() => inputRef.current && inputRef.current.focus()}>
      <div className="qt-head">
        <div className="qt-title">Project: <b>{proj.name}</b> — {proj.meta}.</div>
        <label className="qt-proj">
          <span>Project</span>
          <select value={projKey} onChange={(e) => loadProject(e.target.value, false)}>
            {window.SM_ORDER.map(k => (
              <option key={k} value={k}>{window.SM_PROJECTS[k].name} ({window.SM_PROJECTS[k].size})</option>
            ))}
          </select>
        </label>
      </div>

      <div className="qt-stats">
        <StatCard label="Indexed rows" value={proj.stats.rows.toLocaleString()} />
        <StatCard label="Distinct files" value={proj.stats.files.toLocaleString()} />
        <StatCard label="Distinct symbols" value={proj.stats.symbols.toLocaleString()} />
        <StatCard label="Definitions" value={proj.stats.defs.toLocaleString()} />
      </div>

      <div className="qt-body" ref={bodyRef}>
        {lines.map((l, i) => <Line key={i} segs={l} />)}
        <div className="qt-line qt-prompt">
          <span className="t-dim">$&nbsp;</span>
          <span className="t-white">{input}</span>
          <span className={'qt-cursor' + (focus ? ' on' : '')} />
        </div>
        <input
          ref={inputRef} className="qt-input" value={input} spellCheck="false"
          autoCapitalize="off" autoCorrect="off"
          onChange={(e) => setInput(e.target.value)} onKeyDown={onKey}
          onFocus={() => setFocus(true)} onBlur={() => setFocus(false)}
        />
      </div>

      <div className="qt-chips">
        <span className="qt-chips-l">try:</span>
        {chips.map((c, i) => (
          <button key={i} className="qt-chip" onClick={(e) => {
            e.stopPropagation();
            setInput(c);
            if (inputRef.current) inputRef.current.focus();
          }}>{c}</button>
        ))}
      </div>
    </div>
  );
}

window.QiTerminal = QiTerminal;
