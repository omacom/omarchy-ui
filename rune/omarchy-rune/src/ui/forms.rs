//! Form rows and inputs: SettingRow, Checkbox, Radio, RadioGroup, PasswordField and TextArea,
//! drawn as the Qt binding's `SettingRow.qml`, `Checkbox.qml`, `RadioGroup.qml`,
//! `PasswordField.qml` and `TextArea.qml` draw them.

use omarchy_theme::BorderSpec;
use rune::{El, Key, Live, Spring, TextEdit, col, el, row, text};

use super::navigation::{Axis, ends};
use super::{Attach, Look, glyph};
use crate::{Theme, border, color};

/// An `on_edit` waiting for the app's state type.
type Access = Box<dyn FnOnce(El) -> El>;

/// The rest and hot looks of a bordered input (TextField's ladder).
pub(crate) fn input_looks(t: &Theme) -> (Look, Look) {
    let c = &t.tokens().controls;
    (Look::new(c.normal.fill, c.normal.border.clone()), Look::new(c.hover.fill, c.hover.border.clone()))
}

/// One look as a layer filling its parent.
pub(crate) fn layer(look: &Look, radius: f32) -> El {
    border(el().pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0)).radius(radius).fill(color(look.fill)), &look.border)
}

/// The rest and hot layers of `rest`/`hot`, cross-faded by the nearest hover group, or the hot
/// one alone when `forced` (the keyboard cursor).
pub(crate) fn state_layers(rest: &Look, hot: &Look, forced: bool, radius: f32) -> Vec<El> {
    if forced {
        vec![layer(hot, radius)]
    } else {
        vec![layer(rest, radius).group_opacity(1.0, 0.0), layer(hot, radius).group_opacity(0.0, 1.0)]
    }
}

/// A bold glyph boxed as Qt boxes text (see [`glyph`]), for the check mark.
pub(crate) fn bold_mark(t: &Theme, s: &str, size: i64, ink: rune::Color) -> El {
    let style = t.text_style_bold(size);
    text(s.to_string()).font(style).color(ink).single_line()
}

// ---------------------------------------------------------------------------------- SettingRow

/// One row of a settings list: label and dim description on the left, any control on the
/// right. Rows stacked in a column line their labels and controls up.
pub struct SettingRow {
    label: String,
    description: String,
    control: Option<El>,
    cursor: bool,
    disabled: bool,
    width: Option<f32>,
}

impl SettingRow {
    pub fn new(label: impl Into<String>) -> SettingRow {
        SettingRow { label: label.into(), description: String::new(), control: None, cursor: false, disabled: false, width: None }
    }
    pub fn description(mut self, d: impl Into<String>) -> Self {
        self.description = d.into();
        self
    }
    /// The control at the end: a ToggleSwitch, Dropdown, NumberField, Button...
    pub fn control(mut self, e: El) -> Self {
        self.control = Some(e);
        self
    }
    /// The row's hover fill, for panels walking rows with a keyboard cursor.
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    /// Dim the words (the control disables itself).
    pub fn disabled(mut self, on: bool) -> Self {
        self.disabled = on;
        self
    }
    /// A fixed width; by default the row fills its parent.
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let (ink, dim) = if self.disabled { (t.faint(), t.faint()) } else { (t.fg(), t.dim()) };
        let mut words = col()
            .grow(1.0)
            .min_w(0.0)
            .gap(sp.xxs as f32)
            .child(text(self.label.clone()).font(t.text_style(tk.font.body)).color(ink).single_line().truncate());
        if !self.description.is_empty() {
            words = words.child(text(self.description).font(t.text_style(tk.font.caption)).color(dim).wrap());
        }
        let pad_y = sp.md as f32;
        let xxs = sp.xxs as f32;
        let words = words.py((pad_y - xxs).max(0.0));
        let mut r = row()
            .items_center()
            .gap(sp.xxl as f32)
            .px(sp.row_padding_x as f32)
            .py(xxs)
            .min_h(sp.control_height as f32 + pad_y * 2.0)
            .radius(t.radius())
            .label(self.label)
            .child(words);
        if self.cursor {
            r = r.fill(color(tk.controls.hover.fill));
        }
        if let Some(c) = self.control {
            r = r.child(row().items_center().gap(sp.md as f32).child(c));
        }
        match self.width {
            Some(w) => r.w(w),
            None => r.w_full(),
        }
    }
}

// ------------------------------------------------------------------------- Checkbox and Radio

