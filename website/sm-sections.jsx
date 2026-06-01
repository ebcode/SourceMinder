/* sm-sections.jsx — static page sections for the SourceMinder home.
   Mounts the live <QiTerminal/> (window) inside the hero.
   Exports window.SM_SECTIONS = { Nav, Hero, WhyQi, Collection, GetStarted, Footer }. */

// scattered colorful pixel mark (CSS grid) — the brand's pixel motif
function PixelLogo({ px = 7, gap = 1.5 }) {
  const C = {
    r: '#e8031c', o: '#ff9d00', y: '#ffdb00', g: '#00b131',
    b: '#0056ff', p: '#8f00b0', k: '#fc0076', i: '#001bd2', _: 'transparent',
  };
  const grid = [
    ['r', '_', 'b', '_'],
    ['_', 'g', '_', 'y'],
    ['k', '_', 'o', '_'],
    ['_', 'i', '_', 'p'],
  ];
  return (
    <span className="sm-px" style={{ gridTemplateColumns: `repeat(4, ${px}px)`, gridAutoRows: `${px}px`, gap }}>
      {grid.flat().map((c, i) => <i key={i} style={{ background: C[c] }} />)}
    </span>
  );
}

function Badge({ l, r, c }) {
  return (
    <span className="sm-badge">
      <span className="sm-badge-l">{l}</span>
      <span className="sm-badge-r" style={{ background: c }}>{r}</span>
    </span>
  );
}

function AsciiRule({ label }) {
  return (
    <div className="sm-rule">
      <span className="sm-rule-line">{'·'.repeat(120)}</span>
      {label && <span className="sm-rule-label">{label}</span>}
    </div>
  );
}

function Nav() {
  return (
    <header className="sm-nav">
      <div className="sm-nav-in">
        <a className="sm-brand" href="#">
          <PixelLogo />
          <span className="sm-wordmark">SourceMinder</span>
        </a>
        <nav className="sm-nav-links">
          <a className="on" href="#">home</a>
          <a href="#">docs</a>
          <a href="#">quick&nbsp;start</a>
          <a href="#">github</a>
        </nav>
        <div className="sm-nav-right">
          <span className="sm-ver">v0.4 · MIT</span>
          <a className="sm-star" href="#">★ 2,431</a>
        </div>
      </div>
    </header>
  );
}

function Hero() {
  const Term = window.QiTerminal;
  return (
    <section className="sm-hero" id="top">
      <div className="sm-hero-in">
        <div className="sm-eyebrow">the sourceminder collection</div>
        <h1 className="sm-h1">
          <span className="sm-script">query&#8209;index</span>
          <span className="sm-qi">(qi)</span>
        </h1>
        <div className="sm-tagline">the index your AI agent actually wants.</div>
        <p className="sm-lede">
          Stop letting agents grep their way through your repo. SourceMinder indexes your code
          once — every language — and qi answers in <em>symbols</em>, not strings. Faster, cheaper,
          more accurate.
        </p>
        <div className="sm-cta">
          <a className="sm-btn primary" href="#start">Add to Claude Code</a>
          <a className="sm-btn ghost" href="#start"><span className="t">$</span> brew install sourceminder/qi</a>
        </div>
        <div className="sm-badges">
          <Badge l="build" r="passing" c="#3f7d52" />
          <Badge l="license" r="MIT" c="#3550c8" />
          <Badge l="tokens" r="8× fewer" c="#b5762a" />
          <Badge l="the answer" r="42" c="#6e4480" />
        </div>
      </div>

      <div className="sm-term-wrap">
        <div className="sm-term-cap"><span className="t-amber">●</span> live — type a real qi command, switch the index, see what your agent sees</div>
        {Term ? <Term /> : <div className="qt" style={{ padding: 40, color: '#888' }}>loading terminal…</div>}
      </div>
    </section>
  );
}

