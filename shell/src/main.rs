//! Kestrel Browser v2 — entry point.
//! Rust shell hosting the Servo engine with a native egui chrome.

mod app;
mod delegate;
mod downloads;
mod gui;
mod pages;
mod state;
mod window;

use state::KestrelEvent;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("warn")).init();

    let args: Vec<String> = std::env::args().skip(1).collect();
    let initial_url = args.first().map(|s| s.clone());

    let event_loop = winit::event_loop::EventLoop::<KestrelEvent>::with_user_event().build()?;
    let proxy = event_loop.create_proxy();
    let mut app = app::KestrelApp::new(proxy, initial_url);
    event_loop.run_app(&mut app)?;
    Ok(())
}
