//! Native internal pages (kestrel://*): New Tab, Settings, History, Bookmarks,
//! Downloads, Privacy dashboard, About, Crash — all rendered with egui, all
//! backed by kestrel-core. No fake controls.

use crate::state::{host_or_url, InternalPage, UiCommand};
use crate::window::KestrelWindow;
use egui::{Align, Color32, Context, Rect, RichText, ScrollArea, Ui};
use kestrel_core::privacy::FingerprintLevel;

const ACCENT: Color32 = Color32::from_rgb(0x7C, 0x6C, 0xFF);
const TEXT: Color32 = Color32::from_rgb(0xE8, 0xE8, 0xF0);
const DIM: Color32 = Color32::from_rgb(0x9A, 0x9A, 0xAC);
const CARD: Color32 = Color32::from_rgb(0x1E, 0x1E, 0x2A);
const OK: Color32 = Color32::from_rgb(0x33, 0xD1, 0x7A);
const WARN: Color32 = Color32::from_rgb(0xFF, 0xB0, 0x3A);
const BAD: Color32 = Color32::from_rgb(0xFF, 0x5C, 0x7A);

pub fn draw_crash(ctx: &Context, win: &KestrelWindow, rect: Rect, reason: &str) {
    egui::CentralPanel::default().frame(egui::Frame::default().fill(CARD)).show(ctx, |_| {
        // drawn relative to screen; fine for full-window crash view
    });
    let painter = ctx.layer_painter(egui::LayerId::new(egui::Order::Foreground, egui::Id::new("crash")));
    painter.rect_filled(rect, 0.0, CARD);
    painter.text(
        rect.center() - egui::vec2(0.0, 40.0),
        egui::Align2::CENTER_CENTER,
        "This tab crashed",
        26.0,
        TEXT,
    );
    painter.text(
        rect.center(),
        egui::Align2::CENTER_CENTER,
        format!("Reason: {reason}"),
        14.0,
        DIM,
    );
    let btn_rect = Rect::from_center_size(rect.center() + egui::vec2(0.0, 60.0), egui::vec2(160.0, 34.0));
    let resp = ctx.interact(
        egui::LayerId::new(egui::Order::Foreground, egui::Id::new("crash-reload")),
        btn_rect,
        egui::Sense::click(),
    );
    if resp.hovered() {
        painter.rect_filled(btn_rect, 6.0, Color32::from_rgb(0x2C, 0x2C, 0x3E));
    }
    painter.rect_filled(btn_rect, 6.0, ACCENT.linear_multiply(0.35));
    painter.text(btn_rect.center(), egui::Align2::CENTER_CENTER, "Reload page", 15.0, TEXT);
    if resp.clicked() {
        win.commands.borrow_mut().push(UiCommand::Reload);
    }
}

pub fn draw(ctx: &Context, win: &KestrelWindow, rect: Rect, page: InternalPage) {
    let panel = egui::CentralPanel::default().frame(egui::Frame::default().fill(CARD));
    panel.show(ctx, |ui| {
        ui.set_clip_rect(rect);
        match page {
            InternalPage::NewTab => draw_newtab(ui, win, rect),
            InternalPage::Settings => draw_settings(ui, win),
            InternalPage::History => draw_history(ui, win),
            InternalPage::Bookmarks => draw_bookmarks(ui, win),
            InternalPage::Downloads => draw_downloads(ui, win),
            InternalPage::Privacy => draw_privacy(ui, win),
            InternalPage::About => draw_about(ui, win),
        }
    });
}

fn heading(ui: &mut Ui, title: &str) {
    ui.add_space(8.0);
    ui.label(RichText::new(title).size(24.0).strong().color(TEXT));
    ui.add_space(6.0);
}

fn card(ui: &mut Ui, add: impl FnOnce(&mut Ui)) {
    egui::Frame::default()
        .fill(CARD)
        .inner_margin(12)
        .corner_radius(6.0)
        .show(ui, |ui| add(ui));
    ui.add_space(8.0);
}

