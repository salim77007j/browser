//! Native egui chrome: tab strip, toolbar + omnibox with suggestions,
//! main menu, modal prompts, and webview compositing.

use crate::state::{host_or_url, InternalPage, MenuAction, Prompt, Tab, UiCommand};
use crate::window::KestrelWindow;
use egui::{Align, Color32, Id, Key, Layout, Order, RichText, TextEdit, TopBottomPanel};
use egui_glow::EguiGlow;
use servo::{OffscreenRenderingContext, RenderingContext};
use std::cell::RefCell;
use std::rc::Rc;

const ACCENT: Color32 = Color32::from_rgb(0x7C, 0x6C, 0xFF);
const BG: Color32 = Color32::from_rgb(0x14, 0x14, 0x1B);
const BG_PANEL: Color32 = Color32::from_rgb(0x1B, 0x1B, 0x26);
const BG_INPUT: Color32 = Color32::from_rgb(0x22, 0x22, 0x30);
const BG_HOVER: Color32 = Color32::from_rgb(0x2C, 0x2C, 0x3E);
const BG_ACTIVE: Color32 = Color32::from_rgb(0x2A, 0x2A, 0x3C);
const TEXT: Color32 = Color32::from_rgb(0xE8, 0xE8, 0xF0);
const DIM: Color32 = Color32::from_rgb(0x9A, 0x9A, 0xAC);
const OK: Color32 = Color32::from_rgb(0x33, 0xD1, 0x7A);
const WARN: Color32 = Color32::from_rgb(0xFF, 0xB0, 0x3A);

#[derive(Clone)]
enum SuggestItem {
    Go(String),
    Search(String),
    History { url: String, label: String },
    Bookmark { url: String, label: String },
}

impl SuggestItem {
    fn url(&self) -> &str {
        match self {
            SuggestItem::Go(u)
            | SuggestItem::Search(u)
            | SuggestItem::History { url: u, .. }
            | SuggestItem::Bookmark { url: u, .. } => u,
        }
    }
    fn label(&self) -> String {
        match self {
            SuggestItem::Go(u) => format!("Go to {u}"),
            SuggestItem::Search(q) => format!("Search for “{q}”"),
            SuggestItem::History { label, url } | SuggestItem::Bookmark { label, url } => {
                format!("{label} — {url}")
            },
        }
    }
}

pub struct GlPart {
    rendering_context: Rc<OffscreenRenderingContext>,
    context: EguiGlow,
}

pub struct St {
    chrome_height: f32,
    location: String,
    location_dirty: bool,
    focus_omnibox_req: bool,
    suggestions: Vec<SuggestItem>,
    suggestion_sel: usize,
    style_set: bool,
    prompt_text: String,
    last_repaint_need: bool,
}

pub struct Gui {
    gl: RefCell<GlPart>,
    st: RefCell<St>,
}

impl Gui {
    pub fn new(
        rendering_context: Rc<OffscreenRenderingContext>,
        event_loop: &winit::event_loop::ActiveEventLoop,
    ) -> Self {
        rendering_context.make_current().ok();
        let context = EguiGlow::new(event_loop, rendering_context.glow_gl_api(), None, None, false);
        Self {
            gl: RefCell::new(GlPart { rendering_context, context }),
            st: RefCell::new(St {
                chrome_height: 84.0,
                location: String::new(),
                location_dirty: false,
                focus_omnibox_req: false,
                suggestions: Vec::new(),
                suggestion_sel: 0,
                style_set: false,
                prompt_text: String::new(),
                last_repaint_need: false,
            }),
        }
    }

    pub fn chrome_height(&self) -> f32 {
        self.st.borrow().chrome_height
    }

    pub fn has_kb_focus(&self) -> bool {
        self.gl.borrow().context.egui_ctx.memory(|m| m.focused().is_some())
    }

    pub fn egui_wants_repaint(&self) -> bool {
        self.st.borrow().last_repaint_need
    }

    pub fn request_omnibox_focus(&self) {
        self.st.borrow_mut().focus_omnibox_req = true;
    }

