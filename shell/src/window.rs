//! KestrelWindow: winit window + GL contexts + Servo + tabs + input routing.

use crate::delegate::{DelegateShared, NoopServoDelegate};
use crate::downloads;
use crate::gui::Gui;
use crate::state::*;
use crate::state::DelegateMsg;
use kestrel_core::privacy::{self, FingerprintLevel};
use kestrel_core::KestrelCore;
use servo::{
    InputEvent, MouseButtonAction, MouseButtonEvent, MouseButton as ServoMouseButton,
    OffscreenRenderingContext, RenderingContext, Servo, ServoBuilder, UserContentManager,
    UserScript, WebView, WebViewBuilder, WindowRenderingContext,
};
use std::cell::{Cell, RefCell};
use std::collections::HashMap;
use std::path::PathBuf;
use std::rc::Rc;
use winit::event::{ElementState, MouseButton, WindowEvent};
use winit::event_loop::{ActiveEventLoop, EventLoopProxy};

pub struct KestrelWindow {
    pub winit: winit::window::Window,
    pub window_ctx: Rc<WindowRenderingContext>,
    pub offscreen_ctx: Rc<OffscreenRenderingContext>,
    pub servo: Servo,
    pub gui: RefCell<Gui>,
    pub tabs: Vec<Tab>,
    pub active: usize,
    pub core: Rc<KestrelCore>,
    pub commands: RefCell<Vec<UiCommand>>,
    pub prompts: RefCell<Vec<Prompt>>,
    pub closed_urls: RefCell<Vec<String>>,
    pub shared: Rc<DelegateShared>,
    pub resources_dir: PathBuf,
    pub downloads_dir: PathBuf,
    pub needs_redraw: Cell<bool>,
    pub last_mouse: Cell<(f64, f64)>,
    pub modifiers: Cell<keyboard_types::Modifiers>,
    pub fullscreen: Cell<bool>,
    pub download_seq: Cell<u64>,
    pub session_clean_start: bool,
    pub blocked_this_session: Cell<u64>,
    pub scratch: RefCell<HashMap<String, String>>,
    pub next_key: Cell<u64>,
    pub capture_pending: RefCell<Vec<(String, PathBuf)>>,
    pub render_count: Cell<u32>,
}

impl KestrelWindow {
    pub fn new(
        event_loop: &ActiveEventLoop,
        proxy: EventLoopProxy<KestrelEvent>,
        initial_url: Option<String>,
    ) -> Result<Self, String> {
        let attrs = winit::window::Window::default_attributes()
            .with_title("Kestrel")
            .with_inner_size(winit::dpi::LogicalSize::new(1280.0, 820.0))
            .with_min_inner_size(winit::dpi::LogicalSize::new(680.0, 420.0));
        let winit = event_loop.create_window(attrs).map_err(|e| format!("window: {e}"))?;
        let size = winit.inner_size();

        use winit::raw_window_handle::{HasDisplayHandle, HasWindowHandle};
        let display_handle = winit.display_handle().map_err(|e| e.to_string())?;
        let window_handle = winit.window_handle().map_err(|e| e.to_string())?;
        let window_ctx = Rc::new(
            WindowRenderingContext::new(display_handle, window_handle, size)
                .map_err(|e| format!("gl context: {e:?}"))?,
        );
        window_ctx.make_current().map_err(|e| format!("{e:?}"))?;
        let offscreen_ctx = Rc::new(window_ctx.offscreen_context(size));

        let profile_dir = dirs::data_dir()
            .unwrap_or_else(|| PathBuf::from("."))
            .join("kestrel");
        let core = KestrelCore::open(profile_dir.to_str().unwrap_or("kestrel-profile"))
            .ok_or("failed to open profile")?;
        let session_clean_start = core.store.session_load().is_none();
        let core = Rc::new(core);

        let resources_dir = std::env::var("KESTREL_RESOURCES")
            .map(PathBuf::from)
            .unwrap_or_else(|_| {
                std::env::current_exe()
                    .ok()
                    .and_then(|p| p.parent().map(|d| d.join("resources")))
                    .filter(|d| d.exists())
                    .unwrap_or_else(|| PathBuf::from("resources"))
            });
        if let Err(e) = core.reload_filters(resources_dir.to_str().unwrap_or("resources")) {
            log::warn!("filter engine reload failed: {e}");
        }

        let waker = ProxyWaker { proxy: proxy.clone() };
        let servo = ServoBuilder::default()
            .event_loop_waker(Box::new(waker))
            .build();

        let shared = DelegateShared::new(core.clone(), proxy.clone());
        servo.set_delegate(Rc::new(NoopServoDelegate));

        let downloads_dir = dirs::download_dir()
            .or_else(dirs::home_dir)
            .unwrap_or_else(|| PathBuf::from("."));

        let _ = winit.focus_window();
        let mut win = Self {
            winit,
            window_ctx,
            offscreen_ctx: offscreen_ctx.clone(),
            servo,
            gui: RefCell::new(Gui::new(offscreen_ctx, event_loop)),
            tabs: Vec::new(),
            active: 0,
            core: core.clone(),
            commands: RefCell::new(Vec::new()),
            prompts: RefCell::new(Vec::new()),
            closed_urls: RefCell::new(Vec::new()),
            shared: shared.clone(),
            resources_dir,
            downloads_dir,
            needs_redraw: Cell::new(true),
            last_mouse: Cell::new((-1.0, -1.0)),
            modifiers: Cell::new(keyboard_types::Modifiers::empty()),
            fullscreen: Cell::new(false),
            download_seq: Cell::new(0),
            session_clean_start,
            blocked_this_session: Cell::new(0),
            scratch: RefCell::new(HashMap::new()),
            next_key: Cell::new(1),
            capture_pending: RefCell::new(Vec::new()),
            render_count: Cell::new(0),
        };

        let restore: Option<String> = core.store.session_load();
        let restore_on = core.setting_bool("session.restore", true);
        let urls: Vec<(String, bool)> = if restore_on {
            restore
                .as_deref()
                .and_then(|s| serde_json::from_str::<SessionData>(s).ok())
                .filter(|s| s.clean)
                .map(|s| s.tabs.into_iter().map(|t| (t.url, t.pinned)).collect())
                .filter(|v: &Vec<(String, bool)>| !v.is_empty())
                .unwrap_or_default()
        } else {
            Vec::new()
        };
        if urls.is_empty() {
            win.new_tab(String::new(), None);
        } else {
            for (url, pinned) in urls {
                win.new_tab(url, Some(pinned));
            }
        }
        if let Some(u) = initial_url {
            win.navigate_active(&u);
        }
        Ok(win)
    }

