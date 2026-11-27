//! Forms (spec/components/forms.md): Checkbox, Radio, RadioGroup, PasswordField and TextArea,
//! drawn as the Qt kit's `Checkbox.qml`, `RadioGroup.qml`, `PasswordField.qml` and
//! `TextArea.qml` draw them. SettingRow and Dialog are in `surface.rs`.
//!
//! [`TextAreaInput`]'s editing is adapted from GPUI's `examples/input.rs` (Apache-2.0), as
//! `TextInput` is, for wrapped multi-line text.

use std::ops::Range;
use std::rc::Rc;
use std::time::Duration;

use gpui::prelude::*;
use gpui::{
    Animation, AnimationExt, AnyElement, App, AvailableSpace, Bounds, ClickEvent, ClipboardItem, Context, CursorStyle, Div, ElementId,
    ElementInputHandler, Entity, EntityInputHandler, FocusHandle, Focusable, GlobalElementId, Hsla, KeyDownEvent, LayoutId,
    MouseButton, MouseDownEvent, MouseMoveEvent, MouseUpEvent, Pixels, Point, SharedString, Size, Style, TextRun, UTF16Selection,
    Window, WrappedLine, div, fill, point, px, relative, size,
};
use omarchy_theme::BorderSpec;
use unicode_segmentation::UnicodeSegmentation;

use super::{Handler, Look, focus_handle, id_for, keyed_state, label, label_elided, layer, paragraph, tab_keys};
use crate::{Paint, color};

/// The rest and hot looks of a bordered input (TextField's ladder).
pub(crate) fn input_looks(t: &Paint) -> (Look, Look) {
    let c = &t.tokens().controls;
    (Look::new(c.normal.fill, c.normal.border.clone()), Look::new(c.hover.fill, c.hover.border.clone()))
}

/// The rest and hot layers of a control's part (a checkbox's box), following the nearest chrome
/// group's hover, or the hot one alone when `forced`.
pub(crate) fn state_layers(rest: &Look, hot: &Look, forced: bool, radius: Pixels) -> Vec<Div> {
    if forced {
        vec![layer(hot, radius)]
    } else {
        vec![
            layer(rest, radius).group_hover(super::GROUP, |s| s.opacity(0.0)),
            layer(hot, radius).opacity(0.0).group_hover(super::GROUP, |s| s.opacity(1.0)),
        ]
    }
}

/// The check mark: a bold `✓` sized to a `box_size` box.
pub(crate) fn check_mark(t: &Paint, box_size: f32, ink: Hsla) -> Div {
    label(t, "\u{2713}", (box_size * 0.85).round() as i64, true, ink)
}

/// A glyph boxed as Qt's `Text` boxes one: its advance, widened by any ink that overhangs it, in
/// whole pixels. Nerd Font icons in the non-Mono Nerd Fonts overhang their cell, so the bare
/// advance (GPUI's text box) lets them crowd what follows. The ink comes from the font file
/// (GPUI's Linux text system reports a glyph's cell as its bounds).
pub(crate) fn boxed_glyph(t: &Paint, s: &'static str, size: i64, ink: Hsla) -> Div {
    let ch = s.chars().next().unwrap_or(' ');
    let Some(g) = t.tokens().font.file.as_deref().and_then(|f| super::ink::ink(f, ch)) else {
        return label(t, s, size, false, ink);
    };
    let k = size as f32;
    let (adv, x0, x1) = (g.advance * k, g.x_min * k, g.x_max * k);
    let left = (-x0).max(0.0).ceil();
    let right = if x1 > adv { (x1 - adv).ceil() } else { 0.0 };
    div().flex().flex_shrink_0().w(px(adv + left + right)).pl(px(left)).child(label(t, s, size, false, ink))
}


// ------------------------------------------------------------------------- Checkbox and Radio

/// A check row: a small square box, a label and an optional dim description. Checked fills the
/// box with the selected state and marks it with an accent check; [`Radio`] draws the same row as
/// a circle with an accent dot. The caller owns `checked`: flip it in `on_toggle`. Space and
/// Return toggle while focused; a disabled one dims and ignores the pointer and keys.
#[derive(IntoElement)]
pub struct Checkbox {
    id: ElementId,
    label: SharedString,
    description: SharedString,
    checked: bool,
    exclusive: bool,
    cursor: bool,
    disabled: bool,
    tab_stop: bool,
    on_toggle: Option<Handler<bool>>,
}

impl Checkbox {
    pub fn new(label: impl Into<SharedString>) -> Checkbox {
        let label = label.into();
        Checkbox {
            id: id_for("checkbox", &label),
            label,
            description: SharedString::default(),
            checked: false,
            exclusive: false,
            cursor: false,
            disabled: false,
            tab_stop: true,
            on_toggle: None,
        }
    }
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
        self
    }
    pub fn description(mut self, d: impl Into<SharedString>) -> Self {
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
    /// Force the hover-cursor look on the box.
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    pub fn disabled(mut self, on: bool) -> Self {
        self.disabled = on;
        self
    }
    /// Called with the new value (clicked, or Space/Return while focused).
    pub fn on_toggle(mut self, f: impl Fn(&bool, &mut Window, &mut App) + 'static) -> Self {
        self.on_toggle = Some(Rc::new(f));
        self
    }
    /// Not a tab stop of its own (a RadioGroup's rows; the group takes the keys).
    pub(crate) fn tab_stop(mut self, on: bool) -> Self {
        self.tab_stop = on;
        self
    }
}

/// One choice of several: a [`Checkbox`] drawn as a circle with an accent dot.
pub struct Radio;

impl Radio {
    #[allow(clippy::new_ret_no_self)]
    pub fn new(label: impl Into<SharedString>) -> Checkbox {
        let label = label.into();
        Checkbox::new(label.clone()).id(id_for("radio", &label)).exclusive(true)
    }
}