function WhyQi() {
  return (
    <section className="sm-why" id="why">
      <div className="sm-sec-head">
        <span className="sm-hash">##</span>
        <h2 className="sm-script sm-h2">why bother</h2>
        <span className="sm-sec-note">one symbolic query vs. grep + cat across four files</span>
      </div>
      <div className="sm-compare">
        <div className="sm-cmp">
          <div className="sm-cmp-tag">without qi</div>
          <div className="sm-cmp-num">~12.4k</div>
          <div className="sm-cmp-sub">tokens · grep + cat, 4 files read</div>
        </div>
        <div className="sm-cmp hot">
          <div className="sm-cmp-tag on">with qi</div>
          <div className="sm-cmp-num">1.5k</div>
          <div className="sm-cmp-sub">tokens · one symbolic query</div>
        </div>
        <div className="sm-cmp">
          <div className="sm-cmp-tag">net</div>
          <div className="sm-cmp-num">≈ 8×</div>
          <div className="sm-cmp-sub">fewer tokens · faster · cheaper</div>
        </div>
      </div>
    </section>
  );
}

function Collection() {
  const indexers = [
    ['index-c', 'C'], ['index-go', 'Go'], ['index-python', 'Python'],
    ['index-ts', 'TypeScript'], ['index-php', 'PHP'], ['index-perl', 'Perl'],
  ];
  return (
    <section className="sm-box" id="box">
      <div className="sm-sec-head">
        <span className="sm-hash">##</span>
        <h2 className="sm-script sm-h2">what&rsquo;s in the box</h2>
        <span className="sm-sec-note">a collection, not a single binary</span>
      </div>
      <div className="sm-box-grid">
        <p className="sm-box-lede">
          SourceMinder is a collection: a language <b>indexer</b> per ecosystem, plus the
          <b> query&#8209;index</b> tool that reads them. You run the indexers once; your agent
          talks to qi.
        </p>
        <pre className="sm-tree">{
`sourceminder/
├─ indexers/
│  ├─ index-c        ·  emits a symbol index
│  ├─ index-go       ·  …per language
│  ├─ index-python
│  ├─ index-ts
│  ├─ index-php
│  └─ index-perl
└─ query-index/  (qi)   ◀  your agent queries this`
        }</pre>
      </div>
      <div className="sm-langs">
        {indexers.map(([cmd, lang]) => (
          <span className="sm-lang" key={cmd}><b>{cmd}</b><span>{lang}</span></span>
        ))}
      </div>
    </section>
  );
}

function GetStarted() {
  const steps = [
    ['1', 'install', 'brew install sourceminder/qi'],
    ['2', 'index the repo', 'cd ~/project && sourceminder index .'],
    ['3', 'ask in symbols', "qi '*user*' --def"],
  ];
  return (
    <section className="sm-start" id="start">
      <div className="sm-sec-head">
        <span className="sm-hash">##</span>
        <h2 className="sm-script sm-h2">quick start</h2>
        <span className="sm-sec-note">three commands · no config files · (yes, really)</span>
      </div>
      <div className="sm-steps">
        {steps.map(([n, t, code]) => (
          <div className="sm-step" key={n}>
            <div className="sm-step-n">{n}</div>
            <div className="sm-step-b">
              <div className="sm-step-t">{t}</div>
              <div className="sm-code"><span className="sm-dollar">$</span> {code}</div>
            </div>
          </div>
        ))}
      </div>
      <div className="sm-start-links">
        <a href="#">full quick start →</a>
        <a href="#">read the docs →</a>
        <a href="#">add to Claude Code →</a>
      </div>
    </section>
  );
}

function Footer() {
  return (
    <footer className="sm-foot">
      <div className="sm-foot-in">
        <div className="sm-foot-brand">
          <pre className="sm-foot-ascii">{
`  ___  _ __ ___
 / __|| '_ \` _ \\   sourceminder
 \\__ \\| | | | | |  query-index · qi
 |___/|_| |_| |_|`
          }</pre>
        </div>
        <div className="sm-foot-cols">
          <div><span>resources</span><a href="#">docs</a><a href="#">github</a><a href="#">changelog</a></div>
          <div><span>quick links</span><a href="#">quick start</a><a href="#">home</a></div>
        </div>
      </div>
      <div className="sm-foot-bar">
        <span>generated by ./gendocs.sh</span>
        <span>best viewed in any terminal</span>
        <span className="sm-counter">visitors: 0&#8203;0&#8203;0&#8203;42</span>
        <span>not enterprise-ready (on purpose)</span>
        <span>the answer is 42</span>
      </div>
    </footer>
  );
}

window.SM_SECTIONS = { Nav, Hero, WhyQi, Collection, GetStarted, Footer, AsciiRule };