    fn alloc_key(&self) -> u64 {
        let k = self.next_key.get();
        self.next_key.set(k + 1);
        k
    }

    /* ---------- tab management ---------- */

    pub fn active_tab(&self) -> Option<&Tab> {
        self.tabs.get(self.active)
    }
    pub fn active_tab_mut(&mut self) -> &mut Tab {
        let i = self.active.min(self.tabs.len().saturating_sub(1));
        &mut self.tabs[i]
    }
    pub fn active_webview(&self) -> Option<WebView> {
        self.active_tab().and_then(|t| t.webview.clone())
    }

    fn make_webview(&self, url: &str) -> Option<WebView> {
        let ucm = UserContentManager::new(&self.servo);
        let level =
            FingerprintLevel::from_str(&self.core.setting_str("privacy.fingerprint", "standard"));
        let script = privacy::fingerprint_script(level, "");
        if !script.is_empty() {
            ucm.add_script(Rc::new(UserScript::new(script, None)));
        }
        let parsed = url::Url::parse(url).ok()?;
        Some(
            WebViewBuilder::new(&self.servo, self.offscreen_ctx.clone())
                .delegate(self.shared.delegate.clone())
                .user_content_manager(Rc::new(ucm))
                .hidpi_scale_factor(self.hidpi_scale_factor())
                .url(parsed)
                .build(),
        )
    }

    pub fn new_tab(&mut self, url: String, pinned: Option<bool>) {
        let page = if url.is_empty() {
            Some(InternalPage::NewTab)
        } else {
            InternalPage::from_url(&url)
        };
        let (webview, internal) = if page.is_some() {
            (None, page)
        } else {
            match self.make_webview(&url) {
                Some(wv) => (Some(wv), None),
                None => (None, Some(InternalPage::NewTab)),
            }
        };
        let tab = Tab {
            id: self.alloc_key(),
            webview: webview.clone(),
            internal,
            url: if internal.is_some() && url.is_empty() {
                InternalPage::NewTab.url().into()
            } else {
                url
            },
            title: String::new(),
            loading: false,
            can_back: false,
            can_forward: false,
            crashed: None,
            pinned: pinned.unwrap_or(false),
            zoom: 1.0,
        };
        self.tabs.push(tab);
        self.activate(self.tabs.len() - 1);
    }

    pub fn close_tab(&mut self, idx: usize) {
        if idx >= self.tabs.len() {
            return;
        }
        let tab = self.tabs.remove(idx);
        if let Some(wv) = &tab.webview {
            if tab.internal.is_none() && !tab.url.is_empty() {
                self.closed_urls.borrow_mut().push(tab.url.clone());
            }
            wv.hide();
        }
        if self.tabs.is_empty() {
            self.new_tab(String::new(), None);
            return;
        }
        if self.active >= self.tabs.len() {
            self.active = self.tabs.len() - 1;
        }
        self.activate(self.active);
    }

    pub fn activate(&mut self, idx: usize) {
        if idx >= self.tabs.len() {
            return;
        }
        self.active = idx;
        for (i, t) in self.tabs.iter().enumerate() {
            if let Some(wv) = &t.webview {
                if i == idx {
                    wv.show();
                } else {
                    wv.hide();
                }
            }
        }
        self.needs_redraw.set(true);
    }

    pub fn navigate_active(&mut self, input: &str) {
        let search = self
            .core
            .setting_str("general.search_engine", "https://duckduckgo.com/?q={q}");
        let target = normalize_input(input, &search);
        if target.is_empty() {
            return;
        }
        let target = privacy::https_upgrade(&privacy::strip_tracking_params(&target));
        if let Some(page) = InternalPage::from_url(&target) {
            let tab = self.active_tab_mut();
            if let Some(wv) = tab.webview.take() {
                wv.hide();
            }
            tab.internal = Some(page);
            tab.url = page.url().into();
            tab.title = String::new();
            tab.loading = false;
            tab.crashed = None;
            self.needs_redraw.set(true);
            return;
        }
        let needs_new_webview = {
            let tab = self.active_tab_mut();
            let need = tab.internal.is_some() || tab.webview.is_none() || tab.crashed.is_some();
            if need {
                if let Some(wv) = tab.webview.take() {
                    wv.hide();
                }
            }
            need
        };
        if needs_new_webview {
            match self.make_webview(&target) {
                Some(wv) => {
                    let tab = self.active_tab_mut();
                    tab.webview = Some(wv);
                    tab.internal = None;
                    tab.crashed = None;
                },
                None => return,
            }
        }
        let tab = self.active_tab_mut();
        tab.url = target.clone();
        tab.title = String::new();
        tab.loading = true;
        if let Some(wv) = &tab.webview {
            if let Ok(u) = url::Url::parse(&target) {
                wv.load(u);
            }
        }
        self.activate(self.active);
    }