/// A check row: a small square box, a label and an optional dim description. Checked fills
/// the box with the selected state and marks it with an accent check. [`Radio`] draws the same
/// row as a circle with an accent dot.
pub struct Checkbox {
    label: String,
    description: String,
    checked: bool,
    exclusive: bool,
    cursor: bool,
    disabled: bool,
    key: Option<String>,
    toggle: Option<Attach>,
}

impl Checkbox {
    pub fn new(label: impl Into<String>) -> Checkbox {
        Checkbox {
            label: label.into(),
            description: String::new(),
            checked: false,
            exclusive: false,
            cursor: false,
            disabled: false,
            key: None,
            toggle: None,
        }
    }
    pub fn description(mut self, d: impl Into<String>) -> Self {
        self.description = d.into();
        self
    }
    pub fn checked(mut self, on: bool) -> Self {
        self.checked = on;
        self
    }
    /// Draw as a radio: a circle with a dot.
    pub fn exclusive(mut self, on: bool) -> Self {
        self.exclusive = on;
        self
    }
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    pub fn disabled(mut self, on: bool) -> Self {
        self.disabled = on;
        self
    }
    pub fn key(mut self, key: impl Into<String>) -> Self {
        self.key = Some(key.into());
        self
    }
    /// Pressed, or Space while focused. The app flips `checked`.
    pub fn on_toggle<S: 'static>(mut self, f: impl Fn(&mut S) + 'static) -> Self {
        self.toggle = Some(Box::new(move |e: El| e.on_press(f)));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let c = &tk.controls;
        let sp = &tk.spacing;
        let size = t.space(16.0);
        let line = tk.font.line_px(tk.font.body) as f32;
        let box_y = ((line - size) / 2.0).round().max(0.0);
        let text_y = ((size - line) / 2.0).round().max(0.0);
        let radius = if self.exclusive { size / 2.0 } else { t.radius_small() };

        let rest = if self.checked {
            Look::new(c.selected.fill, c.selected.border.clone())
        } else {
            Look::new(c.normal.fill, c.normal.border.clone())
        };
        let hot = Look::new(c.hover.fill, c.hover.border.clone());
        let layers = if self.disabled { vec![layer(&rest, radius)] } else { state_layers(&rest, &hot, self.cursor, radius) };
        let mut mark_box = el().size(size, size).center().mt(box_y).children(layers);
        if self.checked {
            mark_box = mark_box.child(if self.exclusive {
                let dot = (size / 2.0).round();
                el().size(dot, dot).radius(dot / 2.0).fill(t.accent())
            } else {
                bold_mark(t, "\u{2713}", (size * 0.85).round() as i64, t.accent())
            });
        }
        if self.disabled {
            mark_box = mark_box.opacity(0.5);
        }

        let (ink, dim) = if self.disabled { (t.faint(), t.faint()) } else { (t.fg(), t.dim()) };
        let mut words = col()
            .mt(text_y)
            .min_w(0.0)
            .shrink()
            .gap(sp.xxs as f32)
            .child(text(self.label.clone()).font(t.text_style(tk.font.body)).color(ink).single_line().truncate());
        if !self.description.is_empty() {
            words = words.child(text(self.description).font(t.text_style(tk.font.caption)).color(dim).wrap());
        }

        let mut e = row()
            .items_start()
            .gap(sp.row_gap as f32)
            .role(if self.exclusive { "radio" } else { "checkbox" })
            .label(self.label)
            .checked(self.checked)
            .disabled(self.disabled)
            .child(mark_box)
            .child(words);
        if !self.disabled {
            e = e.hover_group_focus().focusable(true);
        }
        if let Some(k) = self.key {
            e = e.key(k);
        }
        match (self.toggle, self.disabled) {
            (Some(attach), false) => attach(e),
            _ => e,
        }
    }
}

/// One choice of several: a [`Checkbox`] drawn as a circle with an accent dot.
pub struct Radio;

impl Radio {
    #[allow(clippy::new_ret_no_self)]
    pub fn new(label: impl Into<String>) -> Checkbox {
        Checkbox::new(label).exclusive(true)
    }
}

/// A column of radios, one checked. Arrow keys move among them.
pub struct RadioGroup {
    options: Vec<(String, String, String)>,
    value: String,
    cursor: Option<usize>,
    choose: Option<Box<dyn Fn(El, String) -> El>>,
}