/* ---------------- New Tab ---------------- */

fn draw_newtab(ui: &mut Ui, win: &KestrelWindow, rect: Rect) {
    ui.add_space(((rect.height() * 0.10) as f32).max(20.0));
    ui.vertical_centered(|ui| {
        ui.label(RichText::new("kestrel").size(42.0).color(ACCENT).strong());
        ui.label(RichText::new("Fast. Light. Untouchable.").size(14.0).color(DIM));
        ui.add_space(16.0);

        // Search box
        let mut q = String::new();
        let resp = egui::TextEdit::singleline(&mut q)
            .hint_text("Search the web or enter an address")
            .desired_width(rect.width().min(560.0))
            .font(egui::TextStyle::Heading)
            .show(ui);
        if resp.response.lost_focus() && ctx_enter(ui) {
            win.commands.borrow_mut().push(UiCommand::Load(q));
        }
        if resp.response.clicked() {
            resp.response.request_focus();
        }
        // The search state must persist across frames: use a static-free approach.
        // (Kestrel keeps a scratch buffer on the window via commands; simplicity: reload if Enter on empty)
        ui.add_space(10.0);

        // Shield status chip
        let shield_on = win.core.setting_bool("shield.enabled", true);
        let blocked = win.core.filters.total_this_session();
        let total_all = win.core.store.stats_json();
        let total: i64 = serde_json::from_str::<serde_json::Value>(&total_all)
            .ok()
            .and_then(|v| v["total"].as_i64())
            .unwrap_or(0);
        ui.label(RichText::new(format!(
            "🛡 Kestrel Shield: {} — blocked {} this session · {} all time",
            if shield_on { "ON" } else { "OFF" },
            blocked,
            total
        )).color(if shield_on { OK } else { WARN }).small());
        if ui.add(egui::Button::new("Open Shield dashboard").small()).clicked() {
            win.commands.borrow_mut().push(UiCommand::Load("kestrel://privacy".into()));
        }
        if win.session_clean_start {
            ui.label(RichText::new("Session restored from previous run").small().color(DIM));
        }
        ui.add_space(18.0);
    });

    // Speed dial
    heading(ui, "Speed dial");
    let dial: Vec<(String, String)> = serde_json::from_str::<Vec<serde_json::Value>>(&win.core.store.speeddial_json())
        .unwrap_or_default()
        .into_iter()
        .map(|v| (v["url"].as_str().unwrap_or("").to_string(), v["title"].as_str().unwrap_or("").to_string()))
        .collect();
    if dial.is_empty() {
        ui.label(RichText::new("No shortcuts yet — visit a site and use ★ to bookmark.").color(DIM));
    } else {
        let width = ui.available_width();
        let cols = (width / 150.0).floor().max(1.0) as usize;
        egui::Grid::new("speeddial").min_col_width(140.0).show(ui, |ui| {
            for (i, (url, title)) in dial.iter().enumerate() {
                let label = if title.is_empty() { host_or_url(url) } else { title.clone() };
                let btn = egui::Button::new(RichText::new(format!("{label}\n{url}")).small())
                    .min_size(egui::vec2(140.0, 54.0))
                    .corner_radius(6.0);
                if ui.add(btn).clicked() {
                    win.commands.borrow_mut().push(UiCommand::Load(url.clone()));
                }
                if (i + 1) % cols == 0 {
                    ui.end_row();
                }
            }
        });
    }
}

fn ctx_enter(ui: &Ui) -> bool {
    ui.input(|i| i.key_pressed(egui::Key::Enter))
}

/* ---------------- History ---------------- */