impl RenderOnce for Checkbox {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let c = &tk.controls;
        let sp = &tk.spacing;
        let size = t.space(16.0);
        let line = tk.font.line_px(tk.font.body) as f32;
        let box_y = ((line - size) / 2.0).round().max(0.0);
        let text_y = ((size - line) / 2.0).round().max(0.0);
        let radius = if self.exclusive { px(size / 2.0) } else { t.radius_small() };
        let focus = (self.tab_stop && !self.disabled).then(|| focus_handle(&self.id, window, cx));
        let hot_now = !self.disabled && (self.cursor || focus.as_ref().is_some_and(|f| f.is_focused(window)));

        let rest = if self.checked {
            Look::new(c.selected.fill, c.selected.border.clone())
        } else {
            Look::new(c.normal.fill, c.normal.border.clone())
        };
        let hot = Look::new(c.hover.fill, c.hover.border.clone());
        let layers = if self.disabled { vec![layer(&rest, radius)] } else { state_layers(&rest, &hot, hot_now, radius) };
        let mut mark_box = div()
            .relative()
            .flex()
            .flex_shrink_0()
            .items_center()
            .justify_center()
            .size(px(size))
            .mt(px(box_y))
            .children(layers);
        if self.checked {
            mark_box = mark_box.child(if self.exclusive {
                let dot = (size / 2.0).round();
                div().size(px(dot)).rounded(px(dot / 2.0)).bg(t.accent())
            } else {
                check_mark(&t, size, t.accent())
            });
        }
        if self.disabled {
            mark_box = mark_box.opacity(0.5);
        }

        let (ink, dim) = if self.disabled { (t.faint(), t.faint()) } else { (t.fg(), t.dim()) };
        let mut words = div()
            .flex()
            .flex_col()
            .min_w_0()
            .mt(px(text_y))
            .gap(px(sp.xxs as f32))
            .child(label_elided(&t, self.label, tk.font.body, false, ink));
        if !self.description.is_empty() {
            words = words.child(paragraph(&t, self.description, tk.font.caption, dim));
        }

        let mut e = div()
            .id(self.id)
            .flex()
            .flex_row()
            .items_start()
            .gap(px(sp.row_gap as f32))
            .child(mark_box)
            .child(words);
        if self.disabled {
            return e;
        }
        e = e.group(super::GROUP).cursor_pointer();
        let checked = self.checked;
        if let Some(f) = self.on_toggle.clone() {
            e = e.on_click(move |_, window, cx| f(&!checked, window, cx));
        }
        if let Some(focus) = focus {
            let keys = self.on_toggle;
            e = e.track_focus(&focus).on_key_down(move |ev: &KeyDownEvent, window, cx| {
                if tab_keys(ev, window, cx) {
                    cx.stop_propagation();
                } else if super::is_activate(ev) {
                    if let Some(f) = &keys {
                        f(&!checked, window, cx);
                    }
                    cx.stop_propagation();
                }
            });
        }
        e
    }
}

/// A column of radios, one checked: one tab stop; Up/Down/Left/Right and j/k move the choice
/// (stopping at the ends), as Qt's RadioGroup. While it has focus the checked row wears the
/// cursor look.
#[derive(IntoElement)]
pub struct RadioGroup {
    id: ElementId,
    options: Vec<(SharedString, SharedString, SharedString)>,
    value: SharedString,
    cursor: Option<usize>,
    on_choose: Option<Handler<SharedString>>,
}

impl RadioGroup {
    /// `options` are (value, label) pairs.
    pub fn new<V: Into<SharedString>, L: Into<SharedString>>(options: impl IntoIterator<Item = (V, L)>, value: impl Into<SharedString>) -> RadioGroup {
        let options: Vec<_> = options.into_iter().map(|(v, l)| (v.into(), l.into(), SharedString::default())).collect();
        let key = options.iter().map(|o| o.0.as_ref()).collect::<Vec<_>>().join("|");
        RadioGroup { id: id_for("radio-group", &key), options, value: value.into(), cursor: None, on_choose: None }
    }
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
        self
    }
    /// A dim description under the option with this value.
    pub fn description(mut self, value: &str, d: impl Into<SharedString>) -> Self {
        if let Some(o) = self.options.iter_mut().find(|o| o.0.as_ref() == value) {
            o.2 = d.into();
        }
        self
    }
    /// Force the hover-cursor look on one row.
    pub fn cursor(mut self, index: Option<usize>) -> Self {
        self.cursor = index;
        self
    }
    /// Called with the chosen value.
    pub fn on_choose(mut self, f: impl Fn(&SharedString, &mut Window, &mut App) + 'static) -> Self {
        self.on_choose = Some(Rc::new(f));
        self
    }
}

impl RenderOnce for RadioGroup {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let focus = focus_handle(&self.id, window, cx);
        let focused = focus.is_focused(window);
        let values: Rc<Vec<SharedString>> = Rc::new(self.options.iter().map(|o| o.0.clone()).collect());
        let current = values.iter().position(|v| *v == self.value);
        let mut g = div().id(self.id.clone()).flex().flex_col().gap(px(t.tokens().spacing.lg as f32)).track_focus(&focus);
        for (i, (value, text, description)) in self.options.into_iter().enumerate() {
            let checked = value == self.value;
            let mut r = Radio::new(text)
                .id(ElementId::NamedChild(std::sync::Arc::new(self.id.clone()), value.to_string().into()))
                .description(description)
                .checked(checked)
                .cursor(self.cursor == Some(i) || (focused && checked))
                .tab_stop(false);
            if let Some(f) = self.on_choose.clone() {
                let focus = focus.clone();
                r = r.on_toggle(move |_, window, cx| {
                    focus.focus(window, cx);
                    if !checked {
                        f(&value, window, cx);
                    }
                });
            }
            g = g.child(r);
        }
        let keys = self.on_choose;
        g.on_key_down(move |ev: &KeyDownEvent, window, cx| {
            if tab_keys(ev, window, cx) {
                cx.stop_propagation();
                return;
            }
            let step: isize = match ev.keystroke.key.as_str() {
                "down" | "right" | "j" => 1,
                "up" | "left" | "k" => -1,
                _ => return,
            };
            cx.stop_propagation();
            let n = values.len() as isize;
            if n == 0 {
                return;
            }
            let at = current.map_or(if step > 0 { -1 } else { n }, |i| i as isize);
            let next = (at + step).clamp(0, n - 1) as usize;
            if Some(next) != current
                && let Some(f) = &keys
            {
                f(&values[next], window, cx);
            }
        })
    }
}

