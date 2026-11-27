//! ColorPicker: `ColorPicker.qml`. An HSV picker with a saturation/value square, a hue strip,
//! an optional alpha strip, a hex field and a row of the theme's palette colors.

use std::f32::consts::PI;
use std::rc::Rc;

use omarchy_theme::{BorderSpec, Rgba};
use rune::{Cursor, Drag, El, Key, TextEdit, Tx, col, el, row};
use rune_kit::TextInput;

use crate::ui::{Look, chrome};
use crate::{Theme, color};

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

type SetFn<S> = Rc<dyn Fn(&mut S, Hsva)>;
type Attach = Rc<dyn Fn(El, Part, Hsva) -> El>;
type FieldAttach = Rc<dyn Fn(TextInput) -> TextInput>;

#[derive(Clone, Copy)]
enum Part {
    Square,
    Hue { inset: f32 },
    Alpha { inset: f32 },
    Swatch(Hsva),
}

/// Where along a strip the pointer is, 0 at the top: the knob's center travels `inset` in
/// from each end.
fn strip_fraction(d: &Drag, inset: f32) -> f32 {
    ((d.y - inset) / (d.h - inset * 2.0).max(1.0)).clamp(0.0, 1.0)
}

fn step(k: &rune::KeyEvent) -> f32 {
    if k.mods.shift { 0.1 } else { 0.01 }
}

fn attach<S: 'static>(set: SetFn<S>, e: El, part: Part, cur: Hsva) -> El {
    match part {
        Part::Square => {
            let keys = set.clone();
            e.on_drag(move |s: &mut S, d: &mut Drag, _tx| {
                let sat = (d.x / d.w.max(1.0)).clamp(0.0, 1.0);
                let val = 1.0 - (d.y / d.h.max(1.0)).clamp(0.0, 1.0);
                set(s, Hsva { s: sat, v: val, ..cur });
            })
            .on_key(move |s: &mut S, k, _tx: &mut Tx| {
                let st = step(k);
                let next = match k.key {
                    Key::Left => Hsva { s: (cur.s - st).max(0.0), ..cur },
                    Key::Right => Hsva { s: (cur.s + st).min(1.0), ..cur },
                    Key::Up => Hsva { v: (cur.v + st).min(1.0), ..cur },
                    Key::Down => Hsva { v: (cur.v - st).max(0.0), ..cur },
                    _ => return false,
                };
                keys(s, next);
                true
            })
        }
        Part::Hue { inset } | Part::Alpha { inset } => {
            let hue = matches!(part, Part::Hue { .. });
            let with = move |fr: f32| if hue { Hsva { h: fr.min(0.9999), ..cur } } else { Hsva { a: 1.0 - fr, ..cur } };
            let now = if hue { cur.h } else { 1.0 - cur.a };
            let keys = set.clone();
            e.on_drag(move |s: &mut S, d: &mut Drag, _tx| set(s, with(strip_fraction(d, inset)))).on_key(
                move |s: &mut S, k, _tx: &mut Tx| {
                    let fr = match k.key {
                        Key::Up => (now - step(k)).max(0.0),
                        Key::Down => (now + step(k)).min(1.0),
                        _ => return false,
                    };
                    keys(s, with(fr));
                    true
                },
            )
        }
        Part::Swatch(v) => e.on_press(move |s: &mut S| set(s, Hsva { a: cur.a, ..v })),
    }
}

/// A fill inside a border `bw` wide, as Qt's Rectangle paints one: a translucent border shows
/// what's behind the element, not its own fill.
fn inset_fill(fill: rune::Color, bw: f32, radius: f32) -> El {
    el().pin(Some(bw), Some(bw), Some(bw), Some(bw)).radius((radius - bw).max(0.0)).fill(fill)
}

/// An HSV color picker: a saturation/value square over the hue, a hue strip, an optional alpha
/// strip (opaque at the top), a preview swatch and hex field, and a row of the theme's palette
/// colors. Corners are `min(4, radius)`; knobs are the Slider's, ringed in the surface color.
pub struct ColorPicker {
    value: Hsva,
    alpha: bool,
    width: Option<f32>,
    height: Option<f32>,
    palette: Vec<String>,
    cursor: bool,
    hex: TextEdit,
    attach: Option<Attach>,
    field: Option<FieldAttach>,
}

