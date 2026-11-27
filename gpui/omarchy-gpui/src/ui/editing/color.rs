//! ColorPicker: `ColorPicker.qml`. An HSV picker with a saturation/value square, a hue strip,
//! an optional alpha strip, a hex field and a row of the theme's palette colors.

use std::cell::{Cell, RefCell};
use std::rc::Rc;

use gpui::prelude::*;
use gpui::{
    App, ClickEvent, CursorStyle, Div, ElementId, Entity, Focusable, Hsla, KeyDownEvent, SharedString, Subscription, Window, div, linear_color_stop,
    linear_gradient, px,
};
use omarchy_theme::{BorderSpec, Rgba};

use super::{DragFn, DragPhase, follow_drag};
use crate::ui::button::pressable;
use crate::ui::field::TextChanged;
use crate::ui::{Handler, Look, TextField, TextInput, chrome, focus_handle, keyed_state, tab_keys};
use crate::{Paint, color};

/// A color as hue, saturation, value and alpha, each 0–1. The picker keeps HSV so the hue
/// survives dragging to grey or black.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Hsva {
    pub h: f32,
    pub s: f32,
    pub v: f32,
    pub a: f32,
}

impl Hsva {
    pub fn new(h: f32, s: f32, v: f32, a: f32) -> Hsva {
        Hsva { h, s, v, a }
    }
    /// `#rrggbb` or `#rrggbbaa` (the `#` optional). Grey keeps `hue`.
    pub fn from_hex(hex: &str, hue: f32) -> Option<Hsva> {
        let hex = hex.trim().trim_start_matches('#');
        if !(hex.len() == 6 || hex.len() == 8) || !hex.chars().all(|c| c.is_ascii_hexdigit()) {
            return None;
        }
        let byte = |i: usize| u8::from_str_radix(&hex[i..i + 2], 16).ok();
        let (r, g, b) = (byte(0)? as f32 / 255.0, byte(2)? as f32 / 255.0, byte(4)? as f32 / 255.0);
        let a = if hex.len() == 8 { byte(6)? as f32 / 255.0 } else { 1.0 };
        let max = r.max(g).max(b);
        let min = r.min(g).min(b);
        let d = max - min;
        let mut h = hue;
        if d > 0.0 {
            h = if max == r {
                ((g - b) / d) / 6.0
            } else if max == g {
                ((b - r) / d + 2.0) / 6.0
            } else {
                ((r - g) / d + 4.0) / 6.0
            };
            if h < 0.0 {
                h += 1.0;
            }
        }
        Some(Hsva { h, s: if max > 0.0 { d / max } else { 0.0 }, v: max, a })
    }
    /// The 0–255 channels, by the same arithmetic as the Qt picker.
    pub fn rgb8(self) -> [u8; 3] {
        let h6 = (self.h - self.h.floor()) * 6.0;
        let i = h6.floor();
        let f = h6 - i;
        let (s, v) = (self.s, self.v);
        let (p, q, t) = (v * (1.0 - s), v * (1.0 - s * f), v * (1.0 - s * (1.0 - f)));
        let c = match (i as i32).rem_euclid(6) {
            0 => [v, t, p],
            1 => [q, v, p],
            2 => [p, v, t],
            3 => [p, q, v],
            4 => [t, p, v],
            _ => [v, p, q],
        };
        c.map(|x| (x * 255.0).round().clamp(0.0, 255.0) as u8)
    }
    pub fn rgba(self) -> Rgba {
        let [r, g, b] = self.rgb8();
        Rgba { r, g, b, a: (self.a * 255.0).round() as u8 }
    }
    /// `#rrggbb`, or `#rrggbbaa` when alpha is under 1.
    pub fn hex(self) -> String {
        let [r, g, b] = self.rgb8();
        let mut out = format!("#{r:02x}{g:02x}{b:02x}");
        if self.a < 1.0 {
            out.push_str(&format!("{:02x}", (self.a * 255.0).round() as u8));
        }
        out
    }
}

