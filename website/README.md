# Handoff: SourceMinder Homepage

## Overview

This is the homepage for **SourceMinder** — a collection of language indexers + the **query-index (qi)** tool that reads them. The site's job is to introduce `query-index (qi)` to developers, let them try it live in a terminal, and link them to docs/quick-start/github.

The design direction is: **"more nerdy"** — monospace type, man-page sensibility, Web 1.0 nods, README aesthetic. The single deliberate contrast is the **Caveat (handwritten) script font** for the product name and headlines, punching against the all-monospace/square-edged body.

---

## About the Design Files

The files in this bundle are **HTML prototypes** — design references showing intended look, copy, layout, and behavior. They are not production code to copy directly.

Your job is to **recreate these designs in your target codebase** (React app, Next.js site, etc.) using its established patterns, router, and component library. If no framework exists yet, Next.js + Tailwind is a natural fit.

---

## Fidelity

**High-fidelity.** Colors, typography, spacing, interactions, and copy are all final (or near-final). Recreate pixel-faithfully. The live terminal component (`QiTerminal`) has real working logic — port its command parser directly.

---

## Screens / Views

### 1. Homepage (`SourceMinder.html`)

The single page. Sections in order: Nav → Hero → Why qi → The Collection → Quick Start → Footer.

---

### Nav (sticky, top)

- **Height:** ~56px. Sticky, `position: sticky; top: 0; z-index: 40`.
- **Background:** eggshell paper (`#f5f3f0`) at 88% opacity + `backdrop-filter: blur(8px)`. Border-bottom: 1.5px solid `#e0dbd2`.
- **Max content width:** 1120px, centered, `padding: 11px 24px`.
- **Left — brand mark:**
  - 4×4 pixel grid logo (7px × 7px cells, 1.5px gap). Colors exactly: `['#e8031c','transparent','#0056ff','transparent', 'transparent','#00b131','transparent','#ffdb00', '#fc0076','transparent','#ff9d00','transparent', 'transparent','#001bd2','transparent','#8f00b0']` (row-major).
  - "SourceMinder" wordmark: **Caveat 700, 29px**, `#1b1d24`.
- **Center — nav links:** Fira Code 13px, `#4b5160`. Active link: `#1b1d24` + 2px bottom border in accent green. Hover: accent green. Links: `home`, `docs`, `quick start`, `github` — all `href="#"` for now.
- **Right:**
  - Version pill: "v0.4 · MIT" — Fira Code 11px, border 1.5px `#d4cbbf`, border-radius 3px, padding 3px 8px.
  - Star button: "★ 2,431" — Fira Code 12px, border 1.5px `#1b1d24`, border-radius 3px, padding 3px 10px, box-shadow 2px 2px 0 `#1b1d24` (pixel shadow).

---

### Hero

- **Layout:** two stacked blocks — text block above, terminal below.
- **Padding:** 52px top, 24px bottom.

**Text block (max-width 760px):**
- Eyebrow: Fira Code 12px, `#4b5160`, letter-spacing 0.16em, uppercase. Text: `"the sourceminder collection"`.
- H1: two parts on same baseline:
  - `"query‑index"` — **Caveat 700**, `clamp(56px, 9vw, 92px)`, line-height 0.9, `#1b1d24`.
  - `"(qi)"` — Fira Code 500, `clamp(22px, 3vw, 30px)`, accent green `#447a54`.
- Tagline: **Caveat 600**, `clamp(28px, 4vw, 38px)`, `#1b1d24`. Text: `"the index your AI agent actually wants."`
- Lede paragraph: Commissioner 400, 18px, `#4b5160`, max-width 600px. The word "symbols" is highlighted: background `#dee1fb`, `#1b1d24`, padding 0 5px, border-radius 2px.
- CTA row (flex, gap 12px, wrap):
  - **Primary button:** "Add to Claude Code" — Fira Code 14px, background `#447a54`, color `#fff`, border 1.5px `#2e5a3d`, border-radius 4px, padding 11px 18px, box-shadow 3px 3px 0 `#1b1d24`. Hover: background `#569965`, transition 120ms ease-out.
  - **Ghost button:** "$ brew install sourceminder/qi" — same size, background `#faf8f6`, color `#1b1d24`, border `#1b1d24`. The `$` is `#7c8294`. Hover: border + text become accent green.