// ------------------------------------------------------------------------------ PasswordField

#[derive(Default)]
struct Secret {
    revealed: bool,
    /// Rendered before: the error it was built with doesn't shake it.
    seen: bool,
    /// The error shown last frame, and how many times a new one has shaken the field.
    error: Option<SharedString>,
    shakes: usize,
}

/// A TextField for secrets: dots, a lock glyph before them, an optional eye that reveals the
/// text, and an error state (polkit's `borderError` border, its `textError` lock and message
/// underneath, and the polkit shake and flash when the error appears).
#[derive(IntoElement)]
pub struct PasswordField {
    id: ElementId,
    input: Entity<super::TextInput>,
    placeholder: Option<SharedString>,
    width: Option<f32>,
    cursor: bool,
    revealable: bool,
    revealed: Option<bool>,
    error: Option<SharedString>,
    on_reveal: Option<Handler<bool>>,
}

impl PasswordField {
    pub fn new(input: &Entity<super::TextInput>) -> PasswordField {
        PasswordField {
            id: ElementId::Name(format!("omarchy-password-{}", input.entity_id()).into()),
            input: input.clone(),
            placeholder: None,
            width: None,
            cursor: false,
            revealable: false,
            revealed: None,
            error: None,
            on_reveal: None,
        }
    }
    pub fn placeholder(mut self, p: impl Into<SharedString>) -> Self {
        self.placeholder = Some(p.into());
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
    /// The eye at the end, which shows and hides the text.
    pub fn revealable(mut self, on: bool) -> Self {
        self.revealable = on;
        self
    }
    /// Show the text instead of dots (the app owns it; by default the eye flips it itself).
    pub fn revealed(mut self, on: bool) -> Self {
        self.revealed = Some(on);
        self
    }
    /// The eye pressed: called with the new revealed state (the eye shows when this is set).
    pub fn on_reveal(mut self, f: impl Fn(&bool, &mut Window, &mut App) + 'static) -> Self {
        self.revealable = true;
        self.on_reveal = Some(Rc::new(f));
        self
    }
    /// The error message; the field turns urgent, shows it underneath, and shakes when it appears.
    pub fn error(mut self, message: impl Into<SharedString>) -> Self {
        let m = message.into();
        self.error = if m.is_empty() { None } else { Some(m) };
        self
    }
}

/// The polkit shake: −8, +8, 0 (`space(8)`) over 35, 50 and 55 ms.
fn shake_offset(t: f32, amp: f32) -> f32 {
    let ms = t * 140.0;
    let out_quad = |x: f32| 1.0 - (1.0 - x) * (1.0 - x);
    if ms < 35.0 {
        -amp * out_quad(ms / 35.0)
    } else if ms < 85.0 {
        let x = (ms - 35.0) / 50.0;
        let e = if x < 0.5 { 2.0 * x * x } else { 1.0 - (-2.0 * x + 2.0).powi(2) / 2.0 };
        -amp + 2.0 * amp * e
    } else {
        amp * (1.0 - out_quad(((ms - 85.0) / 55.0).min(1.0)))
    }
}

impl RenderOnce for PasswordField {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let c = &tk.controls;
        let state = keyed_state(&self.id, "secret", window, cx, Secret::default);
        let revealed = self.revealed.unwrap_or(state.read(cx).revealed);
        // A new error (not the one the field was built with) shakes it, as Qt's onErrorChanged.
        let shakes = state.update(cx, |s, _| {
            if s.seen && s.error != self.error && self.error.is_some() {
                s.shakes += 1;
            }
            s.seen = true;
            s.error = self.error.clone();
            s.shakes
        });
        let (placeholder, input) = (self.placeholder, self.input.clone());
        input.update(cx, |i, _| {
            if let Some(p) = placeholder {
                i.set_placeholder(p);
            }
            i.set_secure(!revealed);
        });
        let focused = input.read(cx).focus_handle(cx).is_focused(window);

