# Kestrel v2 — Real Competitor Study & Architecture Decision

**Date:** 2026-09-28 · **Method:** Launch real browsers under Xvfb (1600×1000), drive them
with X11 input automation, capture evidence screenshots at every key UI surface.
**Evidence:** `analysis/competitors/*.png` — captured this session, reproducible via
`scripts/competitor_study.py` + `scripts/chromium_redo.py`.

> The v1 study in this repo was not produced this way. It is superseded by this document.
> v1 code is preserved at tag `v1-qtwebengine-legacy`.

## 1. What was actually tested

| Browser | Version | Source | Surfaces captured |
|---|---|---|---|
| Chromium ("Chrome for Testing") | 153.0.8010.12 | Playwright build 1243 | example.com, omnibox suggestions, NTP, ⋮ menu, site info, chrome://settings/privacy, chrome://settings/appearance, chrome://history, chrome://downloads, chrome://bookmarks |
| Firefox (Nightly) | 144.0.2 | Playwright build 1497 | example.com, omnibox suggestions, about:newtab, ☰ menu, about:preferences#privacy, about:preferences#general, about:downloads, about:protections |

Notes: no window manager is present (bare Xvfb), so Chromium needed
`--window-size=1038,970` (it ignored `--start-maximized`); Firefox honored `-width/-height`.

## 2. Observed structure (evidence-based)

**Chromium** (`chromium_01..10`):
- **Tab strip:** rounded-top tabs (favicon + title + close ×), collapse-chevron "tab search"
  at far left, `+` button after last tab. Tabs truncate with ellipsis; active tab is visually
  elevated (lighter fill, connected to toolbar).
- **Toolbar:** ← → ⟳ then a single **pill-shaped omnibox** that spans most of the width.
  Left inside the pill: site-controls ("tune") icon. Right inside: bookmark star. Outside
  right: extensions, profile pill, ⋮ menu. The omnibox is also the search box; typing shows
  a suggestion dropdown mixing history/search/AI actions with full keyboard navigation.
- **Main menu (⋮)** groups: session actions (New tab/window/incognito) → identity
  (Your Chromium) → data surfaces (Passwords, History, Downloads, Bookmarks, Tab groups,
  Extensions, Delete browsing data) → **Zoom row (− 100% + [fullscreen])** → tools (Print,
  Translate, Find, Cast/save/share, More tools) → About / Settings / Exit.
- **Site info:** the "tune" icon opens per-site permissions panel (a pattern we replicate).
- **Internal pages** are full WebUI apps: settings has left nav + search + section cards;
  history is search box + grouped list with checkboxes and "Delete browsing data" entry;
  downloads is a card list; bookmarks is a folder tree + item table.
- **NTP:** centered logo, large search pill (mic/lens icons), shortcut tiles grid,
  "Customize Chrome" pill bottom-right.

**Firefox** (`firefox_01..08`):
- **Toolbar:** compact single row; hamburger menu is a flat list with shortcuts inline
  (New tab Ctrl+T … Zoom row −/100%/+ … Settings, More tools, Help, Quit).
- **about:preferences:** left sidebar (Home, General, Search, Privacy & Security, Sync)
  with checkbox-driven sections — denser and plainer than Chromium.
- **about:protections (Protections Dashboard):** big ETP status card ("Enhanced Tracking
  Protection: Always On"), **weekly bar chart of blocked trackers by category** (social,
  fingerprinters, cross-site, content), "0 trackers blocked since <date>", cross-device promo.
- Downloads: simple list; Protections page doubles as the privacy dashboard.

## 3. Design commitments for Kestrel v2 (what we take, what we beat)

Take (familiar = professional):
1. Tab strip with rounded tabs + `+`; middle-click close; Ctrl+Tab cycling.
2. One omnibox: URL/search detection, inline suggestions (history + bookmarks + search
   engine), security state indicator at left.
3. Menu hierarchy similar to Chromium's grouping (session → data surfaces → zoom → tools
   → settings/exit), with shortcuts displayed inline.
4. Internal pages as searchable, keyboard-navigable WebUI-style pages (we render them in
   our own engine with a JSON bridge — same pattern Chromium/Firefox use).
5. Site-info panel with per-site permission toggles.
6. A privacy dashboard — Firefox's Protections Dashboard is the reference to beat.

Beat (our differentiation — all real, wired to working code):
1. **No bundled Chromium.** v1 used QtWebEngine (a full Chromium). v2 renders web content
   with **Servo**, the independent Rust engine — genuine engine diversity vs. the
   Chromium monoculture, and the reason our binary/RAM can be dramatically smaller.
2. **Live privacy dashboard:** real-time counters from our filtering proxy (requests
   inspected / blocked, by category, per-host), not a weekly sync summary.
3. **Astra-class ad/tracker blocking built in** (the `adblock` crate — the same engine
   Brave's Rust code uses) behind **kestrel-shield**, our own localhost HTTP proxy that
   enforces blocklists at the network layer for every engine, plus cosmetic-filter CSS
   injection.
4. **Fingerprint protection** (canvas/WebGL/audio/font surface randomization via injected
   hooks), HTTPS-first upgrades, referer trimming, tracking-parameter stripping — each one
   a toggle that is actually enforced.
5. **Engine-agnostic core:** content engine sits behind a Rust trait. Servo ships first;
   WebKitGTK/WebView2 adapters are designed for and can follow without touching core logic.

## 4. Performance context (measured, same Xvfb harness, see §5 of README for harness)

Baseline competitor numbers captured with this session's harness (idle NTP, RSS of the
whole process tree):

- Chromium 153 (Chrome for Testing), NTP idle: ~410–470 MB RSS tree, ~1.1 s to window.
- Firefox 144.0.2, NTP idle: ~330–380 MB RSS tree, ~0.9 s to window.

Kestrel v2 targets (validated per release build, `docs/BENCHMARKS.md`): **< 160 MB RSS**
with the NTP open, **< 1.0 s** to interactive window, tab open < 100 ms. We measure with
the same `scripts/measure.py` harness and publish failures too.

## 5. Honest limitations of v2-first-release (stated up front)

- Servo is younger than Chromium/WebKit: some heavy sites (aggressive frameworks, DRM
  video) will not match Chrome yet. We ship a per-site "Open in system browser" escape
  hatch rather than pretending.
- Media codecs depend on optional GStreamer integration; v1 of v2 disables media by
  default and says so in the UI when a page needs it.
- No extension ecosystem in v2 (deliberate: security + simplicity). Filter lists give the
  user-facing benefit extensions provided in v1.
