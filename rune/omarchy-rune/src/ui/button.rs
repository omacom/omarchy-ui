//! Button, IconButton and ButtonGroup: the shell's `Button.qml`, `PanelActionButton.qml` and
//! `ButtonGroup.qml`.

use omarchy_theme::{BorderSpec, Rgba};
use rune::{El, el, row, text};

use super::{Attach, Look, chrome, glyph, reserve};
use crate::{Theme, color};

/// The button. Borderless at rest unless `bordered`; the hover-cursor border under the pointer,
/// keyboard focus or `cursor`; `selected` fills and bolds the label.
pub struct Button {
    label: String,
    icon: Option<String>,
    bordered: bool,
    selected: bool,
    cursor: bool,
    key: Option<String>,
    press: Option<Attach>,
}

impl Button {
    pub fn new(label: impl Into<String>) -> Button {
        Button { label: label.into(), icon: None, bordered: false, selected: false, cursor: false, key: None, press: None }
    }
    /// A glyph before the label (a Nerd Font codepoint, as the shell uses).
    pub fn icon(mut self, glyph: impl Into<String>) -> Self {
        self.icon = Some(glyph.into());
        self
    }
    pub fn bordered(mut self, on: bool) -> Self {
        self.bordered = on;
        self
    }
    pub fn selected(mut self, on: bool) -> Self {
        self.selected = on;
        self
    }
    /// Force the hover-cursor look, as a keyboard-driven panel's cursor does.
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    pub fn key(mut self, key: impl Into<String>) -> Self {
        self.key = Some(key.into());
        self
    }
    pub fn on_press<S: 'static>(mut self, f: impl Fn(&mut S) + 'static) -> Self {
        self.press = Some(Box::new(move |e: El| e.on_press(f)));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let c = &tk.controls;
        let sp = &tk.spacing;
        let selected_border = !c.selected.border.is_none();
        let rest_border = if self.selected && selected_border {
            c.selected.border.clone()
        } else if self.bordered {
            c.normal.border.clone()
        } else {
            BorderSpec::none()
        };
        let rest = Look::new(if self.selected { c.selected.fill } else { Rgba::TRANSPARENT }, rest_border);
        let hot = Look::new(c.hover.fill, c.hover.border.clone());
        let mut looks = vec![&c.hover.border, &c.selected.border, &c.focus.border];
        if self.bordered {
            looks.push(&c.normal.border);
        }
        let [rt, rr, rb, rl] = reserve(&looks);

        let ink = if self.selected { color(c.selected.text) } else { t.fg() };
        let style = if self.selected { t.text_style_bold(tk.font.body) } else { t.text_style(tk.font.body) };
        let mut kids = Vec::new();
        if let Some(icon) = &self.icon {
            let mark = glyph(t, icon, tk.font.icon, ink);
            if self.label.is_empty() {
                kids.push(mark);
            } else {
                // Keep the row height on the body line box so icon + label buttons match text-only siblings.
                kids.push(row().h(tk.font.line_px(tk.font.body) as f32).items_center().child(mark));
            }
        }
        if !self.label.is_empty() {
            kids.push(text(self.label.clone()).font(style).color(ink).single_line());
        }

        let base = row()
            .items_center()
            .justify_center()
            .gap(sp.control_gap as f32)
            .pt(sp.control_padding_y as f32 + rt)
            .pb(sp.control_padding_y as f32 + rb)
            .pl(sp.control_padding_x as f32 + rl)
            .pr(sp.control_padding_x as f32 + rr)
            .role("button")
            .label(self.label.clone())
            .focusable(true)
            .press_scale(0.97);
        let mut e = chrome(t, base, kids, &rest, &hot, self.cursor, t.radius());
        if let Some(k) = self.key {
            e = e.key(k);
        }
        match self.press {
            Some(attach) => attach(e),
            None => e,
        }
    }
}

