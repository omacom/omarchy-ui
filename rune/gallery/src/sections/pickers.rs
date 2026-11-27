//! The `pickers` section (spec/components/forms.md).

use omarchy_rune::Theme;
use omarchy_rune::ui::{Card, ColorWell, Dialog, MenuRow, MultiSelect, SwatchStrip};
use rune::{El, TextEdit, col, el, row};

use crate::State;

const MODULES: [&str; 5] = ["Clock", "Battery", "Network", "Volume", "Bluetooth"];
const NOTIFY: [&str; 5] = ["Calendar", "Mail", "Messages", "Updates", "Battery"];

pub struct Pickers {
    pub modules: Vec<String>,
    pub modules_open: bool,
    pub notify: Vec<String>,
    pub notify_open: bool,
    pub search: TextEdit,
    /// The notify popup's keyboard row (`None`: the keys are in the search).
    pub notify_row: Option<usize>,
    pub swatch: usize,
}

pub fn init() -> Pickers {
    Pickers {
        modules: MODULES[..4].iter().map(|s| s.to_string()).collect(),
        modules_open: false,
        notify: vec!["Mail".into(), "Updates".into()],
        notify_open: true,
        search: TextEdit::new(""),
        notify_row: None,
        swatch: 2,
    }
}

fn flip(list: &mut Vec<String>, v: &str) {
    match list.iter().position(|x| x == v) {
        Some(i) => {
            list.remove(i);
        }
        None => list.push(v.to_string()),
    }
}

pub fn build(s: &State, t: &Theme, layer: &mut Vec<El>) -> Vec<El> {
    let p = &s.pickers;
    let sp = &t.tokens().spacing;
    let closed = MultiSelect::new("ms-modules", MODULES.map(|m| (m, m)), p.modules.clone())
        .label("Bar modules")
        .open(p.modules_open)
        .on_toggle(|s: &mut State| s.pickers.modules_open ^= true)
        .on_choose(|s: &mut State, v: &str| flip(&mut s.pickers.modules, v))
        .build(t, layer);
    // Held open: the popup draws in the overlay layer, below the trigger, inside this box.
    let open = MultiSelect::new("ms-notify", NOTIFY.map(|m| (m, m)), p.notify.clone())
        .label("Notify for")
        .open(p.notify_open)
        .search(&p.search)
        .highlighted(p.notify_row)
        .on_state(|s: &mut State| (&mut s.pickers.notify_open, &mut s.pickers.search, &mut s.pickers.notify_row))
        .on_choose(|s: &mut State, v: &str| flip(&mut s.pickers.notify, v))
        .build(t, layer);
    let selects = row().items_start().gap(sp.xxl as f32).child(closed).child(col().h(280.0).child(open));

    let wells = row()
        .items_center()
        .gap(sp.control_gap as f32)
        .child(ColorWell::new(t.accent()).build(t))
        .child(ColorWell::new(t.success()).cursor(true).build(t))
        .child(ColorWell::new(t.urgent()).build(t));
    let strip = SwatchStrip::new([t.accent(), t.urgent(), t.warning(), t.success(), t.fg(), t.dim(), t.faint(), t.muted()], Some(p.swatch))
        .on_pick(|s: &mut State, i| s.pickers.swatch = i)
        .build(t);

    // Held open over the content it would cover, so the scrim shows.
    let behind = Card::new()
        .title("Displays")
        .width(420.0)
        .child(
            col()
                .w_full()
                .child(MenuRow::new("Built-in display").checked(true).build(t))
                .child(MenuRow::new("External monitor").build(t))
                .child(MenuRow::new("Projector").build(t)),
        )
        .build(t)
        .h(240.0);
    let dialog = Dialog::new("Discard changes?")
        .message("Your edits to Tokyo Night will be lost.")
        .labels("Keep editing", "Discard")
        .destructive(true)
        .scrim(true)
        .build(t);
    let held = el().size(420.0, 240.0).child(behind).child(dialog);

    vec![selects, wells, strip, held]
}