        let bw = c.normal.border.max_width().max(c.hover.border.max_width()) as f32;
        let (mut rest, mut hot) = input_looks(&t);
        let polkit = &tk.surfaces.polkit;
        let error_ink = color(polkit.text_error);
        if self.error.is_some() {
            let w = c.normal.border.max_width().max(1.0);
            let spec = BorderSpec { color: polkit.border_error.color, widths: [w; 4], gradient: None };
            rest.border = spec.clone();
            hot.border = spec;
        }
        let lock = boxed_glyph(&t, "\u{f023}", tk.font.body, if self.error.is_some() { error_ink } else { t.dim() });
        let pad_x = sp.control_padding_x as f32 + bw;
        let pad_y = sp.input_padding_y as f32 + bw;
        let base = div()
            .id(self.id.clone())
            .flex()
            .flex_row()
            .items_center()
            .flex_shrink_0()
            .gap(px(sp.control_gap as f32))
            .px(px(pad_x))
            .py(px(pad_y))
            .font_family(t.family())
            .text_size(px(tk.font.body as f32))
            .line_height(t.line(tk.font.body))
            .text_color(t.fg());
        let base = match self.width {
            Some(w) => base.w(px(w)),
            None => base.w_full(),
        };
        let focus_input = input.clone();
        let mut field = super::chrome(base, &t, &rest, &hot, self.cursor || focused, t.radius())
            .child(lock)
            .child(div().flex_1().min_w_0().overflow_hidden().child(input))
            .on_click(move |_, window, cx| {
                let f = focus_input.read(cx).focus_handle(cx);
                f.focus(window, cx)
            });
        if self.revealable {
            let dim = t.dim();
            let fg = t.fg();
            let mut eye = div()
                .id("reveal")
                .flex_shrink_0()
                .cursor_pointer()
                .text_color(dim)
                .hover(move |s| s.text_color(fg))
                .child(boxed_glyph(&t, if revealed { "\u{f0209}" } else { "\u{f0208}" }, tk.font.body, dim));
            let st = state.clone();
            let f = self.on_reveal.clone();
            eye = eye.on_click(move |_: &ClickEvent, window, cx| {
                cx.stop_propagation();
                let next = !revealed;
                st.update(cx, |s, cx| {
                    s.revealed = next;
                    cx.notify();
                });
                if let Some(f) = &f {
                    f(&next, window, cx);
                }
            });
            field = field.child(eye);
        }
        let field: AnyElement = if shakes > 0 && !tk.reduce_motion {
            let amp = t.space(8.0);
            let flash = polkit.text_error;
            let alpha = tk.controls.selected.fill.a as f64 / 255.0;
            let radius = t.radius();
            div()
                .relative()
                .child(field)
                .with_animation(ElementId::NamedChild(std::sync::Arc::new(self.id.clone()), format!("shake-{shakes}").into()), Animation::new(Duration::from_millis(400)), move |e, x| {
                    let shake = if x * 400.0 < 140.0 { shake_offset(x * 400.0 / 140.0, amp) } else { 0.0 };
                    let fade = (1.0 - x) * (1.0 - x);
                    e.left(px(shake)).child(
                        div()
                            .absolute()
                            .top_0()
                            .left_0()
                            .size_full()
                            .rounded(radius)
                            .bg(color(flash.with_alpha(alpha * fade.clamp(0.0, 1.0) as f64))),
                    )
                })
                .into_any_element()
        } else {
            field.into_any_element()
        };
        let Some(message) = self.error else {
            return div().flex().flex_col().flex_shrink_0().when_some(self.width, |d, w| d.w(px(w))).child(field);
        };
        div()
            .flex()
            .flex_col()
            .flex_shrink_0()
            .gap(px(sp.label_gap as f32))
            .when_some(self.width, |d, w| d.w(px(w)))
            .child(field)
            .child(paragraph(&t, message, tk.font.caption, error_ink))
    }
}

// ------------------------------------------------------------------------------- TextAreaInput

/// The editing behind a [`TextArea`]: wrapped multi-line text, a caret and a selection, IME, the
/// clipboard, the mouse. Keep an `Entity<TextAreaInput>` in your view
/// (`TextAreaInput::new(cx, "")`) and hand it to a `TextArea`. Emits [`super::field::TextChanged`].
pub struct TextAreaInput {
    focus_handle: FocusHandle,
    content: SharedString,
    placeholder: SharedString,
    selected_range: Range<usize>,
    selection_reversed: bool,
    marked_range: Option<Range<usize>>,
    /// Each paragraph's shaped lines, its byte offset and its top (unscrolled), from the last paint.
    last_lines: Vec<(WrappedLine, usize, Pixels)>,
    last_bounds: Option<Bounds<Pixels>>,
    line_height: Pixels,
    scroll: Pixels,
    /// The x the caret keeps while walking up and down.
    goal_x: Option<Pixels>,
    min_rows: usize,
    max_rows: usize,
    is_selecting: bool,
}

impl gpui::EventEmitter<super::field::TextChanged> for TextAreaInput {}

impl TextAreaInput {
    pub fn new(cx: &mut App, text: impl Into<SharedString>) -> Entity<TextAreaInput> {
        let text = text.into();
        cx.new(|cx| TextAreaInput {
            focus_handle: cx.focus_handle().tab_stop(true),
            selected_range: text.len()..text.len(),
            content: text,
            placeholder: SharedString::default(),
            selection_reversed: false,
            marked_range: None,
            last_lines: Vec::new(),
            last_bounds: None,
            line_height: px(16.0),
            scroll: px(0.0),
            goal_x: None,
            min_rows: 3,
            max_rows: 6,
            is_selecting: false,
        })
    }
    pub fn text(&self) -> &SharedString {
        &self.content
    }
    pub fn set_text(&mut self, text: impl Into<SharedString>, cx: &mut Context<Self>) {
        self.content = text.into();
        self.selected_range = self.content.len()..self.content.len();
        self.marked_range = None;
        cx.emit(super::field::TextChanged(self.content.clone()));
        cx.notify();
    }
    pub fn set_placeholder(&mut self, p: impl Into<SharedString>) {
        self.placeholder = p.into();
    }

    fn cursor_offset(&self) -> usize {
        if self.selection_reversed { self.selected_range.start } else { self.selected_range.end }
    }

    fn move_to(&mut self, offset: usize, window: &Window, cx: &mut Context<Self>) {
        self.selected_range = offset..offset;
        self.selection_reversed = false;
        window.notify_text_selection_changed(&self.focus_handle);
        cx.notify()
    }

    fn select_to(&mut self, offset: usize, window: &Window, cx: &mut Context<Self>) {
        if self.selection_reversed {
            self.selected_range.start = offset
        } else {
            self.selected_range.end = offset
        };
        if self.selected_range.end < self.selected_range.start {
            self.selection_reversed = !self.selection_reversed;
            self.selected_range = self.selected_range.end..self.selected_range.start;
        }
        window.notify_text_selection_changed(&self.focus_handle);
        cx.notify()
    }

