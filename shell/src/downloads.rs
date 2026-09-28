//! Real download manager: worker threads fetching via ureq, progress events
//! delivered through the winit event loop proxy.

use crate::state::KestrelEvent;
use std::io::Write;
use std::path::PathBuf;
use winit::event_loop::EventLoopProxy;

pub fn spawn(proxy: EventLoopProxy<KestrelEvent>, id: String, url: String, path: PathBuf, referer: Option<String>) {
    std::thread::spawn(move || {
        let _ = proxy.send_event(KestrelEvent::DownloadQueued(id.clone()));
        let mut req = ureq::get(&url);
        if let Some(r) = &referer {
            req = req.set("Referer", r);
        }
        match req.call() {
            Ok(resp) => {
                let total = resp
                    .header("Content-Length")
                    .and_then(|h| h.parse::<i64>().ok())
                    .unwrap_or(0);
                let file = std::fs::File::create(&path);
                let mut file = match file {
                    Ok(f) => f,
                    Err(e) => {
                        log::error!("download create file failed: {e}");
                        let _ = proxy.send_event(KestrelEvent::DownloadDone { id, ok: false, path: path.display().to_string() });
                        return;
                    },
                };
                let mut reader = resp.into_reader();
                let mut buf = [0u8; 65536];
                let mut received: i64 = 0;
                let mut last_event: i64 = 0;
                let mut ok = true;
                loop {
                    match reader.read(&mut buf) {
                        Ok(0) => break,
                        Ok(n) => {
                            if file.write_all(&buf[..n]).is_err() {
                                ok = false;
                                break;
                            }
                            received += n as i64;
                            if received - last_event >= 262_144 {
                                last_event = received;
                                let _ = proxy.send_event(KestrelEvent::DownloadProgress {
                                    id: id.clone(),
                                    received,
                                    total,
                                });
                            }
                        },
                        Err(_) => {
                            ok = false;
                            break;
                        },
                    }
                }
                let _ = file.flush();
                let _ = proxy.send_event(KestrelEvent::DownloadDone {
                    id,
                    ok,
                    path: path.display().to_string(),
                });
            },
            Err(e) => {
                log::error!("download {url} failed: {e}");
                let _ = proxy.send_event(KestrelEvent::DownloadDone {
                    id,
                    ok: false,
                    path: path.display().to_string(),
                });
            },
        }
    });
}
