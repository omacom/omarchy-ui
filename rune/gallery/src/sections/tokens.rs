//! The `tokens` section (spec/gallery.md).

use omarchy_rune::{Theme, border, color};
use rune::{El, col, el, row, text};

use crate::State;

pub fn build(_s: &State, t: &Theme, _layer: &mut Vec<El>) -> Vec<El> {
    let tk = t.tokens();
    let sp = &tk.spacing;
    let p = &tk.palette;
    let mut swatches = row().gap(sp.lg as f32);
    let r = t.radius_small();
    let bw = 1.0;
    for (name, c) in [
        ("foreground", p.foreground),
        ("background", p.background),
        ("accent", p.accent),
        ("urgent", p.urgent),
        ("muted", p.muted),
    ] {
        swatches = swatches.child(
            col()
                .gap(sp.xs as f32)
                .child(
                    el()
                        .size(64.0, 32.0)
                        .radius(r)
                        .border(bw, color(tk.controls.normal.border.color))
                        .child(el().pin(Some(bw), Some(bw), Some(bw), Some(bw)).radius((r - bw).max(0.0)).fill(color(c))),
                )
                .child(text(name).font(t.text_style(tk.font.caption)).color(t.fg())),
        );
    }
    let c = &tk.controls;
    let mut states = row().gap(sp.lg as f32);
    for (name, st) in [("normal", &c.normal), ("hover-cursor", &c.hover), ("focus", &c.focus), ("selected", &c.selected)] {
        let selected = name == "selected";
        let label = text(name)
            .font(if selected { t.text_style_bold(tk.font.body) } else { t.text_style(tk.font.body) })
            .color(color(st.text));
        let chip = el().size(120.0, sp.control_height as f32).center().radius(t.radius()).fill(color(st.fill)).child(label);
        states = states.child(border(chip, &st.border));
    }
    let f = &tk.font;
    let mut scale = col().gap(sp.xs as f32);
    for (name, size) in [
        ("displayLarge", f.display_large),
        ("display", f.display),
        ("heading", f.heading),
        ("title", f.title),
        ("subtitle", f.subtitle),
        ("body", f.body),
        ("bodySmall", f.body_small),
        ("caption", f.caption),
    ] {
        scale = scale.child(text(format!("{name} {size}")).font(t.text_style(size)).color(t.fg()));
    }
    vec![swatches, states, scale]
}
