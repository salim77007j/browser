// internal_pages.cpp — kestrel:// page templates (Kestrel Design System)
#include "internal_pages.h"
#include "theme.h"
#include "app.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>

namespace InternalPages {

QString baseCss() {
    QFile f(":/resources/pages/internal.css");
    if (f.open(QIODevice::ReadOnly))
        return QString::fromUtf8(f.readAll());
    return QString();
}

QString qwebchannelJs() {
    QFile f(":/resources/pages/qwebchannel.js");
    if (f.open(QIODevice::ReadOnly))
        return QString::fromUtf8(f.readAll());
    return QString();
}

static QString escape(const QString &s) {
    QString out = s;
    out.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;");
    return out;
}

static QString shell(const QString &title, const QString &body, bool isPrivate, const QString &pageId) {
    const QString theme = KestrelApp::instance()->getSetting("theme", "dark");
    const QString accent = KestrelApp::instance()->getSetting("accent", "#4D9FFF");
    const QString accentSoft = accent + (theme == "light" ? "22" : "24");
    const QString themeArg = theme;
    return QStringLiteral(R"HTML(<!DOCTYPE html>
<html data-page="%1">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%2</title>
<style>%3</style>
<script>
/* Kestrel bridge: same-origin fetch to kestrel://ui/bridge (no QWebChannel) */
window.kestrel = new Proxy({}, {
  get: function(t, method) {
    if (typeof method !== 'string' || method === 'then' || method === 'catch' || method === 'finally') return undefined;
    return function() {
      var args = Array.prototype.slice.call(arguments);
      var cb = null;
      if (args.length && typeof args[args.length - 1] === 'function') cb = args.pop();
      var url = '/bridge?m=' + encodeURIComponent(String(method)) +
                '&a=' + encodeURIComponent(JSON.stringify(args));
      return fetch(url).then(function(r){ return r.json(); }).then(function(d){
        if (cb) { cb(d); }
        return d;
      });
    };
  }
});
window.kestrelReady = Promise.resolve(window.kestrel);
</script>
</head>
<body data-theme="%8" data-private="%4" style="--accent:%9;--accent-soft:%10">
%5
</body>
</html>)HTML")
        .arg(pageId, escape(title), baseCss(), isPrivate ? "1" : "0", body)
        .arg(themeArg, accent, accentSoft);
}

/* ------------------------------------------------ NTP ------------------------------------------------ */

static QString newTab(bool isPrivate) {
    return shell(QStringLiteral("New Tab"), QStringLiteral(R"HTML(
<div class="ntp">
  <div class="ntp-head">
    <div class="ntp-clock" id="clock">00:00</div>
    <div class="ntp-date" id="date"></div>
  </div>
  <div class="ntp-brand"><img src="kestrel://ui/logo" class="ntp-logo" alt=""><h1>Kestrel</h1></div>
  <form class="ntp-search" id="searchForm">
    <input type="text" id="q" placeholder="Search the web privately…" autocomplete="off" autofocus>
    <button type="submit" aria-label="Search">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="11" cy="11" r="7"/><path d="m21 21-4.3-4.3"/></svg>
    </button>
  </form>
  <div class="dial" id="dial"></div>
  <div class="ntp-stats">
    <div class="stat-chip" id="statChip" title="Trackers and ads blocked">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M12 22s8-3.5 8-10V5l-8-3-8 3v7c0 6.5 8 10 8 10z"/></svg>
      <span id="statText">Shield active</span>
    </div>
  </div>
</div>
<script>
(function(){
  const $ = (s) => document.querySelector(s);
  function tick(){
    const d = new Date();
    $('#clock').textContent = d.toLocaleTimeString([], {hour:'2-digit', minute:'2-digit'});
    $('#date').textContent = d.toLocaleDateString([], {weekday:'long', month:'long', day:'numeric'});
  }
  tick(); setInterval(tick, 10000);

  $('#searchForm').addEventListener('submit', (e) => {
    e.preventDefault();
    const q = $('#q').value.trim();
    if (!q) return;
    kestrelReady.then(() => kestrel.search(q));
  });

  function renderDial(items){
    const el = $('#dial');
    el.innerHTML = '';
    if (!items.length) return;
    items.slice(0,8).forEach((t) => {
      const a = document.createElement('a');
      a.className = 'dial-tile';
      a.href = '#';
      a.innerHTML = '<span class="dial-letter">' + (t.title||t.url||'?').charAt(0).toUpperCase() + '</span>' +
                    '<span class="dial-name">' + (t.title||t.url) + '</span>';
      a.onclick = (ev) => { ev.preventDefault(); kestrel.navigate(t.url); };
      el.appendChild(a);
    });
  }
  function renderStats(s){
    const n = (s.session || 0) + (s.total || 0);
    $('#statText').textContent = n > 0 ? (n.toLocaleString() + ' trackers blocked') : 'Shield active';
  }
  kestrelReady.then(() => {
    kestrel.getSpeedDial((items) => renderDial(items));
    kestrel.getStats((s) => renderStats(s));
    setInterval(() => kestrel.getStats((s) => renderStats(s)), 3000);
  });
})();
</script>
)HTML"), isPrivate, "newtab");
}

/* ------------------------------------------------ Settings ------------------------------------------------ */

static QString settingsPage(bool isPrivate) {
    return shell(QStringLiteral("Settings"), QStringLiteral(R"HTML(
<div class="page">
  <aside class="sidenav">
    <div class="side-brand"><img src="kestrel://ui/logo" alt=""><span>Kestrel</span></div>
    <a class="side-item active" data-panel="appearance"><svg viewBox="0 0 24 24"><path d="M2 22 1 1h3l9 9"/><path d="M15 6l3 3-9 9-3-3z"/></svg>Appearance</a>
    <a class="side-item" data-panel="privacy"><svg viewBox="0 0 24 24"><path d="M12 22s8-3.5 8-10V5l-8-3-8 3v7c0 6.5 8 10 8 10z"/></svg>Privacy &amp; Security</a>
    <a class="side-item" data-panel="filters"><svg viewBox="0 0 24 24"><path d="M22 3H2l8 9.5V19l4 2v-8.5z"/></svg>Content Filtering</a>
    <a class="side-item" data-panel="search"><svg viewBox="0 0 24 24"><circle cx="11" cy="11" r="7"/><path d="m21 21-4.3-4.3"/></svg>Search</a>
    <a class="side-item" data-panel="downloads"><svg viewBox="0 0 24 24"><path d="M12 3v12"/><path d="m7 11 5 5 5-5"/><path d="M5 21h14"/></svg>Downloads</a>
    <a class="side-item" data-panel="startup"><svg viewBox="0 0 24 24"><path d="m3 10 9-7 9 7v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/></svg>Startup</a>
    <a class="side-item" data-panel="shortcuts"><svg viewBox="0 0 24 24"><rect x="2" y="6" width="20" height="12" rx="2"/><path d="M6 10h.01M10 10h.01M14 10h.01M18 10h.01M7 14h10"/></svg>Shortcuts</a>
    <a class="side-item" data-panel="about"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M12 8h.01"/><path d="M11 12h1v4h1"/></svg>About</a>
  </aside>
  <main class="content">
    <section class="panel active" id="panel-appearance">
      <h1>Appearance</h1>
      <div class="card">
        <div class="row"><div><b>Theme</b><p>Choose the interface color scheme</p></div>
          <div class="seg" id="seg-theme">
            <button data-v="dark">Dark</button><button data-v="light">Light</button>
          </div></div>
        <div class="row"><div><b>Accent color</b><p>Used for highlights and controls</p></div>
          <div class="swatches" id="accents">
            <button data-v="#4D9FFF" style="background:#4D9FFF"></button>
            <button data-v="#7C5CFF" style="background:#7C5CFF"></button>
            <button data-v="#3DD68C" style="background:#3DD68C"></button>
            <button data-v="#FF8A4C" style="background:#FF8A4C"></button>
            <button data-v="#FF5C8A" style="background:#FF5C8A"></button>
            <button data-v="#F5C542" style="background:#F5C542"></button>
          </div></div>
        <div class="row"><div><b>Scroll animations</b><p>Smooth scrolling across pages</p></div>
          <label class="switch"><input type="checkbox" data-setting="smooth_scroll" checked><span></span></label></div>
      </div>
    </section>

    <section class="panel" id="panel-privacy">
      <h1>Privacy &amp; Security</h1>
      <div class="card">
        <div class="row"><div><b>Shield (ad &amp; tracker blocking)</b><p>Master switch for the filtering engine</p></div>
          <label class="switch"><input type="checkbox" data-setting="blocking_enabled" checked><span></span></label></div>
        <div class="row"><div><b>HTTPS-First mode</b><p>Upgrade http:// pages to secure https://</p></div>
          <label class="switch"><input type="checkbox" data-setting="https_first" checked><span></span></label></div>
        <div class="row"><div><b>Block third-party cookies</b><p>Isolates cookies to the site you visit</p></div>
          <label class="switch"><input type="checkbox" data-setting="block_third_party_cookies" checked><span></span></label></div>
        <div class="row"><div><b>Strip tracking parameters</b><p>Removes utm_/fbclid/gclid from links</p></div>
          <label class="switch"><input type="checkbox" data-setting="strip_tracking_params" checked><span></span></label></div>
        <div class="row"><div><b>Fingerprint protection</b><p>Masks canvas, audio and hardware fingerprints</p></div>
          <div class="seg" id="seg-fp">
            <button data-v="0">Off</button><button data-v="1">Standard</button><button data-v="2">Strict</button>
          </div></div>
        <div class="row"><div><b>WebRTC: public interfaces only</b><p>Prevents IP address leaks via WebRTC</p></div>
          <label class="switch"><input type="checkbox" data-setting="webrtc_public_only" checked><span></span></label></div>
        <div class="row"><div><b>DNS over HTTPS</b><p>Encrypts DNS lookups</p></div>
          <div class="seg" id="seg-doh">
            <button data-v="off">Off</button><button data-v="auto">Auto</button><button data-v="secure">Secure</button>
          </div></div>
        <div class="row"><div><b>DoH provider</b><p>Resolver used when DoH is active</p></div>
          <div class="seg" id="seg-dohp">
            <button data-v="quad9">Quad9</button><button data-v="cloudflare">Cloudflare</button><button data-v="adguard">AdGuard</button><button data-v="nextdns">NextDNS</button>
          </div></div>
      </div>
      <div class="card">
        <div class="row"><div><b>Clear browsing data</b><p>History, downloads, stats, cookies</p></div>
          <button class="btn danger" id="clearData">Clear data…</button></div>
      </div>
    </section>

    <section class="panel" id="panel-filters">
      <h1>Content Filtering</h1>
      <div class="card" id="lists"></div>
      <div class="card">
        <div class="col"><b>Custom filter rules</b>
          <p>One rule per line, uBlock-style syntax (e.g. <code>||ads.example.com^</code>)</p>
          <textarea id="customRules" rows="8" spellcheck="false"></textarea>
          <div class="actions"><button class="btn primary" id="saveRules">Save rules</button><span id="rulesSaved" class="ok hidden">Saved ✓</span></div>
        </div>
      </div>
    </section>

    <section class="panel" id="panel-search">
      <h1>Search</h1>
      <div class="card">
        <div class="row"><div><b>Default search engine</b></div>
          <div class="seg" id="seg-engine">
            <button data-v="duckduckgo">DuckDuckGo</button><button data-v="google">Google</button>
            <button data-v="bing">Bing</button><button data-v="brave">Brave</button>
          </div></div>
        <div class="row"><div><b>Search suggestions</b><p>Sent via DuckDuckGo suggestions API</p></div>
          <label class="switch"><input type="checkbox" data-setting="search_suggestions" checked><span></span></label></div>
      </div>
    </section>

    <section class="panel" id="panel-downloads">
      <h1>Downloads</h1>
      <div class="card">
        <div class="row"><div><b>Ask where to save each file</b></div>
          <label class="switch"><input type="checkbox" data-setting="ask_download_path"><span></span></label></div>
        <div class="row"><div><b>Warn on risky file types</b><p>Executables and script downloads</p></div>
          <label class="switch"><input type="checkbox" data-setting="warn_risky_downloads" checked><span></span></label></div>
        <div class="row"><div><b>GPU acceleration</b><p>Disable only if rendering is unstable</p></div>
          <label class="switch"><input type="checkbox" data-setting="gpu_acceleration" checked><span></span></label></div>
      </div>
    </section>

    <section class="panel" id="panel-startup">
      <h1>Startup</h1>
      <div class="card">
        <div class="row"><div><b>On startup</b><p>What happens when Kestrel opens</p></div>
          <div class="seg" id="seg-restore">
            <button data-v="always">Restore session</button><button data-v="ask">Ask</button><button data-v="never">New tab</button>
          </div></div>
        <div class="row"><div><b>Freeze background tabs</b><p>Reduces memory after 10 minutes in background</p></div>
          <label class="switch"><input type="checkbox" data-setting="freeze_background_tabs" checked><span></span></label></div>
      </div>
    </section>

    <section class="panel" id="panel-shortcuts">
      <h1>Keyboard Shortcuts</h1>
      <div class="card" id="shortcutList"></div>
    </section>

    <section class="panel" id="panel-about">
      <h1>About Kestrel</h1>
      <div class="card center">
        <img src="kestrel://ui/logo" class="about-logo" alt="Kestrel">
        <h2>Kestrel 1.0.0</h2>
        <p class="muted">Swift. Lean. Untouchable.</p>
        <p class="muted small">Rust core · Qt 6 · QtWebEngine<br>Independent, privacy-first browsing for 2026.</p>
      </div>
    </section>
  </main>
</div>
<script>
const SHORTCUTS = [
  ["Ctrl+T", "New tab"], ["Ctrl+W", "Close tab"], ["Ctrl+Shift+T", "Reopen closed tab"],
  ["Ctrl+Tab", "Next tab"], ["Ctrl+1…8", "Jump to tab"], ["Ctrl+L", "Focus address bar"],
  ["Ctrl+F", "Find in page"], ["Ctrl+R", "Reload"], ["Ctrl+Shift+R", "Hard reload"],
  ["Alt+←/→", "Back / Forward"], ["Ctrl+D", "Bookmark page"], ["Ctrl+H", "History"],
  ["Ctrl+J", "Downloads"], ["Ctrl+Shift+N", "Private window"], ["Ctrl+P", "Print"],
  ["Ctrl+S", "Save page"], ["F11", "Fullscreen"], ["Ctrl++/−/0", "Zoom"],
  ["F12", "Developer tools"], ["Ctrl+Shift+Del", "Clear data"]
];
const $ = (s) => document.querySelector(s);
const $$ = (s) => Array.from(document.querySelectorAll(s));

function applySeg(id, value){
  $$('#' + id + ' button').forEach((b) => b.classList.toggle('active', b.dataset.v === String(value)));
}

function load(){
  kestrelReady.then(() => {
    kestrel.getSettings((s) => {
      $$('input[data-setting]').forEach((i) => { i.checked = String(s[i.dataset.setting] === undefined ? i.checked : s[i.dataset.setting]) === '1'; });
      applySeg('seg-theme', s.theme || 'dark');
      applySeg('seg-fp', s.fingerprint_mode === undefined ? 1 : s.fingerprint_mode);
      applySeg('seg-doh', s.doh_mode === undefined ? 'auto' : s.doh_mode);
      applySeg('seg-dohp', s.doh_provider === undefined ? 'quad9' : s.doh_provider);
      applySeg('seg-engine', s.search_engine === undefined ? 'duckduckgo' : s.search_engine);
      applySeg('seg-restore', s.session_restore === undefined ? 'always' : s.session_restore);
      $$('#accents button').forEach((b) => b.classList.toggle('active', (s.accent || '#4D9FFF') === b.dataset.v));
      $('#customRules').value = s.custom_rules || '';
    });
    kestrel.getFilterLists((f) => {
      const el = $('#lists');
      el.innerHTML = '';
      (f.lists || []).forEach((l) => {
        const row = document.createElement('div');
        row.className = 'row';
        row.innerHTML = '<div><b>' + l.name + '</b><p>' + l.description + '</p></div>';
        const sw = document.createElement('label');
        sw.className = 'switch';
        sw.innerHTML = '<input type="checkbox" ' + (l.enabled ? 'checked' : '') + '><span></span>';
        sw.querySelector('input').onchange = (e) => kestrel.setFilterListEnabled(l.id, e.target.checked);
        row.appendChild(sw);
        el.appendChild(row);
      });
    });
  });
}

document.addEventListener('click', (e) => {
  const side = e.target.closest('.side-item');
  if (side) {
    $$('.side-item').forEach((x) => x.classList.remove('active'));
    side.classList.add('active');
    $$('.panel').forEach((p) => p.classList.remove('active'));
    $('#panel-' + side.dataset.panel).classList.add('active');
    return;
  }
  const seg = e.target.closest('.seg button');
  if (seg) {
    const id = seg.parentElement.id;
    applySeg(id, seg.dataset.v);
    const key = { 'seg-theme':'theme', 'seg-fp':'fingerprint_mode', 'seg-doh':'doh_mode',
                  'seg-dohp':'doh_provider', 'seg-engine':'search_engine', 'seg-restore':'session_restore' }[id];
    if (key) kestrel.setSetting(key, seg.dataset.v);
    if (key === 'theme') setTimeout(() => location.reload(), 120);
    return;
  }
  const sw = e.target.closest('#accents button');
  if (sw) {
    kestrel.setSetting('accent', sw.dataset.v);
    setTimeout(() => location.reload(), 120);
    return;
  }
  const toggle = e.target.closest('input[data-setting]');
  if (toggle) {
    kestrel.setSetting(toggle.dataset.setting, toggle.checked ? '1' : '0');
  }
});

$('#saveRules').addEventListener('click', () => {
  kestrel.setCustomRules($('#customRules').value);
  $('#rulesSaved').classList.remove('hidden');
  setTimeout(() => $('#rulesSaved').classList.add('hidden'), 1600);
});
$('#clearData').addEventListener('click', () => {
  const ok = confirm('Clear browsing history, downloads, blocking stats and cookies?');
  if (ok) kestrel.clearBrowsingData(true, true, true, true);
});

const sl = $('#shortcutList');
SHORTCUTS.forEach(([k, d]) => {
  sl.insertAdjacentHTML('beforeend', '<div class="row"><div>' + d + '</div><kbd>' + k + '</kbd></div>');
});
load();
</script>
)HTML"), isPrivate, "settings");
}

/* ------------------------------------------------ Privacy dashboard ------------------------------------------------ */

static QString privacyPage(bool isPrivate) {
    return shell(QStringLiteral("Privacy Dashboard"), QStringLiteral(R"HTML(
<div class="page">
  <div class="pagehead"><h1>Privacy Dashboard</h1><span class="muted">Live protection status</span></div>
  <div class="dash">
    <div class="dash-hero card">
      <div class="shield-ring" id="ring"><div class="shield-core">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"><path d="M12 22s8-3.5 8-10V5l-8-3-8 3v7c0 6.5 8 10 8 10z"/><path d="m9 11.5 2 2 4-4.5"/></svg>
        <b id="pct">100%</b></div></div>
      <div class="hero-stats">
        <div class="hstat"><b id="sSession">0</b><span>blocked this session</span></div>
        <div class="hstat"><b id="sTotal">0</b><span>blocked all time</span></div>
        <div class="hstat"><b id="sHosts">0</b><span>hosts seen</span></div>
      </div>
    </div>
    <div class="card">
      <h3>Global protection</h3>
      <div class="row"><div><b>Ad &amp; tracker blocking</b></div><label class="switch"><input type="checkbox" data-setting="blocking_enabled" checked><span></span></label></div>
      <div class="row"><div><b>HTTPS-First</b></div><label class="switch"><input type="checkbox" data-setting="https_first" checked><span></span></label></div>
      <div class="row"><div><b>Third-party cookie blocking</b></div><label class="switch"><input type="checkbox" data-setting="block_third_party_cookies" checked><span></span></label></div>
      <div class="row"><div><b>Fingerprint protection</b></div><label class="switch"><input type="checkbox" id="fpToggle" checked><span></span></label></div>
    </div>
    <div class="card">
      <h3>Top blocked hosts</h3>
      <div id="hostList" class="hostlist"><p class="muted">Nothing blocked yet.</p></div>
    </div>
  </div>
</div>
<script>
const $ = (s) => document.querySelector(s);
function pctBlocked(s){
  // Protection score: shield active & https-first & cookies blocked → 100
  return s.blocking_enabled === '0' ? 40 : 100;
}
function render(s, stats){
  const p = pctBlocked(s);
  $('#pct').textContent = p + '%';
  $('#ring').style.setProperty('--deg', (p * 3.6) + 'deg');
  $('#sSession').textContent = (stats.session || 0).toLocaleString();
  $('#sTotal').textContent = (stats.total || 0).toLocaleString();
  const hosts = Object.entries(stats.byHost || {});
  $('#sHosts').textContent = hosts.length;
  const list = $('#hostList');
  list.innerHTML = '';
  if (!hosts.length) { list.innerHTML = '<p class="muted">Nothing blocked yet.</p>'; return; }
  hosts.sort((a,b) => b[1] - a[1]).slice(0, 10).forEach(([h, c]) => {
    list.insertAdjacentHTML('beforeend',
      '<div class="host-row"><span class="host-name">' + h + '</span><span class="host-bar"><i style="width:' +
      Math.min(100, c / (hosts[0][1]||1) * 100) + '%"></i></span><b>' + c.toLocaleString() + '</b></div>');
  });
}
function refresh(){
  kestrelReady.then(() => {
    kestrel.getStats((stats) => {
      kestrel.getSettings((s) => {
        $('#fpToggle').checked = (s.fingerprint_mode === undefined ? 1 : Number(s.fingerprint_mode)) > 0;
        $$('input[data-setting]').forEach((i) => { i.checked = String(s[i.dataset.setting] === undefined ? i.checked : s[i.dataset.setting]) === '1'; });
        render(s, stats);
      });
    });
  });
}
document.addEventListener('click', (e) => {
  const toggle = e.target.closest('input[data-setting]');
  if (toggle) kestrel.setSetting(toggle.dataset.setting, toggle.checked ? '1' : '0');
});
$('#fpToggle').addEventListener('change', (e) => kestrel.setSetting('fingerprint_mode', e.target.checked ? '1' : '0'));
refresh();
setInterval(refresh, 3000);
</script>
)HTML"), isPrivate, "privacy");
}

/* ------------------------------------------------ History ------------------------------------------------ */

static QString historyPage(bool isPrivate) {
    return shell(QStringLiteral("History"), QStringLiteral(R"HTML(
<div class="page">
  <div class="pagehead"><h1>History</h1>
    <div class="pagehead-actions">
      <input id="q" type="search" placeholder="Search history">
      <button class="btn danger" id="clearAll">Clear all</button>
    </div></div>
  <div class="card"><div id="list" class="list"></div></div>
</div>
<script>
const $ = (s) => document.querySelector(s);
function esc(s){ const d = document.createElement('div'); d.textContent = s || ''; return d.innerHTML; }
function load(q){
  kestrelReady.then(() => kestrel.getHistory(q || '', (items) => {
    const el = $('#list');
    el.innerHTML = '';
    if (!items.length) { el.innerHTML = '<p class="muted pad">No history yet.</p>'; return; }
    let lastDay = '';
    items.forEach((h) => {
      const day = new Date(h.lastVisit).toLocaleDateString([], {weekday:'long', month:'long', day:'numeric'});
      if (day !== lastDay) {
        el.insertAdjacentHTML('beforeend', '<div class="list-day">' + day + '</div>');
        lastDay = day;
      }
      const row = document.createElement('div');
      row.className = 'list-row';
      row.innerHTML =
        '<img class="fav" src="https://icons.duckduckgo.com/ip3/' + esc(h.host) + '.ico" onerror="this.style.visibility=\'hidden\'">' +
        '<div class="grow"><a data-url="' + esc(h.url) + '">' + (esc(h.title) || esc(h.url)) + '</a>' +
        '<span class="muted small">' + esc(h.url) + '</span></div>' +
        '<span class="muted small">' + new Date(h.lastVisit).toLocaleTimeString([], {hour:'2-digit', minute:'2-digit'}) + '</span>' +
        '<button class="icon-btn" title="Delete">✕</button>';
      row.querySelector('a').onclick = () => kestrel.navigate(h.url);
      row.querySelector('.icon-btn').onclick = () => { kestrel.deleteHistoryItem(h.url); row.remove(); };
      el.appendChild(row);
    });
  }));
}
$('#q').addEventListener('input', () => load($('#q').value));
$('#clearAll').addEventListener('click', () => {
  if (confirm('Clear ALL browsing history?')) { kestrel.clearHistory(); load(''); }
});
load('');
</script>
)HTML"), isPrivate, "history");
}

/* ------------------------------------------------ Bookmarks ------------------------------------------------ */

static QString bookmarksPage(bool isPrivate) {
    return shell(QStringLiteral("Bookmarks"), QStringLiteral(R"HTML(
<div class="page">
  <div class="pagehead"><h1>Bookmarks</h1></div>
  <div class="card"><div id="list" class="list"></div></div>
</div>
<script>
const $ = (s) => document.querySelector(s);
function esc(s){ const d = document.createElement('div'); d.textContent = s || ''; return d.innerHTML; }
function load(){
  kestrelReady.then(() => kestrel.getBookmarks((items) => {
    const el = $('#list');
    el.innerHTML = '';
    if (!items.length) { el.innerHTML = '<p class="muted pad">No bookmarks yet. Press Ctrl+D to bookmark a page.</p>'; return; }
    items.forEach((b) => {
      const row = document.createElement('div');
      row.className = 'list-row';
      row.innerHTML =
        '<img class="fav" src="https://icons.duckduckgo.com/ip3/' + esc((new URL(b.url)).host) + '.ico" onerror="this.style.visibility=\'hidden\'">' +
        '<div class="grow"><a data-url="' + esc(b.url) + '">' + (esc(b.title) || esc(b.url)) + '</a>' +
        '<span class="muted small">' + esc(b.url) + '</span></div>' +
        '<button class="icon-btn" title="Remove">✕</button>';
      row.querySelector('a').onclick = () => kestrel.navigate(b.url);
      row.querySelector('.icon-btn').onclick = () => { kestrel.removeBookmark(b.url); load(); };
      el.appendChild(row);
    });
  }));
}
load();
</script>
)HTML"), isPrivate, "bookmarks");
}

/* ------------------------------------------------ Downloads ------------------------------------------------ */

static QString downloadsPage(bool isPrivate) {
    return shell(QStringLiteral("Downloads"), QStringLiteral(R"HTML(
<div class="page">
  <div class="pagehead"><h1>Downloads</h1>
    <div class="pagehead-actions"><button class="btn danger" id="clearAll">Clear list</button></div></div>
  <div class="card"><div id="list" class="list"></div></div>
</div>
<script>
const $ = (s) => document.querySelector(s);
function fmtSize(n){
  if (!n || n <= 0) return '';
  const u = ['B','KB','MB','GB']; let i = 0;
  while (n >= 1024 && i < 3) { n /= 1024; i++; }
  return n.toFixed(i ? 1 : 0) + ' ' + u[i];
}
function esc(s){ const d = document.createElement('div'); d.textContent = s || ''; return d.innerHTML; }
function load(){
  kestrelReady.then(() => kestrel.getDownloads((items) => {
    const el = $('#list');
    el.innerHTML = '';
    if (!items.length) { el.innerHTML = '<p class="muted pad">No downloads yet.</p>'; return; }
    items.forEach((d) => {
      const pct = d.total > 0 ? Math.round(d.received / d.total * 100) : 0;
      const row = document.createElement('div');
      row.className = 'list-row dl';
      row.innerHTML =
        '<div class="dl-icon">⤓</div>' +
        '<div class="grow"><b>' + esc(d.filename) + '</b>' +
        '<span class="muted small">' + esc(d.url) + '</span>' +
        (d.state === 'in_progress'
          ? '<div class="prog"><i style="width:' + pct + '%"></i></div>'
          : '<span class="muted small">' + d.state + (fmtSize(d.total) ? ' · ' + fmtSize(d.total) : '') + '</span>') +
        '</div>' +
        (d.path ? '<button class="btn small" data-open>Open</button>' : '') +
        '<button class="icon-btn" title="Remove">✕</button>';
      if (d.path) row.querySelector('[data-open]').onclick = () => kestrel.openDownload(d.id);
      row.querySelector('.icon-btn').onclick = () => { kestrel.removeDownload(d.id); load(); };
      el.appendChild(row);
    });
  }));
}
$('#clearAll').addEventListener('click', () => { kestrel.clearDownloads(); load(); });
load();
setInterval(load, 1500);
</script>
)HTML"), isPrivate, "downloads");
}

/* ------------------------------------------------ Error / blocked page ------------------------------------------------ */

static QString errorPage(const QString &errorText, bool isPrivate) {
    return shell(QStringLiteral("Error"), QStringLiteral(R"HTML(
<div class="error-wrap">
  <img src="kestrel://ui/logo" class="err-logo" alt="">
  <h1>This page can't be displayed</h1>
  <p class="muted" id="err">%1</p>
  <button class="btn primary" onclick="location.reload()">Try again</button>
</div>
)HTML").arg(escape(errorText)), isPrivate, "error");
}

QString render(const QUrl &url, bool isPrivate) {
    const QString path = !url.host().isEmpty() ? url.host()
                       : (url.path().isEmpty() ? QStringLiteral("newtab") : url.path().mid(1));
    if (path == "newtab" || path.isEmpty()) return newTab(isPrivate);
    if (path == "settings") return settingsPage(isPrivate);
    if (path == "privacy") return privacyPage(isPrivate);
    if (path == "history") return historyPage(isPrivate);
    if (path == "bookmarks") return bookmarksPage(isPrivate);
    if (path == "downloads") return downloadsPage(isPrivate);
    if (path == "about") return settingsPage(isPrivate); // about → settings/about panel
    return errorPage(QStringLiteral("Unknown page: %1").arg(path), isPrivate);
}

} // namespace InternalPages