/// A square icon-only button (the shell's PanelActionButton): odd-sized so the glyph centers on
/// a pixel.
pub struct IconButton {
    glyph: String,
    label: String,
    bordered: bool,
    cursor: bool,
    press: Option<Attach>,
}

impl IconButton {
    pub fn new(glyph: impl Into<String>) -> IconButton {
        IconButton { glyph: glyph.into(), label: String::new(), bordered: false, cursor: false, press: None }
    }
    /// The accessible name (and what a tooltip would say).
    pub fn label(mut self, label: impl Into<String>) -> Self {
        self.label = label.into();
        self
    }
    pub fn bordered(mut self, on: bool) -> Self {
        self.bordered = on;
        self
    }
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    pub fn on_press<S: 'static>(mut self, f: impl Fn(&mut S) + 'static) -> Self {
        self.press = Some(Box::new(move |e: El| e.on_press(f)));
        self
    }

    pub(crate) fn size(t: &Theme) -> f32 {
        let tk = t.tokens();
        ((t.space(22.0)).max((tk.font.icon + tk.spacing.sm * 2) as f32).round() as i64 | 1) as f32
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let c = &tk.controls;
        let icon = tk.font.icon;
        let size = Self::size(t);
        let rest = Look::new(Rgba::TRANSPARENT, if self.bordered { c.normal.border.clone() } else { BorderSpec::none() });
        // PanelActionButton shows the hover border only when bordered.
        let hot = Look::new(c.hover.fill, if self.bordered { c.hover.border.clone() } else { BorderSpec::none() });
        let base = row()
            .size(size, size)
            .center()
            .role("button")
            .label(self.label.clone())
            .focusable(true)
            .press_scale(0.97);
        let style = t.text_style(icon);
        let mark = match rune::text::ink(&self.glyph, style).map(|i| i.bounds) {
            Some([ix, iy, iw, ih]) if iw > 0.0 && ih > 0.0 => {
                let x = (size / 2.0 - (ix + iw / 2.0)).round();
                let y = (size / 2.0 - (iy + ih / 2.0)).round();
                el().size(size, size).child(text(self.glyph).font(style).color(t.fg()).single_line().absolute(x, y))
            }
            _ => glyph(t, &self.glyph, icon, t.fg()),
        };
        let e = chrome(t, base, vec![mark], &rest, &hot, self.cursor, t.radius_small());
        match self.press {
            Some(attach) => attach(e),
            None => e,
        }
    }
}

/// A row of bordered buttons, one selected: a segmented choice.
pub struct ButtonGroup {
    options: Vec<(String, String)>,
    value: String,
    cursor: Option<usize>,
    choose: Option<Box<dyn Fn(El, String) -> El>>,
}

impl ButtonGroup {
    /// `options` are (value, label) pairs.
    pub fn new<V: Into<String>, L: Into<String>>(options: impl IntoIterator<Item = (V, L)>, value: impl Into<String>) -> ButtonGroup {
        let options = options.into_iter().map(|(v, l)| (v.into(), l.into())).collect();
        ButtonGroup { options, value: value.into(), cursor: None, choose: None }
    }
    /// Force the hover-cursor look on one option.
    pub fn cursor(mut self, index: Option<usize>) -> Self {
        self.cursor = index;
        self
    }
    pub fn on_choose<S: 'static>(mut self, f: impl Fn(&mut S, &str) + Clone + 'static) -> Self {
        self.choose = Some(Box::new(move |e: El, v: String| {
            let f = f.clone();
            e.on_press(move |s: &mut S| f(s, &v))
        }));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let gap = t.tokens().spacing.md as f32;
        let mut r = row().gap(gap).role("radiogroup");
        for (i, (value, label)) in self.options.into_iter().enumerate() {
            let b = Button::new(label).bordered(true).selected(value == self.value).cursor(self.cursor == Some(i)).build(t);
            r = r.child(match &self.choose {
                Some(attach) => attach(b, value),
                None => b,
            });
        }
        r
    }
}
