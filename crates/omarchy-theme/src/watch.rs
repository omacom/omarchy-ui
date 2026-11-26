//! Following the theme live, without polling.
//!
//! - A theme switch swaps `~/.local/state/omarchy/current/theme` as a whole directory and then
//!   rewrites `current/theme.name`, so the watch is on `current/`, not on files inside the theme.
//!   The theme directory itself is watched too, re-armed after every change (a swap leaves the
//!   watch on the old directory), so an edit inside it, such as a new `icons.theme`, is seen.
//! - `~/.config/omarchy/shell.toml` (`omarchy display text size`) and fontconfig's `fonts.conf`
//!   (`omarchy font set`) are watched through their parent directories, since editors and the
//!   CLIs replace them by rename.
//! - Hyprland announces `configreloaded` on `.socket2.sock` once new rounding and gaps are in
//!   effect, which also covers `omarchy toggle animations`.
//!
//! Events are coalesced over 150 ms, then the tokens are re-resolved and delivered only if
//! they changed.

use std::collections::BTreeSet;
use std::io::{BufRead, BufReader};
use std::os::unix::net::UnixStream;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, mpsc};
use std::time::Duration;

use notify::{RecursiveMode, Watcher as _};

use crate::Tokens;
use crate::source;

/// Keeps the watch running; dropping it stops delivering changes. Once the drop returns, no
/// new delivery starts (one already running finishes on its own thread).
pub struct Watch {
    _watcher: Arc<Mutex<Option<notify::RecommendedWatcher>>>,
    stop: Arc<AtomicBool>,
    wake: mpsc::Sender<()>,
}

impl Drop for Watch {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::SeqCst);
        // Wake the delivery thread so it sees `stop` and ends, letting the rest go with it.
        let _ = self.wake.send(());
    }
}

fn watch_roots(pinned: bool) -> Vec<PathBuf> {
    let mut roots = vec![source::theme_dir()];
    if !pinned {
        roots.push(source::current_dir());
        if let Some(p) = source::user_shell_path().parent() {
            roots.push(p.to_path_buf());
        }
        if let Some(h) = std::env::var_os("HOME") {
            roots.push(PathBuf::from(h).join(".config/fontconfig"));
        }
    }
    roots
}

fn nearest_existing_dir(mut path: PathBuf) -> Option<PathBuf> {
    while !path.is_dir() {
        let Some(parent) = path.parent() else { return None };
        if parent == path {
            return None;
        }
        path = parent.to_path_buf();
    }
    Some(path)
}

/// Watch each target directory or, in a fresh home where it does not exist yet, its nearest
/// existing parent. Renews `current/theme` when unpinned so a directory swap doesn't leave the
/// watch on the unlinked inode.
fn arm_watches(w: &mut notify::RecommendedWatcher, watched: &mut BTreeSet<PathBuf>, pinned: bool) {
    let wanted: BTreeSet<PathBuf> = watch_roots(pinned).into_iter().filter_map(nearest_existing_dir).collect();
    let theme_dir = source::theme_dir();

    let to_remove: Vec<PathBuf> = watched
        .iter()
        .filter(|p| !wanted.contains(*p) || (!pinned && **p == theme_dir))
        .cloned()
        .collect();
    for path in to_remove {
        let _ = w.unwatch(&path);
        watched.remove(&path);
    }
    for path in wanted {
        if !watched.contains(&path) && w.watch(&path, RecursiveMode::NonRecursive).is_ok() {
            watched.insert(path);
        }
    }
}

/// Call `on_change` on a background thread with fresh tokens whenever the theme, the user's
/// shell.toml, the font or Hyprland's config changes. With `OMARCHY_UI_THEME_DIR` set, watches
/// that directory instead and ignores the desktop.
pub fn watch(initial: Tokens, mut on_change: impl FnMut(Tokens) + Send + 'static) -> Watch {
    let (tx, rx) = mpsc::channel::<()>();
    let pinned = source::is_pinned();
    let mut watched = BTreeSet::new();

    let fs_tx = tx.clone();
    let watcher = notify::recommended_watcher(move |res: notify::Result<notify::Event>| {
        if let Ok(ev) = res
            && !ev.kind.is_access()
        {
            let _ = fs_tx.send(());
        }
    })
    .ok()
    .map(|mut w| {
        arm_watches(&mut w, &mut watched, pinned);
        w
    });
    let watcher = Arc::new(Mutex::new(watcher));
    let rearm = watcher.clone();
    let stop = Arc::new(AtomicBool::new(false));
    let stopped = stop.clone();

    if !pinned {
        let hypr_tx = tx.clone();
        let hypr_stop = stop.clone();
        std::thread::Builder::new()
            .name("omarchy-theme-hypr".into())
            .spawn(move || hypr_events(hypr_tx, hypr_stop))
            .ok();
    }

    std::thread::Builder::new()
        .name("omarchy-theme-watch".into())
        .spawn(move || {
            let mut last = initial;
            while rx.recv().is_ok() {
                // Coalesce the burst a theme switch makes (dir swap, theme.name, Hyprland reload).
                while rx.recv_timeout(Duration::from_millis(150)).is_ok() {}
                if stopped.load(Ordering::SeqCst) {
                    return;
                }
                if let Some(w) = rearm.lock().unwrap().as_mut() {
                    arm_watches(w, &mut watched, pinned);
                }
                let next = crate::load();
                if next != last && !stopped.load(Ordering::SeqCst) {
                    last = next.clone();
                    on_change(next);
                }
            }
        })
        .ok();

    Watch { _watcher: watcher, stop, wake: tx }
}

fn hypr_events(tx: mpsc::Sender<()>, stop: Arc<AtomicBool>) {
    let mut refresh_on_connect = false;
    while !stop.load(Ordering::SeqCst) {
        if let Some(dir) = source::hypr_dir()
            && let Ok(stream) = UnixStream::connect(dir.join(".socket2.sock"))
        {
            // A compositor that started or restarted may have loaded its config before we
            // connected; trigger one coalesced refresh.
            if refresh_on_connect && tx.send(()).is_err() {
                return;
            }
            for line in BufReader::new(stream).lines() {
                let Ok(line) = line else { break };
                if line.starts_with("configreloaded>>") && tx.send(()).is_err() {
                    return;
                }
            }
        }
        // Hyprland restarted or isn't up yet; try again shortly.
        refresh_on_connect = true;
        std::thread::sleep(Duration::from_secs(2));
    }
}