    /* ---------- delegate messages ---------- */

    pub fn pump_servo(&mut self) {
        self.servo.spin_event_loop();
        let msgs = self.shared.drain();
        if !msgs.is_empty() {
            self.apply_messages(msgs);
        }
        let pending: Vec<servo::CreateNewWebViewRequest> = self.shared.take_pending_create();
        for req in pending {
            let wv = req
                .builder(self.offscreen_ctx.clone())
                .delegate(self.shared.delegate.clone())
                .hidpi_scale_factor(self.hidpi_scale_factor())
                .build();
            self.push_webview_tab(wv);
        }
        let captures: Vec<(String, PathBuf)> = self.shared.take_captures();
        for (url, path) in captures {
            self.start_capture(url, path);
        }
    }

    fn tab_index_for(&self, id: servo::WebViewId) -> Option<usize> {
        self.tabs
            .iter()
            .position(|t| t.webview.as_ref().map(|w| w.id()) == Some(id))
    }

    fn push_webview_tab(&mut self, wv: WebView) {
        let id = wv.id();
        let tab = Tab {
            id: self.alloc_key(),
            webview: Some(wv),
            internal: None,
            url: String::new(),
            title: String::new(),
            loading: true,
            can_back: false,
            can_forward: false,
            crashed: None,
            pinned: false,
            zoom: 1.0,
        };
        self.tabs.push(tab);
        self.activate(self.tabs.len() - 1);
        let _ = id;
    }

    fn apply_messages(&mut self, msgs: Vec<DelegateMsg>) {
        let mut flush_stats = false;
        for msg in msgs {
            match msg {
                DelegateMsg::UrlChanged(id, url) => {
                    if let Some(t) = self.tab_index_for(id).and_then(|i| self.tabs.get_mut(i)) {
                        t.url = url;
                        self.needs_redraw.set(true);
                    }
                },
                DelegateMsg::TitleChanged(id, title) => {
                    if let Some(t) = self.tab_index_for(id).and_then(|i| self.tabs.get_mut(i)) {
                        t.title = title;
                        self.needs_redraw.set(true);
                    }
                },
                DelegateMsg::LoadStarted(id) => {
                    if let Some(t) = self.tab_index_for(id).and_then(|i| self.tabs.get_mut(i)) {
                        t.loading = true;
                        self.needs_redraw.set(true);
                    }
                },
                DelegateMsg::LoadComplete(id) => {
                    let mut entry: Option<(String, String, String)> = None;
                    if let Some(t) = self.tab_index_for(id).and_then(|i| self.tabs.get_mut(i)) {
                        t.loading = false;
                        self.needs_redraw.set(true);
                        if t.internal.is_none() {
                            let host = host_or_url(&t.url);
                            entry = Some((t.url.clone(), t.title.clone(), host));
                        }
                    }
                    if let Some((url, _title, _host)) = entry {
                        let title2 = self
                            .tab_index_for(id)
                            .and_then(|i| self.tabs.get(i))
                            .map(|t| t.title.clone())
                            .unwrap_or_default();
                        let host2 = host_or_url(&url);
                        self.core.store.history_add(&url, &title2, &host2);
                        if let Some(wv) = self
                            .tab_index_for(id)
                            .and_then(|i| self.tabs.get(i))
                            .and_then(|t| t.webview.clone())
                        {
                            if self.core.setting_bool("shield.cosmetic", true) {
                                if let Some((css, _)) = self.core.filters.cosmetic_css(&url) {
                                    if !css.is_empty() {
                                        let js = format!(
                                            "(function(){{var s=document.createElement('style');s.textContent={};document.documentElement.appendChild(s);}})();",
                                            serde_json::to_string(&css).unwrap_or_default()
                                        );
                                        let _ = wv.evaluate_javascript(js, |_| {});
                                    }
                                }
                            }
                        }
                    }
                },
                DelegateMsg::HistoryChanged(id, back, fwd) => {
                    if let Some(t) = self.tab_index_for(id).and_then(|i| self.tabs.get_mut(i)) {
                        t.can_back = back;
                        t.can_forward = fwd;
                        self.needs_redraw.set(true);
                    }
                },
                DelegateMsg::FrameReady(_) | DelegateMsg::StatusText(_, _) => {
                    self.needs_redraw.set(true);
                },
                DelegateMsg::Closed(id) => {
                    if let Some(i) = self.tab_index_for(id) {
                        self.close_tab(i);
                    }
                },
                DelegateMsg::Crashed(id, reason) => {
                    if let Some(t) = self.tab_index_for(id).and_then(|i| self.tabs.get_mut(i)) {
                        t.crashed = Some(reason);
                        t.loading = false;
                        self.needs_redraw.set(true);
                    }
                },
                DelegateMsg::Fullscreen(on) => {
                    self.set_fullscreen(on);
                },
                DelegateMsg::NewWebView(wv) => {
                    self.push_webview_tab(wv);
                },
                DelegateMsg::Blocked { host } => {
                    self.blocked_this_session
                        .set(self.blocked_this_session.get() + 1);
                    self.core.filters.record_block(&host);
                    flush_stats = true;
                    self.needs_redraw.set(true);
                },
                DelegateMsg::AskPermission { feature, host, request, .. } => {
                    let stored = self.core.store.permission_get(&host, &feature);
                    match stored.as_deref() {
                        Some("allow") => request.allow(),
                        Some("deny") => request.deny(),
                        _ => {
                            self.prompts.borrow_mut().push(Prompt::Permission {
                                feature,
                                host,
                                request: Some(request),
                            });
                            self.needs_redraw.set(true);
                        },
                    }
                },
                DelegateMsg::AskDialog { control, .. } => {
                    self.prompts
                        .borrow_mut()
                        .push(Prompt::Dialog { control: Some(control) });
                    self.needs_redraw.set(true);
                },
            }
        }
        if flush_stats {
            let counts = self.core.filters.drain_host_counts();
            if !counts.is_empty() {
                self.core.store.stats_flush(&counts);
            }
        }
    }

