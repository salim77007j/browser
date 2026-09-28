# Kestrel Browser 🦅

**Swift. Lean. Untouchable.**

Kestrel is an independent, privacy-first web browser built for 2026.
Rust core + Qt 6 native UI + Chromium-grade rendering — designed to be
faster and lighter than the majors while blocking ads, trackers and
fingerprinting by default.

![Kestrel New Tab](../download/shots/ntp12.png)

## Architecture

```
┌────────────────────────────────────────────────────────────┐
│                    Qt 6 Widgets UI (C++)                   │
│  TabStrip · Omnibox · Toolbar · kestrel:// pages (HTML)    │
└───────────────┬─────────────────────────────▲──────────────┘
                │ QWebEngineProfile            │ fetch bridge
┌───────────────▼─────────────────────────────┴──────────────┐
│              QtWebEngine (Chromium render)                 │
│  · RequestInterceptor: ads/trackers, HTTPS-First,          │
│    referer trimming, tracking-param stripping              │
│  · Cookie filter · Permissions · Downloads · Lifecycle     │
└───────────────┬────────────────────────────────────────────┘
                │ C FFI
┌───────────────▼────────────────────────────────────────────┐
│                 kestrel-core (Rust)                        │
│  · Brave adblock-rust engine (EasyList/EasyPrivacy/        │
│    Fanboy Annoyances + custom rules, serialized cache)     │
│  · SQLite store: history, bookmarks, downloads,            │
│    permissions, per-host zoom, sessions, block stats       │
└────────────────────────────────────────────────────────────┘
```

## Feature set

**Browser fundamentals**
- Tabs: new / close / reopen closed / pin / mute / color groups / drag reorder / middle-click close
- Smart omnibox: URL/search detection, local history completer, DuckDuckGo suggestions, security state
- Bookmarks manager, History manager, Downloads manager (progress, open, remove)
- Session restore + crash recovery, background tab freezing (LifecycleState)
- Find in page (with match counts), zoom per site, fullscreen, print, save page (MHTML)
- Developer tools, view source, private windows (off-the-record profile)
- Full keyboard shortcut set (Chrome-compatible), 20+ shortcuts
- Dark / light theme + 6 accent colors, `kestrel://ui/*` internal pages

**Privacy & security (all real, all verifiable)**
- 🛡️ **Shield**: EasyList + EasyPrivacy + Fanboy Annoyances via Brave's Rust
  adblock engine, with live blocked-count badge and per-host stats
- **Fingerprint protection**: randomized canvas/AudioContext noise, WebGL
  vendor masking, hardware spoofing (cores/deviceMemory) — standard/strict
- **HTTPS-First**: upgrades http://→https:// with localhost exception and
  TLS-failure fallback
- **WebRTC leak protection**: public interfaces only (engine flag)
- **Third-party cookie blocking** via cookie filter API
- **Tracking-parameter stripping**: utm_*, fbclid, gclid, … removed from links
- **Referer trimming**: cross-site requests send origin-only referer
- **DoH** (DNS-over-HTTPS): Quad9 / Cloudflare / AdGuard / NextDNS
- Hardened defaults: invalid TLS certs rejected, clipboard-restricted JS,
  autoplay gated, per-site permission management

## Building

Requirements: Qt ≥ 6.5 (WebEngine), Rust stable, CMake ≥ 3.21, Ninja.

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/app/kestrel
```

CI builds Windows (MSVC) and Linux artifacts on every push — see
`.github/workflows/`.

## Design

See [DESIGN.md](docs/DESIGN.md) for the Kestrel design system:
dark charcoal surfaces, electric-blue accent, rounded tabs, pill omnibox.

## License

MIT for Kestrel code. Bundled filter lists are the property of their
maintainers (EasyList/EasyPrivacy — GPL-ish community lists; Fanboy).
qwebchannel.js is LGPL (Qt Company), used unmodified.
