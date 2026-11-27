//! TextInput (the editing), TextField, NumberField and Dropdown: the shell's `TextField.qml`,
//! `NumberField.qml` and `Dropdown.qml`. Inputs show the normal border at rest and the
//! hover-cursor look under the pointer and while focused.
//!
//! [`TextInput`]'s editing is adapted from GPUI's `examples/input.rs` (Apache-2.0).

use std::ops::Range;
use std::rc::Rc;

use gpui::prelude::*;
use gpui::{
    App, Bounds, ClipboardItem, Context, CursorStyle, ElementId, ElementInputHandler, Entity, EntityInputHandler, FocusHandle, Focusable,
    GlobalElementId, KeyDownEvent, LayoutId, MouseButton, MouseDownEvent, MouseMoveEvent, MouseUpEvent, PaintQuad, Pixels, Point,
    ShapedLine, SharedString, Style, TextRun, UTF16Selection, UnderlineStyle, Window, anchored, deferred, div, fill, point, px, relative,
    size,
};
use unicode_segmentation::UnicodeSegmentation;

use super::surface::MenuRow;
use super::{Handler, Look, border, chrome, focus_handle, id_for, keyed_state, label, label_elided, tab_keys};
use crate::{Paint, color};

fn rest_and_hot(t: &Paint) -> (Look, Look) {
    let c = &t.tokens().controls;
    (Look::new(c.normal.fill, c.normal.border.clone()), Look::new(c.hover.fill, c.hover.border.clone()))
}

fn field_label(t: &Paint, text: SharedString, bold: bool) -> gpui::Div {
    label(t, text, t.tokens().font.body_small, bold, t.dim())
}

// ------------------------------------------------------------------------------- TextInput

/// The editing behind a [`TextField`]: one line of text, a caret and a selection, IME, the
/// clipboard, the mouse. Keep an `Entity<TextInput>` in your view (`TextInput::new(cx, "")`) and
/// hand it to a `TextField`.
pub struct TextInput {
    focus_handle: FocusHandle,
    content: SharedString,
    placeholder: SharedString,
    secure: bool,
    selected_range: Range<usize>,
    selection_reversed: bool,
    marked_range: Option<Range<usize>>,
    last_layout: Option<ShapedLine>,
    last_bounds: Option<Bounds<Pixels>>,
    is_selecting: bool,
    caret: bool,
    on_submit: Option<Rc<dyn Fn(&SharedString, &mut Window, &mut App)>>,
}

/// Emitted by a [`TextInput`] whenever its text changes (`cx.subscribe(&input, ..)`).
#[derive(Clone, Debug)]
pub struct TextChanged(pub SharedString);

impl gpui::EventEmitter<TextChanged> for TextInput {}

impl TextInput {
    /// A new input entity holding `text`.
    pub fn new(cx: &mut App, text: impl Into<SharedString>) -> Entity<TextInput> {
        let text = text.into();
        cx.new(|cx| TextInput {
            focus_handle: cx.focus_handle().tab_stop(true),
            selected_range: text.len()..text.len(),
            content: text,
            placeholder: SharedString::default(),
            secure: false,
            selection_reversed: false,
            marked_range: None,
            last_layout: None,
            last_bounds: None,
            is_selecting: false,
            caret: true,
            on_submit: None,
        })
    }
    pub fn text(&self) -> &SharedString {
        &self.content
    }
    pub fn set_text(&mut self, text: impl Into<SharedString>, cx: &mut Context<Self>) {
        self.content = text.into();
        self.selected_range = self.content.len()..self.content.len();
        self.marked_range = None;
        cx.emit(TextChanged(self.content.clone()));
        cx.notify();
    }
    pub fn set_placeholder(&mut self, p: impl Into<SharedString>) {
        self.placeholder = p.into();
    }
    /// Show bullets instead of the text (passwords).
    pub fn set_secure(&mut self, on: bool) {
        self.secure = on;
    }
    /// Draw the caret while focused (on by default; SearchList's filter line has none).
    pub fn set_caret(&mut self, on: bool) {
        self.caret = on;
    }
    /// Return in the field.
    pub fn on_submit(&mut self, f: impl Fn(&SharedString, &mut Window, &mut App) + 'static) {
        self.on_submit = Some(Rc::new(f));
    }

