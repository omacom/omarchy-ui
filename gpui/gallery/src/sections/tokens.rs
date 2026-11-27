//! The `tokens` section (spec/gallery.md).

use omarchy_gpui::gpui::{prelude::*, *};
use omarchy_gpui::omarchy_theme::{BorderSpec, Rgba};
use omarchy_gpui::ui::{border, label};
use omarchy_gpui::{Paint, color};

use crate::Gallery;

/// Fill inset inside a border ring so translucent borders composite over the page background
/// rather than over the box's own fill (matching Qt's `Rectangle` / `BorderSurface`).
fn surface_box(fill: Rgba, spec: &BorderSpec, radius: Pixels) -> Div {
    let w = spec.widths;
    let r = f32::from(radius);
    let inner_r = |a: f64, b: f64| px((r - a.max(b) as f32).max(0.0));
    let mut box_el = div().relative().rounded(radius);
    if fill.a > 0 {
        box_el = box_el.child(
            div()
                .absolute()
                .top(px(w[0] as f32))
                .right(px(w[1] as f32))
                .bottom(px(w[2] as f32))
                .left(px(w[3] as f32))
                .rounded_tl(inner_r(w[0], w[3]))
                .rounded_tr(inner_r(w[0], w[1]))
                .rounded_br(inner_r(w[2], w[1]))
                .rounded_bl(inner_r(w[2], w[3]))
                .bg(color(fill)),
        );
    }
    if !spec.is_none() {
        box_el = box_el.child(border(div().absolute().top_0().left_0().size_full().rounded(radius), spec));
    }
    box_el
}

pub fn build(_: &mut Gallery, _: &mut Window, cx: &mut Context<Gallery>) -> Vec<AnyElement> {
    let t = Paint::of(cx);
    let tk = t.tokens();
    let sp = &tk.spacing;
    let p = &tk.palette;
    let swatch_border = BorderSpec { color: tk.controls.normal.border.color, widths: [1.0; 4], gradient: None };
    let mut swatches = div().flex().flex_row().gap(px(sp.lg as f32));
    for (name, c) in [("foreground", p.foreground), ("background", p.background), ("accent", p.accent), ("urgent", p.urgent), ("muted", p.muted)] {
        swatches = swatches.child(
            div()
                .flex()
                .flex_col()
                .gap(px(sp.xs as f32))
                .child(surface_box(c, &swatch_border, t.radius_small()).w(px(64.0)).h(px(32.0)))
                .child(label(&t, name, tk.font.caption, false, t.fg())),
        );
    }
    let c = &tk.controls;
    let mut states = div().flex().flex_row().gap(px(sp.lg as f32));
    for (name, st) in [("normal", &c.normal), ("hover-cursor", &c.hover), ("focus", &c.focus), ("selected", &c.selected)] {
        let chip = surface_box(st.fill, &st.border, t.radius())
            .flex()
            .items_center()
            .justify_center()
            .w(px(120.0))
            .h(px(sp.control_height as f32))
            .child(label(&t, name, tk.font.body, name == "selected", color(st.text)));
        states = states.child(chip);
    }
    let f = &tk.font;
    let mut scale = div().flex().flex_col().gap(px(sp.xs as f32));
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
        scale = scale.child(label(&t, format!("{name} {size}"), size, false, t.fg()));
    }
    vec![swatches.into_any_element(), states.into_any_element(), scale.into_any_element()]
}
