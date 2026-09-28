//! Application handler: owns the window, drives servo + rendering.

use crate::state::KestrelEvent;
use crate::window::KestrelWindow;
use winit::application::ApplicationHandler;
use winit::event::WindowEvent;
use winit::event_loop::{ActiveEventLoop, EventLoopProxy};
use winit::window::WindowId;

pub struct KestrelApp {
    proxy: EventLoopProxy<KestrelEvent>,
    initial_url: Option<String>,
    window: Option<KestrelWindow>,
    last_session_save: std::time::Instant,
}

impl KestrelApp {
    pub fn new(proxy: EventLoopProxy<KestrelEvent>, initial_url: Option<String>) -> Self {
        KestrelApp {
            proxy,
            initial_url,
            window: None,
            last_session_save: std::time::Instant::now(),
        }
    }
}

impl ApplicationHandler<KestrelEvent> for KestrelApp {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.window.is_none() {
            match KestrelWindow::new(event_loop, self.proxy.clone(), self.initial_url.take()) {
                Ok(win) => self.window = Some(win),
                Err(e) => {
                    log::error!("failed to create window: {e}");
                    event_loop.exit();
                },
            }
        }
    }

    fn window_event(&mut self, _event_loop: &ActiveEventLoop, id: WindowId, event: WindowEvent) {
        if let Some(win) = &mut self.window {
            if win.winit.id() == id {
                win.handle_window_event(event);
            }
        }
    }

    fn user_event(&mut self, _event_loop: &ActiveEventLoop, event: KestrelEvent) {
        let Some(win) = &mut self.window else { return };
        match event {
            KestrelEvent::Wake => win.pump_servo(),
            KestrelEvent::DownloadQueued(url) => {
                win.queue_download(url, None);
            },
            KestrelEvent::DownloadProgress { id, received, total } => {
                win.core.store.download_update(&id, received, total, "active");
                win.on_external_change();
            },
            KestrelEvent::DownloadDone { id, ok, path } => {
                let state = if ok { "done" } else { "failed" };
                win.core.store.download_update(&id, 0, 0, state);
                if ok {
                    win.core.store.download_set_path(&id, &path, "");
                }
                win.on_external_change();
            },
            KestrelEvent::ToggleFullscreen(on) => {
                win.set_fullscreen(on);
            },
        }
    }

    fn about_to_wait(&mut self, _event_loop: &ActiveEventLoop) {
        let Some(win) = &mut self.window else { return };
        // Pump servo whenever it has work; delegate messages are applied inside.
        win.pump_servo();
        win.process_commands();

        // Periodic session journaling (also serves crash recovery).
        if self.last_session_save.elapsed().as_secs() >= 15 {
            self.last_session_save = std::time::Instant::now();
            win.save_session(false);
        }

        if win.needs_redraw() {
            win.winit.request_redraw();
        }
    }

    fn exiting(&mut self, _event_loop: &ActiveEventLoop) {
        if let Some(win) = &self.window {
            win.save_session(true);
        }
    }
}