    fn shown(&self) -> SharedString {
        if self.secure { "\u{2022}".repeat(self.content.chars().count()).into() } else { self.content.clone() }
    }
    /// A byte offset in the content as one in what's shown (bullets are 3 bytes each).
    fn shown_offset(&self, offset: usize) -> usize {
        if self.secure { self.content[..offset].chars().count() * '\u{2022}'.len_utf8() } else { offset }
    }
    fn content_offset(&self, shown: usize) -> usize {
        if !self.secure {
            return shown;
        }
        let n = shown / '\u{2022}'.len_utf8();
        self.content.char_indices().nth(n).map(|(i, _)| i).unwrap_or(self.content.len())
    }

    fn on_key(&mut self, ev: &KeyDownEvent, window: &mut Window, cx: &mut Context<Self>) {
        let k = &ev.keystroke;
        let m = &k.modifiers;
        let word = m.control || m.alt;
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
            "home" => {
                if m.shift { self.select_to(0, window, cx) } else { self.move_to(0, window, cx) }
                true
            }
            "end" => {
                let end = self.content.len();
                if m.shift { self.select_to(end, window, cx) } else { self.move_to(end, window, cx) }
                true
            }
            "a" if m.control => {
                self.move_to(0, window, cx);
                self.select_to(self.content.len(), window, cx);
                true
            }
            "c" if m.control => {
                if !self.selected_range.is_empty() && !self.secure {
                    cx.write_to_clipboard(ClipboardItem::new_string(self.content[self.selected_range.clone()].to_string()));
                }
                true
            }
            "x" if m.control => {
                if !self.selected_range.is_empty() && !self.secure {
                    cx.write_to_clipboard(ClipboardItem::new_string(self.content[self.selected_range.clone()].to_string()));
                    self.replace_text_in_range(None, "", window, cx);
                }
                true
            }
            "v" if m.control => {
                if let Some(text) = cx.read_from_clipboard().and_then(|item| item.text()) {
                    self.replace_text_in_range(None, &text.replace('\n', " "), window, cx);
                }
                true
            }
            "enter" | "kp_enter" => {
                if let Some(f) = self.on_submit.clone() {
                    let text = self.content.clone();
                    window.defer(cx, move |window, cx| f(&text, window, cx));
                }
                true
            }
            "tab" => {
                if m.shift { window.focus_prev(cx) } else { window.focus_next(cx) }
                true
            }
            _ => false,
        };
        if handled {
            cx.stop_propagation();
        }
    }

    fn on_mouse_down(&mut self, event: &MouseDownEvent, window: &mut Window, cx: &mut Context<Self>) {
        self.is_selecting = true;
        if event.modifiers.shift {
            self.select_to(self.index_for_mouse_position(event.position), window, cx);
        } else {
            self.move_to(self.index_for_mouse_position(event.position), window, cx)
        }
    }

    fn on_mouse_up(&mut self, _: &MouseUpEvent, _window: &mut Window, _: &mut Context<Self>) {
        self.is_selecting = false;
    }

    fn on_mouse_move(&mut self, event: &MouseMoveEvent, window: &mut Window, cx: &mut Context<Self>) {
        if self.is_selecting {
            self.select_to(self.index_for_mouse_position(event.position), window, cx);
        }
    }

    fn move_to(&mut self, offset: usize, window: &Window, cx: &mut Context<Self>) {
        self.selected_range = offset..offset;
        self.selection_reversed = false;
        window.notify_text_selection_changed(&self.focus_handle);
        cx.notify()
    }

    fn cursor_offset(&self) -> usize {
        if self.selection_reversed { self.selected_range.start } else { self.selected_range.end }
    }

    fn index_for_mouse_position(&self, position: Point<Pixels>) -> usize {
        if self.content.is_empty() {
            return 0;
        }
        let (Some(bounds), Some(line)) = (self.last_bounds.as_ref(), self.last_layout.as_ref()) else { return 0 };
        if position.y < bounds.top() {
            return 0;
        }
        if position.y > bounds.bottom() {
            return self.content.len();
        }
        self.content_offset(line.closest_index_for_x(position.x - bounds.left()))
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

    fn offset_from_utf16(&self, offset: usize) -> usize {
        let mut utf8_offset = 0;
        let mut utf16_count = 0;
        for ch in self.content.chars() {
            if utf16_count >= offset {
                break;
            }
            utf16_count += ch.len_utf16();
            utf8_offset += ch.len_utf8();
        }
        utf8_offset
    }

    fn offset_to_utf16(&self, offset: usize) -> usize {
        let mut utf16_offset = 0;
        let mut utf8_count = 0;
        for ch in self.content.chars() {
            if utf8_count >= offset {
                break;
            }
            utf8_count += ch.len_utf8();
            utf16_offset += ch.len_utf16();
        }
        utf16_offset
    }

    fn range_to_utf16(&self, range: &Range<usize>) -> Range<usize> {
        self.offset_to_utf16(range.start)..self.offset_to_utf16(range.end)
    }

    fn range_from_utf16(&self, range_utf16: &Range<usize>) -> Range<usize> {
        self.offset_from_utf16(range_utf16.start)..self.offset_from_utf16(range_utf16.end)
    }

    fn previous_boundary(&self, offset: usize) -> usize {
        self.content.grapheme_indices(true).rev().find_map(|(idx, _)| (idx < offset).then_some(idx)).unwrap_or(0)
    }

    fn next_boundary(&self, offset: usize) -> usize {
        self.content.grapheme_indices(true).find_map(|(idx, _)| (idx > offset).then_some(idx)).unwrap_or(self.content.len())
    }

    fn previous_word(&self, offset: usize) -> usize {
        self.content.unicode_word_indices().rev().find_map(|(idx, _)| (idx < offset).then_some(idx)).unwrap_or(0)
    }

    fn next_word(&self, offset: usize) -> usize {
        self.content
            .unicode_word_indices()
            .find_map(|(idx, w)| (idx + w.len() > offset).then_some(idx + w.len()))
            .unwrap_or(self.content.len())
    }
}

