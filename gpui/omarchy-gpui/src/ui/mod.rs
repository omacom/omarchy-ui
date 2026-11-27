//! Omarchy's components for GPUI, drawn the way the Qt kit draws them (`spec/components.md`):
//! same names, metrics, states, keys and strings.
//!
//! Each is a builder that is a GPUI element (`RenderOnce`): `Button::new("Save").bordered(true)`
//! goes straight into `.child(..)`. Handlers take GPUI's usual `Fn(&Event, &mut Window, &mut
//! App)`, which `cx.listener(..)` makes from a view method. `.cursor(true)` forces the
//! hover-cursor look, as the shell's `hasCursor` does for keyboard-driven panels and stills.
//! Colors come from [`Paint`](crate::Paint), so they fade with a theme switch.
//!
//! Focusable components keep their focus handle under their element id (by default derived from
//! their label); give two of the same label in one view distinct `.id(..)`s.

mod button;
mod data;
mod editing;
mod feedback;
mod field;
mod forms;
mod ink;
mod lists;
mod navigation;
mod pickers;
mod search;
mod slider;
mod surface;
mod toggle;

// Each group module exports its own components (spec/components/*.md).
pub use data::*;
pub use editing::*;
pub use feedback::*;
pub use forms::*;
pub use lists::*;
pub use navigation::*;
pub use pickers::*;
pub use search::*;

pub use button::{Button, ButtonGroup, IconButton};
pub use field::{Dropdown, NumberField, TextChanged, TextField, TextInput};
pub use slider::Slider;
pub use surface::{Card, ConfirmDialog, Dialog, Heading, HeadingLevel, MenuRow, Pane, SettingRow, Side, Tooltip, section_header, separator};
pub use toggle::{Toggle, ToggleSwitch};

use std::rc::Rc;

use gpui::prelude::*;
use gpui::{
    AbsoluteLength, App, Div, ElementId, FocusHandle, FontWeight, Hsla, KeyDownEvent, Pixels, SharedString, Stateful, Window, div, px,
};
use omarchy_theme::{BorderSpec, Rgba};

use crate::{Paint, color};

/// A handler for an event `E`, the shape `cx.listener(..)` returns.
pub type Handler<E> = Rc<dyn Fn(&E, &mut Window, &mut App)>;

/// The group name the chrome layers follow the pointer with.
pub(crate) const GROUP: &str = "omarchy-chrome";

/// One visual state of a control: fill and border.
#[derive(Clone)]
pub(crate) struct Look {
    pub fill: Rgba,
    pub border: BorderSpec,
}

impl Look {
    pub fn new(fill: Rgba, border: BorderSpec) -> Look {
        Look { fill, border }
    }
    pub fn clear() -> Look {
        Look { fill: Rgba::TRANSPARENT, border: BorderSpec::none() }
    }
}

/// The widest border any of `looks` paints, per side (top, right, bottom, left), so changing
/// state never relayouts (the shell reserves the same way).
pub(crate) fn reserve(looks: &[&BorderSpec]) -> [f32; 4] {
    let mut out = [0.0f32; 4];
    for spec in looks {
        for (i, w) in spec.widths.iter().enumerate() {
            out[i] = out[i].max(*w as f32);
        }
    }
    out
}

/// Paint a border spec on an element: per-side widths in the spec's color (a gradient's first
/// stop; GPUI borders are one color).
pub fn border<E: Styled>(mut e: E, spec: &BorderSpec) -> E {
    if spec.is_none() {
        return e;
    }
    let s = e.style();
    let w = |v: f64| Some(AbsoluteLength::Pixels(px(v as f32)));
    s.border_widths.top = w(spec.widths[0]);
    s.border_widths.right = w(spec.widths[1]);
    s.border_widths.bottom = w(spec.widths[2]);
    s.border_widths.left = w(spec.widths[3]);
    e.border_color(color(spec.color))
}

/// A layer filling its parent: a state's fill and border. When both are present, the fill is
/// inset inside the border so a translucent border shows what's behind the control rather than
/// compositing over its own fill (matching Qt's `Rectangle` / `BorderSurface`).
pub(crate) fn layer(look: &Look, radius: Pixels) -> Div {
    if look.border.is_none() {
        let base = div().absolute().top_0().left_0().size_full().rounded(radius);
        return if look.fill.a > 0 { base.bg(color(look.fill)) } else { base };
    }
    let ring = border(div().absolute().top_0().left_0().size_full().rounded(radius), &look.border);
    if look.fill.a == 0 {
        return ring;
    }
    let w = look.border.widths;
    let r = f32::from(radius);
    let inner_r = |a: f64, b: f64| px((r - a.max(b) as f32).max(0.0));
    let fill = div()
        .absolute()
        .top(px(w[0] as f32))
        .right(px(w[1] as f32))
        .bottom(px(w[2] as f32))
        .left(px(w[3] as f32))
        .rounded_tl(inner_r(w[0], w[3]))
        .rounded_tr(inner_r(w[0], w[1]))
        .rounded_br(inner_r(w[2], w[1]))
        .rounded_bl(inner_r(w[2], w[3]))
        .bg(color(look.fill));
    div().absolute().top_0().left_0().size_full().child(fill).child(ring)
}