/// The hex field's link to the picker: what the picker last wrote (so its own rewrite isn't
/// read back as typing), the hue grey keeps, and the app's handler.
#[derive(Default)]
struct HexLink {
    sub: Option<Subscription>,
    last: Rc<RefCell<String>>,
    hue: Rc<Cell<f32>>,
    handler: Rc<RefCell<Option<Handler<Hsva>>>>,
}

/// A fill inside a border `bw` wide, as Qt's Rectangle paints one: a translucent border shows
/// what's behind the element, not its own fill.
fn inset_fill(fill: Hsla, bw: f32, radius: f32) -> Div {
    div().absolute().top(px(bw)).left(px(bw)).right(px(bw)).bottom(px(bw)).rounded(px((radius - bw).max(0.0))).bg(fill)
}

fn layer(radius: f32) -> Div {
    div().absolute().top_0().left_0().size_full().rounded(px(radius))
}

fn rgb(r: u8, g: u8, b: u8, a: u8) -> Hsla {
    color(Rgba { r, g, b, a })
}

/// An HSV color picker: a saturation/value square over the hue, a hue strip, an optional alpha
/// strip (opaque at the top), a preview swatch and hex field, and a row of the theme's palette
/// colors. Corners are `min(4, radius)`; knobs are the Slider's, ringed in the surface color.
/// Arrows move the square's knob 1% (Shift 10%), Up/Down a focused strip; Return or Space pick
/// a focused swatch; a valid hex typed into the field applies at once.
#[derive(IntoElement)]
pub struct ColorPicker {
    id: ElementId,
    value: Hsva,
    alpha: bool,
    width: Option<f32>,
    height: Option<f32>,
    palette: Vec<SharedString>,
    cursor: bool,
    hex: Option<Entity<TextInput>>,
    on_change: Option<Handler<Hsva>>,
}

impl ColorPicker {
    pub fn new(value: Hsva) -> ColorPicker {
        ColorPicker {
            id: ElementId::Name("omarchy-color-picker".into()),
            value,
            alpha: true,
            width: None,
            height: None,
            palette: ["red", "orange", "yellow", "green", "cyan", "blue", "magenta", "foreground"].map(SharedString::from).to_vec(),
            cursor: false,
            hex: None,
            on_change: None,
        }
    }
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
        self
    }
    /// Show the alpha strip (default on).
    pub fn alpha(mut self, on: bool) -> Self {
        self.alpha = on;
        self
    }
    /// Default `space(300)` × `space(260)`; the square takes what the field and the palette leave.
    pub fn size(mut self, w: f32, h: f32) -> Self {
        self.width = Some(w);
        self.height = Some(h);
        self
    }
    /// Named colors from the theme's colors.toml, in order; missing ones are skipped.
    pub fn palette<I: Into<SharedString>>(mut self, names: impl IntoIterator<Item = I>) -> Self {
        self.palette = names.into_iter().map(Into::into).collect();
        self
    }
    /// The hot look of the square's knob.
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    /// The hex field's input, if the app keeps it (to read or focus it); the picker keeps its own
    /// otherwise. The picker rewrites it whenever the color changes and the field isn't focused.
    pub fn hex(mut self, input: &Entity<TextInput>) -> Self {
        self.hex = Some(input.clone());
        self
    }
    /// Every change: the square, the strips, a swatch, or a valid hex typed into the field.
    pub fn on_change(mut self, f: impl Fn(&Hsva, &mut Window, &mut App) + 'static) -> Self {
        self.on_change = Some(Rc::new(f));
        self
    }
}

fn child_id(parent: &ElementId, name: impl Into<SharedString>) -> ElementId {
    ElementId::NamedChild(std::sync::Arc::new(parent.clone()), name.into())
}

