# Kestrel Privacy Promise

**Zero telemetry. Zero accounts. Zero surprises.**

## What Kestrel collects
Nothing. There is no telemetry, no crash reporting, no usage statistics,
no unique identifiers, and no network calls to Kestrel infrastructure
(there is no Kestrel infrastructure).

## What leaves your machine
- **Your searches and page loads** go to the sites you visit (and your
  chosen search engine — DuckDuckGo by default).
- **Suggestions** (optional, on by default) are fetched from DuckDuckGo's
  suggestions API. Disable in Settings → Search.
- **DoH** (optional) encrypts DNS and sends it to your chosen resolver.

## Default protections
| Protection | Default | Where |
|---|---|---|
| Ad & tracker blocking (EasyList, EasyPrivacy, Fanboy) | ON | Rust engine |
| Fingerprint randomization (canvas, audio, WebGL, hardware) | Standard | injected scripts |
| HTTPS-First upgrades | ON | request interceptor |
| Third-party cookie blocking | ON | cookie filter |
| Tracking parameter stripping (utm_*, fbclid, …) | ON | request interceptor |
| Cross-site referer trimming (origin-only) | ON | request interceptor |
| WebRTC public-interfaces-only | ON | engine flag |
| Invalid TLS certificates | always rejected | page handler |
| Autoplay with sound | requires gesture | engine setting |
| Clipboard write from JS | disabled | engine setting |

## Per-site exceptions
Every permission (camera, mic, location, notifications, clipboard, screen)
can be remembered per-host: Allow / Block / Ask, managed from the privacy
dashboard. Fingerprint protection and all shield toggles work globally.

## Data storage
Everything (history, bookmarks, settings, sessions) lives in a local
SQLite database inside your profile directory, written by the Rust core.
No cloud. Clear it any time from Settings → Privacy & Security.

## Private windows
Off-the-record profile: no history writes, no persistent cookies, no
session persistence, in-memory only.