impl ColorPicker {
    pub fn new(value: Hsva) -> ColorPicker {
        ColorPicker {
            value,
            alpha: true,
            width: None,
            height: None,
            palette: ["red", "orange", "yellow", "green", "cyan", "blue", "magenta", "foreground"].map(String::from).to_vec(),
            cursor: false,
            hex: TextEdit::new(value.hex()),
            attach: None,
            field: None,
        }
    }
    /// Show the alpha strip (default on).
    pub fn alpha(mut self, on: bool) -> Self {
        self.alpha = on;
        self
    }
    /// Default `space(300)` × `space(260)`; the square takes what the strips, the field and
    /// the palette leave.
    pub fn size(mut self, w: f32, h: f32) -> Self {
        self.width = Some(w);
        self.height = Some(h);
        self
    }
    /// Named colors from the theme's colors.toml, in order; missing ones are skipped.
    pub fn palette<I: Into<String>>(mut self, names: impl IntoIterator<Item = I>) -> Self {
        self.palette = names.into_iter().map(Into::into).collect();
        self
    }
    /// The hot look of the square's knob.
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    /// The hex field's text, kept by the app (start it at [`Hsva::hex`]).
    pub fn hex(mut self, edit: &TextEdit) -> Self {
        self.hex = edit.clone();
        self
    }
    /// Every change: the square, the strips, a swatch, or a valid hex typed into the field.
    /// `hex` is where the app keeps the field's text; the picker rewrites it as the color
    /// changes elsewhere.
    pub fn on_change<S: 'static>(
        mut self,
        hex: impl for<'a> Fn(&'a mut S) -> &'a mut TextEdit + Clone + 'static,
        f: impl Fn(&mut S, Hsva) + 'static,
    ) -> Self {
        let f = Rc::new(f);
        let rewrite = hex.clone();
        let set: SetFn<S> = {
            let f = f.clone();
            Rc::new(move |s: &mut S, v: Hsva| {
                rewrite(s).replace_text(v.hex());
                f(s, v);
            })
        };
        self.attach = Some(Rc::new(move |e: El, part: Part, cur: Hsva| attach(set.clone(), e, part, cur)));
        let hue = self.value.h;
        let read = hex.clone();
        self.field = Some(Rc::new(move |input: TextInput| {
            let f = f.clone();
            let read = read.clone();
            input.on_edit(hex.clone()).on_change(move |s: &mut S, _tx: &mut Tx| {
                let typed = read(s).text.clone();
                if let Some(v) = Hsva::from_hex(&typed, hue) {
                    f(s, v);
                }
            })
        }));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let c = &tk.controls;
        let w = self.width.unwrap_or(t.space(300.0));
        let h = self.height.unwrap_or(t.space(260.0));
        let small = t.radius_small();
        let knob = 14f32.max((sp.control_height as f32 * 0.38).round());
        let strip_w = t.space(14.0);
        let swatch = t.space(20.0);
        let (md, lg) = (sp.md as f32, sp.lg as f32);
        let bw = c.normal.border.max_width().max(c.hover.border.max_width()) as f32;
        let field_h = tk.font.line_px(tk.font.body) as f32 + 2.0 * (sp.input_padding_y as f32 + bw);
        let square_h = h - field_h - swatch - lg * 2.0;
        let strips = if self.alpha { 2.0 } else { 1.0 };
        let square_w = w - strips * (strip_w + md);
        let v = self.value;
        let [r, g, b] = v.rgb8();
        let opaque = color(Rgba { r, g, b, a: 255 });
        let wire = |e: El, part: Part| match &self.attach {
            Some(a) => a(e, part, v),
            None => e,
        };
        let pin = || el().pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0));

        // ---- the square: hue, white from the left, black from the bottom
        let [hr, hg, hb] = Hsva { s: 1.0, v: 1.0, ..v }.rgb8();
        let white = rune::Color::WHITE;
        let clear_white = color(Rgba { r: 255, g: 255, b: 255, a: 0 });
        let black = color(Rgba { r: 0, g: 0, b: 0, a: 255 });
        let clear_black = color(Rgba { r: 0, g: 0, b: 0, a: 0 });
        let kx = (v.s * square_w - knob / 2.0).round().clamp(0.0, (square_w - knob).max(0.0));
        let ky = ((1.0 - v.v) * square_h - knob / 2.0).round().clamp(0.0, (square_h - knob).max(0.0));
        let sv_knob = el()
            .absolute(kx, ky)
            .size(knob, knob)
            .radius(knob / 2.0)
            .fill(t.bg())
            .scale(if self.cursor { 1.15 } else { 1.0 })
            .child(
                el().pin(Some(1.0), Some(1.0), Some(1.0), Some(1.0))
                    .radius(((knob - 2.0) / 2.0).max(0.0))
                    .fill(t.fg())
                    .child(
                        el().pin(Some(1.0), Some(1.0), Some(1.0), Some(1.0))
                            .radius(((knob - 4.0) / 2.0).max(0.0))
                            .fill(opaque),
                    ),
            );
        let square = el()
            .w(square_w)
            .h(square_h)
            .focusable(true)
            .cursor(Cursor::Crosshair)
            .key("color-square")
            .child(pin().radius(small).fill(color(Rgba { r: hr, g: hg, b: hb, a: 255 })))
            .child(pin().radius(small).linear_gradient(PI / 2.0, [(0.0, white), (1.0, clear_white)]))
            .child(pin().radius(small).gradient(clear_black, black))
            .child(sv_knob);
        let square = wire(square, Part::Square);

        // ---- strips with inset two-tone knobs
        let strip_pad = t.space(2.0);
        let strip_knob_size = 8f32.max(strip_w - strip_pad * 2.0);
        let strip_inset = strip_pad + strip_knob_size / 2.0;
        let strip_knob = |fr: f32| {
            let x = ((strip_w - strip_knob_size) / 2.0).round();
            let y = strip_pad + (fr * (square_h - strip_knob_size - strip_pad * 2.0).max(0.0)).round();
            el().absolute(x, y)
                .size(strip_knob_size, strip_knob_size)
                .radius(strip_knob_size / 2.0)
                .fill(t.bg())
                .child(
                    el().pin(Some(1.0), Some(1.0), Some(1.0), Some(1.0))
                        .radius(((strip_knob_size - 2.0) / 2.0).max(0.0))
                        .fill(t.fg()),
                )
        };
        let hue_stops: Vec<(f32, rune::Color)> = [[255, 0, 0], [255, 255, 0], [0, 255, 0], [0, 255, 255], [0, 0, 255], [255, 0, 255], [255, 0, 0]]
            .iter()
            .enumerate()
            .map(|(i, [r, g, b])| (i as f32 / 6.0, color(Rgba { r: *r, g: *g, b: *b, a: 255 })))
            .collect();
        let hue = el()
            .w(strip_w)
            .h(square_h)
            .focusable(true)
            .cursor(Cursor::Pointer)
            .key("color-hue")
            .child(pin().radius(small).linear_gradient(PI, hue_stops))
            .child(strip_knob(v.h));
        let hue = wire(hue, Part::Hue { inset: strip_inset });
        let mut top = row().gap(md).child(square).child(hue);
        if self.alpha {
            let alpha = el()
                .w(strip_w)
                .h(square_h)
                .focusable(true)
                .cursor(Cursor::Pointer)
                .key("color-alpha")
                .child(pin().radius(small).fill(t.track()))
                .child(pin().radius(small).gradient(opaque, color(Rgba { r, g, b, a: 0 })))
                .child(strip_knob(1.0 - v.a));
            top = top.child(wire(alpha, Part::Alpha { inset: strip_inset }));
        }

        // ---- preview and hex field
        let full_r = t.radius();
        let preview = el().w(field_h).h(field_h).radius(full_r).border(1.0, color(c.normal.border.color)).child(inset_fill(color(v.rgba()), 1.0, full_r));
        let style = t.text_style(tk.font.body);
        let mut input = TextInput::new(&self.hex).plain().width(None).text_style(style);
        if let Some(field) = &self.field {
            input = field(input);
        }
        let base = row()
            .items_center()
            .px((sp.control_padding_x as f32 + bw - 8.0).max(0.0))
            .py(sp.input_padding_y as f32 + bw)
            .h(field_h)
            .grow(1.0);
        let rest = Look::new(c.normal.fill, c.normal.border.clone());
        let hot = Look::new(c.hover.fill, c.hover.border.clone());
        let field = chrome(t, base, vec![input.build(t.kit()).grow(1.0)], &rest, &hot, false, t.radius());
        let middle = row().w(w).gap(md).child(preview).child(field);

        // ---- the theme's palette
        let current = &v.hex()[..7];
        let mut swatches = row().gap(md);
        for name in &self.palette {
            let Some(rgba) = tk.palette.named.get(name) else { continue };
            let hex = format!("#{:02x}{:02x}{:02x}", rgba.r, rgba.g, rgba.b);
            let chosen = hex == current;
            let one = |width: f64, c: Rgba| BorderSpec { color: c, widths: [width; 4], gradient: None };
            let (rest, hot) = if chosen {
                let look = Look::new(Rgba::TRANSPARENT, one(1f64.max(t.space(2.0) as f64), tk.palette.foreground));
                (look.clone(), look)
            } else {
                (Look::new(Rgba::TRANSPARENT, one(1.0, c.normal.border.color)), Look::new(Rgba::TRANSPARENT, one(1.0, c.hover.border.color)))
            };
            let ring = if chosen { 1f32.max(t.space(2.0)) } else { 1.0 };
            let base = el().w(swatch).h(swatch).focusable(true).cursor(Cursor::Pointer).key(format!("swatch-{name}"));
            let base = match Hsva::from_hex(&hex, v.h) {
                Some(sv) => wire(base, Part::Swatch(sv)),
                None => base,
            };
            swatches = swatches.child(chrome(t, base.radius(small), vec![inset_fill(color(*rgba), ring, small)], &rest, &hot, chosen, small));
        }

        col().w(w).h(h).gap(lg).child(top).child(middle).child(swatches)
    }
}