impl EntityInputHandler for TextInput {
    fn text_for_range(&mut self, range_utf16: Range<usize>, actual_range: &mut Option<Range<usize>>, _: &mut Window, _: &mut Context<Self>) -> Option<String> {
        let range = self.range_from_utf16(&range_utf16);
        actual_range.replace(self.range_to_utf16(&range));
        Some(self.content[range].to_string())
    }

    fn selected_text_range(&mut self, _: bool, _: &mut Window, _: &mut Context<Self>) -> Option<UTF16Selection> {
        Some(UTF16Selection { range: self.range_to_utf16(&self.selected_range), reversed: self.selection_reversed })
    }

    fn set_selected_text_range(&mut self, range_utf16: Range<usize>, window: &mut Window, cx: &mut Context<Self>) {
        self.selected_range = self.range_from_utf16(&range_utf16);
        self.selection_reversed = false;
        window.notify_text_selection_changed(&self.focus_handle);
        cx.notify();
    }

    fn text_length_utf16(&mut self, _: &mut Window, _: &mut Context<Self>) -> Option<usize> {
        Some(self.content.encode_utf16().count())
    }

    fn marked_text_range(&self, _: &mut Window, _: &mut Context<Self>) -> Option<Range<usize>> {
        self.marked_range.as_ref().map(|range| self.range_to_utf16(range))
    }