    pub fn on_window_event(
        &self,
        window: &winit::window::Window,
        event: &winit::event::WindowEvent,
    ) -> egui_winit::EventResponse {
        self.gl.borrow_mut().context.on_window_event(window, event)
    }

    pub fn update(&self, win: &KestrelWindow) {
        let mut gl = self.gl.borrow_mut();
        let mut st = self.st.borrow_mut();
        gl.rendering_context.make_current().ok();
        eprintln!("KESTREL: frame begin");
        let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            gl.context.run(&win.winit, |ctx| {
                draw_ui(ctx, win, &mut st);
            })
        }));
        match result {
            Ok(_) => eprintln!("KESTREL: frame ok"),
            Err(_) => eprintln!("KESTREL: frame PANICKED"),
        }
        st.last_repaint_need = false;
    }

    pub fn paint(&self, window: &winit::window::Window) {
        let mut gl = self.gl.borrow_mut();
        gl.rendering_context.make_current().ok();
        gl.rendering_context.parent_context().prepare_for_rendering();
        gl.context.paint(window);
        gl.rendering_context.parent_context().present();
    }
}

fn ensure_style(st: &mut St, ctx: &egui::Context) {
        if st.style_set {
            return;
        }
        st.style_set = true;
        ctx.style_mut(|style| {
            let v = &mut style.visuals;
            v.panel_fill = BG_PANEL;
            v.window_fill = BG_PANEL;
            v.extreme_bg_color = BG_INPUT;
            v.faint_bg_color = BG;
            v.selection.bg_fill = ACCENT;
            v.selection.stroke = egui::Stroke::new(1.0, ACCENT);
            v.override_text_color = Some(TEXT);
        });
    }

