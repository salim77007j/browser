//! kestrel-core storage: SQLite-backed history, bookmarks, downloads,
//! settings, permissions, per-host zoom, sessions, speed dial, stats.

use rusqlite::{Connection, params};
use std::sync::Mutex;

pub struct Store {
    conn: Mutex<Connection>,
}

fn now() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}

impl Store {
    pub fn open(path: &str) -> Result<Store, String> {
        let conn = Connection::open(path).map_err(|e| e.to_string())?;
        conn.pragma_update(None, "journal_mode", "WAL").ok();
        conn.pragma_update(None, "synchronous", "NORMAL").ok();
        conn.execute_batch(
            "CREATE TABLE IF NOT EXISTS history(
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                url TEXT NOT NULL,
                title TEXT NOT NULL DEFAULT '',
                host TEXT NOT NULL DEFAULT '',
                last_visit INTEGER NOT NULL,
                visit_count INTEGER NOT NULL DEFAULT 1);
             CREATE UNIQUE INDEX IF NOT EXISTS idx_history_url ON history(url);
             CREATE INDEX IF NOT EXISTS idx_history_visit ON history(last_visit DESC);
             CREATE TABLE IF NOT EXISTS bookmarks(
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                url TEXT NOT NULL UNIQUE,
                title TEXT NOT NULL DEFAULT '',
                folder TEXT NOT NULL DEFAULT 'Bookmarks Bar',
                added_at INTEGER NOT NULL);
             CREATE TABLE IF NOT EXISTS downloads(
                id TEXT PRIMARY KEY,
                url TEXT NOT NULL,
                path TEXT NOT NULL DEFAULT '',
                filename TEXT NOT NULL DEFAULT '',
                mime TEXT NOT NULL DEFAULT '',
                total INTEGER NOT NULL DEFAULT 0,
                received INTEGER NOT NULL DEFAULT 0,
                state TEXT NOT NULL DEFAULT 'in_progress',
                started_at INTEGER NOT NULL);
             CREATE TABLE IF NOT EXISTS settings(
                key TEXT PRIMARY KEY,
                value TEXT NOT NULL DEFAULT '');
             CREATE TABLE IF NOT EXISTS permissions(
                host TEXT NOT NULL,
                feature TEXT NOT NULL,
                value TEXT NOT NULL,
                PRIMARY KEY(host, feature));
             CREATE TABLE IF NOT EXISTS zooms(
                host TEXT PRIMARY KEY,
                level REAL NOT NULL DEFAULT 1.0);
             CREATE TABLE IF NOT EXISTS sessions(
                id INTEGER PRIMARY KEY CHECK (id = 1),
                data TEXT NOT NULL,
                saved_at INTEGER NOT NULL);
             CREATE TABLE IF NOT EXISTS speeddial(
                url TEXT PRIMARY KEY,
                title TEXT NOT NULL DEFAULT '',
                added_at INTEGER NOT NULL);
             CREATE TABLE IF NOT EXISTS block_stats(
                host TEXT PRIMARY KEY,
                count INTEGER NOT NULL DEFAULT 0,
                last INTEGER NOT NULL DEFAULT 0);
             CREATE TABLE IF NOT EXISTS filter_lists(
                id TEXT PRIMARY KEY,
                enabled INTEGER NOT NULL DEFAULT 1,
                last_updated INTEGER NOT NULL DEFAULT 0);",
        )
        .map_err(|e| e.to_string())?;
        Ok(Store { conn: Mutex::new(conn) })
    }

    /* ---------------- history ---------------- */

    pub fn history_add(&self, url: &str, title: &str, host: &str) {
        let t = now();
        let _ = self.conn.lock().unwrap().execute(
            "INSERT INTO history(url, title, host, last_visit, visit_count)
             VALUES(?1, ?2, ?3, ?4, 1)
             ON CONFLICT(url) DO UPDATE SET
               title = CASE WHEN ?2 != '' THEN ?2 ELSE history.title END,
               last_visit = ?4,
               visit_count = visit_count + 1",
            params![url, title, host, t],
        );
    }

    pub fn history_query(&self, search: &str, limit: i64) -> String {
        let conn = self.conn.lock().unwrap();
        let pattern = format!("%{}%", search.replace('%', ""));
        let mut stmt = match conn.prepare(
            "SELECT url, title, host, last_visit, visit_count FROM history
             WHERE (?1 = '%%' OR url LIKE ?1 OR title LIKE ?1 OR host LIKE ?1)
             ORDER BY last_visit DESC LIMIT ?2",
        ) {
            Ok(s) => s,
            Err(_) => return "[]".into(),
        };
        let rows = stmt.query_map(params![pattern, limit], |r| {
            Ok(serde_json::json!({
                "url": r.get::<_, String>(0).unwrap_or_default(),
                "title": r.get::<_, String>(1).unwrap_or_default(),
                "host": r.get::<_, String>(2).unwrap_or_default(),
                "lastVisit": r.get::<_, i64>(3).unwrap_or(0),
                "visits": r.get::<_, i64>(4).unwrap_or(0),
            }))
        });
        match rows {
            Ok(it) => {
                let items: Vec<_> = it.filter_map(|x| x.ok()).collect();
                serde_json::to_string(&items).unwrap_or_else(|_| "[]".into())
            }
            Err(_) => "[]".into(),
        }
    }

    pub fn history_delete(&self, url: &str) {
        let _ = self.conn.lock().unwrap().execute(
            "DELETE FROM history WHERE url = ?1",
            params![url],
        );
    }

    pub fn history_clear(&self) {
        let _ = self.conn.lock().unwrap().execute("DELETE FROM history", []);
    }

    /* ---------------- bookmarks ---------------- */

    pub fn bookmark_toggle(&self, url: &str, title: &str) -> bool {
        let conn = self.conn.lock().unwrap();
        let exists: bool = conn
            .query_row("SELECT 1 FROM bookmarks WHERE url = ?1", params![url], |_| Ok(true))
            .unwrap_or(false);
        if exists {
            let _ = conn.execute("DELETE FROM bookmarks WHERE url = ?1", params![url]);
            false
        } else {
            let _ = conn.execute(
                "INSERT OR IGNORE INTO bookmarks(url, title, folder, added_at) VALUES(?1, ?2, 'Bookmarks Bar', ?3)",
                params![url, title, now()],
            );
            true
        }
    }

    pub fn is_bookmarked(&self, url: &str) -> bool {
        self.conn
            .lock()
            .unwrap()
            .query_row("SELECT 1 FROM bookmarks WHERE url = ?1", params![url], |_| Ok(true))
            .unwrap_or(false)
    }

    pub fn bookmarks_json(&self) -> String {
        let conn = self.conn.lock().unwrap();
        let mut stmt = match conn.prepare(
            "SELECT url, title, folder, added_at FROM bookmarks ORDER BY added_at DESC",
        ) {
            Ok(s) => s,
            Err(_) => return "[]".into(),
        };
        let rows = stmt.query_map([], |r| {
            Ok(serde_json::json!({
                "url": r.get::<_, String>(0).unwrap_or_default(),
                "title": r.get::<_, String>(1).unwrap_or_default(),
                "folder": r.get::<_, String>(2).unwrap_or_default(),
                "addedAt": r.get::<_, i64>(3).unwrap_or(0),
            }))
        });
        match rows {
            Ok(it) => {
                let items: Vec<_> = it.filter_map(|x| x.ok()).collect();
                serde_json::to_string(&items).unwrap_or_else(|_| "[]".into())
            }
            Err(_) => "[]".into(),
        }
    }

    pub fn bookmark_remove(&self, url: &str) {
        let _ = self.conn.lock().unwrap().execute(
            "DELETE FROM bookmarks WHERE url = ?1",
            params![url],
        );
    }

    pub fn bookmark_rename(&self, url: &str, title: &str) {
        let _ = self.conn.lock().unwrap().execute(
            "UPDATE bookmarks SET title = ?2 WHERE url = ?1",
            params![url, title],
        );
    }

    /* ---------------- downloads ---------------- */

    pub fn download_add(&self, id: &str, url: &str, path: &str, filename: &str, mime: &str, total: i64) {
        let _ = self.conn.lock().unwrap().execute(
            "INSERT OR REPLACE INTO downloads(id, url, path, filename, mime, total, received, state, started_at)
             VALUES(?1, ?2, ?3, ?4, ?5, ?6, 0, 'in_progress', ?7)",
            params![id, url, path, filename, mime, total, now()],
        );
    }

    pub fn download_update(&self, id: &str, received: i64, total: i64, state: &str) {
        let _ = self.conn.lock().unwrap().execute(
            "UPDATE downloads SET received = ?2, total = MAX(total, ?3), state = ?4 WHERE id = ?1",
            params![id, received, total, state],
        );
    }

    pub fn download_set_path(&self, id: &str, path: &str, filename: &str) {
        let _ = self.conn.lock().unwrap().execute(
            "UPDATE downloads SET path = ?2, filename = ?3 WHERE id = ?1",
            params![id, path, filename],
        );
    }

    pub fn downloads_json(&self, limit: i64) -> String {
        let conn = self.conn.lock().unwrap();
        let mut stmt = match conn.prepare(
            "SELECT id, url, path, filename, mime, total, received, state, started_at
             FROM downloads ORDER BY started_at DESC LIMIT ?1",
        ) {
            Ok(s) => s,
            Err(_) => return "[]".into(),
        };
        let rows = stmt.query_map(params![limit], |r| {
            Ok(serde_json::json!({
                "id": r.get::<_, String>(0).unwrap_or_default(),
                "url": r.get::<_, String>(1).unwrap_or_default(),
                "path": r.get::<_, String>(2).unwrap_or_default(),
                "filename": r.get::<_, String>(3).unwrap_or_default(),
                "mime": r.get::<_, String>(4).unwrap_or_default(),
                "total": r.get::<_, i64>(5).unwrap_or(0),
                "received": r.get::<_, i64>(6).unwrap_or(0),
                "state": r.get::<_, String>(7).unwrap_or_default(),
                "startedAt": r.get::<_, i64>(8).unwrap_or(0),
            }))
        });
        match rows {
            Ok(it) => {
                let items: Vec<_> = it.filter_map(|x| x.ok()).collect();
                serde_json::to_string(&items).unwrap_or_else(|_| "[]".into())
            }
            Err(_) => "[]".into(),
        }
    }

    pub fn download_remove(&self, id: &str) {
        let _ = self.conn.lock().unwrap().execute("DELETE FROM downloads WHERE id = ?1", params![id]);
    }

    pub fn downloads_clear(&self) {
        let _ = self.conn.lock().unwrap().execute("DELETE FROM downloads", []);
    }

    /* ---------------- settings KV ---------------- */

    pub fn setting_set(&self, key: &str, value: &str) {
        let _ = self.conn.lock().unwrap().execute(
            "INSERT INTO settings(key, value) VALUES(?1, ?2)
             ON CONFLICT(key) DO UPDATE SET value = ?2",
            params![key, value],
        );
    }

    pub fn setting_get(&self, key: &str) -> Option<String> {
        self.conn
            .lock()
            .unwrap()
            .query_row("SELECT value FROM settings WHERE key = ?1", params![key], |r| {
                r.get::<_, String>(0)
            })
            .ok()
    }

    pub fn settings_all_json(&self) -> String {
        let conn = self.conn.lock().unwrap();
        let mut stmt = match conn.prepare("SELECT key, value FROM settings") {
            Ok(s) => s,
            Err(_) => return "{}".into(),
        };
        let mut map = serde_json::Map::new();
        if let Ok(rows) = stmt.query_map([], |r| {
            Ok((r.get::<_, String>(0)?, r.get::<_, String>(1)?))
        }) {
            for row in rows.flatten() {
                map.insert(row.0, serde_json::Value::String(row.1));
            }
        }
        serde_json::Value::Object(map).to_string()
    }

    /* ---------------- permissions ---------------- */

    pub fn permission_set(&self, host: &str, feature: &str, value: &str) {
        let _ = self.conn.lock().unwrap().execute(
            "INSERT INTO permissions(host, feature, value) VALUES(?1, ?2, ?3)
             ON CONFLICT(host, feature) DO UPDATE SET value = ?3",
            params![host, feature, value],
        );
    }

    pub fn permission_get(&self, host: &str, feature: &str) -> Option<String> {
        self.conn
            .lock()
            .unwrap()
            .query_row(
                "SELECT value FROM permissions WHERE host = ?1 AND feature = ?2",
                params![host, feature],
                |r| r.get::<_, String>(0),
            )
            .ok()
    }

    pub fn permissions_json(&self) -> String {
        let conn = self.conn.lock().unwrap();
        let mut stmt = match conn.prepare("SELECT host, feature, value FROM permissions") {
            Ok(s) => s,
            Err(_) => return "[]".into(),
        };
        let rows = stmt.query_map([], |r| {
            Ok(serde_json::json!({
                "host": r.get::<_, String>(0).unwrap_or_default(),
                "feature": r.get::<_, String>(1).unwrap_or_default(),
                "value": r.get::<_, String>(2).unwrap_or_default(),
            }))
        });
        match rows {
            Ok(it) => {
                let items: Vec<_> = it.filter_map(|x| x.ok()).collect();
                serde_json::to_string(&items).unwrap_or_else(|_| "[]".into())
            }
            Err(_) => "[]".into(),
        }
    }

    pub fn permission_clear(&self, host: &str, feature: &str) {
        let _ = self.conn.lock().unwrap().execute(
            "DELETE FROM permissions WHERE host = ?1 AND feature = ?2",
            params![host, feature],
        );
    }

    /* ---------------- zoom ---------------- */

    pub fn zoom_set(&self, host: &str, level: f64) {
        let _ = self.conn.lock().unwrap().execute(
            "INSERT INTO zooms(host, level) VALUES(?1, ?2)
             ON CONFLICT(host) DO UPDATE SET level = ?2",
            params![host, level],
        );
    }

    pub fn zoom_get(&self, host: &str) -> f64 {
        self.conn
            .lock()
            .unwrap()
            .query_row("SELECT level FROM zooms WHERE host = ?1", params![host], |r| {
                r.get::<_, f64>(0)
            })
            .unwrap_or(1.0)
    }

    /* ---------------- session ---------------- */

    pub fn session_save(&self, data: &str) {
        let _ = self.conn.lock().unwrap().execute(
            "INSERT INTO sessions(id, data, saved_at) VALUES(1, ?1, ?2)
             ON CONFLICT(id) DO UPDATE SET data = ?1, saved_at = ?2",
            params![data, now()],
        );
    }

    pub fn session_load(&self) -> Option<String> {
        self.conn
            .lock()
            .unwrap()
            .query_row("SELECT data FROM sessions WHERE id = 1", [], |r| r.get::<_, String>(0))
            .ok()
    }

    /* ---------------- speed dial ---------------- */

    pub fn speeddial_json(&self) -> String {
        let conn = self.conn.lock().unwrap();
        let mut stmt = match conn.prepare(
            "SELECT url, title, added_at FROM speeddial ORDER BY added_at DESC LIMIT 16",
        ) {
            Ok(s) => s,
            Err(_) => return "[]".into(),
        };
        let rows = stmt.query_map([], |r| {
            Ok(serde_json::json!({
                "url": r.get::<_, String>(0).unwrap_or_default(),
                "title": r.get::<_, String>(1).unwrap_or_default(),
                "addedAt": r.get::<_, i64>(2).unwrap_or(0),
            }))
        });
        match rows {
            Ok(it) => {
                let items: Vec<_> = it.filter_map(|x| x.ok()).collect();
                serde_json::to_string(&items).unwrap_or_else(|_| "[]".into())
            }
            Err(_) => "[]".into(),
        }
    }

    pub fn speeddial_add(&self, url: &str, title: &str) {
        let _ = self.conn.lock().unwrap().execute(
            "INSERT OR IGNORE INTO speeddial(url, title, added_at) VALUES(?1, ?2, ?3)",
            params![url, title, now()],
        );
    }

    pub fn speeddial_remove(&self, url: &str) {
        let _ = self.conn.lock().unwrap().execute("DELETE FROM speeddial WHERE url = ?1", params![url]);
    }

    /* ---------------- block stats (persisted) ---------------- */

    pub fn stats_flush(&self, by_host: &std::collections::HashMap<String, u64>) {
        let conn = self.conn.lock().unwrap();
        for (host, cnt) in by_host {
            let _ = conn.execute(
                "INSERT INTO block_stats(host, count, last) VALUES(?1, ?2, ?3)
                 ON CONFLICT(host) DO UPDATE SET count = count + ?2, last = ?3",
                params![host, *cnt as i64, now()],
            );
        }
    }

    pub fn stats_json(&self) -> String {
        let conn = self.conn.lock().unwrap();
        let total: i64 = conn
            .query_row("SELECT COALESCE(SUM(count),0) FROM block_stats", [], |r| r.get(0))
            .unwrap_or(0);
        let mut stmt = match conn.prepare(
            "SELECT host, count, last FROM block_stats ORDER BY count DESC LIMIT 20",
        ) {
            Ok(s) => s,
            Err(_) => return format!("{{\"total\":{}}}", total),
        };
        let mut map = serde_json::Map::new();
        if let Ok(rows) = stmt.query_map([], |r| {
            Ok((
                r.get::<_, String>(0)?,
                r.get::<_, i64>(1)?,
                r.get::<_, i64>(2)?,
            ))
        }) {
            for (host, count, last) in rows.flatten() {
                map.insert(
                    host,
                    serde_json::json!({"count": count, "last": last}),
                );
            }
        }
        serde_json::json!({"total": total, "byHost": map}).to_string()
    }

    pub fn stats_clear(&self) {
        let _ = self.conn.lock().unwrap().execute("DELETE FROM block_stats", []);
    }

    /* ---------------- filter list metadata ---------------- */

    pub fn filter_list_set_enabled(&self, id: &str, enabled: bool) {
        let _ = self.conn.lock().unwrap().execute(
            "INSERT INTO filter_lists(id, enabled, last_updated) VALUES(?1, ?2, 0)
             ON CONFLICT(id) DO UPDATE SET enabled = ?2",
            params![id, enabled as i64],
        );
    }

    pub fn filter_list_set_updated(&self, id: &str, ts: i64) {
        let _ = self.conn.lock().unwrap().execute(
            "INSERT INTO filter_lists(id, enabled, last_updated) VALUES(?1, 1, ?2)
             ON CONFLICT(id) DO UPDATE SET last_updated = ?2",
            params![id, ts],
        );
    }

    pub fn filter_list_enabled(&self, id: &str) -> bool {
        self.conn
            .lock()
            .unwrap()
            .query_row(
                "SELECT enabled FROM filter_lists WHERE id = ?1",
                params![id],
                |r| r.get::<_, i64>(0),
            )
            .map(|v| v != 0)
            .unwrap_or(true)
    }

    pub fn filter_lists_json(&self, defaults: &[(&str, &str, &str)]) -> String {
        let conn = self.conn.lock().unwrap();
        let mut arr = Vec::new();
        for (id, name, desc) in defaults {
            let enabled: i64 = conn
                .query_row(
                    "SELECT enabled FROM filter_lists WHERE id = ?1",
                    params![id],
                    |r| r.get(0),
                )
                .unwrap_or(1);
            arr.push(serde_json::json!({
                "id": id, "name": name, "description": desc, "enabled": enabled != 0
            }));
        }
        serde_json::to_string(&arr).unwrap_or_else(|_| "[]".into())
    }
}