    fn unmark_text(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        self.marked_range = None;
        window.notify_text_input_changed(&self.focus_handle);
        cx.notify();
    }

    fn replace_text_in_range(&mut self, range_utf16: Option<Range<usize>>, new_text: &str, window: &mut Window, cx: &mut Context<Self>) {
        let range = range_utf16
            .as_ref()
            .map(|r| self.range_from_utf16(r))
            .or(self.marked_range.clone())
            .unwrap_or(self.selected_range.clone());
        self.content = (self.content[0..range.start].to_owned() + new_text + &self.content[range.end..]).into();
        self.selected_range = range.start + new_text.len()..range.start + new_text.len();
        self.selection_reversed = false;
        self.marked_range.take();
        window.notify_text_input_changed(&self.focus_handle);
        cx.emit(TextChanged(self.content.clone()));
        cx.notify();
    }

    fn replace_and_mark_text_in_range(
        &mut self,
        range_utf16: Option<Range<usize>>,
        new_text: &str,
        new_selected_range_utf16: Option<Range<usize>>,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        let range = range_utf16
            .as_ref()
            .map(|r| self.range_from_utf16(r))
            .or(self.marked_range.clone())
            .unwrap_or(self.selected_range.clone());
        self.content = (self.content[0..range.start].to_owned() + new_text + &self.content[range.end..]).into();
        self.marked_range = if new_text.is_empty() { None } else { Some(range.start..range.start + new_text.len()) };
        let start16 = self.offset_to_utf16(range.start);
        let len16 = new_text.encode_utf16().count();
        self.selected_range = new_selected_range_utf16
            .map(|s| self.range_from_utf16(&(start16 + s.start.min(len16)..start16 + s.end.min(len16))))
            .unwrap_or_else(|| range.start + new_text.len()..range.start + new_text.len());
        self.selection_reversed = false;
        window.notify_text_input_changed(&self.focus_handle);
        cx.emit(TextChanged(self.content.clone()));
        cx.notify();
    }

    fn bounds_for_range(&mut self, range_utf16: Range<usize>, bounds: Bounds<Pixels>, _: &mut Window, _: &mut Context<Self>) -> Option<Bounds<Pixels>> {
        let line = self.last_layout.as_ref()?;
        let range = self.range_from_utf16(&range_utf16);
        Some(Bounds::from_corners(
            point(bounds.left() + line.x_for_index(self.shown_offset(range.start)), bounds.top()),
            point(bounds.left() + line.x_for_index(self.shown_offset(range.end)), bounds.bottom()),
        ))
    }

    fn character_index_for_point(&mut self, p: Point<Pixels>, _: &mut Window, _: &mut Context<Self>) -> Option<usize> {
        let local = self.last_bounds?.localize(&p)?;
        let line = self.last_layout.as_ref()?;
        Some(self.offset_to_utf16(self.content_offset(line.closest_index_for_x(local.x))))
    }
}

/// The line of text, caret and selection, painted from the input's state.
struct TextElement {
    input: Entity<TextInput>,
}

struct PrepaintState {
    line: Option<ShapedLine>,
    cursor: Option<PaintQuad>,
    selection: Option<PaintQuad>,
}

impl IntoElement for TextElement {
    type Element = Self;
    fn into_element(self) -> Self::Element {
        self
    }
}

impl gpui::Element for TextElement {
    type RequestLayoutState = ();
    type PrepaintState = PrepaintState;

    fn id(&self) -> Option<ElementId> {
        None
    }
    fn source_location(&self) -> Option<&'static core::panic::Location<'static>> {
        None
    }

    fn request_layout(&mut self, _: Option<&GlobalElementId>, _: Option<&gpui::InspectorElementId>, window: &mut Window, cx: &mut App) -> (LayoutId, ()) {
        let mut style = Style::default();
        style.size.width = relative(1.).into();
        style.size.height = window.line_height().into();
        (window.request_layout(style, [], cx), ())
    }