    fn previous_boundary(&self, offset: usize) -> usize {
        self.content.grapheme_indices(true).rev().find_map(|(i, _)| (i < offset).then_some(i)).unwrap_or(0)
    }
    fn next_boundary(&self, offset: usize) -> usize {
        self.content.grapheme_indices(true).find_map(|(i, _)| (i > offset).then_some(i)).unwrap_or(self.content.len())
    }
    fn previous_word(&self, offset: usize) -> usize {
        self.content.unicode_word_indices().rev().find_map(|(i, _)| (i < offset).then_some(i)).unwrap_or(0)
    }
    fn next_word(&self, offset: usize) -> usize {
        self.content.unicode_word_indices().find_map(|(i, w)| (i + w.len() > offset).then_some(i + w.len())).unwrap_or(self.content.len())
    }
    fn line_start(&self, offset: usize) -> usize {
        self.content[..offset].rfind('\n').map_or(0, |i| i + 1)
    }
    fn line_end(&self, offset: usize) -> usize {
        self.content[offset..].find('\n').map_or(self.content.len(), |i| offset + i)
    }

    /// Where `offset` is, relative to the text's top-left (unscrolled).
    fn position_for(&self, offset: usize) -> Option<Point<Pixels>> {
        for (line, start, top) in &self.last_lines {
            if offset >= *start && offset <= start + line.len() {
                let p = line.position_for_index(offset - start, self.line_height)?;
                return Some(point(p.x, p.y + *top));
            }
        }
        None
    }

    /// The offset nearest `p`, relative to the text's top-left (unscrolled).
    fn offset_for(&self, p: Point<Pixels>) -> usize {
        let Some(first) = self.last_lines.first() else { return 0 };
        if p.y < first.2 {
            return 0;
        }
        for (line, start, top) in &self.last_lines {
            let h = self.line_height * (line.wrap_boundaries().len() + 1) as f32;
            if p.y < *top + h {
                let local = point(p.x.max(px(0.0)), p.y - *top);
                let ix = match line.closest_index_for_position(local, self.line_height) {
                    Ok(i) | Err(i) => i,
                };
                return start + ix.min(line.len());
            }
        }
        self.content.len()
    }

    fn index_for_mouse_position(&self, position: Point<Pixels>) -> usize {
        let Some(bounds) = self.last_bounds else { return 0 };
        self.offset_for(point(position.x - bounds.left(), position.y - bounds.top() + self.scroll))
    }

    fn vertical(&mut self, rows: f32, shift: bool, window: &Window, cx: &mut Context<Self>) {
        let at = self.cursor_offset();
        let Some(p) = self.position_for(at) else { return };
        let x = *self.goal_x.get_or_insert(p.x);
        let to = if p.y + self.line_height * rows < px(0.0) {
            0
        } else {
            self.offset_for(point(x, p.y + self.line_height * rows + self.line_height / 2.0))
        };
        let goal = self.goal_x;
        if shift { self.select_to(to, window, cx) } else { self.move_to(to, window, cx) }
        self.goal_x = goal;
    }

    fn on_key(&mut self, ev: &KeyDownEvent, window: &mut Window, cx: &mut Context<Self>) {
        let k = &ev.keystroke;
        let m = &k.modifiers;
        let word = m.control || m.alt;
        let vertical = matches!(k.key.as_str(), "up" | "down");
        let handled = match k.key.as_str() {
            "backspace" => {
                if self.selected_range.is_empty() {
                    let prev = if word { self.previous_word(self.cursor_offset()) } else { self.previous_boundary(self.cursor_offset()) };
                    self.select_to(prev, window, cx);
                }
                self.replace_text_in_range(None, "", window, cx);
                true
            }
            "delete" => {
                if self.selected_range.is_empty() {
                    let next = self.next_boundary(self.cursor_offset());
                    self.select_to(next, window, cx);
                }
                self.replace_text_in_range(None, "", window, cx);
                true
            }
            "left" => {
                let to = if word { self.previous_word(self.cursor_offset()) } else { self.previous_boundary(self.cursor_offset()) };
                if m.shift {
                    self.select_to(to, window, cx)
                } else if self.selected_range.is_empty() || word {
                    self.move_to(to, window, cx)
                } else {
                    self.move_to(self.selected_range.start, window, cx)
                }
                true
            }
            "right" => {
                let to = if word { self.next_word(self.cursor_offset()) } else { self.next_boundary(self.cursor_offset()) };
                if m.shift {
                    self.select_to(to, window, cx)
                } else if self.selected_range.is_empty() || word {
                    self.move_to(to, window, cx)
                } else {
                    self.move_to(self.selected_range.end, window, cx)
                }
                true
            }
            "up" => {
                self.vertical(-1.0, m.shift, window, cx);
                true
            }
            "down" => {
                self.vertical(1.0, m.shift, window, cx);
                true
            }
            "home" => {
                let to = if m.control { 0 } else { self.line_start(self.cursor_offset()) };
                if m.shift { self.select_to(to, window, cx) } else { self.move_to(to, window, cx) }
                true
            }
            "end" => {
                let to = if m.control { self.content.len() } else { self.line_end(self.cursor_offset()) };
                if m.shift { self.select_to(to, window, cx) } else { self.move_to(to, window, cx) }
                true
            }
            "enter" | "kp_enter" => {
                self.replace_text_in_range(None, "\n", window, cx);
                true
            }
            "a" if m.control => {
                self.move_to(0, window, cx);
                self.select_to(self.content.len(), window, cx);
                true
            }
            "c" | "x" if m.control => {
                if !self.selected_range.is_empty() {
                    cx.write_to_clipboard(ClipboardItem::new_string(self.content[self.selected_range.clone()].to_string()));
                    if k.key == "x" {
                        self.replace_text_in_range(None, "", window, cx);
                    }
                }
                true
            }
            "v" if m.control => {
                if let Some(text) = cx.read_from_clipboard().and_then(|item| item.text()) {
                    self.replace_text_in_range(None, &text, window, cx);
                }
                true
            }
            "tab" => {
                if m.shift { window.focus_prev(cx) } else { window.focus_next(cx) }
                true
            }
            _ => false,
        };
        if !vertical {
            self.goal_x = None;
        }
        if handled {
            cx.stop_propagation();
        }
    }

