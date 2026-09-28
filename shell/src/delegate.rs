//! Servo delegates: tab metadata, Shield enforcement, permissions, dialogs.

use crate::state::{DelegateMsg, KestrelEvent};
use kestrel_core::KestrelCore;
use servo::webview_delegate::{
    EmbedderControl, NavigationRequest, PermissionRequest, WebViewDelegate, WebResourceLoad,
    WebResourceResponse,
};
use servo::{Cursor, LoadStatus, ServoUrl, WebView};
use std::cell::RefCell;
use std::path::PathBuf;
use std::rc::{Rc, Weak};
use winit::event_loop::EventLoopProxy;

pub struct NoopServoDelegate;
impl servo::ServoDelegate for NoopServoDelegate {}

pub struct DelegateShared {
    pub core: Rc<KestrelCore>,
    pub proxy: EventLoopProxy<KestrelEvent>,
    pub msgs: RefCell<Vec<DelegateMsg>>,
    pub pending_create: RefCell<Vec<servo::CreateNewWebViewRequest>>,
    pub captures: RefCell<Vec<(String, PathBuf)>>,
    pub delegate: Rc<KestrelDelegate>,
}

impl DelegateShared {
    pub fn new(core: Rc<KestrelCore>, proxy: EventLoopProxy<KestrelEvent>) -> Rc<Self> {
        std::rc::Rc::new_cyclic(|weak: &Weak<DelegateShared>| Self {
            core,
            proxy,
            msgs: RefCell::new(Vec::new()),
            pending_create: RefCell::new(Vec::new()),
            captures: RefCell::new(Vec::new()),
            delegate: Rc::new(KestrelDelegate { shared: weak.clone() }),
        })
    }

    pub fn push(&self, msg: DelegateMsg) {
        self.msgs.borrow_mut().push(msg);
    }

    pub fn drain(&self) -> Vec<DelegateMsg> {
        std::mem::take(&mut *self.msgs.borrow_mut())
    }

    pub fn pending_new_webview(&self, req: servo::CreateNewWebViewRequest) {
        self.pending_create.borrow_mut().push(req);
    }

    pub fn take_pending_create(&self) -> Vec<servo::CreateNewWebViewRequest> {
        std::mem::take(&mut *self.pending_create.borrow_mut())
    }

    pub fn queue_capture(&self, url: String, path: PathBuf) {
        self.captures.borrow_mut().push((url, path));
    }

    pub fn take_captures(&self) -> Vec<(String, PathBuf)> {
        std::mem::take(&mut *self.captures.borrow_mut())
    }
}

pub struct KestrelDelegate {
    shared: Weak<DelegateShared>,
}

impl KestrelDelegate {
    fn shared(&self) -> Option<Rc<DelegateShared>> {
        self.shared.upgrade()
    }

    /// Shield decision for a resource request. Returns true when blocked.
    fn shield_block(&self, url: &url::Url, referrer: Option<&url::Url>, is_main_frame: bool) -> bool {
        let Some(shared) = self.shared() else { return false };
        if is_main_frame {
            return false; // never block top-level navigation via filters
        }
        let src = referrer.map(|u| u.to_string()).unwrap_or_default();
        let dest = match url.path().rsplit('.').next() {
            Some("js") => "script",
            Some("css") => "stylesheet",
            Some("png") | Some("jpg") | Some("jpeg") | Some("gif") | Some("webp") | Some("svg") => "image",
            Some("woff") | Some("woff2") | Some("ttf") => "font",
            _ => "other",
        };
        let blocked = shared.core.filters.check(&url.to_string(), &src, dest);
        if blocked {
            let host = url.host_str().unwrap_or("").to_string();
            shared.push(DelegateMsg::Blocked { host });
        }
        blocked
    }
}

impl WebViewDelegate for KestrelDelegate {
    fn notify_url_changed(&self, webview: WebView, url: ServoUrl) {
        if let Some(s) = self.shared() {
            s.push(DelegateMsg::UrlChanged(webview.id(), url.to_string()));
        }
    }

    fn notify_page_title_changed(&self, webview: WebView, title: Option<String>) {
        if let Some(s) = self.shared() {
            s.push(DelegateMsg::TitleChanged(webview.id(), title.unwrap_or_default()));
        }
    }