- Badges row (flex, gap 8px, wrap). Each badge: Fira Code 11px, border-radius 3px, overflow hidden, border 1px rgba(0,0,0,.25). Left half: background `#41434d`, color `#fff`. Right half color varies:
  - `build / passing` → `#3f7d52`
  - `license / MIT` → `#3550c8`
  - `tokens / 8× fewer` → `#b5762a`
  - `the answer / 42` → `#6e4480`

**Terminal caption:** Fira Code 12px, `#4b5160`, with an amber `●` dot. Text: `"live — type a real qi command, switch the index, see what your agent sees"`.

---

### QiTerminal (live interactive component)

This is the centrepiece. Port the logic from `sm-terminal.jsx` + `sm-data.js` exactly.

**Outer shell:**
- Background: `#0c1322` (navy). Border: 1.5px `#233252`. Border-radius 12px. Box-shadow: `0 18px 50px -22px rgba(8,14,30,.7)`.

**Header bar** (padding 18px 22px 14px):
- Left: "Project: **{name}**" in Fira Code 14px, `#c7d3e8`. Name is bold white `#eaf0fc`.
- Right: "Project" label + `<select>` for switching between `slim`, `negroni`, `acme`. Select styled dark navy.

**Stat cards** (4-column grid, gap 14px, padding 0 22px 16px):
- Each card: background `#15203c`, border 1.5px `#233252`, border-radius 8px, padding 13px 16px.
- Label: Fira Code 11px, `#5e6e8e`.
- Value: Fira Code 600, 25px, `#eaf0fc`, `font-feature-settings: "tnum"`.
- Stats: "Indexed rows", "Distinct files", "Distinct symbols", "Definitions".

**Scrollback body** (border-top 1.5px `#233252`, background `#0f1830`, padding 14px 22px, min-height 330px, max-height 430px, overflow-y auto):
- Font: Fira Code 13px, line-height 1.62.
- Color tokens for output segments:
  - `fg` → `#c7d3e8`
  - `dim` → `#5e6e8e`
  - `white` → `#eaf0fc`
  - `amber` → `#dba85f` (file paths)
  - `green` → `#6cc090` (success, match counts)
  - `err` → `#e0728a`
  - `hl` → background `#284270`, color `#cfe0ff`, border-radius 2px, padding 0 2px (matched symbols)
- Prompt line: `$ ` in dim + typed text in white + blinking block cursor (8×15px, `#5b8cff`, 1s blink).
- Hidden `<input>` captures keystrokes (opacity 0, 1px × 1px, positioned at bottom-left).

**Command chips bar** (border-top `#233252`, padding 11px 22px, background `#0c1322`):
- "try:" label in Fira Code 12px dim.
- Chips: Fira Code 12px, background `#15203c`, border 1.5px `#233252`, border-radius 5px, padding 4px 10px. Hover: border `#5b8cff`, color `#eaf0fc`.
- Default chips: `qi '*user*' --def`, `qi % -f logger.go --toc`, `load negroni`, `help`.

**Supported commands:**
```
qi <pattern> [-i KIND] [--def] [--limit N] [--json]
qi % -f <file> --toc       → table of contents for one file
load <project>             → switch index (slim · negroni · acme)
ls                         → list files in current index
help / man
clear / cls
↑/↓                        → command history
Ctrl+L                     → clear
```

**Boot scrollback:** On load, the terminal pre-populates with a scripted session: negroni TOC → `load slim` → Slim stats. See `sm-terminal.jsx → bootScrollback()`.

---

### Why qi section