    fn prepaint(
        &mut self,
        _: Option<&GlobalElementId>,
        _: Option<&gpui::InspectorElementId>,
        bounds: Bounds<Pixels>,
        _: &mut (),
        window: &mut Window,
        cx: &mut App,
    ) -> PrepaintState {
        let t = Paint::of(cx);
        let input = self.input.read(cx);
        let content = input.shown();
        let selected = input.shown_offset(input.selected_range.start)..input.shown_offset(input.selected_range.end);
        let cursor = input.shown_offset(input.cursor_offset());
        let style = window.text_style();
        let (display, ink) = if content.is_empty() { (input.placeholder.clone(), t.faint()) } else { (content, style.color) };
        let run = TextRun { len: display.len(), font: style.font(), color: ink, background_color: None, underline: None, strikethrough: None };
        let runs = match input.marked_range.as_ref() {
            Some(m) if !input.content.is_empty() => {
                let m = input.shown_offset(m.start)..input.shown_offset(m.end);
                vec![
                    TextRun { len: m.start, ..run.clone() },
                    TextRun { len: m.end - m.start, underline: Some(UnderlineStyle { color: Some(run.color), thickness: px(1.0), wavy: false }), ..run.clone() },
                    TextRun { len: display.len() - m.end, ..run },
                ]
                .into_iter()
                .filter(|r| r.len > 0)
                .collect()
            }
            _ => vec![run],
        };
        let font_size = style.font_size.to_pixels(window.rem_size());
        let line = window.text_system().shape_line(display, font_size, &runs, None);
        let (selection, cursor) = if selected.is_empty() || input.content.is_empty() {
            let x = if input.content.is_empty() { px(0.) } else { line.x_for_index(cursor) };
            (None, Some(fill(Bounds::new(point(bounds.left() + x, bounds.top()), size(px(1.), bounds.size.height)), t.fg())))
        } else {
            let sel = color(t.tokens().controls.selection.fill);
            (
                Some(fill(
                    Bounds::from_corners(
                        point(bounds.left() + line.x_for_index(selected.start), bounds.top()),
                        point(bounds.left() + line.x_for_index(selected.end), bounds.bottom()),
                    ),
                    sel,
                )),
                None,
            )
        };
        PrepaintState { line: Some(line), cursor, selection }
    }

    fn paint(
        &mut self,
        _: Option<&GlobalElementId>,
        _: Option<&gpui::InspectorElementId>,
        bounds: Bounds<Pixels>,
        _: &mut (),
        prepaint: &mut PrepaintState,
        window: &mut Window,
        cx: &mut App,
    ) {
        let focus = self.input.read(cx).focus_handle.clone();
        window.handle_input(&focus, ElementInputHandler::new(bounds, self.input.clone()), cx);
        if let Some(selection) = prepaint.selection.take() {
            window.paint_quad(selection)
        }
        let line = prepaint.line.take().unwrap();
        let _ = line.paint(bounds.origin, window.line_height(), gpui::TextAlign::Left, None, window, cx);
        if focus.is_focused(window)
            && self.input.read(cx).caret
            && let Some(cursor) = prepaint.cursor.take()
        {
            window.paint_quad(cursor);
        }
        self.input.update(cx, |input, _| {
            input.last_layout = Some(line);
            input.last_bounds = Some(bounds);
        });
    }
}

impl Render for TextInput {
    fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
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
            .child(TextElement { input: cx.entity() })
    }
}

impl Focusable for TextInput {
    fn focus_handle(&self, _: &App) -> FocusHandle {
        self.focus_handle.clone()
    }
}

// ------------------------------------------------------------------------------- TextField

/// A single-line text input: the editing is the [`TextInput`] entity you keep; the chrome is
/// Omarchy's. Placeholder in `textFaint`, selection in the selection fill.
#[derive(IntoElement)]
pub struct TextField {
    input: Entity<TextInput>,
    placeholder: Option<SharedString>,
    width: Option<f32>,
    cursor: bool,
    secure: bool,
}