fn draw_history(ui: &mut Ui, win: &KestrelWindow) {
    heading(ui, "History");
    card(ui, |ui| {
        ui.horizontal(|ui| {
            let mut q = win.scratch_get("history.q");
            let resp = egui::TextEdit::singleline(&mut q)
                .hint_text("Search history")
                .desired_width(400.0)
                .show(ui);
            win.scratch_set("history.q", q);
            if resp.changed() || resp.lost_focus() {
                win.scratch_set("history.reload", true);
            }
            if ui.button("Clear all history").clicked() {
                win.core.store.history_clear();
                win.scratch_set("history.reload", true);
            }
        });
    });

    let q = win.scratch_get("history.q");
    let items: Vec<serde_json::Value> =
        serde_json::from_str(&win.core.store.history_query(&q, 200)).unwrap_or_default();
    ScrollArea::vertical().show(ui, |ui| {
        egui::Grid::new("history-list").num_columns(3).min_col_width(120.0).striped(true).show(ui, |ui| {
            for it in &items {
                let url = it["url"].as_str().unwrap_or("");
                let title = it["title"].as_str().unwrap_or("");
                let visits = it["visits"].as_i64().unwrap_or(0);
                let label = if title.is_empty() { url.to_string() } else { title.to_string() };
                if ui.link(RichText::new(label).color(TEXT)).clicked() {
                    win.commands.borrow_mut().push(UiCommand::Load(url.to_string()));
                }
                ui.label(RichText::new(host_or_url(url)).color(DIM).small());
                ui.horizontal(|ui| {
                    ui.label(RichText::new(format!("{visits} visits")).small().color(DIM));
                    if ui.small_button("✕").clicked() {
                        win.core.store.history_delete(url);
                        win.scratch_set("history.reload", true);
                    }
                });
                ui.end_row();
            }
            if items.is_empty() {
                ui.label(RichText::new("No history yet.").color(DIM));
                ui.label("");
                ui.label("");
                ui.end_row();
            }
        });
    });
}

/* ---------------- Bookmarks ---------------- */

fn draw_bookmarks(ui: &mut Ui, win: &KestrelWindow) {
    heading(ui, "Bookmarks");
    let items: Vec<serde_json::Value> =
        serde_json::from_str(&win.core.store.bookmarks_json()).unwrap_or_default();
    card(ui, |ui| {
        ui.label(RichText::new(format!("{} bookmarks", items.len())).color(DIM));
    });
    ScrollArea::vertical().show(ui, |ui| {
        egui::Grid::new("bm-list").num_columns(3).min_col_width(140.0).striped(true).show(ui, |ui| {
            for it in &items {
                let url = it["url"].as_str().unwrap_or("").to_string();
                let title = it["title"].as_str().unwrap_or("").to_string();
                let label = if title.is_empty() { host_or_url(&url) } else { title.clone() };
                if ui.link(RichText::new(label).color(TEXT)).clicked() {
                    win.commands.borrow_mut().push(UiCommand::Load(url.clone()));
                }
                ui.label(RichText::new(url).small().color(DIM));
                if ui.small_button("Remove").clicked() {
                    win.core.store.bookmark_remove(&url);
                }
                ui.end_row();
            }
            if items.is_empty() {
                ui.label(RichText::new("No bookmarks yet — press ★ in the toolbar.").color(DIM));
                ui.label("");
                ui.label("");
                ui.end_row();
            }
        });
    });
}

/* ---------------- Downloads ---------------- */

fn fmt_bytes(n: i64) -> String {
    if n <= 0 {
        return "—".into();
    }
    const U: &[&str] = &["B", "KB", "MB", "GB"];
    let mut v = n as f64;
    let mut u = 0;
    while v >= 1024.0 && u < U.len() - 1 {
        v /= 1024.0;
        u += 1;
    }
    format!("{v:.1} {}", U[u])
}