    /* ---------- ui commands ---------- */

    pub fn process_commands(&mut self) {
        let cmds: Vec<UiCommand> = std::mem::take(&mut *self.commands.borrow_mut());
        for cmd in cmds {
            match cmd {
                UiCommand::Back => {
                    if let Some(wv) = self.active_webview() {
                        wv.go_back(1);
                    }
                },
                UiCommand::Forward => {
                    if let Some(wv) = self.active_webview() {
                        wv.go_forward(1);
                    }
                },
                UiCommand::Reload => {
                    if let Some(t) = self.tabs.get(self.active) {
                        if t.internal.is_some() {
                            self.needs_redraw.set(true);
                        } else if let Some(wv) = &t.webview {
                            wv.reload();
                        }
                    }
                },
                UiCommand::Load(input) => self.navigate_active(&input),
                UiCommand::NewTab(url) => self.new_tab(url.unwrap_or_default(), None),
                UiCommand::CloseTab(i) => self.close_tab(i),
                UiCommand::SelectTab(i) => self.activate(i),
                UiCommand::MoveTab(from, to) => {
                    if from < self.tabs.len() && to < self.tabs.len() && from != to {
                        let t = self.tabs.remove(from);
                        self.tabs.insert(to, t);
                        self.active = to;
                        self.needs_redraw.set(true);
                    }
                },
                UiCommand::TogglePin(i) => {
                    if let Some(t) = self.tabs.get_mut(i) {
                        t.pinned = !t.pinned;
                        self.needs_redraw.set(true);
                    }
                },
                UiCommand::DuplicateTab(i) => {
                    if let Some(t) = self.tabs.get(i) {
                        let url = t.url.clone();
                        self.new_tab(url, None);
                    }
                },
                UiCommand::ReopenClosed => {
                    let url = self.closed_urls.borrow_mut().pop();
                    if let Some(url) = url {
                        self.new_tab(url, None);
                    }
                },
                UiCommand::OpenInternal(page) => {
                    let url = page.url().to_string();
                    self.navigate_active(&url);
                },
                UiCommand::FocusOmnibox => {
                    self.gui.borrow_mut().request_omnibox_focus();
                    self.needs_redraw.set(true);
                },
                UiCommand::ZoomIn | UiCommand::ZoomOut | UiCommand::ZoomReset => {
                    let new_zoom = {
                        let t = self.active_tab_mut();
                        t.zoom = match cmd {
                            UiCommand::ZoomIn => (t.zoom * 1.1).min(5.0),
                            UiCommand::ZoomOut => (t.zoom / 1.1).max(0.3),
                            _ => 1.0,
                        };
                        t.zoom
                    };
                    if let Some(wv) = self.active_webview() {
                        wv.set_page_zoom(new_zoom);
                    }
                    self.needs_redraw.set(true);
                },
                UiCommand::MenuAction(a) => self.menu_action(a),
                UiCommand::ResolvePrompt { allow, text } => self.resolve_prompt(allow, text),
                UiCommand::ReloadShield => self.reload_shield(),
                UiCommand::ApplyFingerprintLevel => self.apply_fingerprint_level(),
            }
        }
    }

    fn menu_action(&mut self, a: MenuAction) {
        match a {
            MenuAction::BookmarkToggle => {
                if let Some(t) = self.active_tab() {
                    let is_b = self.core.store.is_bookmarked(&t.url);
                    if is_b {
                        self.core.store.bookmark_remove(&t.url);
                    } else {
                        self.core.store.bookmark_toggle(&t.url, &t.display_title());
                    }
                    self.needs_redraw.set(true);
                }
            },
            MenuAction::CapturePage => {
                if let Some(t) = self.active_tab() {
                    let url = t.url.clone();
                    let host = host_or_url(&url);
                    let path = self
                        .downloads_dir
                        .join(format!("kestrel-{}-{host}.bmp", now_stamp()));
                    self.shared.queue_capture(url.clone(), path.clone());
                    self.capture_pending.borrow_mut().push((url, path));
                }
            },
            MenuAction::DownloadsPage => self.navigate_active("kestrel://downloads"),
            MenuAction::HistoryPage => self.navigate_active("kestrel://history"),
            MenuAction::BookmarksPage => self.navigate_active("kestrel://bookmarks"),
            MenuAction::PrivacyPage => self.navigate_active("kestrel://privacy"),
            MenuAction::SettingsPage => self.navigate_active("kestrel://settings"),
            MenuAction::AboutPage => self.navigate_active("kestrel://about"),
            MenuAction::Exit => {
                self.save_session(true);
                std::process::exit(0);
            },
        }
    }

