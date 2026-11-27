//! Button, IconButton and ButtonGroup: the shell's `Button.qml`, `PanelActionButton.qml` and
//! `ButtonGroup.qml`.

use std::rc::Rc;

use gpui::prelude::*;
use gpui::{App, ClickEvent, ElementId, KeyDownEvent, SharedString, Window, div, px};
use omarchy_theme::{BorderSpec, Rgba};

use super::{Handler, Look, chrome, focus_handle, id_for, is_activate, keyed_state, label, reserve, tab_keys};
use crate::{Paint, color};

/// A glyph (a Nerd Font codepoint) in the Omarchy font, one line, unboxed.
pub(crate) fn glyph(t: &Paint, s: impl Into<SharedString>, size: i64, ink: gpui::Hsla) -> gpui::Div {
    label(t, s, size, false, ink)
}

/// The button. Borderless at rest unless `bordered`; the hover-cursor border under the pointer,
/// keyboard focus or `cursor`; `selected` fills and bolds the label. Return or Space presses it
/// when focused.
#[derive(IntoElement)]
pub struct Button {
    id: ElementId,
    label: SharedString,
    icon: Option<SharedString>,
    bordered: bool,
    selected: bool,
    cursor: bool,
    focusable: bool,
    on_click: Option<Handler<ClickEvent>>,
}

impl Button {
    pub fn new(label: impl Into<SharedString>) -> Button {
        let label = label.into();
        Button { id: id_for("button", &label), label, icon: None, bordered: false, selected: false, cursor: false, focusable: true, on_click: None }
    }
    /// Off inside a control that is one tab stop itself (ButtonGroup).
    pub(crate) fn focusable(mut self, on: bool) -> Self {
        self.focusable = on;
        self
    }
    /// The element id (focus and state are kept under it); the label by default.
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
        self
    }
    /// A glyph before the label (a Nerd Font codepoint, as the shell uses).
    pub fn icon(mut self, glyph: impl Into<SharedString>) -> Self {
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
    pub fn on_click(mut self, f: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static) -> Self {
        self.on_click = Some(Rc::new(f));
        self
    }
}

/// Click and keyboard activation for a focusable control.
pub(crate) fn pressable(
    e: gpui::Stateful<gpui::Div>,
    focus: &gpui::FocusHandle,
    on_click: Option<Handler<ClickEvent>>,
) -> gpui::Stateful<gpui::Div> {
    let keys = on_click.clone();
    let e = e.track_focus(focus).on_key_down(move |ev: &KeyDownEvent, window, cx| {
        if tab_keys(ev, window, cx) {
            cx.stop_propagation();
        } else if is_activate(ev) {
            if let Some(f) = &keys {
                f(&ClickEvent::default(), window, cx);
            }
            cx.stop_propagation();
        }
    });
    match on_click {
        Some(f) => e.on_click(move |ev, window, cx| f(ev, window, cx)),
        None => e,
    }
}

impl RenderOnce for Button {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let c = &tk.controls;
        let sp = &tk.spacing;
        let focus = self.focusable.then(|| focus_handle(&self.id, window, cx));
        let hot_now = self.cursor || focus.as_ref().is_some_and(|f| f.is_focused(window));
        let rest_border = if self.selected && !c.selected.border.is_none() {
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
        let mut content_w = 0.0f32;
        if let Some(icon) = &self.icon {
            content_w += super::lists::glyph_width(&t, window, icon, tk.font.icon);
            if !self.label.is_empty() {
                content_w += sp.control_gap as f32;
            }
        }
        if !self.label.is_empty() {
            content_w += super::navigation::advance(window, &t, &self.label, tk.font.body, self.selected);
        }
        let min_w = content_w.ceil() + (sp.control_padding_x * 2) as f32 + rl + rr;
        let base = div()
            .id(self.id)
            .flex()
            .flex_row()
            .flex_shrink_0()
            .items_center()
            .justify_center()
            .min_w(px(min_w))
            .gap(px(sp.control_gap as f32))
            .pt(px(sp.control_padding_y as f32 + rt))
            .pb(px(sp.control_padding_y as f32 + rb))
            .pl(px(sp.control_padding_x as f32 + rl))
            .pr(px(sp.control_padding_x as f32 + rr));
        let mut e = chrome(base, &t, &rest, &hot, hot_now, t.radius());
        if let Some(icon) = self.icon {
            let icon_el = super::lists::glyph(&t, window, icon, tk.font.icon, ink);
            if !self.label.is_empty() {
                // Keep icon + label buttons the same height as text-only siblings.
                e = e.child(div().flex().flex_shrink_0().items_center().h(t.line(tk.font.body)).child(icon_el));
            } else {
                e = e.child(icon_el);
            }
        }
        if !self.label.is_empty() {
            e = e.child(label(&t, self.label, tk.font.body, self.selected, ink));
        }
        match (focus, self.on_click) {
            (Some(focus), on_click) => pressable(e, &focus, on_click),
            (None, Some(f)) => e.on_click(move |ev, window, cx| f(ev, window, cx)),
            (None, None) => e,
        }
    }
}

/// A square icon-only button (the shell's PanelActionButton): odd-sized so the glyph centers on
/// a pixel.
#[derive(IntoElement)]
pub struct IconButton {
    id: ElementId,
    glyph: SharedString,
    bordered: bool,
    cursor: bool,
    on_click: Option<Handler<ClickEvent>>,
}