    fn notify_load_status_changed(&self, webview: WebView, status: LoadStatus) {
        if let Some(s) = self.shared() {
            match status {
                LoadStatus::Started => s.push(DelegateMsg::LoadStarted(webview.id())),
                LoadStatus::Complete => s.push(DelegateMsg::LoadComplete(webview.id())),
                _ => {},
            }
        }
    }

    fn notify_history_changed(&self, webview: WebView, entries: Vec<ServoUrl>, current: usize) {
        if let Some(s) = self.shared() {
            let back = current > 0;
            let fwd = current + 1 < entries.len();
            s.push(DelegateMsg::HistoryChanged(webview.id(), back, fwd));
        }
    }

    fn notify_new_frame_ready(&self, webview: WebView) {
        if let Some(s) = self.shared() {
            s.push(DelegateMsg::FrameReady(webview.id()));
        }
    }

    fn notify_status_text_changed(&self, webview: WebView, status: Option<String>) {
        if let Some(s) = self.shared() {
            s.push(DelegateMsg::StatusText(webview.id(), status));
        }
    }

    fn notify_closed(&self, webview: WebView) {
        if let Some(s) = self.shared() {
            s.push(DelegateMsg::Closed(webview.id()));
        }
    }

    fn notify_crashed(&self, webview: WebView, reason: String, _backtrace: Option<String>) {
        if let Some(s) = self.shared() {
            s.push(DelegateMsg::Crashed(webview.id(), reason));
        }
    }

    fn notify_fullscreen_state_changed(&self, _webview: WebView, is_fullscreen: bool) {
        if let Some(s) = self.shared() {
            let _ = s.proxy.send_event(KestrelEvent::ToggleFullscreen(is_fullscreen));
        }
    }

    fn notify_cursor_changed(&self, _webview: WebView, _cursor: Cursor) {
        // Cursor shapes are rendered by the OS via winit; left default.
    }

    fn request_navigation(&self, _webview: WebView, navigation: NavigationRequest) {
        let Some(s) = self.shared() else {
            navigation.allow();
            return;
        };
        let url = navigation.url().clone();
        if kestrel_core::privacy::looks_like_download(&url.to_string()) {
            navigation.deny();
            // Real download through our own manager (progress tracked in kestrel://downloads).
            let referer = None;
            let _ = referer;
            let proxy = s.proxy.clone();
            let url_str = url.to_string();
            // The window assigns ids/paths; delegate asks the window via a command event.
            let _ = proxy.send_event(KestrelEvent::DownloadQueued(url_str));
            return;
        }
        navigation.allow();
    }

    fn request_create_new(&self, _parent_webview: WebView, request: servo::CreateNewWebViewRequest) {
        // Popups become tabs. The window builds the webview on its own thread context
        // via the request's builder; here we only own the request, so we hand the
        // responsibility to the window through a Wake + pending request slot.
        if let Some(s) = self.shared() {
            s.pending_new_webview(request);
        }
    }

    fn request_permission(&self, webview: WebView, request: PermissionRequest) {
        let Some(s) = self.shared() else {
            request.deny();
            return;
        };
        let feature = format!("{:?}", request.feature());
        let host = webview
            .url()
            .and_then(|u| u.host_str().map(|h| h.to_string()))
            .unwrap_or_default();
        s.push(DelegateMsg::AskPermission {
            webview_id: webview.id(),
            feature,
            host,
            request,
        });
    }

    fn show_embedder_control(&self, webview: WebView, control: EmbedderControl) {
        if let Some(s) = self.shared() {
            s.push(DelegateMsg::AskDialog { webview_id: webview.id(), control });
        }
    }

    fn load_web_resource(&self, _webview: WebView, load: WebResourceLoad) {
        let req = load.request();
        let url = req.url.clone();
        let blocked = self.shield_block(&url, req.referrer_url.as_ref(), req.is_for_main_frame);
        if blocked {
            let resp = WebResourceResponse::new(url).status_code(
                http::StatusCode::OK,
            );
            let mut intercepted = load.intercept(resp);
            intercepted.finish();
        }
        // Non-intercepted loads continue automatically on drop.
    }
}