impl RenderOnce for ColorPicker {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let c = &tk.controls;
        let v = self.value;
        let w = self.width.unwrap_or(t.space(300.0));
        let h = self.height.unwrap_or(t.space(260.0));
        let small = tk.radius_small() as f32;
        let knob = 14f32.max((sp.control_height as f32 * 0.38).round());
        let ring = 1f32.max(t.space(2.0));
        let surface = color(tk.surfaces.popups.background);
        let strip_w = t.space(14.0);
        let swatch = t.space(20.0);
        let (md, lg) = (sp.md as f32, sp.lg as f32);
        let bw = c.normal.border.max_width().max(c.hover.border.max_width()).max(c.focus.border.max_width()) as f32;
        let field_h = tk.font.line_px(tk.font.body) as f32 + 2.0 * (sp.input_padding_y as f32 + bw);
        let square_h = h - field_h - swatch - lg * 2.0;
        let strips = if self.alpha { 2.0 } else { 1.0 };
        let square_w = w - strips * (strip_w + md);
        let [r, g, b] = v.rgb8();
        let opaque = rgb(r, g, b, 255);

        // ---- the hex field, and the link that reads what's typed into it
        let input = match &self.hex {
            Some(i) => i.clone(),
            None => window
                .use_keyed_state(child_id(&self.id, "hex-input"), cx, |_, cx| TextInput::new(cx, v.hex()))
                .read(cx)
                .clone(),
        };
        let link = keyed_state(&self.id, "hex-link", window, cx, HexLink::default);
        let (last, hue_cell, handler) = {
            let l = link.read(cx);
            (l.last.clone(), l.hue.clone(), l.handler.clone())
        };
        hue_cell.set(v.h);
        *handler.borrow_mut() = self.on_change.clone();
        if link.read(cx).sub.is_none() {
            let (last, hue, handler) = (last.clone(), hue_cell.clone(), handler.clone());
            let sub = window.subscribe(&input, cx, move |_, ev: &TextChanged, window, cx| {
                if *last.borrow() == ev.0.as_ref() {
                    return;
                }
                let f = handler.borrow().clone();
                if let (Some(next), Some(f)) = (Hsva::from_hex(&ev.0, hue.get()), f) {
                    f(&next, window, cx);
                }
            });
            link.update(cx, |l, _| l.sub = Some(sub));
        }
        let focused = input.read(cx).focus_handle(cx).is_focused(window);
        if !focused && *input.read(cx).text() != v.hex() {
            *last.borrow_mut() = v.hex();
            input.update(cx, |i, cx| i.set_text(v.hex(), cx));
        }
        // A change from the picker itself: write the hex first, so the field doesn't read it back.
        let set: Option<Handler<Hsva>> = self.on_change.clone().map(|f| {
            let (input, last) = (input.clone(), last.clone());
            Rc::new(move |next: &Hsva, window: &mut Window, cx: &mut App| {
                let hex = next.hex();
                *last.borrow_mut() = hex.clone();
                input.update(cx, |i, cx| i.set_text(hex, cx));
                f(next, window, cx);
            }) as Handler<Hsva>
        });

        let knob_el = |x: f32, y: f32, fill: Hsla, hot: bool| {
            let k = if hot { (knob * 1.15).round() } else { knob };
            div()
                .absolute()
                .left(px(x + (knob - k) / 2.0))
                .top(px(y + (knob - k) / 2.0))
                .size(px(k))
                .rounded(px(k / 2.0))
                .bg(fill)
                .border(px(ring))
                .border_color(surface)
        };