impl TextField {
    pub fn new(input: &Entity<TextInput>) -> TextField {
        TextField { input: input.clone(), placeholder: None, width: None, cursor: false, secure: false }
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
    /// Hide what's typed (passwords).
    pub fn secure(mut self) -> Self {
        self.secure = true;
        self
    }
}

impl RenderOnce for TextField {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let (rest, hot) = rest_and_hot(&t);
        let c = &tk.controls;
        let bw = c.normal.border.max_width().max(c.hover.border.max_width()).max(c.focus.border.max_width()) as f32;
        let (placeholder, secure) = (self.placeholder, self.secure);
        self.input.update(cx, |i, _| {
            if let Some(p) = placeholder {
                i.placeholder = p;
            }
            if secure {
                i.secure = true;
            }
        });
        let focused = self.input.read(cx).focus_handle.is_focused(window);
        let base = div()
            .id(ElementId::View(self.input.entity_id()))
            .flex()
            .flex_row()
            .items_center()
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
        chrome(base, &t, &rest, &hot, self.cursor || focused, t.radius())
            .child(div().flex_1().min_w_0().overflow_hidden().child(self.input))
            .on_click(move |_, window, cx| {
                let f = input.read(cx).focus_handle.clone();
                f.focus(window, cx)
            })
    }
}

// ------------------------------------------------------------------------------- NumberField

/// An integer stepper with a label above: [−] value [+]. Up and Down (or + and −) step it when
/// focused.
#[derive(IntoElement)]
pub struct NumberField {
    id: ElementId,
    label: SharedString,
    value: i64,
    from: i64,
    to: i64,
    step: i64,
    cursor: bool,
    on_change: Option<Handler<i64>>,
}

impl NumberField {
    pub fn new(label: impl Into<SharedString>, value: i64) -> NumberField {
        let label = label.into();
        NumberField { id: id_for("number", &label), label, value, from: 0, to: 100, step: 1, cursor: false, on_change: None }
    }
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
        self
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
    /// Called with the new value.
    pub fn on_change(mut self, f: impl Fn(&i64, &mut Window, &mut App) + 'static) -> Self {
        self.on_change = Some(Rc::new(f));
        self
    }
}

impl RenderOnce for NumberField {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let c = &tk.controls;
        let (rest, hot) = rest_and_hot(&t);
        let focus = focus_handle(&self.id, window, cx);
        let h = (sp.control_height as f32).max((tk.font.body + sp.control_padding_y * 2) as f32);
        let inset = c.normal.border.widths[0] as f32;
        let side = h - inset * 2.0;
        let (value, from, to, step) = (self.value, self.from, self.to, self.step);
        let hover = color(c.hover.fill);
        let pressed = color(c.pressed.fill);
        let step_button = |glyph: &'static str, target: i64, enabled: bool| {
            let mut b = div()
                .id(glyph)
                .flex()
                .flex_shrink_0()
                .items_center()
                .justify_center()
                .size(px(side))
                .rounded(t.radius_small())
                .when(enabled, |b| b.hover(move |s| s.bg(hover)).active(move |s| s.bg(pressed)))
                .opacity(if enabled { 1.0 } else { 0.4 })
                .child(label(&t, glyph, tk.font.body, false, t.fg()));
            if let (Some(f), true) = (self.on_change.clone(), enabled) {
                b = b.on_click(move |_, window, cx| f(&target, window, cx));
            }
            b
        };
        let down = step_button("\u{2212}", (value - step).max(from), value > from);
        let up = step_button("+", (value + step).min(to), value < to);
        let base = div()
            .id(self.id.clone())
            .flex()
            .flex_row()
            .flex_shrink_0()
            .w(px(sp.number_field_width as f32))
            .h(px(h))
            .items_center()
            .justify_between()
            .px(px(inset));
        let keys = self.on_change.clone();
        let field = chrome(base, &t, &rest, &hot, self.cursor || focus.is_focused(window), t.radius())
            .child(down)
            .child(label(&t, value.to_string(), tk.font.body, false, t.fg()))
            .child(up)
            .track_focus(&focus)
            .on_key_down(move |ev: &KeyDownEvent, window, cx| {
                if tab_keys(ev, window, cx) {
                    cx.stop_propagation();
                    return;
                }
                let next = match ev.keystroke.key.as_str() {
                    "up" | "right" | "+" | "=" | "k" | "l" => (value + step).min(to),
                    "down" | "left" | "-" | "j" | "h" => (value - step).max(from),
                    "home" => from,
                    "end" => to,
                    _ => return,
                };
                if next != value
                    && let Some(f) = &keys
                {
                    f(&next, window, cx);
                }
                cx.stop_propagation();
            });
        let mut out = div().flex().flex_col().flex_shrink_0().gap(px(sp.md as f32));
        if !self.label.is_empty() {
            out = out.child(field_label(&t, self.label, false));
        }
        out.child(field)
    }
}