impl IconButton {
    pub fn new(glyph: impl Into<SharedString>) -> IconButton {
        let glyph = glyph.into();
        IconButton { id: id_for("icon-button", &glyph), glyph, bordered: false, cursor: false, on_click: None }
    }
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
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
    pub fn on_click(mut self, f: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static) -> Self {
        self.on_click = Some(Rc::new(f));
        self
    }
}

/// The square side of an [`IconButton`] in points: whole and odd so a glyph centers cleanly.
pub(crate) fn icon_button_size(t: &Paint) -> f32 {
    let tk = t.tokens();
    ((t.space(22.0)).max((tk.font.icon + tk.spacing.sm * 2) as f32).round() as i64 | 1) as f32
}

impl RenderOnce for IconButton {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let c = &tk.controls;
        let icon = tk.font.icon;
        let size = icon_button_size(&t);
        let focus = focus_handle(&self.id, window, cx);
        let hot_now = self.cursor || focus.is_focused(window);
        let rest = Look::new(Rgba::TRANSPARENT, if self.bordered { c.normal.border.clone() } else { BorderSpec::none() });
        // PanelActionButton shows the hover border only when bordered.
        let hot = Look::new(c.hover.fill, if self.bordered { c.hover.border.clone() } else { BorderSpec::none() });
        let base = div().id(self.id).flex().flex_shrink_0().items_center().justify_center().size(px(size));
        let mark = super::navigation::optical_xy(window, cx, &t, &self.glyph, icon, t.fg(), None, size, size);
        let e = chrome(base, &t, &rest, &hot, hot_now, t.radius_small()).child(mark);
        pressable(e, &focus, self.on_click)
    }
}

/// A row of bordered buttons, `md` apart, one selected: a segmented choice. Left and Right move
/// the choice when one of its buttons has focus.
#[derive(IntoElement)]
pub struct ButtonGroup {
    id: ElementId,
    options: Vec<(SharedString, SharedString)>,
    value: SharedString,
    cursor: Option<usize>,
    on_choose: Option<Handler<SharedString>>,
}

impl ButtonGroup {
    /// `options` are (value, label) pairs.
    pub fn new<V: Into<SharedString>, L: Into<SharedString>>(options: impl IntoIterator<Item = (V, L)>, value: impl Into<SharedString>) -> ButtonGroup {
        let options: Vec<(SharedString, SharedString)> = options.into_iter().map(|(v, l)| (v.into(), l.into())).collect();
        let key = options.iter().map(|(v, _)| v.as_ref()).collect::<Vec<_>>().join("|");
        ButtonGroup { id: id_for("button-group", &key), options, value: value.into(), cursor: None, on_choose: None }
    }
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
        self
    }
    /// Force the hover-cursor look on one option.
    pub fn cursor(mut self, index: Option<usize>) -> Self {
        self.cursor = index;
        self
    }
    /// Called with the chosen option's value.
    pub fn on_choose(mut self, f: impl Fn(&SharedString, &mut Window, &mut App) + 'static) -> Self {
        self.on_choose = Some(Rc::new(f));
        self
    }
}

impl RenderOnce for ButtonGroup {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let gap = t.tokens().spacing.md as f32;
        let focus = focus_handle(&self.id, window, cx);
        let values: Rc<Vec<SharedString>> = Rc::new(self.options.iter().map(|(v, _)| v.clone()).collect());
        let selected = values.iter().position(|v| *v == self.value);
        // The keyboard cursor while the group has focus: on the selected option when focus
        // arrives (Qt's `_focusedIndex`), moved by Left/Right (h/l), chosen with Return/Space.
        let at = keyed_state(&self.id, "button-group", window, cx, || None::<usize>);
        let focused = focus.is_focused(window);
        let cursor = if focused { *at.read(cx) } else { None };
        if focused && cursor.is_none() {
            at.update(cx, |c, _| *c = Some(selected.unwrap_or(0)));
        } else if !focused && at.read(cx).is_some() {
            at.update(cx, |c, _| *c = None);
        }
        let cursor = cursor.or(if focused { Some(selected.unwrap_or(0)) } else { None });
        let mut r = div().id(self.id.clone()).flex().flex_row().gap(px(gap)).track_focus(&focus);
        for (i, (value, text)) in self.options.into_iter().enumerate() {
            let mut b = Button::new(text)
                .id(ElementId::NamedChild(std::sync::Arc::new(self.id.clone()), value.to_string().into()))
                .focusable(false)
                .bordered(true)
                .selected(value == self.value)
                .cursor(self.cursor == Some(i) || cursor == Some(i));
            let (f, focus) = (self.on_choose.clone(), focus.clone());
            let v = value.clone();
            b = b.on_click(move |_, window, cx| {
                focus.focus(window, cx);
                if let Some(f) = &f {
                    f(&v, window, cx);
                }
            });
            r = r.child(b);
        }
        let choose = self.on_choose;
        r.on_key_down(move |ev: &KeyDownEvent, window, cx| {
            if tab_keys(ev, window, cx) {
                cx.stop_propagation();
                return;
            }
            let n = values.len();
            let here = at.read(cx).or(selected).unwrap_or(0);
            match ev.keystroke.key.as_str() {
                "left" | "h" => at.update(cx, |c, cx| {
                    *c = Some(here.saturating_sub(1));
                    cx.notify();
                }),
                "right" | "l" => at.update(cx, |c, cx| {
                    *c = Some((here + 1).min(n.saturating_sub(1)));
                    cx.notify();
                }),
                _ if is_activate(ev) => {
                    if let (Some(f), Some(v)) = (&choose, values.get(here)) {
                        f(v, window, cx);
                    }
                }
                _ => return,
            }
            cx.stop_propagation();
        })
    }
}