    fn on_mouse_down(&mut self, event: &MouseDownEvent, window: &mut Window, cx: &mut Context<Self>) {
        self.is_selecting = true;
        self.goal_x = None;
        let at = self.index_for_mouse_position(event.position);
        if event.modifiers.shift { self.select_to(at, window, cx) } else { self.move_to(at, window, cx) }
    }
    fn on_mouse_up(&mut self, _: &MouseUpEvent, _: &mut Window, _: &mut Context<Self>) {
        self.is_selecting = false;
    }
    fn on_mouse_move(&mut self, event: &MouseMoveEvent, window: &mut Window, cx: &mut Context<Self>) {
        if self.is_selecting {
            self.select_to(self.index_for_mouse_position(event.position), window, cx);
        }
    }

    fn offset_from_utf16(&self, offset: usize) -> usize {
        let (mut utf8, mut utf16) = (0, 0);
        for ch in self.content.chars() {
            if utf16 >= offset {
                break;
            }
            utf16 += ch.len_utf16();
            utf8 += ch.len_utf8();
        }
        utf8
    }
    fn offset_to_utf16(&self, offset: usize) -> usize {
        let (mut utf16, mut utf8) = (0, 0);
        for ch in self.content.chars() {
            if utf8 >= offset {
                break;
            }
            utf8 += ch.len_utf8();
            utf16 += ch.len_utf16();
        }
        utf16
    }
    fn range_to_utf16(&self, r: &Range<usize>) -> Range<usize> {
        self.offset_to_utf16(r.start)..self.offset_to_utf16(r.end)
    }
    fn range_from_utf16(&self, r: &Range<usize>) -> Range<usize> {
        self.offset_from_utf16(r.start)..self.offset_from_utf16(r.end)
    }
}

impl EntityInputHandler for TextAreaInput {
    fn text_for_range(&mut self, r: Range<usize>, actual: &mut Option<Range<usize>>, _: &mut Window, _: &mut Context<Self>) -> Option<String> {
        let range = self.range_from_utf16(&r);
        actual.replace(self.range_to_utf16(&range));
        Some(self.content[range].to_string())
    }
    fn selected_text_range(&mut self, _: bool, _: &mut Window, _: &mut Context<Self>) -> Option<UTF16Selection> {
        Some(UTF16Selection { range: self.range_to_utf16(&self.selected_range), reversed: self.selection_reversed })
    }
    fn set_selected_text_range(&mut self, r: Range<usize>, window: &mut Window, cx: &mut Context<Self>) {
        self.selected_range = self.range_from_utf16(&r);
        self.selection_reversed = false;
        window.notify_text_selection_changed(&self.focus_handle);
        cx.notify();
    }
    fn text_length_utf16(&mut self, _: &mut Window, _: &mut Context<Self>) -> Option<usize> {
        Some(self.content.encode_utf16().count())
    }
    fn marked_text_range(&self, _: &mut Window, _: &mut Context<Self>) -> Option<Range<usize>> {
        self.marked_range.as_ref().map(|r| self.range_to_utf16(r))
    }
    fn unmark_text(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        self.marked_range = None;
        window.notify_text_input_changed(&self.focus_handle);
        cx.notify();
    }
    fn replace_text_in_range(&mut self, r: Option<Range<usize>>, new_text: &str, window: &mut Window, cx: &mut Context<Self>) {
        let range = r.as_ref().map(|r| self.range_from_utf16(r)).or(self.marked_range.clone()).unwrap_or(self.selected_range.clone());
        self.content = (self.content[0..range.start].to_owned() + new_text + &self.content[range.end..]).into();
        self.selected_range = range.start + new_text.len()..range.start + new_text.len();
        self.selection_reversed = false;
        self.marked_range.take();
        window.notify_text_input_changed(&self.focus_handle);
        cx.emit(super::field::TextChanged(self.content.clone()));
        cx.notify();
    }
    fn replace_and_mark_text_in_range(
        &mut self,
        r: Option<Range<usize>>,
        new_text: &str,
        new_selected: Option<Range<usize>>,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        let range = r.as_ref().map(|r| self.range_from_utf16(r)).or(self.marked_range.clone()).unwrap_or(self.selected_range.clone());
        self.content = (self.content[0..range.start].to_owned() + new_text + &self.content[range.end..]).into();
        self.marked_range = if new_text.is_empty() { None } else { Some(range.start..range.start + new_text.len()) };
        let start16 = self.offset_to_utf16(range.start);
        let len16 = new_text.encode_utf16().count();
        self.selected_range = new_selected
            .map(|s| self.range_from_utf16(&(start16 + s.start.min(len16)..start16 + s.end.min(len16))))
            .unwrap_or_else(|| range.start + new_text.len()..range.start + new_text.len());
        self.selection_reversed = false;
        window.notify_text_input_changed(&self.focus_handle);
        cx.emit(super::field::TextChanged(self.content.clone()));
        cx.notify();
    }
    fn bounds_for_range(&mut self, r: Range<usize>, bounds: Bounds<Pixels>, _: &mut Window, _: &mut Context<Self>) -> Option<Bounds<Pixels>> {
        let range = self.range_from_utf16(&r);
        let a = self.position_for(range.start)?;
        let b = self.position_for(range.end).unwrap_or(a);
        let origin = point(bounds.left() + a.x, bounds.top() + a.y - self.scroll);
        Some(Bounds::new(origin, size((b.x - a.x).max(px(1.0)), self.line_height)))
    }
    fn character_index_for_point(&mut self, p: Point<Pixels>, _: &mut Window, _: &mut Context<Self>) -> Option<usize> {
        let b = self.last_bounds?;
        Some(self.offset_to_utf16(self.offset_for(point(p.x - b.left(), p.y - b.top() + self.scroll))))
    }
}