// ------------------------------------------------------------------------------- Dropdown

#[derive(Default)]
struct DropState {
    open: bool,
    highlight: Option<usize>,
}

/// A labeled choice of one option; the list opens below the trigger on the popups surface.
/// Return, Space, Down or j opens it; in it Down/j and Up/k walk, Return chooses, Escape closes.
#[derive(IntoElement)]
pub struct Dropdown {
    id: ElementId,
    label: SharedString,
    options: Vec<(SharedString, SharedString)>,
    value: SharedString,
    open: Option<bool>,
    cursor: bool,
    width: Option<f32>,
    on_choose: Option<Handler<SharedString>>,
}

impl Dropdown {
    /// `options` are (value, label) pairs.
    pub fn new<V: Into<SharedString>, L: Into<SharedString>>(
        id: impl Into<ElementId>,
        options: impl IntoIterator<Item = (V, L)>,
        value: impl Into<SharedString>,
    ) -> Dropdown {
        Dropdown {
            id: id.into(),
            label: SharedString::default(),
            options: options.into_iter().map(|(v, l)| (v.into(), l.into())).collect(),
            value: value.into(),
            open: None,
            cursor: false,
            width: None,
            on_choose: None,
        }
    }
    pub fn label(mut self, label: impl Into<SharedString>) -> Self {
        self.label = label.into();
        self
    }
    /// Force the list open or closed (stills, or an app that owns it); by default the dropdown
    /// opens and closes itself.
    pub fn open(mut self, open: bool) -> Self {
        self.open = Some(open);
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
    /// Called with the chosen value.
    pub fn on_choose(mut self, f: impl Fn(&SharedString, &mut Window, &mut App) + 'static) -> Self {
        self.on_choose = Some(Rc::new(f));
        self
    }
}

impl RenderOnce for Dropdown {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let (rest, hot) = rest_and_hot(&t);
        let focus = focus_handle(&self.id, window, cx);
        let state = keyed_state(&self.id, "dropdown", window, cx, DropState::default);
        let open = self.open.unwrap_or(state.read(cx).open);
        let width = self.width.unwrap_or(sp.dropdown_width as f32);
        let bw = tk.controls.normal.border.max_width() as f32;
        let current = self.options.iter().find(|(v, _)| *v == self.value).map(|(_, l)| l.clone()).unwrap_or_default();
        let selected = self.options.iter().position(|(v, _)| *v == self.value);
        let values: Rc<Vec<SharedString>> = Rc::new(self.options.iter().map(|(v, _)| v.clone()).collect());

