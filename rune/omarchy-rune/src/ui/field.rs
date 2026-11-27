//! TextField, NumberField and Dropdown: the shell's `TextField.qml`, `NumberField.qml` and
//! `Dropdown.qml`. Inputs show the normal border at rest and the hover-cursor look under the
//! pointer and while focused.

use rune::{Align, El, Side, TextEdit, col, el, row, text};
use rune_kit::TextInput;

use super::{Attach, Look, chrome};
use crate::{Theme, border, color};

fn rest_and_hot(t: &Theme) -> (Look, Look) {
    let c = &t.tokens().controls;
    (Look::new(c.normal.fill, c.normal.border.clone()), Look::new(c.hover.fill, c.hover.border.clone()))
}

fn field_label(t: &Theme, label: &str, size: i64, bold: bool) -> El {
    let style = if bold { t.text_style_bold(size) } else { t.text_style(size) };
    text(label.to_string()).font(style).color(t.dim())
}

/// A single-line text input. The editing (caret, selection, IME, clipboard) is Rune's; the
/// chrome is Omarchy's.
pub struct TextField {
    edit: TextEdit,
    placeholder: String,
    width: Option<f32>,
    cursor: bool,
    secure: bool,
    access: Option<Box<dyn FnOnce(TextInput) -> TextInput>>,
}

impl TextField {
    pub fn new(edit: &TextEdit) -> TextField {
        TextField { edit: edit.clone(), placeholder: String::new(), width: None, cursor: false, secure: false, access: None }
    }
    pub fn placeholder(mut self, p: impl Into<String>) -> Self {
        self.placeholder = p.into();
        self
    }
    /// A fixed width; by default the field fills its parent.
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    /// Hide what's typed (passwords).
    pub fn secure(mut self) -> Self {
        self.secure = true;
        self
    }
    /// Where the app keeps this field's `TextEdit`, so typing edits it.
    pub fn on_edit<S: 'static>(mut self, f: impl for<'a> Fn(&'a mut S) -> &'a mut TextEdit + 'static) -> Self {
        self.access = Some(Box::new(move |i: TextInput| i.on_edit(f)));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let (rest, hot) = rest_and_hot(t);
        let bw = tk.controls.normal.border.max_width().max(tk.controls.hover.border.max_width()) as f32;
        let style = t.text_style(tk.font.body);
        let mut input = TextInput::new(&self.edit).plain().width(None).text_style(style).placeholder(self.placeholder);
        if self.secure {
            input = input.secure();
        }
        if let Some(access) = self.access {
            input = access(input);
        }
        let inner = input.build(t.kit());
        let mut base = row()
            .items_center()
            // rune-kit's plain input keeps its own 8 point inner inset; take it back so the
            // text starts where Qt's does.
            .px((sp.control_padding_x as f32 + bw - 8.0).max(0.0))
            .py(sp.input_padding_y as f32 + bw)
            .h(tk.font.line_px(tk.font.body) as f32 + 2.0 * (sp.input_padding_y as f32 + bw));
        base = match self.width {
            Some(w) => base.w(w),
            None => base.w_full(),
        };
        chrome(t, base, vec![inner.grow(1.0)], &rest, &hot, self.cursor, t.radius())
    }
}

/// An integer stepper with a label above: [−] value [+].
pub struct NumberField {
    label: String,
    value: i64,
    from: i64,
    to: i64,
    step: i64,
    cursor: bool,
    change: Option<Box<dyn Fn(El, i64) -> El>>,
}

impl NumberField {
    pub fn new(label: impl Into<String>, value: i64) -> NumberField {
        NumberField { label: label.into(), value, from: 0, to: 100, step: 1, cursor: false, change: None }
    }
    pub fn range(mut self, from: i64, to: i64) -> Self {
        self.from = from;
        self.to = to;
        self
    }
    pub fn step(mut self, step: i64) -> Self {
        self.step = step;
        self
    }
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    pub fn on_change<S: 'static>(mut self, f: impl Fn(&mut S, i64) + Clone + 'static) -> Self {
        self.change = Some(Box::new(move |e: El, v: i64| {
            let f = f.clone();
            e.on_press(move |s: &mut S| f(s, v))
        }));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let c = &tk.controls;
        let (rest, hot) = rest_and_hot(t);
        let h = (sp.control_height as f32).max((tk.font.body + sp.control_padding_y * 2) as f32);
        let inset = c.normal.border.widths[0] as f32;
        let side = h - inset * 2.0;
        let step_button = |glyph: &str, target: i64, enabled: bool| {
            let b = el()
                .size(side, side)
                .center()
                .radius(t.radius_small())
                .hover(color(c.hover.fill))
                .press(color(c.pressed.fill))
                .role("button")
                .label(glyph.to_string())
                .opacity(if enabled { 1.0 } else { 0.4 })
                .child(text(glyph.to_string()).font(t.text_style(tk.font.body)).color(t.fg()));
            match (&self.change, enabled) {
                (Some(attach), true) => attach(b, target),
                _ => b,
            }
        };
        let down = step_button("\u{2212}", (self.value - self.step).max(self.from), self.value > self.from);
        let up = step_button("+", (self.value + self.step).min(self.to), self.value < self.to);
        let value = text(self.value.to_string()).font(t.text_style(tk.font.body)).color(t.fg());
        let base = row().w(sp.number_field_width as f32).h(h).items_center().justify_between().px(inset).role("spinbutton");
        let field = chrome(t, base, vec![down, value, up], &rest, &hot, self.cursor, t.radius());
        let mut out = col().gap(sp.md as f32);
        if !self.label.is_empty() {
            out = out.child(field_label(t, &self.label, tk.font.body_small, false));
        }
        out.child(field)
    }
}