/// Shape `text` in the window's text style, wrapped at `width`.
fn shape(text: &SharedString, ink: Hsla, width: Pixels, window: &mut Window) -> Vec<WrappedLine> {
    let style = window.text_style();
    let run = TextRun { len: text.len(), font: style.font(), color: ink, background_color: None, underline: None, strikethrough: None };
    let font_size = style.font_size.to_pixels(window.rem_size());
    window.text_system().shape_text(text.clone(), font_size, &[run], Some(width), None).map(|l| l.into_vec()).unwrap_or_default()
}

/// The wrapped text, caret, selection and scrollbar, painted from a [`TextAreaInput`].
struct AreaElement {
    input: Entity<TextAreaInput>,
}

struct AreaPrepaint {
    lines: Vec<(WrappedLine, usize, Pixels)>,
    quads: Vec<gpui::PaintQuad>,
    caret: Option<gpui::PaintQuad>,
    bar: Option<gpui::PaintQuad>,
    line_height: Pixels,
    scroll: Pixels,
}

impl IntoElement for AreaElement {
    type Element = Self;
    fn into_element(self) -> Self {
        self
    }
}

impl gpui::Element for AreaElement {
    type RequestLayoutState = ();
    type PrepaintState = AreaPrepaint;

    fn id(&self) -> Option<ElementId> {
        None
    }
    fn source_location(&self) -> Option<&'static core::panic::Location<'static>> {
        None
    }

    fn request_layout(&mut self, _: Option<&GlobalElementId>, _: Option<&gpui::InspectorElementId>, window: &mut Window, cx: &mut App) -> (LayoutId, ()) {
        let mut style = Style::default();
        style.size.width = relative(1.).into();
        let input = self.input.clone();
        let line_height = window.line_height();
        let (min, max) = {
            let i = input.read(cx);
            (i.min_rows, i.max_rows)
        };
        let id = window.request_measured_layout(style, move |known: Size<Option<Pixels>>, avail: Size<AvailableSpace>, window, cx| {
            let width = known.width.unwrap_or(match avail.width {
                AvailableSpace::Definite(w) => w,
                _ => px(10000.0),
            });
            let text = input.read(cx).content.clone();
            let rows: usize = if text.is_empty() {
                1
            } else {
                shape(&text, gpui::black(), width, window).iter().map(|l| l.wrap_boundaries().len() + 1).sum()
            };
            size(width, line_height * rows.clamp(min, max) as f32)
        });
        (id, ())
    }

    fn prepaint(
        &mut self,
        _: Option<&GlobalElementId>,
        _: Option<&gpui::InspectorElementId>,
        bounds: Bounds<Pixels>,
        _: &mut (),
        window: &mut Window,
        cx: &mut App,
    ) -> AreaPrepaint {
        let t = Paint::of(cx);
        let line_height = window.line_height();
        let input = self.input.read(cx);
        let focused = input.focus_handle.is_focused(window);
        let empty = input.content.is_empty();
        let (text, ink) = if empty { (input.placeholder.clone(), t.faint()) } else { (input.content.clone(), window.text_style().color) };
        let shaped = shape(&text, ink, bounds.size.width, window);
        let mut lines = Vec::new();
        let (mut start, mut top) = (0usize, px(0.0));
        for l in shaped {
            let h = line_height * (l.wrap_boundaries().len() + 1) as f32;
            let len = l.len();
            lines.push((l, start, top));
            start += len + 1;
            top += h;
        }
        let content_h = top;
        // Scroll the caret into view.
        let pos = |offset: usize| -> Option<Point<Pixels>> {
            for (line, start, top) in &lines {
                if offset >= *start && offset <= start + line.len() {
                    let p = line.position_for_index(offset - start, line_height)?;
                    return Some(point(p.x, p.y + *top));
                }
            }
            None
        };
        let caret_at = if empty { Some(point(px(0.0), px(0.0))) } else { pos(input.cursor_offset()) };
        let mut scroll = input.scroll.min((content_h - bounds.size.height).max(px(0.0))).max(px(0.0));
        if focused && let Some(c) = caret_at {
            if c.y < scroll {
                scroll = c.y;
            } else if c.y + line_height > scroll + bounds.size.height {
                scroll = c.y + line_height - bounds.size.height;
            }
        }
        let origin = point(bounds.left(), bounds.top() - scroll);
        let mut quads = Vec::new();
        let sel = input.selected_range.clone();
        if !empty && !sel.is_empty() {
            let fill_color = color(t.tokens().controls.selection.fill);
            if let (Some(a), Some(b)) = (pos(sel.start), pos(sel.end)) {
                let row = |y: Pixels, x0: Pixels, x1: Pixels| {
                    fill(Bounds::from_corners(point(origin.x + x0, origin.y + y), point(origin.x + x1, origin.y + y + line_height)), fill_color)
                };
                if a.y == b.y {
                    quads.push(row(a.y, a.x, b.x));
                } else {
                    quads.push(row(a.y, a.x, bounds.size.width));
                    let mut y = a.y + line_height;
                    while y < b.y {
                        quads.push(row(y, px(0.0), bounds.size.width));
                        y += line_height;
                    }
                    quads.push(row(b.y, px(0.0), b.x));
                }
            }
        }
        let caret = (focused && sel.is_empty())
            .then_some(caret_at)
            .flatten()
            .map(|c| fill(Bounds::new(point(origin.x + c.x, origin.y + c.y), size(px(1.), line_height)), t.fg()));
        let bar = (content_h > bounds.size.height).then(|| {
            let thick = px(t.tokens().spacing.scrollbar_thickness as f32);
            let h = bounds.size.height * (bounds.size.height / content_h);
            let y = bounds.top() + (bounds.size.height - h) * (scroll / (content_h - bounds.size.height));
            let pad = px(t.tokens().spacing.control_padding_x as f32);
            gpui::quad(
                Bounds::new(point(bounds.right() + pad - thick, y), size(thick, h)),
                t.radius_small(),
                t.faint(),
                px(0.0),
                gpui::transparent_black(),
                gpui::BorderStyle::default(),
            )
        });
        AreaPrepaint { lines, quads, caret, bar, line_height, scroll }
    }