        // ---- the square: hue, white from the left, black from the bottom
        let square_id = child_id(&self.id, "square");
        let square_focus = focus_handle(&square_id, window, cx);
        let [hr, hg, hb] = Hsva { s: 1.0, v: 1.0, ..v }.rgb8();
        let square = div()
            .id(square_id.clone())
            .relative()
            .flex_shrink_0()
            .w(px(square_w))
            .h(px(square_h))
            .cursor(CursorStyle::Crosshair)
            .track_focus(&square_focus)
            .child(layer(small).bg(rgb(hr, hg, hb, 255)))
            .child(
                layer(small).bg(
                    linear_gradient(90.0, linear_color_stop(rgb(255, 255, 255, 255), 0.0), linear_color_stop(rgb(255, 255, 255, 0), 1.0))
                        .color_space(gpui::ColorSpace::Srgb),
                ),
            )
            .child(
                layer(small).bg(
                    linear_gradient(0.0, linear_color_stop(rgb(0, 0, 0, 255), 0.0), linear_color_stop(rgb(0, 0, 0, 0), 1.0))
                        .color_space(gpui::ColorSpace::Srgb),
                ),
            )
            .child(knob_el(
                (v.s * square_w - knob / 2.0).round(),
                ((1.0 - v.v) * square_h - knob / 2.0).round(),
                opaque,
                self.cursor || square_focus.is_focused(window),
            ));
        let keys = set.clone();
        let square = square.on_key_down(move |ev: &KeyDownEvent, window, cx| {
            if tab_keys(ev, window, cx) {
                cx.stop_propagation();
                return;
            }
            let st = if ev.keystroke.modifiers.shift { 0.1 } else { 0.01 };
            let next = match ev.keystroke.key.as_str() {
                "left" => Hsva { s: (v.s - st).max(0.0), ..v },
                "right" => Hsva { s: (v.s + st).min(1.0), ..v },
                "up" => Hsva { v: (v.v + st).min(1.0), ..v },
                "down" => Hsva { v: (v.v - st).max(0.0), ..v },
                _ => return,
            };
            if let Some(f) = &keys {
                f(&next, window, cx);
            }
            cx.stop_propagation();
        });
        let (f, fh) = (set.clone(), square_focus.clone());
        let drag: DragFn = Rc::new(move |d, window, cx| {
            if d.phase == DragPhase::Down {
                fh.focus(window, cx);
            }
            if d.phase == DragPhase::Up {
                return;
            }
            let next = Hsva { s: (d.x / d.w.max(1.0)).clamp(0.0, 1.0), v: 1.0 - (d.y / d.h.max(1.0)).clamp(0.0, 1.0), ..v };
            if let Some(f) = &f {
                f(&next, window, cx);
            }
        });
        let square = follow_drag(square, &square_id, false, drag, window, cx);

