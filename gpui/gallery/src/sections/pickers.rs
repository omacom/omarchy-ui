//! The `pickers` section (spec/components/forms.md).

use omarchy_gpui::Paint;
use omarchy_gpui::gpui::{prelude::*, *};
use omarchy_gpui::ui::{Card, ColorWell, Dialog, MenuRow, MultiSelect, SwatchStrip};

use crate::Gallery;

const MODULES: [&str; 5] = ["Clock", "Battery", "Network", "Volume", "Bluetooth"];
const NOTIFY: [&str; 5] = ["Calendar", "Mail", "Messages", "Updates", "Battery"];

/// What this section keeps between frames.
pub struct State {
    pub modules: Vec<SharedString>,
    pub notify: Vec<SharedString>,
    pub swatch: usize,
}

impl State {
    pub fn new(_cx: &mut App) -> State {
        State {
            modules: MODULES[..4].iter().map(|s| SharedString::from(*s)).collect(),
            notify: vec!["Mail".into(), "Updates".into()],
            swatch: 2,
        }
    }
}

fn flip(list: &mut Vec<SharedString>, v: &SharedString) {
    match list.iter().position(|x| x == v) {
        Some(i) => {
            list.remove(i);
        }
        None => list.push(v.clone()),
    }
}

pub fn build(g: &mut Gallery, _window: &mut Window, cx: &mut Context<Gallery>) -> Vec<AnyElement> {
    let t = Paint::of(cx);
    let sp = t.tokens().spacing.clone();
    let p = &g.more.pickers;
    let closed = MultiSelect::new("ms-modules", MODULES.map(|m| (m, m)), p.modules.clone()).label("Bar modules").on_choose(cx.listener(
        |g: &mut Gallery, v: &SharedString, _, cx| {
            flip(&mut g.more.pickers.modules, v);
            cx.notify();
        },
    ));
    // Held open: the popup draws above the page, below the trigger, inside this box.
    let open = MultiSelect::new("ms-notify", NOTIFY.map(|m| (m, m)), p.notify.clone()).label("Notify for").default_open(true).on_choose(
        cx.listener(|g: &mut Gallery, v: &SharedString, _, cx| {
            flip(&mut g.more.pickers.notify, v);
            cx.notify();
        }),
    );
    let selects = div().flex().flex_row().items_start().gap(px(sp.xxl as f32)).child(closed).child(div().h(px(280.0)).child(open));

    let wells = div()
        .flex()
        .flex_row()
        .items_center()
        .gap(px(sp.control_gap as f32))
        .child(ColorWell::new(t.accent()).id("well-accent"))
        .child(ColorWell::new(t.success()).id("well-success").cursor(true))
        .child(ColorWell::new(t.urgent()).id("well-urgent"));
    let strip = SwatchStrip::new([t.accent(), t.urgent(), t.warning(), t.success(), t.fg(), t.dim(), t.faint(), t.muted()], Some(p.swatch))
        .on_pick(cx.listener(|g: &mut Gallery, i: &usize, _, cx| {
            g.more.pickers.swatch = *i;
            cx.notify();
        }));

    // Held open over the content it would cover, so the scrim shows.
    let behind = div().absolute().top_0().left_0().size_full().flex().child(
        Card::new().title("Displays").width(420.0).child(
            div()
                .flex()
                .flex_col()
                .w_full()
                .child(MenuRow::new("Built-in display").checked(true))
                .child(MenuRow::new("External monitor"))
                .child(MenuRow::new("Projector")),
        ),
    );
    let dialog = Dialog::new("Discard changes?")
        .message("Your edits to Tokyo Night will be lost.")
        .labels("Keep editing", "Discard")
        .destructive(true)
        .scrim(true);
    let held = div().relative().flex_shrink_0().w(px(420.0)).h(px(240.0)).child(behind).child(dialog);

    vec![selects.into_any_element(), wells.into_any_element(), strip.into_any_element(), held.into_any_element()]
}