/// A labeled choice of one option. The trigger is built here; its list goes in the overlay
/// `layer` (rune-kit's convention) while `open`, anchored below the trigger.
pub struct Dropdown {
    key: String,
    label: String,
    options: Vec<(String, String)>,
    value: String,
    open: bool,
    cursor: bool,
    width: Option<f32>,
    toggle: Option<Attach>,
    choose: Option<Box<dyn Fn(El, String) -> El>>,
}

impl Dropdown {
    /// `key` names the trigger (the list anchors to it); `options` are (value, label) pairs.
    pub fn new<V: Into<String>, L: Into<String>>(key: impl Into<String>, options: impl IntoIterator<Item = (V, L)>, value: impl Into<String>) -> Dropdown {
        Dropdown {
            key: key.into(),
            label: String::new(),
            options: options.into_iter().map(|(v, l)| (v.into(), l.into())).collect(),
            value: value.into(),
            open: false,
            cursor: false,
            width: None,
            toggle: None,
            choose: None,
        }
    }
    pub fn label(mut self, label: impl Into<String>) -> Self {
        self.label = label.into();
        self
    }
    pub fn open(mut self, open: bool) -> Self {
        self.open = open;
        self
    }
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    pub fn on_toggle<S: 'static>(mut self, f: impl Fn(&mut S) + 'static) -> Self {
        self.toggle = Some(Box::new(move |e: El| e.on_press(f)));
        self
    }
    pub fn on_choose<S: 'static>(mut self, f: impl Fn(&mut S, &str) + Clone + 'static) -> Self {
        self.choose = Some(Box::new(move |e: El, v: String| {
            let f = f.clone();
            e.on_press(move |s: &mut S| f(s, &v))
        }));
        self
    }

    pub fn build(self, t: &Theme, layer: &mut Vec<El>) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let (rest, hot) = rest_and_hot(t);
        let width = self.width.unwrap_or(sp.dropdown_width as f32);
        let bw = tk.controls.normal.border.max_width() as f32;
        let current = self.options.iter().find(|(v, _)| *v == self.value).map(|(_, l)| l.clone()).unwrap_or_default();

        let words = text(current).font(t.text_style(tk.font.body)).color(t.fg()).single_line().truncate().grow(1.0).min_w(0.0);
        let chevron = super::glyph(t, "\u{f0140}", tk.font.body, t.dim());
        let base = row()
            .key(self.key.clone())
            .w(width)
            .h(sp.control_height as f32)
            .items_center()
            .gap(sp.md as f32)
            .pl(sp.control_padding_x as f32 + bw)
            .pr(sp.control_gap as f32 + bw)
            .role("combobox")
            .label(self.label.clone())
            .expanded(self.open)
            .focusable(true);
        let mut trigger = chrome(t, base, vec![words, chevron], &rest, &hot, self.cursor || self.open, t.radius());
        if let Some(attach) = self.toggle {
            trigger = attach(trigger);
        }

        if self.open {
            let pop = &tk.surfaces.popups;
            let mut list = col().w(width).padding(tk.controls.normal.border.max_width() as f32).fill(color(pop.background)).radius(t.radius());
            list = border(list, &pop.border.with_width(tk.controls.normal.border.max_width().max(1.0)));
            for (value, label) in self.options {
                let checked = value == self.value;
                let row_el = super::MenuRow::new(label).checked(checked).build(t);
                list = list.child(match &self.choose {
                    Some(attach) => attach(row_el, value),
                    None => row_el,
                });
            }
            layer.push(list.anchored(self.key, Side::Bottom, Align::Start, sp.xxs as f32));
        }

        let mut out = col().gap(sp.label_gap as f32);
        if !self.label.is_empty() {
            out = out.child(field_label(t, &self.label, tk.font.caption, true));
        }
        out.child(trigger)
    }
}
