# Plan: Source-Backed Flags for Web qi (`-C`/`-A`/`-B`, `-e`, `--raw`)

Design for the read-only **host bridge** that lets the WASM qi target render
source-dependent flags in the browser. Supersedes Phase 8 of
`WEB_QI_FLAGS_PLAN.md`.

**Status:** design agreed; not yet implemented.

---

## 1. The flags and what they actually need

All three flag families reduce to a single host capability — *read a
contiguous line range from a file*. The C/WASM side already computes every
span locally; only the file bytes are missing.

| Flag | C already knows (from DB row + flags) | Needs from host |
|------|----------------------------------------|-----------------|
| `-C N` / `-A N` / `-B N` | `path`, `target_line`, patterns to highlight | lines `[target_line − B, target_line + A]` |
| `-e` (expand) | `path`, `source_location` (`"start:col - end:col"`, already a DB column) | lines `[start_line, end_line]` |
| `--raw` | nothing — it is a render modifier | nothing |

Line numbering, ANSI highlighting, column trimming, and grep-style `--`
separators are all pure rendering and stay in C. The host bridge's *entire*
job is:

```
fetchFile(path) → text
```

One uniform primitive. Small attack surface, one thing to cache and throttle.

---

## 2. Decisions made

1. **No API server.** Source files are served as static plaintext by the same
   origin that already serves the page, the `.wasm`, and the snapshot DB. The
   worker fetches and slices lines client-side. This is *more secure* than a
   custom file API (nginx's hardened static path handles traversal; the doc
   root is exactly the curated, public demo source) and the perf cost is
   negligible for small curated files that are cached after first fetch.
2. **Same-origin only.** No CORS, no auth; the browser same-origin policy is
   the boundary.
3. **C/WASM renders.** The worker does I/O; C applies all formatting, for
   exact CLI parity. Rendering decisions never leak into JS.
4. **Render via web twins**, not a unified core. New
   `shared-web/source-render-web.c` mirrors `shared/toc.c` ↔ `shared-web/toc-web.c`:
   native stays the untouched reference, the web copy is buffer + `WebOutput`.
5. **Whole files into WASM** (not pre-sliced ranges). C's render logic already
   thinks "open file, walk lines"; passing whole files keeps it nearly
   unchanged. Bounded by the curated repo size (below).
6. **Not a dead end.** The bridge contract is one function, `fetchFile(path)`.
   If file fan-out ever justifies a batch endpoint, only that one worker helper
   changes — no C moves.

### Project constraint: curated, small repos

The download ceiling is `code-index.db`, not the source fetches. (A ~270-file
Python tool like pytest yields a ~44 MB DB — too large for 4G mobile.) Demo
repos are curated to **~50 files each** to keep the DB 4G-friendly. As a side
effect this caps source-fetch fan-out at ~50 files. The worst case is a single
broad query (`qi % -C 3`) on a fresh session: ~50 small files (~500 KB raw /
~150 KB gzipped), fetched once, then cached for every later query.

---

## 3. The ordering constraint that shapes the protocol

Rendering needs source bytes → knowing which bytes needs the result rows →
rows come from the worker's SQL execution. And `ccall` is synchronous
(Asyncify deliberately excluded), so **C cannot call `fetch()` mid-render.**
This forces a "resolve all source first, then render with data in hand" shape,
which also fits the existing stateless two-pass formatting design.

---

## 4. Protocol — no third export

Extends the existing two-call flow (`qi_web_build` → worker runs SQL →
`qi_web_format`) plus one fetch step the worker performs in between.

**Key insight that avoids duplicated logic:** the worker only provides a
*superset* of the files C might render. C alone decides, per row, whether to
render source (`-e` → `is_definition==1` with non-empty `source_location`;
`-C/-A/-B` → context window). The worker just collects "distinct files among
the displayed rows" — always a safe superset — and fetches them. Over-fetching
a file C does not end up rendering is harmless (cached, dropped). **All parity
stays in C.**

### Flow

1. **`qi_web_build(cmd)`** — build_info gains `NEEDS_SOURCE|1`, plus the params
   C already parsed: `EXPAND|1`, `CONTEXT_BEFORE|N`, `CONTEXT_AFTER|N`,
   `RAW|1`. Worker executes SQL → 14-column TSV rows (unchanged).
2. **Worker, if `NEEDS_SOURCE`:** collect distinct `directory+filename` from
   the rows, resolve them via `SourceCache.getFilesBlob(paths)` (§5), assemble
   the sources blob.
3. **`qi_web_format(build_info, rows_tsv, total, shown, …, sources_blob)`** —
   new trailing arg. C indexes the blob into a `path → {ptr, len}` map and,
   inside its existing per-row loop, calls the web twin of
   `print_expansion_or_context` after each row. The two-pass column-width logic
   is untouched.

### Sources blob framing — length-prefixed, binary-safe

Source contains tabs, newlines, and pipes, so `\t`/`|` framing is unsafe. Use:

```
<path>\n<byte_length>\n<exactly byte_length bytes><path>\n<byte_length>\n…
```

C reads path to `\n`, length to `\n`, then *exactly* that many bytes (any
content). No escaping; C indexes into the blob in place (no per-file copy).

