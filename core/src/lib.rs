//! kestrel-core — Rust core of the Kestrel Browser v2.
//! Engine-agnostic: filtering (ads/trackers), storage, privacy engine, settings.
//! The legacy C ABI (used by the v1 Qt frontend) is preserved behind `feature = "capi"`.

mod adb;
mod store;

pub mod privacy;

#[cfg(feature = "capi")]
mod ffi;

pub use adb::FilterEngine;
pub use store::Store;

use std::path::PathBuf;

pub struct KestrelCore {
    pub store: Store,
    pub filters: FilterEngine,
    pub profile_dir: PathBuf,
}

impl KestrelCore {
    /// Open (or create) the profile database and prepare the profile directory.
    pub fn open(profile_dir: &str) -> Option<KestrelCore> {
        if profile_dir.is_empty() {
            return None;
        }
        let _ = std::fs::create_dir_all(profile_dir);
        let db_path = PathBuf::from(profile_dir).join("kestrel.db");
        let store = Store::open(db_path.to_str()?).ok()?;
        Some(KestrelCore {
            store,
            filters: FilterEngine::new(),
            profile_dir: PathBuf::from(profile_dir),
        })
    }

    /// Paths of enabled filter lists, as (id, file path).
    pub fn enabled_filter_paths(&self, resources_dir: &str) -> Vec<(String, String)> {
        let defaults = Self::default_filter_lists(resources_dir);
        defaults
            .into_iter()
            .filter(|(id, path, _)| {
                self.store.filter_list_enabled(id)
                    && std::path::Path::new(path).exists()
            })
            .map(|(id, path, _)| (id, path))
            .collect()
    }

    /// (id, path, human name) of the filter lists we ship.
    pub fn default_filter_lists(resources_dir: &str) -> Vec<(String, String, String)> {
        vec![
            (
                "easylist".into(),
                format!("{resources_dir}/easylist.txt"),
                "EasyList (ads)".into(),
            ),
            (
                "easyprivacy".into(),
                format!("{resources_dir}/easyprivacy.txt"),
                "EasyPrivacy (trackers)".into(),
            ),
        ]
    }

    pub fn reload_filters(&self, resources_dir: &str) -> Result<(), String> {
        let paths = self.enabled_filter_paths(resources_dir);
        let custom = self
            .store
            .setting_get("shield.custom_rules")
            .unwrap_or_default();
        let cache = self.profile_dir.join("filters.cache");
        self.filters.reload(
            &self.store,
            &paths,
            &custom,
            cache.to_str().unwrap_or("filters.cache"),
        )
    }

    pub fn setting_bool(&self, key: &str, default: bool) -> bool {
        self.store
            .setting_get(key)
            .map(|v| v == "1" || v == "true")
            .unwrap_or(default)
    }

    pub fn setting_str(&self, key: &str, default: &str) -> String {
        self.store
            .setting_get(key)
            .filter(|v| !v.is_empty())
            .unwrap_or_else(|| default.to_string())
    }
}