impl RadioGroup {
    /// `options` are (value, label) pairs.
    pub fn new<V: Into<String>, L: Into<String>>(options: impl IntoIterator<Item = (V, L)>, value: impl Into<String>) -> RadioGroup {
        RadioGroup {
            options: options.into_iter().map(|(v, l)| (v.into(), l.into(), String::new())).collect(),
            value: value.into(),
            cursor: None,
            choose: None,
        }
    }
    /// A dim description under the option with this value.
    pub fn description(mut self, value: &str, d: impl Into<String>) -> Self {
        if let Some(o) = self.options.iter_mut().find(|o| o.0 == value) {
            o.2 = d.into();
        }
        self
    }
    /// Force the hover-cursor look on one row.
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
        let mut g = col().gap(t.tokens().spacing.lg as f32).role("radiogroup").focus_group();
        let n = self.options.len();
        for (i, (value, label, description)) in self.options.into_iter().enumerate() {
            let r = Radio::new(label)
                .description(description)
                .checked(value == self.value)
                .cursor(self.cursor == Some(i))
                .key(format!("radio-{value}"))
                .build(t);
            let r = t.key_map(r, ends(i, n, Axis::Both));
            g = g.child(match &self.choose {
                Some(attach) => attach(r, value),
                None => r,
            });
        }
        // As Qt's RadioGroup: j and k walk too, and the ends don't wrap (above).
        t.key_map(g, &[("j", Some(Key::Down)), ("k", Some(Key::Up))])
    }
}

// ------------------------------------------------------------------------------ PasswordField

/// A bare single-line editor in the field type with TextField's placeholder (foreground darker
/// 1.6), for chrome drawn around it here.
pub(crate) fn bare_input(t: &Theme, edit: &TextEdit, placeholder: String) -> El {
    let tk = t.tokens();
    rune::text_input(edit, t.text_style(tk.font.body))
        .single_line()
        .key("input")
        .grow(1.0)
        .shrink()
        .min_w(0.0)
        .color(t.fg())
        .label(placeholder.clone())
        .placeholder(placeholder, t.faint())
        .caret(t.fg(), color(tk.controls.selection.fill))
}

/// A TextField for secrets: dots, a lock glyph before them, an optional eye that reveals the
/// text, and an error state (polkit's `borderError` border, its `textError` message underneath,
/// and a shake through [`PasswordField::shaker`]).
pub struct PasswordField {
    edit: TextEdit,
    placeholder: String,
    width: Option<f32>,
    cursor: bool,
    revealable: bool,
    revealed: bool,
    error: Option<String>,
    shaker: Option<Live<[f32; 2]>>,
    access: Option<Access>,
    reveal: Option<Attach>,
}

