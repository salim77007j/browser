//! kestrel-core — Rust core of the Kestrel Browser.
//! Exposes a C ABI consumed by the Qt6 UI layer:
//! filtering (ads/trackers), storage, privacy stats, settings.

mod adb;
mod store;

pub use adb::FilterEngine;
pub use store::Store;

use std::collections::HashMap;
use std::ffi::{c_char, c_double, c_int, CStr, CString};
use std::path::PathBuf;

pub struct KestrelCore {
    pub store: Store,
    pub filters: FilterEngine,
    pub profile_dir: PathBuf,
}

/* ============================================================
 * FFI helpers
 * ============================================================ */

fn s2c(s: String) -> *mut c_char {
    match CString::new(s) {
        Ok(c) => c.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

fn opt2c(s: Option<String>) -> *mut c_char {
    match s {
        Some(v) => s2c(v),
        None => std::ptr::null_mut(),
    }
}

unsafe fn c2s<'a>(p: *const c_char) -> &'a str {
    if p.is_null() {
        ""
    } else {
        match CStr::from_ptr(p).to_str() {
            Ok(s) => s,
            Err(_) => "",
        }
    }
}

/// Free a string returned by kestrel-core.
#[no_mangle]
pub unsafe extern "C" fn kestrel_string_free(p: *mut c_char) {
    if !p.is_null() {
        drop(CString::from_raw(p));
    }
}

/* ============================================================
 * Lifecycle
 * ============================================================ */

#[no_mangle]
pub unsafe extern "C" fn kestrel_core_new(profile_dir: *const c_char) -> *mut KestrelCore {
    let dir = c2s(profile_dir);
    let dir: &str = dir.as_ref();
    if dir.is_empty() {
        return std::ptr::null_mut();
    }
    let _ = std::fs::create_dir_all(dir);
    let db_path = PathBuf::from(dir).join("kestrel.db");
    let store = match Store::open(db_path.to_str().unwrap_or("kestrel.db")) {
        Ok(s) => s,
        Err(_) => return std::ptr::null_mut(),
    };
    Box::into_raw(Box::new(KestrelCore {
        store,
        filters: FilterEngine::new(),
        profile_dir: PathBuf::from(dir),
    }))
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_core_free(core: *mut KestrelCore) {
    if core.is_null() {
        return;
    }
    let core = Box::from_raw(core);
    // Flush pending per-host stats
    let drained: HashMap<String, u64> = core.filters.drain_host_counts();
    core.store.stats_flush(&drained);
    drop(core);
}

/* ============================================================
 * Filtering engine
 * ============================================================ */

/// Returns 1 when the request must be blocked, 0 when allowed.
#[no_mangle]
pub unsafe extern "C" fn kestrel_check_url(
    core: *mut KestrelCore,
    url: *const c_char,
    source_url: *const c_char,
    req_type: *const c_char,
) -> c_int {
    let core = match core.as_ref() {
        Some(c) => c,
        None => return 0,
    };
    let url = c2s(url);
    let src = c2s(source_url);
    let typ = c2s(req_type);
    let blocked = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        core.filters.check(url, src, typ)
    }))
    .unwrap_or(false);
    if blocked {
        let host = host_of(url);
        core.filters.record_block(&host);
        1
    } else {
        0
    }
}

/// Returns CSS (hide selectors) + optional JS scriptlets as JSON:
/// {"css":"...", "js":"..."} — empty strings when nothing applies.
#[no_mangle]
pub unsafe extern "C" fn kestrel_cosmetic_json(
    core: *mut KestrelCore,
    url: *const c_char,
) -> *mut c_char {
    let core = match core.as_ref() {
        Some(c) => c,
        None => return s2c("{}".into()),
    };
    let url = c2s(url);
    match core.filters.cosmetic_css(url) {
        Some((css, js)) => s2c(format!("{{\"css\":{},\"js\":{}}}",
            serde_json::to_string(&css).unwrap_or_else(|_| "\"\"".into()),
            serde_json::to_string(&js).unwrap_or_else(|_| "\"\"".into()))),
        None => s2c("{}".into()),
    }
}