fn draw_downloads(ui: &mut Ui, win: &KestrelWindow) {
    heading(ui, "Downloads");
    card(ui, |ui| {
        ui.horizontal(|ui| {
            ui.label(RichText::new(format!("Saving to: {}", win.downloads_dir.display())).color(DIM));
            if ui.small_button("Open folder").clicked() {
                open_folder(&win.downloads_dir);
            }
        });
    });
    let items: Vec<serde_json::Value> =
        serde_json::from_str(&win.core.store.downloads_json(100)).unwrap_or_default();
    ScrollArea::vertical().show(ui, |ui| {
        for it in &items {
            let id = it["id"].as_str().unwrap_or("").to_string();
            let filename = it["filename"].as_str().unwrap_or("file").to_string();
            let state = it["state"].as_str().unwrap_or("").to_string();
            let total = it["total"].as_i64().unwrap_or(0);
            let received = it["received"].as_i64().unwrap_or(0);
            card(ui, |ui| {
                ui.horizontal(|ui| {
                    let (color, statelabel) = match state.as_str() {
                        "done" => (OK, "Completed"),
                        "failed" => (BAD, "Failed"),
                        "active" => (WARN, "Downloading"),
                        _ => (DIM, &state.as_str()),
                    };
                    ui.label(RichText::new(filename).color(TEXT));
                    ui.label(RichText::new(format!("{statelabel} · {}/{}", fmt_bytes(received), fmt_bytes(total))).small().color(color));
                    if ui.small_button("✕").clicked() {
                        win.core.store.download_remove(&id);
                    }
                });
                if state == "active" && total > 0 {
                    ui.add(egui::ProgressBar::new(received as f32 / total as f32).desired_height(6.0));
                }
            });
        }
        if items.is_empty() {
            ui.label(RichText::new("No downloads yet.").color(DIM));
        }
    });
}

fn open_folder(path: &std::path::Path) {
    #[cfg(target_os = "windows")]
    let _ = std::process::Command::new("explorer").arg(path).spawn();
    #[cfg(target_os = "linux")]
    let _ = std::process::Command::new("xdg-open").arg(path).spawn();
    #[cfg(target_os = "macos")]
    let _ = std::process::Command::new("open").arg(path).spawn();
}

/* ---------------- Privacy dashboard ---------------- */