fn draw_ui(ctx: &egui::Context, win: &KestrelWindow, st: &mut St) {
        ensure_style(st, ctx);
        sync_location(win, st);
        let sr = ctx.screen_rect();
        eprintln!("KESTREL: screen_rect={sr:?}");
        ctx.layer_painter(egui::LayerId::new(egui::Order::Foreground, egui::Id::new("probe")))
            .rect_filled(egui::Rect::from_min_size(egui::pos2(200.0, 120.0), egui::vec2(400.0, 300.0)), 0.0, egui::Color32::RED);
        ctx.layer_painter(egui::LayerId::new(egui::Order::Foreground, egui::Id::new("probe-text")))
            .text(egui::pos2(220.0, 150.0), egui::Align2::LEFT_TOP, "PROBE TEXT 123", egui::FontId::proportional(28.0), egui::Color32::WHITE);
        egui::Area::new(egui::Id::new("probe-area"))
            .fixed_pos(egui::pos2(650.0, 130.0))
            .show(ctx, |ui| {
                ui.add(egui::Button::new(egui::RichText::new("Probe Btn").color(egui::Color32::YELLOW).size(20.0)).fill(egui::Color32::BLUE));
            });
        egui::Area::new(egui::Id::new("probe-area2"))
            .fixed_pos(egui::pos2(650.0, 190.0))
            .show(ctx, |ui| {
                egui::TopBottomPanel::top("probe-panel").frame(egui::Frame::default().fill(egui::Color32::GREEN)).show_inside(ui, |_| {});
            });

        // Background under the content area (webview blits first, then chrome paints;
        // for internal pages the page paints its own bg over this).
        ctx.layer_painter(egui::LayerId::new(Order::Background, Id::new("content-bg")))
            .rect_filled(ctx.screen_rect(), 0.0, BG);

        // ---- Row 1: tab strip ----
        let mut close_idx: Option<usize> = None;
        TopBottomPanel::top("tabstrip")
            .frame(egui::Frame::default().fill(BG).inner_margin(egui::Margin::symmetric(4, 3)))
            .show(ctx, |ui| {
                ui.horizontal(|ui| {
                    let n = win.tabs.len().max(1);
                    let avail = ui.available_width() - 40.0;
                    let per = ((avail / n as f32).clamp(110.0, 240.0)).min(avail.max(110.0));
                    for (i, tab) in win.tabs.iter().enumerate() {
                        let selected = i == win.active;
                        let title = truncate(&tab.display_title(), 26);
                        let btn = egui::Button::new(
                            RichText::new(title).color(if selected { TEXT } else { DIM }),
                        )
                        .fill(if selected { BG_ACTIVE } else { Color32::TRANSPARENT })
                        .min_size(egui::vec2(per, 26.0));
                        let resp = ui.add(btn).on_hover_text(tab.url.clone());
                        if resp.clicked() {
                            win.commands.borrow_mut().push(UiCommand::SelectTab(i));
                        }
                        if resp.middle_clicked() {
                            close_idx = Some(i);
                        }
                        if selected {
                            ui.painter().rect_filled(
                                egui::Rect::from_min_size(
                                    resp.rect.left_bottom(),
                                    egui::vec2(resp.rect.width(), 2.0),
                                ),
                                1.0,
                                ACCENT,
                            );
                        }
                        resp.context_menu(|ui| {
                            if ui
                                .button(if tab.pinned { "Unpin tab" } else { "Pin tab" })
                                .clicked()
                            {
                                win.commands.borrow_mut().push(UiCommand::TogglePin(i));
                                ui.close_menu();
                            }
                            if ui.button("Duplicate tab").clicked() {
                                win.commands.borrow_mut().push(UiCommand::DuplicateTab(i));
                                ui.close_menu();
                            }
                            if ui.button("Close tab").clicked() {
                                close_idx = Some(i);
                                ui.close_menu();
                            }
                        });
                    }
                    if ui
                        .add(egui::Button::new("＋").min_size(egui::vec2(26.0, 26.0)))
                        .on_hover_text("New tab (Ctrl+T)")
                        .clicked()
                    {
                        win.commands.borrow_mut().push(UiCommand::NewTab(None));
                    }
                });
            });
        if let Some(i) = close_idx {
            win.commands.borrow_mut().push(UiCommand::CloseTab(i));
        }

        // ---- Row 2: toolbar ----
        let mut chrome_h = st.chrome_height;
        let mut suggestions_open = false;
        TopBottomPanel::top("toolbar")
            .frame(egui::Frame::default().fill(BG_PANEL).inner_margin(egui::Margin::symmetric(6, 5)))
            .show(ctx, |ui| {
                chrome_h = ui.max_rect().bottom();
                let tab = win.active_tab().cloned().unwrap_or_else(|| Tab {
                    id: 0u64,
                    webview: None,
                    internal: Some(InternalPage::NewTab),
                    url: String::new(),
                    title: String::new(),
                    loading: false,
                    can_back: false,
                    can_forward: false,
                    crashed: None,
                    pinned: false,
                    zoom: 1.0,
                });
                ui.horizontal(|ui| {
                    if ui
                        .add_enabled(tab.can_back, egui::Button::new("⏴").min_size(egui::vec2(30.0, 26.0)))
                        .on_hover_text("Back (Alt+←)")
                        .clicked()
                    {
                        win.commands.borrow_mut().push(UiCommand::Back);
                    }
                    if ui
                        .add_enabled(tab.can_forward, egui::Button::new("⏵").min_size(egui::vec2(30.0, 26.0)))
                        .on_hover_text("Forward (Alt+→)")
                        .clicked()
                    {
                        win.commands.borrow_mut().push(UiCommand::Forward);
                    }
                    let reload_label = if tab.loading { "✕" } else { "↻" };
                    if ui
                        .add(egui::Button::new(reload_label).min_size(egui::vec2(30.0, 26.0)))
                        .on_hover_text(if tab.loading { "Stop" } else { "Reload (Ctrl+R)" })
                        .clicked()
                    {
                        win.commands.borrow_mut().push(UiCommand::Reload);
                    }

                    // Shield badge
                    let shield_on = win.core.setting_bool("shield.enabled", true);
                    let blocked = win.core.filters.total_this_session();
                    if ui
                        .add(
                            egui::Button::new(
                                RichText::new(format!("🛡 {blocked}"))
                                    .color(if shield_on { OK } else { DIM })
                                    .small(),
                            )
                            .fill(Color32::TRANSPARENT),
                        )
                        .on_hover_text("Kestrel Shield — open privacy dashboard")
                        .clicked()
                    {
                        win.commands.borrow_mut().push(UiCommand::MenuAction(MenuAction::PrivacyPage));
                    }

                    // Omnibox
                    let omni_id = Id::new("omnibox");
                    let mut editing = st.location.clone();
                    let out = TextEdit::singleline(&mut editing)
                        .id(omni_id)
                        .desired_width(ui.available_width() - 108.0)
                        .hint_text("Search or enter address")
                        .show(ui);
                    let omnibox_rect = out.response.rect;
                    let editing_now = out.response.has_focus();
                    if st.focus_omnibox_req {
                        ctx.memory_mut(|m| m.request_focus(omni_id));
                        st.focus_omnibox_req = false;
                    }
                    if editing_now {
                        let changed = editing != st.location;
                        st.location = editing.clone();
                        if changed {
                            st.location_dirty = true;
                            rebuild_suggestions(win, st, &editing);
                            st.suggestion_sel = 0;
                        }
                    }

                    // security indicator
                    let secure = tab.url.starts_with("https://");
                    let internal = tab.url.starts_with("kestrel://");
                    let dot = if internal {
                        DIM
                    } else if secure {
                        OK
                    } else if tab.url.starts_with("http://") {
                        WARN
                    } else {
                        DIM
                    };
                    ui.painter().circle_filled(
                        omnibox_rect.left_top() + egui::vec2(-6.0, omnibox_rect.height() * 0.5),
                        3.0,
                        dot,
                    );

                    // omnibox keyboard handling
                    let (enter, down, up, esc, tabk) = ctx.input(|i| {
                        (
                            i.key_pressed(Key::Enter),
                            i.key_pressed(Key::ArrowDown),
                            i.key_pressed(Key::ArrowUp),
                            i.key_pressed(Key::Escape),
                            i.key_pressed(Key::Tab),
                        )
                    });
                    let mut commit: Option<String> = None;
                    if editing_now && !st.suggestions.is_empty() {
                        if down {
                            st.suggestion_sel =
                                (st.suggestion_sel + 1).min(st.suggestions.len() - 1);
                        }
                        if up {
                            st.suggestion_sel = st.suggestion_sel.saturating_sub(1);
                        }
                        if tabk {
                            st.suggestion_sel =
                                (st.suggestion_sel + 1) % st.suggestions.len();
                        }
                        if esc {
                            st.location_dirty = false;
                            ctx.memory_mut(|m| m.surrender_focus(omni_id));
                        }
                        if enter {
                            let s = &st.suggestions[st.suggestion_sel.min(st.suggestions.len() - 1)];
                            commit = Some(s.url().to_string());
                        }
                        suggestions_open = true;
                    } else if editing_now && enter {
                        commit = Some(st.location.clone());
                    }
                    if let Some(url) = commit {
                        st.location_dirty = false;
                        ctx.memory_mut(|m| m.surrender_focus(omni_id));
                        win.commands.borrow_mut().push(UiCommand::Load(url));
                    }

                    // suggestions dropdown
                    if suggestions_open {
                        let sugg = st.suggestions.clone();
                        let sel = st.suggestion_sel;
                        egui::Area::new(Id::new("suggestions"))
                            .order(Order::Foreground)
                            .fixed_pos(omnibox_rect.left_bottom() + egui::vec2(0.0, 4.0))
                            .show(ctx, |ui| {
                                egui::Frame::default()
                                    .fill(BG_PANEL)
                                    .inner_margin(4)
                                    .show(ui, |ui| {
                                        ui.set_min_width(omnibox_rect.width());
                                        for (idx, s) in sugg.iter().enumerate() {
                                            let is_sel = idx == sel;
                                            let r = ui.add(
                                                egui::Button::new(RichText::new(s.label()).color(
                                                    if is_sel { TEXT } else { DIM },
                                                ))
                                                .fill(if is_sel { BG_HOVER } else { Color32::TRANSPARENT })
                                                .min_size(egui::vec2(ui.available_width(), 22.0)),
                                            );
                                            if r.clicked() {
                                                st.location_dirty = false;
                                                ctx.memory_mut(|m| m.surrender_focus(omni_id));
                                                win.commands
                                                    .borrow_mut()
                                                    .push(UiCommand::Load(s.url().to_string()));
                                            }
                                            if r.hovered() {
                                                st.suggestion_sel = idx;
                                            }
                                        }
                                    });
                            });
                    }

                    // bookmark star
                    let is_bookmark = !tab.url.is_empty() && win.core.store.is_bookmarked(&tab.url);
                    if ui
                        .add(
                            egui::Button::new(
                                RichText::new(if is_bookmark { "★" } else { "☆" })
                                    .color(if is_bookmark { WARN } else { DIM })
                                    .size(15.0),
                            )
                            .fill(Color32::TRANSPARENT),
                        )
                        .on_hover_text("Bookmark this page (Ctrl+D)")
                        .clicked()
                    {
                        win.commands.borrow_mut().push(UiCommand::MenuAction(MenuAction::BookmarkToggle));
                    }

                    // menu
                    ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                        let menu_resp = ui.add(egui::Button::new("☰").min_size(egui::vec2(30.0, 26.0)));
                        if menu_resp.clicked() {
                            ui.memory_mut(|m| m.toggle_popup(Id::new("main-menu")));
                        }
                        let menu_open = ui.memory(|m| m.is_popup_open(Id::new("main-menu")));
                        if menu_open {
                            main_menu(ui, win, menu_resp.rect.left_bottom() + egui::vec2(0.0, 4.0));
                        }
                    });
                });
            });
        st.chrome_height = chrome_h;

        // ---- content: internal page UI or servo webview blit ----
        let content = ctx.available_rect();
        if let Some(tab) = win.active_tab().cloned() {
            if let Some(crash) = &tab.crashed {
                let reason = crash.clone();
                crate::pages::draw_crash(ctx, win, content, &reason);
            } else if let Some(page) = tab.internal {
                crate::pages::draw(ctx, win, content, page);
            } else if let Some(wv) = &tab.webview {
                let scale = win.winit.scale_factor() as f32;
                let w = (content.width() * scale).max(1.0) as u32;
                let h = (content.height() * scale).max(1.0) as u32;
                if wv.size().width as u32 != w || wv.size().height as u32 != h {
                    wv.resize(winit::dpi::PhysicalSize::new(w, h));
                }
                wv.paint();
            }
        }

        // ---- modal prompts ----
        draw_prompts(ctx, win, st);
    }