/// Control chrome under `base`'s children: the `rest` look, swapped for the `hot` look under the
/// pointer, with keyboard focus (Omarchy's focus mirrors hover) or when `forced`; the pressed
/// fill while the mouse is down. Add the content after this, so it paints on top.
pub(crate) fn chrome(base: Stateful<Div>, t: &Paint, rest: &Look, hot: &Look, forced: bool, radius: Pixels) -> Stateful<Div> {
    let pressed = color(t.tokens().controls.pressed.fill);
    let base = base.relative().group(GROUP).rounded(radius);
    let base = if forced {
        base.child(layer(hot, radius))
    } else {
        base.child(layer(rest, radius).group_hover(GROUP, |s| s.opacity(0.0)))
            .child(layer(hot, radius).opacity(0.0).group_hover(GROUP, |s| s.opacity(1.0)))
    };
    base.child(div().id("pressed").absolute().top_0().left_0().size_full().rounded(radius).group_active(GROUP, move |s| s.bg(pressed)))
}

/// Text in the Omarchy font at `size` px, one line, in `ink`; bold for selection and titles. The
/// line box is Qt's (the font's ascent + descent), so controls come out the same height.
pub fn label(t: &Paint, s: impl Into<SharedString>, size: i64, bold: bool, ink: Hsla) -> Div {
    let d = div()
        .font_family(t.family())
        .text_size(px(size as f32))
        .line_height(px(t.exact_line(size)))
        .pb(px(t.line_rest(size)))
        .text_color(ink)
        .whitespace_nowrap()
        .flex_shrink_0()
        .child(s.into());
    if bold { d.font_weight(FontWeight::BOLD) } else { d.font_weight(FontWeight::NORMAL) }
}

/// [`label`] that elides with … when it doesn't fit.
pub fn label_elided(t: &Paint, s: impl Into<SharedString>, size: i64, bold: bool, ink: Hsla) -> Div {
    label(t, s, size, bold, ink).flex_shrink(1.0).min_w_0().overflow_hidden().text_ellipsis()
}

/// Wrapping text in the Omarchy font.
pub fn paragraph(t: &Paint, s: impl Into<SharedString>, size: i64, ink: Hsla) -> Div {
    div()
        .font_family(t.family())
        .text_size(px(size as f32))
        .line_height(px(t.exact_line(size)))
        .pb(px(t.line_rest(size)))
        .text_color(ink)
        .font_weight(FontWeight::NORMAL)
        .child(s.into())
}

/// The focus handle kept for the element `id` across frames (a tab stop).
pub(crate) fn focus_handle(id: &ElementId, window: &mut Window, cx: &mut App) -> FocusHandle {
    let key = ElementId::NamedChild(Arc::new(id.clone()), "focus".into());
    window.use_keyed_state(key, cx, |_, cx| cx.focus_handle().tab_stop(true)).read(cx).clone()
}

use std::sync::Arc;

/// Persistent per-element state kept under the element `id` (a scroll offset, an open flag).
pub(crate) fn keyed_state<S: 'static>(
    id: &ElementId,
    name: &'static str,
    window: &mut Window,
    cx: &mut App,
    init: impl FnOnce() -> S,
) -> gpui::Entity<S> {
    let key = ElementId::NamedChild(Arc::new(id.clone()), name.into());
    window.use_keyed_state(key, cx, |_, _| init())
}

/// Tab and Shift+Tab move focus through the window's tab stops; true when `ev` was one.
pub(crate) fn tab_keys(ev: &KeyDownEvent, window: &mut Window, cx: &mut App) -> bool {
    if ev.keystroke.key != "tab" {
        return false;
    }
    if ev.keystroke.modifiers.shift {
        window.focus_prev(cx);
    } else {
        window.focus_next(cx);
    }
    true
}

/// True for the keys that activate a focused control: Return, Enter and Space.
pub(crate) fn is_activate(ev: &KeyDownEvent) -> bool {
    matches!(ev.keystroke.key.as_str(), "enter" | "space" | "kp_enter") && !ev.keystroke.modifiers.modified()
}

/// An element id from a component's label, for its focus handle and state.
pub(crate) fn id_for(kind: &'static str, label: &str) -> ElementId {
    ElementId::Name(format!("omarchy-{kind}-{label}").into())
}

/// A key combination shown after a tooltip's text.
pub(crate) fn shortcut_hint(t: &Paint, combo: SharedString) -> gpui::AnyElement {
    navigation::tooltip_shortcut(t, combo)
}