- Border-top: 1.5px `#e0dbd2`. Padding 46px 0.
- Section head: `##` in Fira Code 22px `#d4cbbf` + `"why bother"` in Caveat 700 `clamp(34px,5vw,46px)`.
- 3-column comparison grid (gap 16px):
  - "without qi": `~12.4k` tokens
  - "with qi": `1.5k` tokens — highlighted with blue border + box-shadow `4px 4px 0 #dee1fb`
  - "net": `≈ 8×` fewer tokens
- Each card: background `#faf8f6`, border 1.5px `#d4cbbf`, border-radius 5px, padding 20px.
- Number: Caveat 700, 58px, `#1b1d24` (accent green for "with qi" card).
- Sub-label: Fira Code 12px, `#7c8294`.

---

### What's in the box (The Collection)

- Section head: `##` + `"what's in the box"`.
- Two-column grid (1fr / 1.15fr, gap 26px):
  - **Left:** lede paragraph 17px `#4b5160`. "indexer" and "query‑index" bolded in `#1b1d24`.
  - **Right:** file tree rendered in a `<pre>` block — Fira Code 13px, background `#faf8f6`, border 1.5px `#d4cbbf`, border-radius 6px, padding 18px 20px.
- Language chips row (flex wrap, gap 10px): each chip shows command (`index-go`) bold + language name dim. Border 1.5px `#d4cbbf`, border-radius 4px, padding 5px 11px, background `#faf8f6`.

---

### Quick start

- Section head: `##` + `"quick start"`.
- 3-column grid of steps (gap 16px). Each step: numbered circle (30×30px, border 1.5px `#1b1d24`, border-radius 50%) + step body.
- Step title: Caveat 600, 23px, `#1b1d24`.
- Shell block: Fira Code 12.5px, background `#0c1322`, color `#c7d3e8`, border 1.5px `#233252`, border-radius 5px, padding 9px 12px. `$` in `#5e6e8e`.
- Link row (flex wrap, gap 22px): Fira Code 13px green links — "full quick start →", "read the docs →", "add to Claude Code →".

---

### Footer

- Background: `#faf8f9`. Border-top: 3px solid `#e0dbd2`. Color: `#1b1d24`.
- Two-zone layout:
  - **Top:** max-width 1120px centered, padding 40px 24px 26px. Flex row: ASCII art left + link columns right.
    - ASCII art (Fira Code 12px, `#1b1d24`):
      ```
        ___  _ __ ___
       / __|| '_ ` _ \   sourceminder
       \__ \| | | | | |  query-index · qi
       |___/|_| |_| |_|
      ```
    - Link columns (Fira Code 13px blue links, uppercase mono headers):
      - **resources:** docs, github, changelog
      - **quick links:** quick start, home
  - **Bottom bar:** Fira Code 11px `#4b5160`. Items: "generated by ./gendocs.sh" · "best viewed in any terminal" · visitor counter (monospace, dark bg) · "not enterprise-ready (on purpose)" · "the answer is 42".
  - Visitor counter: background `#1b1d24`, color `#fff`, border 1.5px, border-radius 3px, padding 2px 8px, letter-spacing 0.18em. Value: `00042`.

---

## Interactions & Behavior

| Interaction | Behavior |
|---|---|
| Nav — sticky on scroll | Already sticky; paper bg + backdrop-blur covers content |
| Primary CTA hover | Background `#447a54` → `#569965`, 120ms ease-out |
| Ghost button hover | Border + text → `#447a54` |
| Star button | Pixel shadow drop on hover (optional tactile press) |
| Terminal — click anywhere | Focuses the hidden input |
| Terminal — chip click | Fills input with chip text, focuses input |
| Terminal — Enter | Runs command, appends output to scrollback, auto-scrolls |
| Terminal — ↑/↓ | History navigation |
| Terminal — Ctrl+L | Clear scrollback |
| Terminal — project select | Runs `load <project>`, appends status lines, updates stats |
| Nav links | All `href="#"` until pages are built |

---

## State Management