/// Rebuild the filter engine. lists_json: {"lists":[{"id":"..","path":".."}],
/// "custom":"..."} — list enablement stored in DB.
#[no_mangle]
pub unsafe extern "C" fn kestrel_reload_filters(
    core: *mut KestrelCore,
    lists_json: *const c_char,
) -> c_int {
    let core = match core.as_ref() {
        Some(c) => c,
        None => return 0,
    };
    let json = c2s(lists_json);
    let val: serde_json::Value = serde_json::from_str(json).unwrap_or(serde_json::json!({}));
    let mut paths: Vec<(String, String)> = Vec::new();
    if let Some(lists) = val.get("lists").and_then(|v| v.as_array()) {
        for l in lists {
            let id = l.get("id").and_then(|v| v.as_str()).unwrap_or("");
            let path = l.get("path").and_then(|v| v.as_str()).unwrap_or("");
            if !id.is_empty() && !path.is_empty() && core.store.filter_list_enabled(id) {
                paths.push((id.to_string(), path.to_string()));
            }
        }
    }
    let custom = val.get("custom").and_then(|v| v.as_str()).unwrap_or("").to_string();
    let cache = core.profile_dir.join("engine.cache");
    match core.filters.reload(&core.store, &paths, &custom, cache.to_str().unwrap_or("engine.cache")) {
        Ok(()) => 1,
        Err(_) => 0,
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_filter_count(core: *mut KestrelCore) -> i64 {
    match core.as_ref() {
        Some(_) => 0, // populated via reload result in future
        None => 0,
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_session_block_total(core: *mut KestrelCore) -> u64 {
    core.as_ref().map(|c| c.filters.total_this_session()).unwrap_or(0)
}

/// Flush per-host stats to the database (call periodically + on exit).
#[no_mangle]
pub unsafe extern "C" fn kestrel_stats_flush(core: *mut KestrelCore) {
    if let Some(c) = core.as_ref() {
        let drained = c.filters.drain_host_counts();
        c.store.stats_flush(&drained);
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_stats_json(core: *mut KestrelCore) -> *mut c_char {
    match core.as_ref() {
        Some(c) => s2c(c.store.stats_json()),
        None => s2c("{}".into()),
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_stats_clear(core: *mut KestrelCore) {
    if let Some(c) = core.as_ref() {
        c.store.stats_clear();
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_filter_lists_json(core: *mut KestrelCore) -> *mut c_char {
    match core.as_ref() {
        Some(c) => s2c(c.store.filter_lists_json(adb::BUILTIN_LISTS)),
        None => s2c("[]".into()),
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_filter_list_set_enabled(
    core: *mut KestrelCore,
    id: *const c_char,
    enabled: c_int,
) {
    if let Some(c) = core.as_ref() {
        c.store.filter_list_set_enabled(c2s(id), enabled != 0);
    }
}

/* ============================================================
 * History
 * ============================================================ */

#[no_mangle]
pub unsafe extern "C" fn kestrel_history_add(
    core: *mut KestrelCore,
    url: *const c_char,
    title: *const c_char,
    host: *const c_char,
) {
    if let Some(c) = core.as_ref() {
        c.store.history_add(c2s(url), c2s(title), c2s(host));
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_history_query(
    core: *mut KestrelCore,
    search: *const c_char,
    limit: c_int,
) -> *mut c_char {
    match core.as_ref() {
        Some(c) => s2c(c.store.history_query(c2s(search), limit as i64)),
        None => s2c("[]".into()),
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_history_delete(core: *mut KestrelCore, url: *const c_char) {
    if let Some(c) = core.as_ref() {
        c.store.history_delete(c2s(url));
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_history_clear(core: *mut KestrelCore) {
    if let Some(c) = core.as_ref() {
        c.store.history_clear();
    }
}

/* ============================================================
 * Bookmarks
 * ============================================================ */

/// Returns 1 when the URL is bookmarked after the call, 0 when not.
#[no_mangle]
pub unsafe extern "C" fn kestrel_bookmark_toggle(
    core: *mut KestrelCore,
    url: *const c_char,
    title: *const c_char,
) -> c_int {
    match core.as_ref() {
        Some(c) => c.store.bookmark_toggle(c2s(url), c2s(title)) as c_int,
        None => 0,
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_is_bookmarked(core: *mut KestrelCore, url: *const c_char) -> c_int {
    match core.as_ref() {
        Some(c) => c.store.is_bookmarked(c2s(url)) as c_int,
        None => 0,
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_bookmarks_json(core: *mut KestrelCore) -> *mut c_char {
    match core.as_ref() {
        Some(c) => s2c(c.store.bookmarks_json()),
        None => s2c("[]".into()),
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_bookmark_remove(core: *mut KestrelCore, url: *const c_char) {
    if let Some(c) = core.as_ref() {
        c.store.bookmark_remove(c2s(url));
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_bookmark_rename(
    core: *mut KestrelCore,
    url: *const c_char,
    title: *const c_char,
) {
    if let Some(c) = core.as_ref() {
        c.store.bookmark_rename(c2s(url), c2s(title));
    }
}

/* ============================================================
 * Downloads
 * ============================================================ */

#[no_mangle]
pub unsafe extern "C" fn kestrel_download_add(
    core: *mut KestrelCore,
    id: *const c_char,
    url: *const c_char,
    path: *const c_char,
    filename: *const c_char,
    mime: *const c_char,
    total: i64,
) {
    if let Some(c) = core.as_ref() {
        c.store.download_add(c2s(id), c2s(url), c2s(path), c2s(filename), c2s(mime), total);
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_download_update(
    core: *mut KestrelCore,
    id: *const c_char,
    received: i64,
    total: i64,
    state: *const c_char,
) {
    if let Some(c) = core.as_ref() {
        c.store.download_update(c2s(id), received, total, c2s(state));
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_download_set_path(
    core: *mut KestrelCore,
    id: *const c_char,
    path: *const c_char,
    filename: *const c_char,
) {
    if let Some(c) = core.as_ref() {
        c.store.download_set_path(c2s(id), c2s(path), c2s(filename));
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_downloads_json(core: *mut KestrelCore, limit: c_int) -> *mut c_char {
    match core.as_ref() {
        Some(c) => s2c(c.store.downloads_json(limit as i64)),
        None => s2c("[]".into()),
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_download_remove(core: *mut KestrelCore, id: *const c_char) {
    if let Some(c) = core.as_ref() {
        c.store.download_remove(c2s(id));
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_downloads_clear(core: *mut KestrelCore) {
    if let Some(c) = core.as_ref() {
        c.store.downloads_clear();
    }
}

/* ============================================================
 * Settings KV
 * ============================================================ */

#[no_mangle]
pub unsafe extern "C" fn kestrel_setting_set(
    core: *mut KestrelCore,
    key: *const c_char,
    value: *const c_char,
) {
    if let Some(c) = core.as_ref() {
        c.store.setting_set(c2s(key), c2s(value));
    }
}

/// Null when the key is unset.
#[no_mangle]
pub unsafe extern "C" fn kestrel_setting_get(core: *mut KestrelCore, key: *const c_char) -> *mut c_char {
    match core.as_ref() {
        Some(c) => opt2c(c.store.setting_get(c2s(key))),
        None => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_settings_all(core: *mut KestrelCore) -> *mut c_char {
    match core.as_ref() {
        Some(c) => s2c(c.store.settings_all_json()),
        None => s2c("{}".into()),
    }
}

/* ============================================================
 * Permissions (per host + feature)
 * ============================================================ */

#[no_mangle]
pub unsafe extern "C" fn kestrel_permission_set(
    core: *mut KestrelCore,
    host: *const c_char,
    feature: *const c_char,
    value: *const c_char,
) {
    if let Some(c) = core.as_ref() {
        c.store.permission_set(c2s(host), c2s(feature), c2s(value));
    }
}

/// Null when no stored decision.
#[no_mangle]
pub unsafe extern "C" fn kestrel_permission_get(
    core: *mut KestrelCore,
    host: *const c_char,
    feature: *const c_char,
) -> *mut c_char {
    match core.as_ref() {
        Some(c) => opt2c(c.store.permission_get(c2s(host), c2s(feature))),
        None => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_permissions_json(core: *mut KestrelCore) -> *mut c_char {
    match core.as_ref() {
        Some(c) => s2c(c.store.permissions_json()),
        None => s2c("[]".into()),
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_permission_clear(
    core: *mut KestrelCore,
    host: *const c_char,
    feature: *const c_char,
) {
    if let Some(c) = core.as_ref() {
        c.store.permission_clear(c2s(host), c2s(feature));
    }
}

/* ============================================================
 * Zoom (per host)
 * ============================================================ */

#[no_mangle]
pub unsafe extern "C" fn kestrel_zoom_set(core: *mut KestrelCore, host: *const c_char, level: c_double) {
    if let Some(c) = core.as_ref() {
        c.store.zoom_set(c2s(host), level);
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_zoom_get(core: *mut KestrelCore, host: *const c_char) -> c_double {
    core.as_ref()
        .map(|c| c.store.zoom_get(c2s(host)))
        .unwrap_or(1.0)
}

/* ============================================================
 * Session
 * ============================================================ */

#[no_mangle]
pub unsafe extern "C" fn kestrel_session_save(core: *mut KestrelCore, data: *const c_char) {
    if let Some(c) = core.as_ref() {
        c.store.session_save(c2s(data));
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_session_load(core: *mut KestrelCore) -> *mut c_char {
    match core.as_ref() {
        Some(c) => opt2c(c.store.session_load()),
        None => std::ptr::null_mut(),
    }
}

/* ============================================================
 * Speed dial
 * ============================================================ */

#[no_mangle]
pub unsafe extern "C" fn kestrel_speeddial_json(core: *mut KestrelCore) -> *mut c_char {
    match core.as_ref() {
        Some(c) => s2c(c.store.speeddial_json()),
        None => s2c("[]".into()),
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_speeddial_add(
    core: *mut KestrelCore,
    url: *const c_char,
    title: *const c_char,
) {
    if let Some(c) = core.as_ref() {
        c.store.speeddial_add(c2s(url), c2s(title));
    }
}

#[no_mangle]
pub unsafe extern "C" fn kestrel_speeddial_remove(core: *mut KestrelCore, url: *const c_char) {
    if let Some(c) = core.as_ref() {
        c.store.speeddial_remove(c2s(url));
    }
}

/* ============================================================
 * Utility
 * ============================================================ */

fn host_of(url: &str) -> String {
    if let Some(rest) = url.split("://").nth(1) {
        let host = rest.split('/').next().unwrap_or("");
        host.split('@').next_back().unwrap_or(host).to_string()
    } else {
        url.split('/').next().unwrap_or("").to_string()
    }
}

/// Extract hostname from a URL (used by C++ for display + permissions).
#[no_mangle]
pub unsafe extern "C" fn kestrel_host_of(url: *const c_char) -> *mut c_char {
    s2c(host_of(c2s(url)))
}