fn sync_location(win: &KestrelWindow, st: &mut St) {
    if st.location_dirty {
        return;
    }
    if let Some(tab) = win.active_tab() {
        if tab.url != st.location {
            st.location = tab.url.clone();
        }
    }
}

fn rebuild_suggestions(win: &KestrelWindow, st: &mut St, q: &str) {
        st.suggestions.clear();
        let q = q.trim();
        if q.is_empty() {
            return;
        }
        let search =
            win.core.setting_str("general.search_engine", "https://duckduckgo.com/?q={q}");
        let normalized = crate::state::normalize_input(q, &search);
        if normalized != q {
            st.suggestions.push(SuggestItem::Go(normalized));
        }
        st.suggestions.push(SuggestItem::Search(q.to_string()));
        if let Ok(items) =
            serde_json::from_str::<Vec<serde_json::Value>>(&win.core.store.bookmarks_json())
        {
            let ql = q.to_lowercase();
            for it in items.iter() {
                let url = it["url"].as_str().unwrap_or("");
                let title = it["title"].as_str().unwrap_or("");
                if url.to_lowercase().contains(&ql) || title.to_lowercase().contains(&ql) {
                    st.suggestions.push(SuggestItem::Bookmark {
                        url: url.into(),
                        label: title_or_host(title, url),
                    });
                    if st.suggestions.len() >= 8 {
                        break;
                    }
                }
            }
        }
        if st.suggestions.len() < 8 {
            if let Ok(items) =
                serde_json::from_str::<Vec<serde_json::Value>>(&win.core.store.history_query(q, 6))
            {
                for it in items {
                    let url = it["url"].as_str().unwrap_or("").to_string();
                    if url.is_empty() || st.suggestions.iter().any(|s| s.url() == url) {
                        continue;
                    }
                    let title = it["title"].as_str().unwrap_or("");
                    st.suggestions.push(SuggestItem::History {
                        url,
                        label: title_or_host(title, &it["url"].as_str().unwrap_or("")),
                    });
                    if st.suggestions.len() >= 8 {
                        break;
                    }
                }
            }
        }
    }