impl PasswordField {
    pub fn new(edit: &TextEdit) -> PasswordField {
        PasswordField {
            edit: edit.clone(),
            placeholder: String::new(),
            width: None,
            cursor: false,
            revealable: false,
            revealed: false,
            error: None,
            shaker: None,
            access: None,
            reveal: None,
        }
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
    /// Show the text instead of dots.
    pub fn revealed(mut self, on: bool) -> Self {
        self.revealed = on;
        self
    }
    /// The eye at the end, which calls `f` (the app flips `revealed`).
    pub fn on_reveal<S: 'static>(mut self, f: impl Fn(&mut S) + 'static) -> Self {
        self.revealable = true;
        self.reveal = Some(Box::new(move |e: El| e.on_press(f)));
        self
    }
    /// Show the eye without a handler (still renders).
    pub fn revealable(mut self, on: bool) -> Self {
        self.revealable = on;
        self
    }
    /// The error message; the field turns urgent and shows it underneath.
    pub fn error(mut self, message: impl Into<String>) -> Self {
        let m = message.into();
        self.error = if m.is_empty() { None } else { Some(m) };
        self
    }
    /// Drive the shake from `live`, which [`PasswordField::shake`] pulses. Keep it in app state.
    pub fn shaker(mut self, live: &Live<[f32; 2]>) -> Self {
        self.shaker = Some(live.clone());
        self
    }
    /// Shake a field bound with [`PasswordField::shaker`], as polkit does on a wrong password.
    /// Under reduced motion it stays still.
    pub fn shake(live: &Live<[f32; 2]>, t: &Theme) {
        if t.tokens().reduce_motion {
            return;
        }
        live.pulse([-t.space(8.0), 0.0], [0.0, 0.0], std::time::Duration::from_millis(35));
    }
    pub fn on_edit<S: 'static>(mut self, f: impl for<'a> Fn(&'a mut S) -> &'a mut TextEdit + 'static) -> Self {
        self.access = Some(Box::new(move |e: El| e.on_edit(f)));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let c = &tk.controls;
        let bw = c.normal.border.max_width().max(c.hover.border.max_width()) as f32;
        let (mut rest, mut hot) = input_looks(t);
        let polkit = &tk.surfaces.polkit;
        let error_ink = color(polkit.text_error);
        if self.error.is_some() {
            let w = (c.normal.border.max_width().max(1.0)) as f64;
            let spec = BorderSpec { color: polkit.border_error.color, widths: [w; 4], gradient: None };
            rest.border = spec.clone();
            hot.border = spec;
        }

        let mut edit = self.edit.clone();
        edit.set_secure(true);
        // The placeholder in textFaint, as every placeholder.
        let placeholder = self.placeholder.clone();
        let mut input = bare_input(t, &edit, self.placeholder).placeholder(placeholder, t.faint()).revealed(self.revealed);
        if let Some(access) = self.access {
            input = access(input);
        }
        let lock = glyph(t, "\u{f023}", tk.font.body, if self.error.is_some() { error_ink } else { t.dim() });
        let mut children = vec![lock, input];
        if self.revealable {
            let eye = glyph(t, if self.revealed { "\u{f0209}" } else { "\u{f0208}" }, tk.font.body, t.dim());
            let eye = el().role("button").label("Reveal").child(eye);
            children.push(match self.reveal {
                Some(attach) => attach(eye),
                None => eye,
            });
        }

        let pad_x = sp.control_padding_x as f32 + bw;
        let pad_y = sp.input_padding_y as f32 + bw;
        let mut base = row()
            .items_center()
            .gap(sp.control_gap as f32)
            .px(pad_x)
            .py(pad_y)
            .role("textbox")
            .h(tk.font.line_px(tk.font.body) as f32 + 2.0 * pad_y)
            .hover_group_focus()
            .radius(t.radius());
        base = match self.width {
            Some(w) => base.w(w),
            None => base.w_full(),
        };
        let mut field = base.children(state_layers(&rest, &hot, self.cursor, t.radius())).children(children);
        if let Some(live) = &self.shaker {
            field = field.live_offset(live.smooth(Spring::bouncy(0.14, 0.3)));
        }
        let Some(message) = self.error else { return field };
        let mut out = col().gap(sp.label_gap as f32).child(field).child(
            text(message).font(t.text_style(tk.font.caption)).color(error_ink).wrap().role("alert"),
        );
        out = match self.width {
            Some(w) => out.w(w),
            None => out.w_full(),
        };
        out
    }
}

// ----------------------------------------------------------------------------------- TextArea

/// A multi-line TextField: the same chrome, padding, type and placeholder, wrapping its text.
/// `min_rows` lines tall at least, growing with its text to `max_rows`, then scrolling.
pub struct TextArea {
    edit: TextEdit,
    placeholder: String,
    width: Option<f32>,
    min_rows: usize,
    max_rows: usize,
    cursor: bool,
    access: Option<Access>,
}

impl TextArea {
    pub fn new(edit: &TextEdit) -> TextArea {
        TextArea { edit: edit.clone(), placeholder: String::new(), width: None, min_rows: 3, max_rows: 6, cursor: false, access: None }
    }
    pub fn placeholder(mut self, p: impl Into<String>) -> Self {
        self.placeholder = p.into();
        self
    }
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    /// At least `min` lines tall, growing to `max`, then scrolling (default 3 to 6).
    pub fn rows(mut self, min: usize, max: usize) -> Self {
        self.min_rows = min.max(1);
        self.max_rows = max.max(self.min_rows);
        self
    }
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    pub fn on_edit<S: 'static>(mut self, f: impl for<'a> Fn(&'a mut S) -> &'a mut TextEdit + 'static) -> Self {
        self.access = Some(Box::new(move |e: El| e.on_edit(f)));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let c = &tk.controls;
        let bw = c.normal.border.max_width().max(c.hover.border.max_width()) as f32;
        let (rest, hot) = input_looks(t);
        let mut field = rune::text_input(&self.edit, t.text_style(tk.font.body))
            .key("input")
            .w_full()
            .color(t.fg())
            .placeholder(self.placeholder, t.faint())
            .caret(t.fg(), color(c.selection.fill));
        if let Some(access) = self.access {
            field = access(field);
        }
        let viewport = rune::auto_grow(field, self.min_rows, self.max_rows);
        let mut base = col()
            .px(sp.control_padding_x as f32 + bw)
            .py(sp.input_padding_y as f32 + bw)
            .role("textbox")
            .hover_group_focus()
            .radius(t.radius());
        base = match self.width {
            Some(w) => base.w(w),
            None => base.w_full(),
        };
        base.children(state_layers(&rest, &hot, self.cursor, t.radius())).child(viewport)
    }
}