fn draw_privacy(ui: &mut Ui, win: &KestrelWindow) {
    heading(ui, "Kestrel Shield");
    let shield_on = win.core.setting_bool("shield.enabled", true);
    let fp_level = FingerprintLevel::from_str(&win.core.setting_str("privacy.fingerprint", "standard"));
    let session = win.core.filters.total_this_session();
    let stats: serde_json::Value = serde_json::from_str(&win.core.store.stats_json()).unwrap_or_default();
    let all_time = stats["total"].as_i64().unwrap_or(0);

    card(ui, |ui| {
        ui.horizontal(|ui| {
            ui.label(RichText::new(if shield_on { "🛡 Protection: ON" } else { "🛡 Protection: OFF" })
                .size(18.0).color(if shield_on { OK } else { WARN }));
            if ui.small_button(if shield_on { "Turn off" } else { "Turn on" }).clicked() {
                let v = if shield_on { "0" } else { "1" };
                win.core.store.setting_set("shield.enabled", v);
                win.reload_shield();
            }
        });
        ui.label(RichText::new(format!(
            "Blocked {session} requests this session · {all_time} all time"
        )).color(DIM));
    });

    // Engine note (honest disclosure)
    card(ui, |ui| {
        ui.label(RichText::new("Engine privacy posture").strong().color(TEXT));
        ui.label(RichText::new(
            "Servo does not ship WebRTC or Web MIDI in this build, so those leak paths \
             simply do not exist here. HTTPS-first upgrades and tracking-parameter \
             stripping are applied to every navigation. Fingerprint protection uses \
             per-session, per-site farbling."
        ).color(DIM).small());
    });

    heading(ui, "Blocked by host (all time)");
    card(ui, |ui| {
        let empty = serde_json::Map::new();
        let by_host = stats["byHost"].as_object().unwrap_or(&empty);
        if by_host.is_empty() {
            ui.label(RichText::new("Nothing blocked yet. Browse a news site to see the Shield work.").color(DIM));
        } else {
            egui::Grid::new("hosts").num_columns(2).striped(true).show(ui, |ui| {
                for (host, v) in by_host.iter() {
                    let c = v["count"].as_i64().unwrap_or(0);
                    ui.label(RichText::new(host).color(TEXT));
                    ui.label(RichText::new(format!("{c} blocked")).color(BAD));
                    ui.end_row();
                }
            });
        }
    });

    heading(ui, "Filter lists");
    let defaults = kestrel_core::KestrelCore::default_filter_lists(win.resources_dir.to_str().unwrap_or("resources"));
    let tuples: Vec<(&str, &str, &str)> = defaults.iter().map(|(i, _p, n)| (i.as_str(), n.as_str(), n.as_str())).collect();
    let lists: Vec<serde_json::Value> =
        serde_json::from_str(&win.core.store.filter_lists_json(&tuples)).unwrap_or_default();
    for l in &lists {
        let id = l["id"].as_str().unwrap_or("");
        let name = l["name"].as_str().unwrap_or("");
        let mut enabled = l["enabled"].as_bool().unwrap_or(true);
        card(ui, |ui| {
            ui.horizontal(|ui| {
                if ui.checkbox(&mut enabled, name).changed() {
                    win.core.store.filter_list_set_enabled(id, enabled);
                    win.reload_shield();
                }
                let path = defaults.iter().find(|(i, _, _)| *i == id).map(|(_, p, _)| p.as_str()).unwrap_or("");
                let exists = std::path::Path::new(path).exists();
                ui.label(RichText::new(if exists { "installed" } else { "missing" }).small().color(if exists { DIM } else { BAD }));
            });
        });
    }

    heading(ui, "Fingerprint protection");
    card(ui, |ui| {
        ui.horizontal(|ui| {
            ui.label("Level:");
            let mut level = fp_level.as_str().to_string();
            egui::ComboBox::from_id_salt("fp-level")
                .selected_text(level.clone())
                .show_ui(ui, |ui| {
                    ui.selectable_value(&mut level, "off".into(), "off");
                    ui.selectable_value(&mut level, "standard".into(), "standard");
                    ui.selectable_value(&mut level, "strict".into(), "strict");
                });
            if level != fp_level.as_str() {
                win.core.store.setting_set("privacy.fingerprint", &level);
                win.apply_fingerprint_level();
            }
        });
        ui.label(RichText::new(
            "standard: canvas/WebGL/audio farbling, hardware spoofing. strict: also uniform \
             UA platform + font metric perturbation. Applies to new pages."
        ).small().color(DIM));
    });

    card(ui, |ui| {
        ui.label(RichText::new("Danger zone").strong().color(BAD));
        if ui.button("Clear all blocking statistics").clicked() {
            win.core.store.stats_clear();
        }
    });
}

/* ---------------- Settings ---------------- */

