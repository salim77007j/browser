# Kestrel Browser 🦅

**Fast. Light. Untouchable.**

Kestrel v2 is an independent, privacy-first browser with a **pure-Rust stack**:
a native Rust shell (winit + egui) hosting the **Servo web engine** — no bundled
Chromium, no QtWebEngine, no web-based UI. Ad and tracker blocking is built in
through **Kestrel Shield** (the same matching engine Brave uses, backed by
EasyList + EasyPrivacy) and enforced at the resource-request level.

> v1 (Rust core + Qt6/QtWebEngine frontend) is preserved at tag
> `v1-qtwebengine-legacy`. It was replaced because bundling a full Chromium is
> exactly the herd behavior Kestrel set out to avoid — see
> [docs/COMPETITIVE_ANALYSIS.md](docs/COMPETITIVE_ANALYSIS.md) for the real
> competitor study that drove the redesign.

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│                 kestrel-shell (Rust)                     │
│  winit window · native egui chrome (tabs, omnibox,       │
│  menus, prompts) · kestrel:// native pages               │
│  (newtab, settings, history, bookmarks, downloads,       │
│  privacy dashboard — all real, all native)               │
└───────────────┬───────────────────────▲──────────────────┘
                │ WebViewDelegate API   │ request interception
┌───────────────▼───────────────────────┴──────────────────┐
│                    Servo 0.6 (Rust)                      │
│  Independent web engine: SpiderMonkey JS, Stylo CSS,     │
│  WebRender GPU compositing. Multiprocess-ready.          │
└───────────────┬──────────────────────────────────────────┘
                │ Rc
┌───────────────▼──────────────────────────────────────────┐
│                 kestrel-core (Rust)                      │
│  Kestrel Shield: adblock-rust engine (Brave's crate)     │
│  + EasyList/EasyPrivacy, cosmetic filtering              │
│  Privacy engine: fingerprint farbling (canvas/WebGL/     │
│  audio/fonts), HTTPS-first, tracking-param stripping     │
│  SQLite store: history, bookmarks, downloads, settings,  │
│  permissions, sessions, block statistics                 │
└──────────────────────────────────────────────────────────┘
```

**Why this is different:** Chrome/Edge/Brave/Vivaldi all ship Chromium.
Firefox ships Gecko. Kestrel v2 ships **Servo** — the independent Rust engine —
behind a shell that stays out of your way and out of your RAM. The engine sits
behind the embedder API, so WebKit/WebView2 adapters can follow without touching
core logic.

## Feature set (every control is wired to real behavior)

- **Tabs**: new (Ctrl+T), close (Ctrl+W), reopen closed (Ctrl+Shift+T), switch
  (Ctrl+Tab, click), middle-click close, pin, duplicate, context menu. Hidden
  tabs are suspended (show/hide), so background tabs cost near-zero CPU.
- **Omnibox**: URL/search detection, security dot (green https / amber http),
  live suggestions from history + bookmarks + search, keyboard navigation.
- **Kestrel Shield** (`kestrel://privacy`): live per-session and all-time block
  counters, per-host breakdown, per-list toggles (EasyList/EasyPrivacy),
  cosmetic filtering, honest engine posture disclosure.
- **Fingerprint protection**: standard (canvas/WebGL/audio farbling, hardware
  spoofing) and strict (adds uniform UA + font metric perturbation), applied
  per-session per-site — toggle in settings, applies to new pages.
- **HTTPS-first** upgrades + **tracking-parameter stripping** (utm_*, fbclid,
  gclid, …) on every navigation.
- **Real downloads**: file-type navigations are intercepted and fetched by
  Kestrel's own manager with progress in `kestrel://downloads`.
- **Permissions**: camera/mic/location/etc. prompts with per-host remember.
- **Session**: crash-safe journaling every 15 s + restore on start.
- **Page capture**: menu → "Capture page as image" saves a real screenshot.
- **Zoom** per tab (Ctrl+Plus/Minus/0), fullscreen (F11), keyboard shortcuts.

Honest limitations of v2.0 (also shown in-app): Servo is younger than Chromium
— some heavy sites won't match Chrome yet; no extension API (Shield replaces
the core ad-block use case); find-in-page and print are not wired yet because
the engine does not expose those APIs to embedders.

## Building

Linux:
```bash
sudo apt install build-essential clang libclang-dev cmake pkg-config python3 \
  autoconf2.13 libfontconfig1-dev libdbus-1-dev libxkbcommon-dev \
  libxkbcommon-x11-dev nasm
cargo build --release -p kestrel-shell
# binary: target/release/kestrel  (put easylist.txt/easyprivacy.txt next to it
# in a `resources/` folder, or set KESTREL_RESOURCES)
```

Windows: see `.github/workflows/windows.yml` (needs MozillaBuild "moztools 4.0"
and LLVM for bindgen). CI builds both platforms on every push to `main` —
artifacts: **kestrel-linux.zip**, **kestrel-windows.zip**.

## Testing & benchmarks

- `cargo test -p kestrel-core` — privacy engine + state unit tests.
- `tests/` — live test pages (ad-block probe, fingerprint probe).
- `docs/BENCHMARKS.md` — measured startup/RAM/CPU vs Chromium & Firefox,
  produced with `scripts/measure.py` under Xvfb, updated per release.

## License

MIT — see [LICENSE](LICENSE).