    /// Called by the delegate (via shared queue) when a page screenshot is ready.
    pub fn start_capture(&mut self, _url: String, path: PathBuf) {
        let Some(wv) = self.active_webview() else { return };
        let id = format!("capture-{}", self.download_seq.get() + 1);
        self.download_seq.set(self.download_seq.get() + 1);
        let core = self.core.clone();
        let proxy = self.shared.proxy.clone();
        core.store.download_add(
            &id,
            "kestrel:capture",
            path.to_str().unwrap_or(""),
            &path.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default(),
            "image/bmp",
            0,
        );
        core.store.download_update(&id, 0, 0, "active");
        wv.take_screenshot(None, move |result| {
            let ok = match result {
                Ok(img) => {
                    let (w, h) = (img.width(), img.height());
                    save_image_bmp(&img.into_raw(), w, h, &path)
                }
                Err(_) => false,
            };
            core.store.download_update(&id, 0, 0, if ok { "done" } else { "failed" });
            let _ = proxy.send_event(KestrelEvent::Wake);
        });
        self.needs_redraw.set(true);
    }

    /* ---------- helpers used by pages ---------- */

    pub fn scratch_get(&self, key: &str) -> String {
        self.scratch.borrow().get(key).cloned().unwrap_or_default()
    }
    pub fn scratch_set(&self, key: &str, val: String) {
        self.scratch.borrow_mut().insert(key.into(), val);
    }

    pub fn reload_shield(&mut self) {
        if let Err(e) = self
            .core
            .reload_filters(self.resources_dir.to_str().unwrap_or("resources"))
        {
            log::warn!("reload filters: {e}");
        }
        self.needs_redraw.set(true);
    }

    pub fn apply_fingerprint_level(&mut self) {
        // Recreate webviews with fresh UserContentManagers.
        let urls: Vec<(usize, String, bool)> = self
            .tabs
            .iter()
            .enumerate()
            .filter(|(_, t)| t.webview.is_some())
            .map(|(i, t)| (i, t.url.clone(), i == self.active))
            .collect();
        for (i, url, is_active) in urls {
            if let Some(wv) = self.make_webview(&url) {
                let old = {
                    let t = &mut self.tabs[i];
                    let old = t.webview.replace(wv);
                    if is_active {
                        if let Some(n) = &t.webview {
                            n.show();
                        }
                    } else if let Some(n) = &t.webview {
                        n.hide();
                    }
                    old
                };
                if let Some(o) = old {
                    o.hide();
                }
            }
        }
    }

    pub fn clear_site_data(&self) {
        // Servo site data: cookies + storage via SiteDataManager.
        self.servo.site_data_manager().clear_cookies(None);
        self.servo
            .site_data_manager()
            .clear_site_data(&[], servo::StorageType::all());
        self.needs_redraw.set(true);
    }

    pub fn resolve_prompt(&mut self, allow: bool, text: String) {
        if let Some(p) = self.prompts.borrow_mut().first_mut() {
            match p {
                Prompt::Permission { request, .. } => {
                    if let Some(req) = request.take() {
                        if allow {
                            req.allow();
                        } else {
                            req.deny();
                        }
                    }
                },
                Prompt::Dialog { control } => {
                    if let Some(c) = control.take() {
                        match c {
                            servo::EmbedderControl::SimpleDialog(d) => match d {
                                servo::SimpleDialog::Alert(a) => a.confirm(),
                                servo::SimpleDialog::Confirm(c) => {
                                    if allow {
                                        c.confirm()
                                    } else {
                                        c.dismiss()
                                    }
                                },
                                servo::SimpleDialog::Prompt(mut p) => {
                                    if allow {
                                        p.set_current_value(&text);
                                        p.confirm();
                                    } else {
                                        p.dismiss();
                                    }
                                },
                            },
                            servo::EmbedderControl::SelectElement(mut s) => {
                                if allow {
                                    s.select(vec![0]);
                                }
                                s.submit();
                            },
                            other => {
                                drop(other);
                            },
                        }
                    }
                },
            }
        }
        self.prompts.borrow_mut().remove(0);
        self.needs_redraw.set(true);
    }

    /* ---------- session ---------- */

    pub fn save_session(&self, clean: bool) {
        let data = SessionData {
            clean,
            active: self.active,
            tabs: self
                .tabs
                .iter()
                .map(|t| SessionTab {
                    url: t.url.clone(),
                    pinned: t.pinned,
                    internal: t.internal.map(|p| p.url().to_string()),
                })
                .collect(),
        };
        if let Ok(json) = serde_json::to_string(&data) {
            self.core.store.session_save(&json);
        }
    }

    /* ---------- input + rendering ---------- */

    pub fn hidpi_scale_factor(
        &self,
    ) -> euclid::Scale<f32, servo::DeviceIndependentPixel, servo::DevicePixel> {
        euclid::Scale::new(self.winit.scale_factor() as f32)
    }

    pub fn chrome_height_px(&self) -> f32 {
        self.gui.borrow().chrome_height()
    }