**QiTerminal component state:**
- `projKey` (string) — current project key (`slim` | `negroni` | `acme`)
- `lines` (array of segment arrays) — scrollback buffer
- `input` (string) — current typed input
- `hist` (string[]) — command history
- `hIdx` (number) — history cursor (-1 = none)
- `focus` (bool) — cursor blink state

**App-level:**
- `accent` (string hex) — persisted to localStorage via Tweaks panel. Default `#447a54`.

---

## Design Tokens

### Page palette
| Token | Value | Usage |
|---|---|---|
| `--paper` | `#f5f3f0` | Page background |
| `--card` | `#faf8f9` | Footer, cards |
| `--ink` | `#1b1d24` | Primary text |
| `--ink-2` | `#4b5160` | Secondary text |
| `--ink-3` | `#7c8294` | Tertiary / placeholder |
| `--rule` | `#e0dbd2` | Dividers, borders |
| `--rule-2` | `#d4cbbf` | Heavier borders |
| `--blue` (accent) | `#447a54` | Buttons, links, active states |
| `--peri` | `#dee1fb` | Selection bg, "symbols" highlight |

### Terminal palette
| Token | Value |
|---|---|
| `--t-bg` | `#0c1322` |
| `--t-panel` | `#0f1830` |
| `--t-card` | `#15203c` |
| `--t-rule` | `#233252` |
| `--t-fg` | `#c7d3e8` |
| `--t-dim` | `#5e6e8e` |
| `--t-white` | `#eaf0fc` |
| `--t-amber` | `#dba85f` |
| `--t-green` | `#6cc090` |
| `--t-blue` | `#5b8cff` |
| `--t-err` | `#e0728a` |
| `--t-hl-bg` | `#284270` |
| `--t-hl-fg` | `#cfe0ff` |

### Typography
| Role | Family | Weight | Size |
|---|---|---|---|
| Wordmarks, headlines, taglines | Caveat | 700 | varies (H1: clamp 56–92px) |
| Body text | Commissioner | 400 | 16px |
| Nav links, eyebrows, code, labels, stats | Fira Code | 400–700 | 11–14px |
| Terminal output | Fira Code | 400 | 13px |

### Spacing
- Section vertical padding: 46px
- Content max-width: 1120px
- Content horizontal padding: 24px
- Card internal padding: 13–20px

### Radii & shadows
- Most containers: 4–8px radius
- Terminal: 12px
- Primary button pixel shadow: `3px 3px 0 #1b1d24`
- Star button pixel shadow: `2px 2px 0 #1b1d24`

---

## Assets

- **Pixel logo** — generated in JSX as a 4×4 CSS grid of `<i>` elements (7×7px, 1.5px gap). Colors are hardcoded in `sm-sections.jsx → PixelLogo`. No image file needed.
- **Fonts** — loaded from Google Fonts:
  ```
  Caveat: wght@500;600;700
  Commissioner: wght@400;500;600;700
  Fira Code: wght@400;450;500;600;700
  ```
- **No external images** used anywhere.

---

## Files in this bundle

| File | Purpose |
|---|---|
| `SourceMinder.html` | Main page shell — CSS tokens, layout, Tweaks panel mount |
| `sm-sections.jsx` | All page sections as React components (Nav, Hero, WhyQi, Collection, GetStarted, Footer) |
| `sm-terminal.jsx` | Live qi terminal — command parser, project switching, scrollback, stat cards |
| `sm-data.js` | Seeded symbol index data for 3 demo projects (slim, negroni, acme-api) |

---

## Notes for the developer

1. **Port `sm-terminal.jsx` faithfully.** The command parser, glob regex, TOC renderer, and boot scrollback are all production-intent logic — not placeholder.
2. **"query-index" must appear before "qi"** — the hero introduces the full name first. Don't shorten it in the H1.
3. **No in-page anchor links in nav or footer.** All links should navigate to real pages.
4. **The dot-grid background** (`radial-gradient` of 1px dots, 9×9 grid, `background-attachment: fixed`) is a deliberate nerd nod — keep it.
