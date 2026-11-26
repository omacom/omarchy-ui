//! Starting in a fresh HOME with no theme or config directories yet: `watch` climbs to the
//! nearest existing parent so the first theme created after startup is followed, and subsequent
//! theme swaps keep working once the tree exists.
//!
//! One test in its own binary, because it sets HOME and XDG_RUNTIME_DIR for the whole process.

use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::mpsc;
use std::time::Duration;

fn root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn theme_set(home: &Path, name: &str) {
    let ok = Command::new(root().join("tools/live/theme-set")).arg(home).arg(name).status().expect("tools/live/theme-set");
    assert!(ok.success(), "theme-set {name}");
}

fn isolate() -> PathBuf {
    let tmp = std::env::temp_dir().join(format!("omarchy-theme-first-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&tmp);
    std::fs::create_dir_all(tmp.join("home")).unwrap();
    std::fs::create_dir_all(tmp.join("run")).unwrap();
    let cache = std::env::var_os("XDG_CACHE_HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(std::env::var_os("HOME").unwrap_or_default()).join(".cache"));
    // SAFETY: this binary's only test sets these before it starts any thread.
    unsafe {
        std::env::set_var("HOME", tmp.join("home"));
        std::env::set_var("XDG_RUNTIME_DIR", tmp.join("run"));
        std::env::set_var("XDG_CACHE_HOME", cache);
        std::env::set_var("OMARCHY_UI_ROUNDING", "0");
        std::env::remove_var("OMARCHY_UI_THEME_DIR");
        std::env::remove_var("HYPRLAND_INSTANCE_SIGNATURE");
    }
    tmp
}

#[test]
fn first_theme_in_empty_home_is_followed() {
    let tmp = isolate();
    let home = tmp.join("home");

    let initial = omarchy_theme::load();
    assert_eq!(initial.palette.background.hex(), "#101315ff");
    assert_eq!(initial.theme, "");

    let (tx, rx) = mpsc::channel();
    let _watch = omarchy_theme::watch(initial, move |fresh| {
        let _ = tx.send(fresh);
    });
    assert!(rx.recv_timeout(Duration::from_millis(300)).is_err(), "the initial load is not a change");

    theme_set(&home, "tokyo-night");
    let first = rx.recv_timeout(Duration::from_secs(5)).expect("the first theme is delivered");
    assert_eq!(first.palette.background.hex(), "#1a1b26ff");
    assert_eq!(first.theme, "tokyo-night");
    assert!(rx.recv_timeout(Duration::from_millis(600)).is_err(), "one switch is one change");

    theme_set(&home, "catppuccin-latte");
    let second = rx.recv_timeout(Duration::from_secs(5)).expect("a later swap still reaches the watch");
    assert_eq!(second.palette.background.hex(), "#eff1f5ff");
    assert_eq!(second.theme, "catppuccin-latte");

    let _ = std::fs::remove_dir_all(&tmp);
}
