//! Shared types: tabs, UI commands, delegate messages, events.

use servo::webview_delegate::{EmbedderControl, PermissionRequest};
use servo::{WebView, WebViewId};

/// Native (non-web) tab pages drawn directly with egui by the shell.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum InternalPage {
    NewTab,
    Settings,
    History,
    Bookmarks,
    Downloads,
    Privacy,
    About,
}

impl InternalPage {
    pub fn from_url(url: &str) -> Option<InternalPage> {
        let rest = url.strip_prefix("kestrel://")?;
        let page = rest.split('?').next()?;
        Some(match page {
            "newtab" | "" => InternalPage::NewTab,
            "settings" => InternalPage::Settings,
            "history" => InternalPage::History,
            "bookmarks" => InternalPage::Bookmarks,
            "downloads" => InternalPage::Downloads,
            "privacy" => InternalPage::Privacy,
            "about" => InternalPage::About,
            _ => return None,
        })
    }
    pub fn url(&self) -> &'static str {
        match self {
            InternalPage::NewTab => "kestrel://newtab",
            InternalPage::Settings => "kestrel://settings",
            InternalPage::History => "kestrel://history",
            InternalPage::Bookmarks => "kestrel://bookmarks",
            InternalPage::Downloads => "kestrel://downloads",
            InternalPage::Privacy => "kestrel://privacy",
            InternalPage::About => "kestrel://about",
        }
    }
}

#[derive(Clone)]
pub struct Tab {
    pub id: WebViewId,
    pub webview: Option<WebView>,
    pub internal: Option<InternalPage>,
    pub url: String,
    pub title: String,
    pub loading: bool,
    pub can_back: bool,
    pub can_forward: bool,
    pub crashed: Option<String>,
    pub pinned: bool,
    pub zoom: f32,
}

impl Tab {
    pub fn display_title(&self) -> String {
        if !self.title.is_empty() {
            self.title.clone()
        } else if !self.url.is_empty() {
            host_or_url(&self.url)
        } else {
            "New Tab".into()
        }
    }
}

pub fn host_or_url(url: &str) -> String {
    url::Url::parse(url)
        .ok()
        .and_then(|u| u.host_str().map(|h| h.to_string()))
        .unwrap_or_else(|| url.to_string())
}

/// Commands produced by the egui chrome, applied by the window after the frame.
#[derive(Clone)]
pub enum UiCommand {
    Back,
    Forward,
    Reload,
    Load(String),
    NewTab(Option<String>),
    CloseTab(usize),
    SelectTab(usize),
    MoveTab(usize, usize),
    TogglePin(usize),
    DuplicateTab(usize),
    ReopenClosed,
    OpenInternal(InternalPage),
    FocusOmnibox,
    ZoomIn,
    ZoomOut,
    ZoomReset,
    MenuAction(MenuAction),
}

#[derive(Clone, Copy, PartialEq)]
pub enum MenuAction {
    BookmarkToggle,
    CapturePage,
    DownloadsPage,
    HistoryPage,
    BookmarksPage,
    PrivacyPage,
    SettingsPage,
    AboutPage,
    Exit,
}

/// Events delivered through the winit event loop proxy.
pub enum KestrelEvent {
    /// Servo needs the event loop pumped.
    Wake,
    DownloadQueued(String),
    DownloadProgress { id: String, received: i64, total: i64 },
    DownloadDone { id: String, ok: bool, path: String },
    ToggleFullscreen(bool),
}

/// Messages queued by servo delegates while the servo event loop spins.
pub enum DelegateMsg {
    UrlChanged(WebViewId, String),
    TitleChanged(WebViewId, String),
    LoadStarted(WebViewId),
    LoadComplete(WebViewId),
    HistoryChanged(WebViewId, bool, bool),
    FrameReady(WebViewId),
    Closed(WebViewId),
    Crashed(WebViewId, String),
    Fullscreen(bool),
    NewWebView(WebView),
    Blocked { host: String },
    AskPermission {
        webview_id: WebViewId,
        feature: String,
        host: String,
        request: PermissionRequest,
    },
    AskDialog {
        webview_id: WebViewId,
        control: EmbedderControl,
    },
    StatusText(WebViewId, Option<String>),
}

/// A modal prompt waiting for user action.
pub enum Prompt {
    Permission {
        webview_id: WebViewId,
        feature: String,
        host: String,
        request: Option<PermissionRequest>,
    },
    Dialog {
        webview_id: WebViewId,
        control: Option<EmbedderControl>,
    },
}

impl Prompt {
    pub fn host_label(&self) -> String {
        match self {
            Prompt::Permission { host, feature, .. } => format!("{host} requests {feature}"),
            Prompt::Dialog { control, .. } => {
                let msg = match control {
                    Some(EmbedderControl::SimpleDialog(s)) => s.message().to_string(),
                    Some(EmbedderControl::SelectElement(_)) => "Choose an option".to_string(),
                    Some(EmbedderControl::ColorPicker(_)) => "Pick a color".to_string(),
                    _ => "Page dialog".to_string(),
                };
                msg
            },
        }
    }
}

/// Normalize what the user typed in the omnibox into a loadable URL.
pub fn normalize_input(input: &str, search_url: &str) -> String {
    let input = input.trim();
    if input.is_empty() {
        return String::new();
    }
    if input.starts_with("kestrel://") {
        return input.to_string();
    }
    let looks_like_url = input.starts_with("http://")
        || input.starts_with("https://")
        || (input.contains('.')
            && !input.contains(' ')
            && url::Url::parse(&format!("https://{input}")).is_ok());
    if looks_like_url {
        if input.starts_with("http") {
            input.to_string()
        } else {
            format!("https://{input}")
        }
    } else {
        search_url.replace("{q}", &urlencoding_lite(input))
    }
}

/// Minimal percent-encoding for a search query.
pub fn urlencoding_lite(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for b in s.bytes() {
        match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => {
                out.push(b as char)
            },
            b'+' => out.push('+'),
            _ => out.push_str(&format!("%{b:02X}")),
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn internal_pages_roundtrip() {
        for p in [
            InternalPage::NewTab,
            InternalPage::Settings,
            InternalPage::History,
            InternalPage::Bookmarks,
            InternalPage::Downloads,
            InternalPage::Privacy,
            InternalPage::About,
        ] {
            assert_eq!(InternalPage::from_url(p.url()), Some(p));
        }
    }

    #[test]
    fn normalize_urls_and_search() {
        assert_eq!(normalize_input("example.com", "https://ddg/{q}"), "https://example.com");
        assert_eq!(normalize_input("kestrel://settings", "x"), "kestrel://settings");
        assert_eq!(
            normalize_input("rust async", "https://duckduckgo.com/?q={q}"),
            "https://duckduckgo.com/?q=rust%20async"
        );
    }
}