fn main_menu(ui: &mut egui::Ui, win: &KestrelWindow, below: egui::Pos2) {
        egui::Area::new(Id::new("main-menu-area"))
            .order(Order::Foreground)
            .fixed_pos(below)
            .show(ui.ctx(), |ui| {
                egui::Frame::default()
                    .fill(BG_PANEL)
                    .inner_margin(6)
                    .show(ui, |ui| {
                        ui.set_min_width(250.0);
                        ui.with_layout(Layout::top_down_justified(Align::LEFT), |ui| {
                            let zoom = win.active_tab().map(|t| t.zoom).unwrap_or(1.0);
                            m_item(ui, "New tab", "Ctrl+T", &mut || {
                                win.commands.borrow_mut().push(UiCommand::NewTab(None))
                            });
                            m_item(ui, "Bookmark this page", "Ctrl+D", &mut || {
                                win.commands
                                    .borrow_mut()
                                    .push(UiCommand::MenuAction(MenuAction::BookmarkToggle))
                            });
                            m_item(ui, "Capture page as image", "", &mut || {
                                win.commands
                                    .borrow_mut()
                                    .push(UiCommand::MenuAction(MenuAction::CapturePage))
                            });
                            ui.separator();
                            m_item(ui, "History", "Ctrl+H", &mut || {
                                win.commands
                                    .borrow_mut()
                                    .push(UiCommand::MenuAction(MenuAction::HistoryPage))
                            });
                            m_item(ui, "Bookmarks", "Ctrl+Shift+O", &mut || {
                                win.commands
                                    .borrow_mut()
                                    .push(UiCommand::MenuAction(MenuAction::BookmarksPage))
                            });
                            m_item(ui, "Downloads", "Ctrl+J", &mut || {
                                win.commands
                                    .borrow_mut()
                                    .push(UiCommand::MenuAction(MenuAction::DownloadsPage))
                            });
                            ui.separator();
                            m_item(ui, &format!("Zoom in ({:.0}%)", zoom * 100.0), "Ctrl++", &mut || {
                                win.commands.borrow_mut().push(UiCommand::ZoomIn)
                            });
                            m_item(ui, "Zoom out", "Ctrl+-", &mut || {
                                win.commands.borrow_mut().push(UiCommand::ZoomOut)
                            });
                            m_item(ui, "Reset zoom", "Ctrl+0", &mut || {
                                win.commands.borrow_mut().push(UiCommand::ZoomReset)
                            });
                            ui.separator();
                            m_item(ui, "Kestrel Shield dashboard", "Ctrl+P", &mut || {
                                win.commands
                                    .borrow_mut()
                                    .push(UiCommand::MenuAction(MenuAction::PrivacyPage))
                            });
                            m_item(ui, "Settings", "Ctrl+,", &mut || {
                                win.commands
                                    .borrow_mut()
                                    .push(UiCommand::MenuAction(MenuAction::SettingsPage))
                            });
                            m_item(ui, "About Kestrel", "", &mut || {
                                win.commands
                                    .borrow_mut()
                                    .push(UiCommand::MenuAction(MenuAction::AboutPage))
                            });
                            ui.separator();
                            m_item(ui, "Exit", "Ctrl+Q", &mut || {
                                win.commands
                                    .borrow_mut()
                                    .push(UiCommand::MenuAction(MenuAction::Exit))
                            });
                        });
                    });
            });
        // click-away closes
        if ui.input(|i| i.pointer.any_click()) {
            // handled by memory popup logic; keep open state managed by toggle
        }
    }