    fn paint(
        &mut self,
        _: Option<&GlobalElementId>,
        _: Option<&gpui::InspectorElementId>,
        bounds: Bounds<Pixels>,
        _: &mut (),
        p: &mut AreaPrepaint,
        window: &mut Window,
        cx: &mut App,
    ) {
        let focus = self.input.read(cx).focus_handle.clone();
        window.handle_input(&focus, ElementInputHandler::new(bounds, self.input.clone()), cx);
        let origin = point(bounds.left(), bounds.top() - p.scroll);
        window.with_content_mask(Some(gpui::ContentMask { bounds }), |window| {
            for q in p.quads.drain(..) {
                window.paint_quad(q);
            }
            for (line, _, top) in &p.lines {
                let _ = line.paint(point(origin.x, origin.y + *top), p.line_height, gpui::TextAlign::Left, None, window, cx);
            }
            if let Some(c) = p.caret.take() {
                window.paint_quad(c);
            }
        });
        if let Some(bar) = p.bar.take() {
            window.paint_quad(bar);
        }
        let (lines, scroll, line_height) = (std::mem::take(&mut p.lines), p.scroll, p.line_height);
        self.input.update(cx, |i, _| {
            i.last_lines = lines;
            i.last_bounds = Some(bounds);
            i.scroll = scroll;
            i.line_height = line_height;
        });
    }
}

impl Render for TextAreaInput {
    fn render(&mut self, _: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        div()
            .flex()
            .w_full()
            .track_focus(&self.focus_handle)
            .cursor(CursorStyle::IBeam)
            .on_key_down(cx.listener(Self::on_key))
            .on_mouse_down(MouseButton::Left, cx.listener(Self::on_mouse_down))
            .on_mouse_up(MouseButton::Left, cx.listener(Self::on_mouse_up))
            .on_mouse_up_out(MouseButton::Left, cx.listener(Self::on_mouse_up))
            .on_mouse_move(cx.listener(Self::on_mouse_move))
            .on_scroll(cx.listener(|this, ev: &gpui::ScrollEvent, _, cx| {
                let dy = ev.delta.pixel_delta(this.line_height).y;
                this.scroll = (this.scroll - dy).max(px(0.0));
                cx.notify();
            }))
            .child(AreaElement { input: cx.entity() })
    }
}

impl Focusable for TextAreaInput {
    fn focus_handle(&self, _: &App) -> FocusHandle {
        self.focus_handle.clone()
    }
}

// ----------------------------------------------------------------------------------- TextArea

/// A multi-line TextField: the same chrome, padding, type and placeholder, wrapping its text.
/// `min_rows` lines tall at least, growing with its text to `max_rows`, then scrolling (a thin
/// `textFaint` scrollbar shows then). The editing is the [`TextAreaInput`] entity you keep.
#[derive(IntoElement)]
pub struct TextArea {
    input: Entity<TextAreaInput>,
    placeholder: Option<SharedString>,
    width: Option<f32>,
    rows: Option<(usize, usize)>,
    cursor: bool,
}

impl TextArea {
    pub fn new(input: &Entity<TextAreaInput>) -> TextArea {
        TextArea { input: input.clone(), placeholder: None, width: None, rows: None, cursor: false }
    }
    pub fn placeholder(mut self, p: impl Into<SharedString>) -> Self {
        self.placeholder = Some(p.into());
        self
    }
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    /// At least `min` lines tall, growing to `max`, then scrolling (default 3 to 6).
    pub fn rows(mut self, min: usize, max: usize) -> Self {
        let min = min.max(1);
        self.rows = Some((min, max.max(min)));
        self
    }
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
}

impl RenderOnce for TextArea {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let c = &tk.controls;
        let bw = c.normal.border.max_width().max(c.hover.border.max_width()).max(c.focus.border.max_width()) as f32;
        let (rest, hot) = input_looks(&t);
        let (placeholder, rows) = (self.placeholder, self.rows);
        self.input.update(cx, |i, _| {
            if let Some(p) = placeholder {
                i.placeholder = p;
            }
            if let Some((min, max)) = rows {
                i.min_rows = min;
                i.max_rows = max;
            }
        });
        let focused = self.input.read(cx).focus_handle.is_focused(window);
        let base = div()
            .id(ElementId::View(self.input.entity_id()))
            .flex()
            .flex_col()
            .flex_shrink_0()
            .px(px(sp.control_padding_x as f32 + bw))
            .py(px(sp.input_padding_y as f32 + bw))
            .font_family(t.family())
            .text_size(px(tk.font.body as f32))
            .line_height(t.line(tk.font.body))
            .text_color(t.fg());
        let base = match self.width {
            Some(w) => base.w(px(w)),
            None => base.w_full(),
        };
        let input = self.input.clone();
        super::chrome(base, &t, &rest, &hot, self.cursor || focused, t.radius())
            .child(div().w_full().child(self.input))
            .on_click(move |_, window, cx| {
                let f = input.read(cx).focus_handle.clone();
                if !f.is_focused(window) {
                    f.focus(window, cx)
                }
            })
    }
}
