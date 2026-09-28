//! Kestrel filtering engine: Brave's adblock-rust engine + multi-list
//! management + custom rules + per-host blocking statistics.

use crate::store::Store;
use adblock::Engine;
use adblock::lists::{FilterFormat, ParseOptions, RuleTypes};
use adblock::request::Request;
use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Mutex;

pub struct FilterEngine {
    engine: Mutex<Engine>,
    /// Per-host blocked counts since startup (flushed to sqlite periodically)
    pub host_counts: Mutex<HashMap<String, u64>>,
    pub session_total: AtomicU64,
    pub source_hash: Mutex<u64>,
}

/// Built-in filter lists shipped with the browser.
pub const BUILTIN_LISTS: &[(&str, &str, &str)] = &[
    ("easylist", "EasyList", "Removes ads from webpages (primary ad filter)"),
    ("easyprivacy", "EasyPrivacy", "Blocks tracking scripts and beacons"),
    ("annoyances", "uBlock Annoyances", "Blocks cookie banners, popups and nag screens"),
];

fn parse_options() -> ParseOptions {
    ParseOptions {
        format: FilterFormat::Standard,
        rule_types: RuleTypes::All,
        permissions: Default::default(),
    }
}

fn fnv1a(data: &[u8]) -> u64 {
    let mut h: u64 = 0xcbf29ce484222325;
    for &b in data {
        h ^= b as u64;
        h = h.wrapping_mul(0x100000001b3);
    }
    h
}

impl FilterEngine {
    pub fn new() -> Self {
        FilterEngine {
            engine: Mutex::new(Engine::from_rules(Vec::<String>::new(), parse_options())),
            host_counts: Mutex::new(HashMap::new()),
            session_total: AtomicU64::new(0),
            source_hash: Mutex::new(0),
        }
    }

    /// Rebuild the engine from enabled list files + custom rules.
    /// Writes a serialized cache so subsequent startups are instant.
    pub fn reload(&self, store: &Store, list_paths: &[(String, String)], custom_rules: &str, cache_path: &str) -> Result<(), String> {
        // hash of all sources to know if cache is valid
        let mut hash = fnv1a(custom_rules.as_bytes());
        let mut rule_count = 0usize;
        let mut rules: Vec<String> = Vec::with_capacity(80_000);
        for (_id, path) in list_paths {
            match std::fs::read(path) {
                Ok(bytes) => {
                    hash ^= fnv1a(bytes.as_slice()).rotate_left(17);
                    let text = String::from_utf8_lossy(&bytes);
                    for line in text.lines() {
                        let line = line.trim();
                        if line.is_empty() || line.starts_with('!') || line.starts_with("[Adblock") {
                            continue;
                        }
                        rules.push(line.to_string());
                        rule_count += 1;
                        if rules.len() >= 400_000 {
                            break;
                        }
                    }
                }
                Err(_) => continue,
            }
        }
        for line in custom_rules.lines() {
            let line = line.trim();
            if !line.is_empty() && !line.starts_with('!') {
                rules.push(line.to_string());
                rule_count += 1;
            }
        }

        // Fast path: serialized engine cache valid for identical sources
        let hash_path = format!("{}.hash", cache_path);
        let cache_valid = std::fs::read(&hash_path)
            .ok()
            .and_then(|b| String::from_utf8(b).ok())
            .map(|s| s.trim().parse::<u64>().ok() == Some(hash))
            .unwrap_or(false);
        if cache_valid && self.init_from_cache(cache_path) {
            *self.source_hash.lock().unwrap() = hash;
            return Ok(());
        }

        let engine = Engine::from_rules(rules.iter().map(|s| s.as_str()), parse_options());
        // Persist compiled engine for fast startup
        if let Ok(bytes) = engine.serialize_raw() {
            let _ = std::fs::write(cache_path, &bytes);
            let _ = std::fs::write(&hash_path, format!("{}", hash));
        }
        *self.source_hash.lock().unwrap() = hash;
        *self.engine.lock().unwrap() = engine;
        Ok(())
    }

    /// Fast-path init from a previously serialized engine cache.
    pub fn init_from_cache(&self, cache_path: &str) -> bool {
        if let Ok(cache) = std::fs::read(cache_path) {
            let mut engine = Engine::new(false);
            if engine.deserialize(&cache).is_ok() {
                *self.engine.lock().unwrap() = engine;
                return true;
            }
        }
        false
    }

    /// Network request check. Returns true when the request should be blocked.
    /// Empty source_url (top-level navigation) is treated as first-party.
    pub fn check(&self, url: &str, source_url: &str, req_type: &str) -> bool {
        let source = if source_url.is_empty() { url } else { source_url };
        let req = match Request::new(url, source, req_type) {
            Ok(r) => r,
            Err(_) => return false,
        };
        let engine = self.engine.lock().unwrap();
        let result = engine.check_network_request(&req);
        result.matched
    }

    /// Cosmetic CSS for a page (hide selectors).
    pub fn cosmetic_css(&self, url: &str) -> Option<(String, String)> {
        let engine = self.engine.lock().unwrap();
        let res = engine.url_cosmetic_resources(url);
        if res.hide_selectors.is_empty() && res.injected_script.is_empty() {
            return None;
        }
        let mut selectors: Vec<&String> = res.hide_selectors.iter().collect();
        selectors.sort();
        let mut css = String::new();
        for sel in &selectors {
            css.push_str(sel);
            css.push_str("{display:none !important;visibility:hidden !important;z-index:-2147483647 !important;}");
        }
        if !css.is_empty() {
            css = format!("/*Kestrel Cosmetic*/{}", css);
        }
        Some((css, res.injected_script))
    }

    pub fn record_block(&self, host: &str) {
        self.session_total.fetch_add(1, Ordering::Relaxed);
        let mut map = self.host_counts.lock().unwrap();
        *map.entry(host.to_string()).or_insert(0) += 1;
    }

    pub fn total_this_session(&self) -> u64 {
        self.session_total.load(Ordering::Relaxed)
    }

    pub fn drain_host_counts(&self) -> HashMap<String, u64> {
        let mut map = self.host_counts.lock().unwrap();
        std::mem::take(&mut map)
    }
}