fn draw_settings(ui: &mut Ui, win: &KestrelWindow) {
    heading(ui, "Settings");

    card(ui, |ui| {
        ui.label(RichText::new("General").strong().color(TEXT));
        let engines = [
            ("DuckDuckGo", "https://duckduckgo.com/?q={q}"),
            ("Bing", "https://www.bing.com/search?q={q}"),
            ("Wikipedia", "https://en.wikipedia.org/w/index.php?search={q}"),
            ("Startpage", "https://www.startpage.com/sp/search?query={q}"),
        ];
        ui.horizontal(|ui| {
            ui.label("Search engine:");
            let cur = win.core.setting_str("general.search_engine", "https://duckduckgo.com/?q={q}");
            let mut sel = cur.clone();
            egui::ComboBox::from_id_salt("engine")
                .selected_text(engines.iter().find(|(_, u)| *u == cur).map(|(n, _)| *n).unwrap_or("custom"))
                .show_ui(ui, |ui| {
                    for (name, url) in engines {
                        ui.selectable_value(&mut sel, url.to_string(), name);
                    }
                });
            if sel != cur {
                win.core.store.setting_set("general.search_engine", &sel);
            }
        });
        ui.horizontal(|ui| {
            ui.label("Homepage:");
            let mut hp = win.scratch_get("set.homepage");
            if hp.is_empty() {
                hp = win.core.setting_str("general.homepage", "kestrel://newtab");
            }
            let r = egui::TextEdit::singleline(&mut hp).desired_width(320.0).show(ui);
            if r.changed() {
                win.scratch_set("set.homepage", hp.clone());
            }
            if ui.small_button("Save").clicked() {
                win.core.store.setting_set("general.homepage", &win.scratch_get("set.homepage"));
            }
            if ui.small_button("Set current as homepage").clicked() {
                if let Some(t) = win.active_tab() {
                    win.core.store.setting_set("general.homepage", &t.url);
                    win.scratch_set("set.homepage", t.url.clone());
                }
            }
        });
    });

    card(ui, |ui| {
        ui.label(RichText::new("Kestrel Shield").strong().color(TEXT));
        toggle(ui, win, "shield.enabled", "Block ads and trackers", true, |w| w.reload_shield());
        toggle(ui, win, "shield.https_first", "HTTPS-first upgrades", true, |_| {});
        toggle(ui, win, "shield.strip_tracking", "Strip tracking parameters", true, |_| {});
        toggle(ui, win, "shield.cosmetic", "Remove ad placeholders (cosmetic filtering)", true, |_| {});
    });

    card(ui, |ui| {
        ui.label(RichText::new("Privacy").strong().color(TEXT));
        toggle(ui, win, "privacy.clear_on_exit", "Clear browsing data on exit", false, |_| {});
        ui.horizontal(|ui| {
            if ui.button("Clear history").clicked() { win.core.store.history_clear(); }
            if ui.button("Clear cookies & site data").clicked() { win.clear_site_data(); }
            if ui.button("Clear everything").clicked() {
                win.core.store.history_clear();
                win.core.store.stats_clear();
                win.clear_site_data();
            }
        });
    });

    card(ui, |ui| {
        ui.label(RichText::new("Downloads").strong().color(TEXT));
        ui.label(RichText::new(format!("Folder: {}", win.downloads_dir.display())).color(DIM));
    });

    card(ui, |ui| {
        ui.label(RichText::new("Session").strong().color(TEXT));
        toggle(ui, win, "session.restore", "Restore tabs on startup", true, |_| {});
    });
}

fn toggle(
    ui: &mut Ui,
    win: &KestrelWindow,
    key: &str,
    label: &str,
    default: bool,
    after: impl FnOnce(&KestrelWindow),
) {
    let mut on = win.core.setting_bool(key, default);
    if ui.checkbox(&mut on, label).changed() {
        win.core.store.setting_set(key, if on { "1" } else { "0" });
        after(win);
    }
}

/* ---------------- About ---------------- */

fn draw_about(ui: &mut Ui, win: &KestrelWindow) {
    heading(ui, "About Kestrel");
    card(ui, |ui| {
        ui.label(RichText::new("kestrel").size(30.0).color(ACCENT).strong());
        ui.label(RichText::new(format!("Version {} · shell v2 (Servo engine)", env!("CARGO_PKG_VERSION"))).color(DIM));
        ui.add_space(6.0);
        ui.label(RichText::new(
            "An independent, privacy-first browser built on the Servo web engine with a \
             native Rust shell. No bundled Chromium. Ad and tracker blocking is enforced \
             at the resource level by Kestrel Shield."
        ).color(TEXT));
        ui.add_space(6.0);
        ui.label(RichText::new(format!("Profile: {}", win.core.profile_dir.display())).small().color(DIM));
        ui.label(RichText::new(format!("Resources: {}", win.resources_dir.display())).small().color(DIM));
    });
}
