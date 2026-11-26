//! When Hyprland appears after startup (or restarts) and sends no `configreloaded` event,
//! reconnecting to `.socket2.sock` still refreshes Hyprland options.
//!
//! One test in its own binary, because it sets HOME and XDG_RUNTIME_DIR for the whole process.

use std::io::{Read, Write};
use std::os::unix::net::UnixListener;
use std::path::PathBuf;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc;
use std::time::Duration;

fn isolate() -> PathBuf {
    // Unix socket paths have a 108-byte limit (SUN_LEN), so keep the temp root short under /tmp.
    let tmp = PathBuf::from(format!("/tmp/oth-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&tmp);
    std::fs::create_dir_all(tmp.join("h")).unwrap();
    std::fs::create_dir_all(tmp.join("r")).unwrap();
    let cache = std::env::var_os("XDG_CACHE_HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(std::env::var_os("HOME").unwrap_or_default()).join(".cache"));
    // SAFETY: this binary's only test sets these before it starts any thread.
    unsafe {
        std::env::set_var("HOME", tmp.join("h"));
        std::env::set_var("XDG_RUNTIME_DIR", tmp.join("r"));
        std::env::set_var("XDG_CACHE_HOME", cache);
        std::env::remove_var("OMARCHY_UI_ROUNDING");
        std::env::remove_var("OMARCHY_UI_THEME_DIR");
        std::env::remove_var("HYPRLAND_INSTANCE_SIGNATURE");
    }
    tmp
}

#[test]
fn refreshes_options_after_hyprland_connects_without_config_event() {
    let tmp = isolate();
    let run = tmp.join("r");

    let initial = omarchy_theme::load();
    assert_eq!(initial.radius, 0);

    let (tx, rx) = mpsc::channel();
    let watch = omarchy_theme::watch(initial, move |fresh| {
        let _ = tx.send(fresh);
    });
    assert!(rx.recv_timeout(Duration::from_millis(300)).is_err(), "no Hyprland yet, no change");

    let instance = run.join("hypr/test");
    std::fs::create_dir_all(&instance).unwrap();
    let options = UnixListener::bind(instance.join(".socket.sock")).unwrap();
    options.set_nonblocking(true).unwrap();
    let _events = UnixListener::bind(instance.join(".socket2.sock")).unwrap();

    let done = Arc::new(AtomicBool::new(false));
    let worker_done = done.clone();
    let worker = std::thread::spawn(move || {
        while !worker_done.load(Ordering::SeqCst) {
            match options.accept() {
                Ok((mut conn, _)) => {
                    let _ = conn.set_read_timeout(Some(Duration::from_secs(2)));
                    let mut buf = [0u8; 256];
                    let n = conn.read(&mut buf).unwrap_or(0);
                    let reply: &[u8] = if &buf[..n] == b"j/getoption decoration:rounding" {
                        b"{\"int\":8}"
                    } else {
                        b"{}"
                    };
                    let _ = conn.write_all(reply);
                }
                Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                    std::thread::sleep(Duration::from_millis(20));
                }
                Err(_) => break,
            }
        }
    });

    let fresh = rx.recv_timeout(Duration::from_secs(7)).expect("reconnect refreshes rounding");
    assert_eq!(fresh.radius, 8);
    assert!(rx.recv_timeout(Duration::from_millis(600)).is_err(), "one reconnect is one change");

    drop(watch);
    done.store(true, Ordering::SeqCst);
    let _ = worker.join();
    let _ = std::fs::remove_dir_all(&tmp);
}