    pub fn handle_window_event(&mut self, event: WindowEvent) {
        use WindowEvent::*;
        match event {
            RedrawRequested => {
                self.render();
                return;
            },
            CloseRequested => {
                self.save_session(true);
                std::process::exit(0);
            },
            ref e => {
                // Shell shortcuts win over both egui and the page.
                if let WindowEvent::KeyboardInput { event: kev, .. } = e {
                    if self.handle_shortcut(kev) {
                        return;
                    }
                }
                let resp = self.gui.borrow_mut().on_window_event(&self.winit, e);
                if resp.repaint {
                    self.needs_redraw.set(true);
                }
                if resp.consumed {
                    return;
                }
            },
        }
        match event {
            Resized(size) => {
                self.window_ctx.resize(size);
                self.needs_redraw.set(true);
            },
            ScaleFactorChanged { .. } => {
                if let Some(wv) = self.active_webview() {
                    wv.set_hidpi_scale_factor(self.hidpi_scale_factor());
                }
                self.needs_redraw.set(true);
            },
            ModifiersChanged(m) => {
                self.modifiers
                    .set(winit_modifiers_to_kb(m.state()));
            },
            CursorMoved { position, .. } => {
                self.last_mouse.set((position.x, position.y));
                let chrome_top = self.chrome_height_px() * self.winit.scale_factor() as f32;
                if (position.y as f32) < chrome_top {
                    return; // over the chrome: egui owns it
                }
                if let Some(wv) = self.active_webview() {
                    let point = servo::DevicePoint::new(
                        position.x as f32,
                        (position.y as f32 - chrome_top).max(0.0),
                    );
                    wv.notify_input_event(InputEvent::MouseMove(servo::MouseMoveEvent::new(point.into())));
                }
            },
            CursorLeft { .. } => {
                if let Some(wv) = self.active_webview() {
                    wv.notify_input_event(InputEvent::MouseLeftViewport(Default::default()));
                }
            },
            MouseInput { state, button, .. } => {
                if state == ElementState::Pressed {
                    let _ = self.winit.focus_window();
                }
                let (mx, my) = self.last_mouse.get();
                let chrome_top = self.chrome_height_px() * self.winit.scale_factor() as f32;
                if my >= 0.0 && (my as f32) < chrome_top {
                    return;
                }
                if let Some(wv) = self.active_webview() {
                    let (sbtn, action) = (
                        match button {
                            MouseButton::Left => ServoMouseButton::Primary,
                            MouseButton::Right => ServoMouseButton::Secondary,
                            MouseButton::Middle => ServoMouseButton::Auxiliary,
                            _ => ServoMouseButton::Other(0),
                        },
                        match state {
                            ElementState::Pressed => MouseButtonAction::Down,
                            ElementState::Released => MouseButtonAction::Up,
                        },
                    );
                    let point = servo::DevicePoint::new(mx as f32, (my as f32 - chrome_top).max(0.0));
                    wv.notify_input_event(InputEvent::MouseButton(MouseButtonEvent::new(action, sbtn, point.into())));
                }
            },
            MouseWheel { delta, .. } => {
                let (mx, my) = self.last_mouse.get();
                let chrome_top = self.chrome_height_px() * self.winit.scale_factor() as f32;
                if my >= 0.0 && (my as f32) < chrome_top {
                    return;
                }
                if let Some(wv) = self.active_webview() {
                    let (dx, dy) = match delta {
                        winit::event::MouseScrollDelta::LineDelta(x, y) => {
                            (x as f64 * 40.0, y as f64 * 40.0)
                        },
                        winit::event::MouseScrollDelta::PixelDelta(p) => (p.x, p.y),
                    };
                    let wd = servo::WheelDelta {
                        x: dx,
                        y: dy,
                        z: 0.0,
                        mode: servo::WheelMode::DeltaPixel,
                    };
                    let point = servo::DevicePoint::new(mx as f32, (my as f32 - chrome_top).max(0.0));
                    wv.notify_input_event(InputEvent::Wheel(servo::WheelEvent::new(wd, point.into())));
                }
            },
            KeyboardInput { event: kev, .. } => {
                // Not a shell shortcut and not consumed by egui (no focused field):
                // deliver to the page.
                if let Some(wv) = self.active_webview() {
                    let kb = keyboard_event_from_winit(&kev);
                    wv.notify_input_event(InputEvent::Keyboard(kb));
                }
            },
            _ => {},
        }
    }