        // ---- strips with Slider knobs
        let strip = |name: &'static str, fraction: f32, with: Rc<dyn Fn(f32) -> Hsva>, layers: Vec<Div>, window: &mut Window, cx: &mut App| {
            let id = child_id(&self.id, name);
            let focus = focus_handle(&id, window, cx);
            let mut e = div()
                .id(id.clone())
                .relative()
                .flex_shrink_0()
                .w(px(strip_w))
                .h(px(square_h))
                .cursor(CursorStyle::PointingHand)
                .track_focus(&focus)
                .children(layers)
                .child(knob_el(((strip_w - knob) / 2.0).round(), (fraction * (square_h - knob)).round(), t.fg(), false));
            let (keys, with_k) = (set.clone(), with.clone());
            e = e.on_key_down(move |ev: &KeyDownEvent, window, cx| {
                if tab_keys(ev, window, cx) {
                    cx.stop_propagation();
                    return;
                }
                let st = if ev.keystroke.modifiers.shift { 0.1 } else { 0.01 };
                let fr = match ev.keystroke.key.as_str() {
                    "up" => (fraction - st).max(0.0),
                    "down" => (fraction + st).min(1.0),
                    _ => return,
                };
                if let Some(f) = &keys {
                    f(&with_k(fr), window, cx);
                }
                cx.stop_propagation();
            });
            let (f, fh) = (set.clone(), focus.clone());
            let inset = knob / 2.0;
            let drag: DragFn = Rc::new(move |d, window, cx| {
                if d.phase == DragPhase::Down {
                    fh.focus(window, cx);
                }
                if d.phase == DragPhase::Up {
                    return;
                }
                let fr = ((d.y - inset) / (d.h - inset * 2.0).max(1.0)).clamp(0.0, 1.0);
                if let Some(f) = &f {
                    f(&with(fr), window, cx);
                }
            });
            follow_drag(e, &id, false, drag, window, cx)
        };
        // The hue strip: red, yellow, green, cyan, blue, magenta, red, top to bottom, in six bands.
        let stops: [[u8; 3]; 7] = [[255, 0, 0], [255, 255, 0], [0, 255, 0], [0, 255, 255], [0, 0, 255], [255, 0, 255], [255, 0, 0]];
        let band = square_h / 6.0;
        let hue_layers: Vec<Div> = (0..6)
            .map(|i| {
                let ([r0, g0, b0], [r1, g1, b1]) = (stops[i], stops[i + 1]);
                let top = (band * i as f32).round();
                let bottom = if i == 5 { square_h } else { (band * (i + 1) as f32).round() };
                let mut d = div()
                    .absolute()
                    .left_0()
                    .top(px(top))
                    .w(px(strip_w))
                    .h(px(bottom - top))
                    .bg(
                        linear_gradient(180.0, linear_color_stop(rgb(r0, g0, b0, 255), 0.0), linear_color_stop(rgb(r1, g1, b1, 255), 1.0))
                            .color_space(gpui::ColorSpace::Srgb),
                    );
                if i == 0 {
                    d = d.rounded_t(px(small));
                }
                if i == 5 {
                    d = d.rounded_b(px(small));
                }
                d
            })
            .collect();
        let hue = strip("hue", v.h, Rc::new(move |fr| Hsva { h: fr.min(0.9999), ..v }), hue_layers, window, cx);
        let mut top = div().flex().flex_row().gap(px(md)).child(square).child(hue);
        if self.alpha {
            let layers = vec![
                layer(small).bg(t.track()),
                layer(small).bg(
                    linear_gradient(180.0, linear_color_stop(opaque, 0.0), linear_color_stop(rgb(r, g, b, 0), 1.0))
                        .color_space(gpui::ColorSpace::Srgb),
                ),
            ];
            top = top.child(strip("alpha", 1.0 - v.a, Rc::new(move |fr| Hsva { a: 1.0 - fr, ..v }), layers, window, cx));
        }

        // ---- preview and hex field
        let preview = div()
            .relative()
            .flex_shrink_0()
            .size(px(field_h))
            .rounded(px(small))
            .border_1()
            .border_color(color(c.normal.border.color))
            .child(inset_fill(color(v.rgba()), 0.0, (small - 1.0).max(0.0)));
        let middle = div().flex().flex_row().w(px(w)).gap(px(md)).child(preview).child(div().flex_1().min_w_0().child(TextField::new(&input)));

        // ---- the theme's palette
        let current = v.hex()[..7].to_string();
        let mut swatches = div().flex().flex_row().gap(px(md));
        for name in &self.palette {
            let rgba = match tk.palette.named.get(name.as_ref()) {
                Some(c) => *c,
                None if name.as_ref() == "foreground" => tk.palette.foreground,
                None if name.as_ref() == "background" => tk.palette.background,
                None => continue,
            };
            let hex = format!("#{:02x}{:02x}{:02x}", rgba.r, rgba.g, rgba.b);
            let chosen = hex == current;
            let one = |width: f64, c: Rgba| BorderSpec { color: c, widths: [width; 4], gradient: None };
            let (rest, hot) = if chosen {
                let look = Look::new(Rgba::TRANSPARENT, one(1f64.max(t.space(2.0) as f64), tk.palette.foreground));
                (look.clone(), look)
            } else {
                (Look::new(Rgba::TRANSPARENT, one(1.0, c.normal.border.color)), Look::new(Rgba::TRANSPARENT, one(1.0, c.hover.border.color)))
            };
            let inner = if chosen { 1f32.max(t.space(2.0)) } else { 1.0 };
            let id = child_id(&self.id, format!("swatch-{name}"));
            let focus = focus_handle(&id, window, cx);
            let base = div().id(id).flex_shrink_0().size(px(swatch)).cursor(CursorStyle::PointingHand);
            let e = chrome(base, &t, &rest, &hot, chosen || focus.is_focused(window), px(small)).child(inset_fill(color(Rgba { a: 255, ..rgba }), inner, small));
            let pick = match (Hsva::from_hex(&hex, v.h), set.clone()) {
                (Some(sv), Some(f)) => Some(Rc::new(move |_: &ClickEvent, window: &mut Window, cx: &mut App| f(&Hsva { a: v.a, ..sv }, window, cx)) as Handler<ClickEvent>),
                _ => None,
            };
            swatches = swatches.child(pressable(e, &focus, pick));
        }

        div().flex().flex_col().flex_shrink_0().w(px(w)).h(px(h)).gap(px(lg)).child(top).child(middle).child(swatches)
    }
}