**Missing files = parity for free.** A 404 → worker omits that path → C's map
lookup returns NULL → C emits the same `Warning: Could not read file '%s'…`
the native CLI prints. The miss *is* the warning path.

**Refinement (deferred):** Emscripten string `ccall` assumes UTF-8. For full
binary-safety across all seven languages, pass the blob as `HEAPU8`
(pointer + length) rather than a JS string. UTF-8-as-string is fine for the
demo; flagged as a known refinement.

---

## 5. `SourceCache` — one deep module in the worker

The query handler asks for files and gets back the C-boundary blob; it knows
nothing about how. Narrow interface:

```
SourceCache.getFilesBlob(paths) → length-framed blob
```

Hidden inside (one responsibility — "fetch + cache + throttle + frame"):

- **dedup** of requested paths
- **session cache:** `Map<path, lines>`, on top of nginx ETag/`Cache-Control`,
  so a file is fetched at most once per session
- **bounded-concurrency pool:** at most K fetches in flight (K≈4–6), the rest
  queued; an optional small inter-dispatch delay is a tunable knob
- **404 → omit** (so C produces the warning)
- **length-prefixed framing** (it owns the C-boundary format)

`fetchFile` itself is `fetch(path)` → `text.split('\n')`. Same-origin, so no
CORS. This one helper is the entire swappable backend.

---

## 6. Render refactor — `shared-web/source-render-web.c`

Three functions to twin: `print_lines_range` (`-e`), `print_context_lines`
(`-C/-A/-B`), and their orchestrator `print_expansion_or_context`. Each mixes
two host dependencies to abstract:

| Axis | Native (reference) | Web twin |
|------|--------------------|----------|
| Source | `safe_fopen` + `fgets`, `current_line++` | walk an in-memory buffer line by line |
| Sink | `printf` to stdout | `wo_printf` to `WebOutput` |

**Source abstraction:** a `next_line()` helper over the buffer that mirrors
`fgets` exactly — one line at a time, handles the final line with no trailing
`\n`, consistent newline-inclusion. The existing loops are "iterate lines,
track `current_line` from 1, act when in `[start,end]`," so swapping `fgets`
for `next_line` is nearly mechanical and preserves line-counting parity.

**Sink abstraction:** `wo_printf` (already exists in the format path).

**Parity details the twin must preserve exactly:**

- Line-number prefix `%d:%s`; ANSI green-background highlight
  (`\033[42m` / `\033[0m`) — renders correctly in xterm.js.
- `-e` column trimming: single-line (`start_col`→`end_col`), first-line (skip
  to `start_col`), last-line (up to `end_col`), middle (whole line). Operates
  on *full* lines — the worker passes full lines; C trims. Do **not** pre-slice
  columns in JS.
- `--` separators before/after blocks; `--raw` suppresses prefixes, highlight,
  and separators.

**Integration point:** inside `qi_web_format`'s existing row loop — after each
table row, if `NEEDS_SOURCE`, call the web `print_expansion_or_context(row,
sources_map, …)`. No new export.

---

## 7. Two-layer rate limiting

The two throttles protect different things; only one is a real boundary.

- **Browser side (politeness/UX):** the bounded-concurrency pool inside
  `SourceCache`. Smooths the ~50-file burst, caps in-memory whole-file content.
  Untrusted — anyone can bypass the JS and hit nginx directly.
- **nginx side (the actual guarantee):** `limit_req_zone` + `limit_req`
  (leaky-bucket rate with a `burst`) and `limit_conn` (concurrent connections
  per IP). Enforced server-side precisely because the client can't be trusted —
  same principle as not trusting client paths. "2ms between files" ≈
  `rate=500r/s`; pick a `burst` that clears one legitimate ~50-file query.

Static serving with `sendfile` is already cheap, so these directives are abuse
protection, not throughput tuning.

---

## 8. Implementation order

1. **Render refactor first, native-verifiable.** Add `next_line()` + a
   buffer-based core, prove parity against the native FILE*-based output with a
   minimal boundary test (precept 20) before any WASM/JS is involved.
2. **`shared-web/source-render-web.c`** — web twins on buffer + `WebOutput`.
   Wire into `configure` (`WEB_SHARED_SRC`, `-Ishared-web`).
3. **Protocol C side** — `NEEDS_SOURCE|1` + context/expand/raw params in
   `qi_web_build`; `sources_blob` arg + blob indexing in `qi_web_format`; call
   the twin in the row loop.
4. **`SourceCache`** in the worker — dedup, cache, bounded pool, framing, 404.
5. **Worker wiring** — on `NEEDS_SOURCE`, gather distinct paths, call
   `getFilesBlob`, pass to `qi_web_format`.
6. **nginx config** — `limit_req` + `limit_conn` on the static source location.
7. **Parity pass** — side-by-side `qi -C 3`, `qi --def -e`, `qi -e --raw`,
   `-A`/`-B` asymmetric, missing-file warning; CLI vs browser.

### Boundary tests (precept 20)

- `next_line()` vs `fgets` on: no trailing newline, CRLF, empty file,
  single line.
- Blob round-trip: pack two files (one containing tabs/newlines/pipes) →
  index in C → byte-exact recovery.
- 404 path: omitted file → C emits the warning, other files still render.