    fn handle_shortcut(&mut self, event: &winit::event::KeyEvent) -> bool {
        use winit::keyboard::{Key, NamedKey};
        if event.state != ElementState::Pressed {
            return false;
        }
        let m = self.modifiers.get();
        use keyboard_types::Modifiers as KM;
        let ctrl = m.contains(KM::CONTROL) || m.contains(KM::META);
        if let Key::Named(NamedKey::F11) = event.logical_key {
            self.set_fullscreen(!self.fullscreen.get());
            return true;
        }
        if !ctrl {
            // Alt+arrows for back/forward
            if m.contains(KM::ALT) {
                match &event.logical_key {
                    Key::Named(winit::keyboard::NamedKey::ArrowLeft) => {
                        self.commands.borrow_mut().push(UiCommand::Back);
                        return true;
                    },
                    Key::Named(winit::keyboard::NamedKey::ArrowRight) => {
                        self.commands.borrow_mut().push(UiCommand::Forward);
                        return true;
                    },
                    _ => {},
                }
            }
            return false;
        }
        match &event.logical_key {
            Key::Named(NamedKey::F5) => {
                self.commands.borrow_mut().push(UiCommand::Reload);
                true
            },
            Key::Named(NamedKey::Tab) => {
                let n = self.tabs.len();
                if n > 1 {
                    let next = (self.active + 1) % n;
                    self.commands.borrow_mut().push(UiCommand::SelectTab(next));
                }
                true
            },
            Key::Character(c) => {
                let c = c.to_string();
                let lower = c.to_lowercase();
                match lower.as_str() {
                    "t" => {
                        self.commands.borrow_mut().push(UiCommand::NewTab(None));
                        true
                    },
                    "w" => {
                        let i = self.active;
                        self.commands.borrow_mut().push(UiCommand::CloseTab(i));
                        true
                    },
                    "l" => {
                        self.commands.borrow_mut().push(UiCommand::FocusOmnibox);
                        true
                    },
                    "r" => {
                        self.commands.borrow_mut().push(UiCommand::Reload);
                        true
                    },
                    "d" => {
                        self.commands
                            .borrow_mut()
                            .push(UiCommand::MenuAction(MenuAction::BookmarkToggle));
                        true
                    },
                    "j" => {
                        self.commands
                            .borrow_mut()
                            .push(UiCommand::MenuAction(MenuAction::DownloadsPage));
                        true
                    },
                    "h" => {
                        self.commands
                            .borrow_mut()
                            .push(UiCommand::MenuAction(MenuAction::HistoryPage));
                        true
                    },
                    "p" => {
                        self.commands
                            .borrow_mut()
                            .push(UiCommand::MenuAction(MenuAction::PrivacyPage));
                        true
                    },
                    "," => {
                        self.commands
                            .borrow_mut()
                            .push(UiCommand::MenuAction(MenuAction::SettingsPage));
                        true
                    },
                    "q" => {
                        self.save_session(true);
                        std::process::exit(0);
                    },
                    "+" | "=" => {
                        self.commands.borrow_mut().push(UiCommand::ZoomIn);
                        true
                    },
                    "-" => {
                        self.commands.borrow_mut().push(UiCommand::ZoomOut);
                        true
                    },
                    "0" => {
                        self.commands.borrow_mut().push(UiCommand::ZoomReset);
                        true
                    },
                    _ if c == "T" => {
                        self.commands.borrow_mut().push(UiCommand::ReopenClosed);
                        true
                    },
                    _ if c == "O" => {
                        self.commands
                            .borrow_mut()
                            .push(UiCommand::MenuAction(MenuAction::BookmarksPage));
                        true
                    },
                    _ => false,
                }
            },
            _ => false,
        }
    }

    pub fn set_fullscreen(&mut self, on: bool) {
        self.fullscreen.set(on);
        let fs = if on {
            Some(winit::window::Fullscreen::Borderless(None))
        } else {
            None
        };
        self.winit.set_fullscreen(fs);
        self.needs_redraw.set(true);
    }

    fn render(&mut self) {
        {
            let gui = self.gui.borrow_mut();
            gui.update(self);
        }
        {
            let gui = self.gui.borrow_mut();
            gui.paint(&self.winit);
        }
        // egui needs a few warm-up frames after startup (font atlas upload,
        // area positioning). Keep redrawing for the first few frames.
        if self.render_count.get() < 4 {
            self.render_count.set(self.render_count.get() + 1);
            self.needs_redraw.set(true);
        } else {
            self.needs_redraw.set(false);
        }
    }

    pub fn on_external_change(&self) {
        self.needs_redraw.set(true);
    }

    pub fn needs_redraw(&self) -> bool {
        self.needs_redraw.get()
            || self.tabs.iter().any(|t| t.loading)
            || !self.prompts.borrow().is_empty()
            || self.gui.borrow().ui_active()
            || self.gui.borrow().egui_wants_repaint()
    }

    pub fn queue_download(&mut self, url: String, referer: Option<String>) {
        let name = url
            .rsplit('/')
            .next()
            .unwrap_or("download.bin")
            .split('?')
            .next()
            .unwrap_or("download.bin")
            .to_string();
        let name = if name.is_empty() { "download.bin".into() } else { name };
        let seq = self.download_seq.get() + 1;
        self.download_seq.set(seq);
        let id = format!("dl-{seq}");
        let path = self.downloads_dir.join(&name);
        self.core
            .store
            .download_add(&id, &url, path.to_str().unwrap_or(""), &name, "application/octet-stream", 0);
        let proxy = self.shared.proxy.clone();
        downloads::spawn(proxy, id, url, path, referer);
        self.needs_redraw.set(true);
    }
}

/// Map winit modifiers to keyboard-types modifiers.
fn winit_modifiers_to_kb(m: winit::keyboard::ModifiersState) -> keyboard_types::Modifiers {
    let mut out = keyboard_types::Modifiers::empty();
    if m.control_key() { out |= keyboard_types::Modifiers::CONTROL; }
    if m.shift_key() { out |= keyboard_types::Modifiers::SHIFT; }
    if m.alt_key() { out |= keyboard_types::Modifiers::ALT; }
    if m.super_key() { out |= keyboard_types::Modifiers::META; }
    out
}