fn draw_prompts(ctx: &egui::Context, win: &KestrelWindow, st: &mut St) {
        if win.prompts.borrow().is_empty() {
            return;
        }
        ctx.layer_painter(egui::LayerId::new(Order::Tooltip, Id::new("prompt-dim")))
            .rect_filled(ctx.screen_rect(), 0.0, Color32::from_black_alpha(120));
        let screen = ctx.screen_rect();
        let rect = egui::Rect::from_center_size(screen.center(), egui::vec2(440.0, 200.0));
        egui::Area::new(Id::new("prompt-area"))
            .order(Order::Tooltip)
            .fixed_pos(rect.left_top())
            .show(ctx, |ui| {
                ui.set_min_size(rect.size());
                egui::Frame::default()
                    .fill(BG_PANEL)
                    .inner_margin(14)
                    .show(ui, |ui| {
                        ui.heading(RichText::new("Kestrel").color(ACCENT));
                        ui.add_space(6.0);
                        let label = {
                            let b = win.prompts.borrow();
                            b.first().map(|p| p.host_label()).unwrap_or_default()
                        };
                        ui.label(RichText::new(label).color(TEXT));

                        // Prompt dialogs get a text field.
                        let is_prompt = {
                            let b = win.prompts.borrow();
                            matches!(
                                b.first(),
                                Some(Prompt::Dialog {
                                    control: Some(servo::EmbedderControl::SimpleDialog(
                                        servo::SimpleDialog::Prompt(_),
                                    )),
                                    ..
                                })
                            )
                        };
                        if is_prompt {
                            let mut t = st.prompt_text.clone();
                            let r = TextEdit::singleline(&mut t)
                                .hint_text("Your answer")
                                .desired_width(ui.available_width())
                                .show(ui);
                            st.prompt_text = t;
                            if r.response.lost_focus()
                                && ui.input(|i| i.key_pressed(Key::Enter))
                            {
                                let text = st.prompt_text.clone();
                                st.prompt_text.clear();
                                win.commands.borrow_mut().push(UiCommand::ResolvePrompt { allow: true, text });
                            }
                        }
                        ui.add_space(10.0);
                        ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                            if ui.button("OK").clicked() {
                                let text = st.prompt_text.clone();
                                st.prompt_text.clear();
                                win.commands.borrow_mut().push(UiCommand::ResolvePrompt { allow: true, text });
                            }
                            if ui.button("Cancel").clicked() {
                                st.prompt_text.clear();
                                win.commands.borrow_mut().push(UiCommand::ResolvePrompt { allow: false, text: String::new() });
                            }
                        });
                    });
            });
    }

fn m_item(ui: &mut egui::Ui, label: &str, shortcut: &str, action: &mut impl FnMut()) {
    let r = ui
        .add(
            egui::Button::new(RichText::new(format!("{label:<26}{shortcut}")).small())
                .fill(Color32::TRANSPARENT)
                .min_size(egui::vec2(240.0, 22.0)),
        )
        .on_hover_cursor(egui::CursorIcon::PointingHand);
    if r.clicked() {
        action();
        ui.memory_mut(|m| m.close_popup(Id::new("main-menu")));
    }
}

fn truncate(s: &str, n: usize) -> String {
    if s.chars().count() <= n {
        s.to_string()
    } else {
        let t: String = s.chars().take(n.saturating_sub(1)).collect();
        format!("{t}…")
    }
}

fn title_or_host(title: &str, url: &str) -> String {
    if title.is_empty() {
        host_or_url(url)
    } else {
        title.to_string()
    }
}