        let words = label_elided(&t, current, tk.font.body, false, t.fg()).flex_1();
        let chevron = label(&t, "\u{f0140}", tk.font.body, false, t.dim());
        let base = div()
            .id(self.id.clone())
            .flex()
            .flex_row()
            .flex_shrink_0()
            .w(px(width))
            .h(px(sp.control_height as f32))
            .items_center()
            .gap(px(sp.md as f32))
            .pl(px(sp.control_padding_x as f32 + bw))
            .pr(px(sp.control_gap as f32 + bw));
        let st = state.clone();
        let st_keys = state.clone();
        let choose = self.on_choose.clone();
        let vals = values.clone();
        let trigger = chrome(base, &t, &rest, &hot, self.cursor || open || focus.is_focused(window), t.radius())
            .child(words)
            .child(chevron)
            .track_focus(&focus)
            .on_click(move |_, _, cx| {
                st.update(cx, |s, cx| {
                    s.open = !s.open;
                    s.highlight = selected;
                    cx.notify();
                })
            })
            .on_key_down(move |ev: &KeyDownEvent, window, cx| {
                if tab_keys(ev, window, cx) {
                    cx.stop_propagation();
                    return;
                }
                let key = ev.keystroke.key.as_str();
                let (is_open, at) = {
                    let s = st_keys.read(cx);
                    (s.open, s.highlight.or(selected).unwrap_or(0))
                };
                let n = vals.len();
                let set = |open: bool, hl: Option<usize>, cx: &mut App| {
                    st_keys.update(cx, |s, cx| {
                        s.open = open;
                        s.highlight = hl;
                        cx.notify();
                    })
                };
                match (is_open, key) {
                    (false, "enter" | "space" | "down" | "j") => set(true, selected.or(Some(0)), cx),
                    (true, "escape") => set(false, None, cx),
                    (true, "down" | "j") => set(true, Some((at + 1).min(n.saturating_sub(1))), cx),
                    (true, "up" | "k") => set(true, Some(at.saturating_sub(1)), cx),
                    (true, "enter" | "space") => {
                        set(false, None, cx);
                        if let (Some(f), Some(v)) = (&choose, vals.get(at)) {
                            f(v, window, cx);
                        }
                    }
                    _ => return,
                }
                cx.stop_propagation();
            });

        let mut out = div().relative().flex().flex_col().flex_shrink_0().gap(px(sp.label_gap as f32));
        if !self.label.is_empty() {
            out = out.child(label(&t, self.label, tk.font.caption, true, t.dim()));
        }
        out = out.child(trigger);
        if open {
            let pop = &tk.surfaces.popups;
            let line = tk.controls.normal.border.max_width().max(1.0) as f32;
            let hair = sp.hairline as f32;
            let highlight = state.read(cx).highlight;
            let mut list = div()
                .id(ElementId::NamedChild(std::sync::Arc::new(self.id.clone()), "list".into()))
                .occlude()
                .flex()
                .flex_col()
                .w(px(width))
                .gap(px(sp.label_gap as f32))
                .p(px(hair))
                .bg(color(pop.background))
                .rounded(t.radius());
            list = border(list, &pop.border.with_width(line as f64));
            for (i, (value, text)) in self.options.into_iter().enumerate() {
                let mut row = MenuRow::new(text).id(ElementId::NamedChild(std::sync::Arc::new(self.id.clone()), value.to_string().into()));
                row = row.checked(value == self.value).highlighted(highlight == Some(i));
                let st = state.clone();
                let f = self.on_choose.clone();
                row = row.on_click(move |_, window, cx| {
                    st.update(cx, |s, cx| {
                        s.open = false;
                        cx.notify();
                    });
                    if let Some(f) = &f {
                        f(&value, window, cx);
                    }
                });
                list = list.child(row);
            }
            let st = state.clone();
            let list = list.on_mouse_down_out(move |_, _, cx| {
                st.update(cx, |s, cx| {
                    s.open = false;
                    cx.notify();
                })
            });
            let bottom = px(sp.xxs as f32);
            out = out.child(
                div().absolute().left_0().bottom_0().child(deferred(anchored().offset(point(px(0.), bottom)).child(list)).with_priority(1)),
            );
        }
        out
    }
}
