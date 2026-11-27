//! ToggleSwitch and Toggle: the shell's `ToggleSwitch.qml` and `Toggle.qml`.

use std::rc::Rc;

use gpui::prelude::*;
use gpui::{App, ClickEvent, Div, ElementId, SharedString, Window, div, px};
use omarchy_theme::Rgba;

use super::button::pressable;
use super::{Handler, Look, chrome, focus_handle, id_for, label_elided, paragraph};
use crate::{Paint, color};

/// The track and knob alone.
fn track(t: &Paint, checked: bool) -> Div {
    let tk = t.tokens();
    let c = &tk.controls;
    let rounded = tk.radius > 0;
    let track_h = 22f32.max((tk.spacing.control_height as f32 * 0.55).round());
    let track_w = (track_h * 1.9).round();
    let knob = 6f32.max((track_h * 0.72).round());
    let inset = 1f32.max(((track_h - knob) / 2.0).round());
    let state = if checked { &c.selected } else { &c.normal };
    let knob_color = if checked { c.selected.text } else { tk.roles.text_dim };
    let r = px(if rounded { track_h / 2.0 } else { 0.0 });
    let knob_el = div()
        .absolute()
        .left(px(if checked { track_w - knob - inset } else { inset }))
        .top(px((track_h - knob) / 2.0))
        .size(px(knob))
        .rounded(px(if rounded { knob / 2.0 } else { 0.0 }))
        .bg(color(knob_color));
    div()
        .relative()
        .flex_shrink_0()
        .w(px(track_w))
        .h(px(track_h))
        .rounded(r)
        .child(super::layer(&Look::new(state.fill, state.border.clone()), r))
        .child(knob_el)
}

/// A bare switch. Pill-shaped when Hyprland rounds corners, square when it doesn't. The cursor is
/// a ring around the track, not a change to it. Return or Space flips it when focused.
#[derive(IntoElement)]
pub struct ToggleSwitch {
    id: ElementId,
    checked: bool,
    cursor: bool,
    interactive: bool,
    on_toggle: Option<Handler<bool>>,
}

impl ToggleSwitch {
    pub fn new(id: impl Into<ElementId>, checked: bool) -> ToggleSwitch {
        ToggleSwitch { id: id.into(), checked, cursor: false, interactive: true, on_toggle: None }
    }
    /// Off when a surrounding row owns the click and the cursor (Qt's `interactive: false`): the
    /// bare track, with no focus, ring or padding.
    pub fn interactive(mut self, on: bool) -> Self {
        self.interactive = on;
        self
    }
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    /// Called with the new value.
    pub fn on_toggle(mut self, f: impl Fn(&bool, &mut Window, &mut App) + 'static) -> Self {
        self.on_toggle = Some(Rc::new(f));
        self
    }
}

fn flip(checked: bool, f: Option<Handler<bool>>) -> Option<Handler<ClickEvent>> {
    f.map(|f| Rc::new(move |_: &ClickEvent, window: &mut Window, cx: &mut App| f(&!checked, window, cx)) as Handler<ClickEvent>)
}

impl RenderOnce for ToggleSwitch {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        if !self.interactive {
            return track(&t, self.checked).into_any_element();
        }
        let tk = t.tokens();
        let focus = focus_handle(&self.id, window, cx);
        let hot_now = self.cursor || focus.is_focused(window);
        let pad = t.space(6.0);
        let ring = Look::new(Rgba::TRANSPARENT, tk.controls.hover.border.clone());
        let base = div().id(self.id).flex().flex_shrink_0().items_center().justify_center().p(px(pad));
        let e = chrome(base, &t, &Look::clear(), &ring, hot_now, t.radius()).child(track(&t, self.checked));
        pressable(e, &focus, flip(self.checked, self.on_toggle)).into_any_element()
    }
}

/// A labeled switch row: title, optional description, and the switch at the end. The whole row
/// is the control.
#[derive(IntoElement)]
pub struct Toggle {
    id: ElementId,
    label: SharedString,
    description: SharedString,
    checked: bool,
    cursor: bool,
    width: Option<f32>,
    on_toggle: Option<Handler<bool>>,
}

impl Toggle {
    pub fn new(label: impl Into<SharedString>) -> Toggle {
        let label = label.into();
        Toggle { id: id_for("toggle", &label), label, description: SharedString::default(), checked: false, cursor: false, width: None, on_toggle: None }
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
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    /// A fixed width; by default the row fills its parent.
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    /// Called with the new value.
    pub fn on_toggle(mut self, f: impl Fn(&bool, &mut Window, &mut App) + 'static) -> Self {
        self.on_toggle = Some(Rc::new(f));
        self
    }
}

impl RenderOnce for Toggle {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let c = &tk.controls;
        let sp = &tk.spacing;
        let focus = focus_handle(&self.id, window, cx);
        let hot_now = self.cursor || focus.is_focused(window);
        let rest = Look::new(c.normal.fill, c.normal.border.clone());
        let hot = Look::new(c.hover.fill, c.hover.border.clone());
        let bw = c.normal.border.max_width() as f32;

        let mut words = div()
            .flex()
            .flex_col()
            .flex_1()
            .min_w_0()
            .gap(px(sp.xs as f32))
            .child(label_elided(&t, self.label.clone(), tk.font.subtitle, true, t.fg()));
        if !self.description.is_empty() {
            words = words.child(paragraph(&t, self.description, tk.font.caption, t.dim()));
        }
        let base = div()
            .id(self.id)
            .flex()
            .flex_row()
            .items_center()
            .gap(px(sp.row_padding_x as f32))
            .min_h(px(54.0))
            .py(px(sp.huge as f32 / 2.0))
            .px(px(sp.row_padding_x as f32 + bw));
        let base = match self.width {
            Some(w) => base.w(px(w)).flex_shrink_0(),
            None => base.w_full(),
        };
        let e = chrome(base, &t, &rest, &hot, hot_now, t.radius()).child(words).child(track(&t, self.checked));
        pressable(e, &focus, flip(self.checked, self.on_toggle))
    }
}