/// Convert winit key events to servo keyboard events.
pub fn keyboard_event_from_winit(event: &winit::event::KeyEvent) -> servo::KeyboardEvent {
    use winit::keyboard::{Key as WK, NamedKey as WN};
    let key = match &event.logical_key {
        WK::Character(c) => keyboard_types::Key::Character(c.to_string()),
        WK::Named(n) => {
            if *n == WN::Space {
                keyboard_types::Key::Character(" ".to_string())
            } else {
                keyboard_types::Key::Named(named_to_kb(n))
            }
        },
        _ => keyboard_types::Key::Named(keyboard_types::NamedKey::Unidentified),
    };
    let location = match event.location {
        winit::keyboard::KeyLocation::Standard => keyboard_types::Location::Standard,
        winit::keyboard::KeyLocation::Left => keyboard_types::Location::Left,
        winit::keyboard::KeyLocation::Right => keyboard_types::Location::Right,
        winit::keyboard::KeyLocation::Numpad => keyboard_types::Location::Numpad,
    };
    servo::KeyboardEvent::new(keyboard_types::KeyboardEvent {
        state: match event.state {
            ElementState::Pressed => keyboard_types::KeyState::Down,
            ElementState::Released => keyboard_types::KeyState::Up,
        },
        key,
        code: keyboard_types::Code::Unidentified, // physical codes unused by the shell
        location,
        modifiers: keyboard_types::Modifiers::empty(),
        repeat: event.repeat,
        is_composing: false,
    })
}

fn named_to_kb(n: &winit::keyboard::NamedKey) -> keyboard_types::NamedKey {
    use winit::keyboard::NamedKey as W;
    match n {
        W::Enter => keyboard_types::NamedKey::Enter,
        W::Tab => keyboard_types::NamedKey::Tab,
        W::ArrowDown => keyboard_types::NamedKey::ArrowDown,
        W::ArrowUp => keyboard_types::NamedKey::ArrowUp,
        W::ArrowLeft => keyboard_types::NamedKey::ArrowLeft,
        W::ArrowRight => keyboard_types::NamedKey::ArrowRight,
        W::Backspace => keyboard_types::NamedKey::Backspace,
        W::Escape => keyboard_types::NamedKey::Escape,
        W::Home => keyboard_types::NamedKey::Home,
        W::End => keyboard_types::NamedKey::End,
        W::PageUp => keyboard_types::NamedKey::PageUp,
        W::PageDown => keyboard_types::NamedKey::PageDown,
        W::Delete => keyboard_types::NamedKey::Delete,
        W::Insert => keyboard_types::NamedKey::Insert,
        W::F1 => keyboard_types::NamedKey::F1,
        W::F2 => keyboard_types::NamedKey::F2,
        W::F3 => keyboard_types::NamedKey::F3,
        W::F4 => keyboard_types::NamedKey::F4,
        W::F5 => keyboard_types::NamedKey::F5,
        W::F6 => keyboard_types::NamedKey::F6,
        W::F7 => keyboard_types::NamedKey::F7,
        W::F8 => keyboard_types::NamedKey::F8,
        W::F9 => keyboard_types::NamedKey::F9,
        W::F10 => keyboard_types::NamedKey::F10,
        W::F11 => keyboard_types::NamedKey::F11,
        W::F12 => keyboard_types::NamedKey::F12,
        _ => keyboard_types::NamedKey::Unidentified,
    }
}

struct ProxyWaker {
    proxy: EventLoopProxy<KestrelEvent>,
}

impl servo::EventLoopWaker for ProxyWaker {
    fn clone_box(&self) -> Box<dyn servo::EventLoopWaker> {
        Box::new(ProxyWaker { proxy: self.proxy.clone() })
    }
    fn wake(&self) {
        let _ = self.proxy.send_event(KestrelEvent::Wake);
    }
}

#[derive(serde::Serialize, serde::Deserialize)]
pub struct SessionData {
    pub clean: bool,
    pub active: usize,
    pub tabs: Vec<SessionTab>,
}

#[derive(serde::Serialize, serde::Deserialize)]
pub struct SessionTab {
    pub url: String,
    pub pinned: bool,
    pub internal: Option<String>,
}

fn now_stamp() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

pub fn save_image_bmp(data: &[u8], w: u32, h: u32, path: &std::path::Path) -> bool {
    let row_pad = (4 - (w * 4) % 4) % 4;
    let data_size = (w * 4 + row_pad) * h;
    let mut buf: Vec<u8> = Vec::with_capacity(54 + data_size as usize);
    buf.extend_from_slice(b"BM");
    buf.extend_from_slice(&(54 + data_size as u32).to_le_bytes());
    buf.extend_from_slice(&0u32.to_le_bytes());
    buf.extend_from_slice(&54u32.to_le_bytes());
    buf.extend_from_slice(&40u32.to_le_bytes());
    buf.extend_from_slice(&(w as i32).to_le_bytes());
    buf.extend_from_slice(&(h as i32).to_le_bytes());
    buf.extend_from_slice(&1u16.to_le_bytes());
    buf.extend_from_slice(&32u16.to_le_bytes());
    buf.extend_from_slice(&0u32.to_le_bytes());
    buf.extend_from_slice(&(data_size as u32).to_le_bytes());
    for _ in 0..4 {
        buf.extend_from_slice(&0u32.to_le_bytes());
    }
    for y in (0..h).rev() {
        for x in 0..w {
            let p = ((y * w + x) * 4) as usize;
            if p + 3 < data.len() {
                buf.push(data[p + 2]);
                buf.push(data[p + 1]);
                buf.push(data[p]);
                buf.push(data[p + 3]);
            } else {
                buf.extend_from_slice(&[0, 0, 0, 255]);
            }
        }
        for _ in 0..row_pad {
            buf.push(0);
        }
    }
    std::fs::write(path, buf).is_ok()
}

fn event_device() -> winit::event::DeviceId {
    #[allow(unused_unsafe)]
    unsafe {
        winit::event::DeviceId::dummy()
    }
}
