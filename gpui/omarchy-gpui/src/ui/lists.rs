//! Lists (spec/components/lists.md): ListRow, ScrollArea, KeyValue and Hero, the CursorModel that
//! drives them, and the tile pieces (Badge, StatusDot, Avatar, TileGrid). Drawn as the Qt binding's
//! `ListRow.qml`, `ScrollArea.qml`, `KeyValue.qml`, `Hero.qml`, ... draw them.

mod cursor;
mod tiles;

pub use cursor::{CursorKey, CursorModel, CursorSection, printable};
pub use tiles::{Avatar, AvatarSize, Badge, StatusDot, Tile, TileGrid};

use std::cell::Cell;
use std::rc::Rc;

use gpui::prelude::*;
use gpui::{
    AnyElement, App, Bounds, ClickEvent, Div, ElementId, FontWeight, Hsla, MouseButton, MouseDownEvent, MouseMoveEvent, MouseUpEvent,
    Pixels, Point, ScrollHandle, SharedString, TextRun, Window, canvas, div, fill, point, px,
};
use omarchy_theme::{BorderSpec, Rgba, Tokens};

use super::{Handler, id_for, keyed_state, label, label_elided};
use crate::{Paint, color};

/// A status color: the tint of a status line, a Badge, a StatusDot or a KeyValue value.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Tone {
    /// The component's own neutral ink (dim for a status line, foreground for a badge).
    #[default]
    Neutral,
    Accent,
    Success,
    Warning,
    Urgent,
    Muted,
}

impl Tone {
    /// The tone's color, or `neutral` for [`Tone::Neutral`].
    pub fn rgba(self, t: &Tokens, neutral: Rgba) -> Rgba {
        let p = &t.palette;
        match self {
            Tone::Neutral => neutral,
            Tone::Accent => p.accent,
            Tone::Success => p.success,
            Tone::Warning => p.warning,
            Tone::Urgent => p.urgent,
            Tone::Muted => p.muted,
        }
    }
}

/// Natural width of [`glyph`] (advance plus any overhanging ink).
pub(crate) fn glyph_width(t: &Paint, window: &mut Window, s: &str, size: i64) -> f32 {
    let font_size = px(size as f32);
    let run = TextRun { len: s.len(), font: gpui::font(t.family()), color: t.fg(), background_color: None, underline: None, strikethrough: None };
    let advance = f32::from(window.text_system().layout_line(s, font_size, &[run], None).width);
    let (mut lo, mut hi) = (0.0f32, advance);
    if let Some(file) = t.tokens().font.file.as_deref() {
        let mut x = 0.0f32;
        for ch in s.chars() {
            let Some(g) = super::ink::ink(file, ch) else { continue };
            lo = lo.min(x + g.x_min * size as f32);
            hi = hi.max(x + g.x_max * size as f32);
            x += g.advance * size as f32;
        }
    }
    let left = (-lo).max(0.0).ceil();
    let right = (hi - advance).max(0.0).ceil();
    advance + left + right
}

/// A glyph boxed the way Qt's `Text` boxes one: the advance, widened by any ink that overhangs
/// it, in whole pixels. Nerd Font icons in the non-Mono Nerd Fonts overhang their cell to the
/// right, so centering the bare advance (as GPUI lays text out) leaves the icon right of center.
pub(crate) fn glyph(t: &Paint, window: &mut Window, s: impl Into<SharedString>, size: i64, ink: Hsla) -> Div {
    let s: SharedString = s.into();
    let e = label(t, s.clone(), size, false, ink);
    let font_size = px(size as f32);
    let run = TextRun { len: s.len(), font: gpui::font(t.family()), color: ink, background_color: None, underline: None, strikethrough: None };
    let advance = f32::from(window.text_system().layout_line(s.as_ref(), font_size, &[run], None).width);
    let (mut lo, mut hi) = (0.0f32, advance);
    if let Some(file) = t.tokens().font.file.as_deref() {
        let mut x = 0.0f32;
        for ch in s.chars() {
            let Some(g) = super::ink::ink(file, ch) else { continue };
            lo = lo.min(x + g.x_min * size as f32);
            hi = hi.max(x + g.x_max * size as f32);
            x += g.advance * size as f32;
        }
    }
    let left = (-lo).max(0.0).ceil();
    let right = (hi - advance).max(0.0).ceil();
    if left == 0.0 && right == 0.0 {
        return e;
    }
    div().flex().flex_shrink_0().w(px(advance + left + right)).pl(px(left)).child(e)
}

/// The width of a ListRow's leading slot: glyphs center in it and a small Avatar fills it, so
/// titles line up down a list whatever leads them.
pub(crate) fn lead_width(t: &Tokens) -> f32 {
    t.font.icon_large as f32
}

/// The group a row's end slot follows the pointer with (the action reveals under it).
const ROW: &str = "omarchy-list-row";

/// One row of a keyboard-and-mouse list (the shell's NetworkRow, DeviceRow, ...): a leading glyph
/// or element, a bold title over a dim subtitle and a tinted status line, a trailing value or
/// element, and an end slot holding a trailing glyph at rest and an action in its place under the
/// cursor or the pointer (the network panel's lock that turns into forget). Like the shell's
/// CursorSurface, the look follows `cursor` and `current` only; the pointer moves the cursor
/// through [`ListRow::on_pointer_move`] and a [`CursorModel`].
#[derive(IntoElement)]
pub struct ListRow {
    id: ElementId,
    glyph: SharedString,
    glyph_color: Option<Rgba>,
    leading: Option<AnyElement>,
    title: SharedString,
    subtitle: SharedString,
    status: SharedString,
    tone: Tone,
    value: SharedString,
    trailing: Option<AnyElement>,
    trailing_glyph: SharedString,
    trailing_tone: Tone,
    expansion: Option<AnyElement>,
    action: SharedString,
    action_urgent: bool,
    action_cursor: bool,
    cursor: bool,
    current: bool,
    disabled: bool,
    width: Option<f32>,
    on_click: Option<Handler<ClickEvent>>,
    on_action: Option<Handler<ClickEvent>>,
    on_pointer_move: Option<Handler<Point<Pixels>>>,
}

impl ListRow {
    pub fn new(title: impl Into<SharedString>) -> ListRow {
        let title = title.into();
        ListRow {
            id: id_for("list-row", &title),
            glyph: SharedString::default(),
            glyph_color: None,
            leading: None,
            title,
            subtitle: SharedString::default(),
            status: SharedString::default(),
            tone: Tone::Neutral,
            value: SharedString::default(),
            trailing: None,
            trailing_glyph: SharedString::default(),
            trailing_tone: Tone::Neutral,
            expansion: None,
            action: SharedString::default(),
            action_urgent: false,
            action_cursor: false,
            cursor: false,
            current: false,
            disabled: false,
            width: None,
            on_click: None,
            on_action: None,
            on_pointer_move: None,
        }
    }
    /// The element id (the title by default); give rows with the same title distinct ids.
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
        self
    }
    /// A leading Nerd Font glyph, at `font.title`.
    pub fn glyph(mut self, g: impl Into<SharedString>) -> Self {
        self.glyph = g.into();
        self
    }
    pub fn glyph_color(mut self, c: Rgba) -> Self {
        self.glyph_color = Some(c);
        self
    }
    /// Any leading element instead of a glyph: an [`Avatar`] (`AvatarSize::Small` fills the slot).
    pub fn leading(mut self, e: impl IntoElement) -> Self {
        self.leading = Some(e.into_any_element());
        self
    }
    pub fn subtitle(mut self, s: impl Into<SharedString>) -> Self {
        self.subtitle = s.into();
        self
    }
    /// A status line under the subtitle ("Connected", "Failed"), dim unless `tone` tints it.
    pub fn status(mut self, s: impl Into<SharedString>, tone: Tone) -> Self {
        self.status = s.into();
        self.tone = tone;
        self
    }
    /// Dim text on the trailing edge ("5 GHz", "80%").
    pub fn value(mut self, v: impl Into<SharedString>) -> Self {
        self.value = v.into();
        self
    }
    /// Any trailing element instead of a value: a [`Badge`], a ToggleSwitch drawn as a picture.
    pub fn trailing(mut self, e: impl IntoElement) -> Self {
        self.trailing = Some(e.into_any_element());
        self
    }
    /// A glyph that stays in the end slot (a lock, signal bars, a drill-in chevron, a check), dim
    /// unless `tone` tints it. An [`action`](Self::action) takes its place while it shows.
    pub fn trailing_glyph(mut self, g: impl Into<SharedString>, tone: Tone) -> Self {
        self.trailing_glyph = g.into();
        self.trailing_tone = tone;
        self
    }
    /// Opens the row: any element across it under the main line, inside the row's fill (the
    /// network panel's password field and Connect button), `space(4)` below.
    pub fn expansion(mut self, e: impl IntoElement) -> Self {
        self.expansion = Some(e.into_any_element());
        self
    }
    /// An action glyph in the end slot, shown while the row has the cursor or the pointer (or the
    /// action has the cursor), in place of any trailing glyph. `urgent` for a destructive one.
    pub fn action(mut self, g: impl Into<SharedString>, urgent: bool) -> Self {
        self.action = g.into();
        self.action_urgent = urgent;
        self
    }
    /// The keyboard cursor is on the action rather than the row: ringed in its tone.
    pub fn action_cursor(mut self, on: bool) -> Self {
        self.action_cursor = on;
        self
    }
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    /// The chosen row (the connected network): the selected fill.
    pub fn current(mut self, on: bool) -> Self {
        self.current = on;
        self
    }
    pub fn disabled(mut self, on: bool) -> Self {
        self.disabled = on;
        self
    }
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    pub fn on_click(mut self, f: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static) -> Self {
        self.on_click = Some(Rc::new(f));
        self
    }
    /// The revealed action was clicked (forget).
    pub fn on_action(mut self, f: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static) -> Self {
        self.on_action = Some(Rc::new(f));
        self
    }
    /// The pointer moved over the row, at a window point: hand it to
    /// [`CursorModel::pointer_moved`].
    pub fn on_pointer_move(mut self, f: impl Fn(&Point<Pixels>, &mut Window, &mut App) + 'static) -> Self {
        self.on_pointer_move = Some(Rc::new(f));
        self
    }
}

impl RenderOnce for ListRow {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens().clone();
        let c = &tk.controls;
        let sp = &tk.spacing;
        let f = &tk.font;
        let faint = tk.roles.text_faint;
        let ink = if self.disabled { faint } else { tk.palette.foreground };
        let dim = if self.disabled { faint } else { tk.roles.text_dim };

        let (fill, edge) = if self.cursor {
            (c.hover.fill, c.hover.border.clone())
        } else if self.current {
            (c.selected.fill, c.selected.border.clone())
        } else {
            (Rgba::TRANSPARENT, BorderSpec::none())
        };

        let mut body = div().flex().flex_row().w_full().items_center();
        let lead = match (self.leading, self.glyph.is_empty()) {
            (Some(e), _) => Some(e),
            (None, false) => {
                let g = self.glyph_color.filter(|_| !self.disabled).unwrap_or(ink);
                let lw = lead_width(&tk);
                let lh = lw.max((f.title as f32 * 1.35).ceil());
                Some(super::navigation::optical(window, cx, &t, &self.glyph, f.title, color(g), None, lw, lh).into_any_element())
            }
            (None, true) => None,
        };
        if let Some(e) = lead {
            body = body
                .child(div().flex().flex_shrink_0().w(px(lead_width(&tk))).justify_center().items_center().child(e))
                .child(div().flex_shrink_0().w(px(t.space(10.0))));
        }

        let mut info = div()
            .flex()
            .flex_col()
            .flex_1()
            .min_w_0()
            .gap(px(t.space(1.0)))
            .child(label_elided(&t, self.title.clone(), f.body, true, color(ink)));
        if !self.subtitle.is_empty() {
            info = info.child(label_elided(&t, self.subtitle, f.caption, false, color(dim)));
        }
        if !self.status.is_empty() {
            let tint = if self.disabled { faint } else { self.tone.rgba(&tk, tk.roles.text_dim) };
            info = info.child(label_elided(&t, self.status, f.caption, false, color(tint)));
        }
        body = body.child(info);

        // Trailing: value or element, then the end slot (kept while empty, so revealing the action
        // never reflows the row).
        let gap = px(t.space(8.0));
        let mut trail: Vec<AnyElement> = Vec::new();
        if let Some(e) = self.trailing {
            trail.push(e);
        } else if !self.value.is_empty() {
            trail.push(label(&t, self.value, f.body_small, false, color(dim)).into_any_element());
        }
        let has_action = !self.action.is_empty();
        if has_action || !self.trailing_glyph.is_empty() {
            let side_f = t.space(22.0);
            let side = px(side_f);
            let shown = has_action && !self.disabled && (self.cursor || self.action_cursor);
            let live = has_action && !self.disabled;
            let slot_id = ElementId::NamedChild(std::sync::Arc::new(self.id.clone()), "action".into());
            let selector = slot_id.clone();
            let mut slot = div()
                .id(slot_id)
                .debug_selector(move || selector.to_string())
                .relative()
                .flex_shrink_0()
                .size(side)
                .rounded(t.radius_small());
            if live && self.action_cursor {
                // The cursor look in the action's own ink, whatever color the theme gives the cursor.
                let tone = if self.action_urgent { tk.palette.urgent } else { tk.palette.foreground };
                let w = c.hover.border.max_width();
                let look = super::Look::new(
                    tone.with_alpha(c.hover.fill.alpha_f() as f64),
                    BorderSpec { color: tone.with_alpha(c.hover.border.color.alpha_f() as f64), widths: [w; 4], gradient: None },
                );
                slot = slot.child(super::layer(&look, t.radius_small()));
            }
            if !self.trailing_glyph.is_empty() {
                let tint = if self.disabled { faint } else { self.trailing_tone.rgba(&tk, tk.roles.text_dim) };
                let mark = div()
                    .absolute()
                    .top_0()
                    .left_0()
                    .size_full()
                    .child(super::navigation::optical(window, cx, &t, &self.trailing_glyph, f.subtitle, color(tint), None, side_f, side_f));
                let mark = if shown { mark.opacity(0.0) } else { mark };
                slot = slot.child(if live { mark.group_hover(ROW, |s| s.opacity(0.0)) } else { mark });
            }
            if has_action {
                let ink = if self.action_urgent { tk.palette.urgent } else { tk.roles.text_dim };
                let mark = div()
                    .absolute()
                    .top_0()
                    .left_0()
                    .size_full()
                    .child(super::navigation::optical(window, cx, &t, &self.action, f.subtitle, color(ink), None, side_f, side_f));
                slot = slot.child(match (live, shown) {
                    (false, _) => mark.opacity(0.0),
                    (true, true) => mark,
                    (true, false) => mark.opacity(0.0).group_hover(ROW, |s| s.opacity(1.0)),
                });
                if live && let Some(f) = self.on_action {
                    slot = slot.on_click(move |e, w, cx| {
                        cx.stop_propagation();
                        f(e, w, cx)
                    });
                }
            }
            trail.push(slot.into_any_element());
        }
        if !trail.is_empty() {
            body = body.child(div().flex().flex_row().flex_shrink_0().items_center().gap(gap).pl(gap).children(trail));
        }

        let content = match self.expansion {
            Some(x) => div().flex().flex_col().w_full().gap(px(t.space(4.0))).child(body).child(x).into_any_element(),
            None => body.into_any_element(),
        };
        let selector = self.id.clone();
        let mut e = div()
            .id(self.id)
            .debug_selector(move || selector.to_string())
            .relative()
            .group(ROW)
            .flex()
            .flex_row()
            .flex_shrink_0()
            .items_center()
            .px(px(sp.control_padding_x as f32))
            .py(px(sp.row_padding_x as f32 / 2.0))
            .rounded(t.radius())
            // The border is drawn inside the padding (Qt's CursorSurface), so it never moves the content.
            .child(super::layer(&super::Look::new(fill, edge), t.radius()))
            .child(content);
        e = match self.width {
            Some(w) => e.w(px(w)),
            None => e.w_full(),
        };
        if self.disabled {
            return e;
        }
        if let Some(f) = self.on_pointer_move {
            e = e.on_mouse_move(move |ev: &MouseMoveEvent, w, cx| f(&ev.position, w, cx));
        }
        match self.on_click {
            Some(f) => e.on_click(move |ev, w, cx| f(ev, w, cx)),
            None => e,
        }
    }
}

/// A vertical scroll viewport capped at `max_height`, with Omarchy's thin scrollbar over its right
/// edge: a `track`-colored groove `scrollbarThickness` wide and a thumb in the normal border color
/// (`textDim` under the pointer or while dragged), both `min(4, radius)`. The bar shows only while
/// the content overflows and reserves no gutter (the shell's AsNeeded bars), so a short list is as
/// wide as its siblings and nothing reflows when the bar appears; rows keep clear of it with their
/// own padding. The thumb drags; scrolling stops at the ends. Reveal a child with
/// [`ScrollArea::ensure_visible`].
#[derive(IntoElement)]
pub struct ScrollArea {
    id: ElementId,
    max_height: Option<f32>,
    width: Option<f32>,
    gap: f32,
    handle: Option<ScrollHandle>,
    children: Vec<AnyElement>,
}

#[derive(Default)]
struct BarDrag {
    /// The pointer's y and the scroll offset when the thumb was pressed.
    from: Cell<Option<(Pixels, Pixels)>>,
}

impl ScrollArea {
    pub fn new(id: impl Into<ElementId>) -> ScrollArea {
        ScrollArea { id: id.into(), max_height: None, width: None, gap: 0.0, handle: None, children: Vec::new() }
    }
    pub fn max_height(mut self, h: f32) -> Self {
        self.max_height = Some(h);
        self
    }
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    /// Space between children.
    pub fn gap(mut self, g: f32) -> Self {
        self.gap = g;
        self
    }
    /// Scroll with the app's own handle (to reveal rows, read or set the offset); one is kept
    /// under the id otherwise.
    pub fn track_scroll(mut self, handle: &ScrollHandle) -> Self {
        self.handle = Some(handle.clone());
        self
    }
    /// Scroll just far enough to show child `index` (the cursor's row after a key).
    pub fn ensure_visible(handle: &ScrollHandle, index: usize) {
        handle.scroll_to_item(index);
    }
}

impl ParentElement for ScrollArea {
    fn extend(&mut self, elements: impl IntoIterator<Item = AnyElement>) {
        self.children.extend(elements);
    }
}

impl RenderOnce for ScrollArea {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let thick = tk.spacing.scrollbar_thickness as f32;
        let rs = t.radius_small();
        let min_thumb = t.space(24.0);
        let handle = match self.handle {
            Some(h) => h,
            None => keyed_state(&self.id, "scroll", window, cx, ScrollHandle::new).read(cx).clone(),
        };
        let drag = keyed_state(&self.id, "scrollbar", window, cx, || Rc::new(BarDrag::default())).read(cx).clone();
        let groove = t.track();
        let (rest, hot) = (color(tk.controls.normal.border.color), t.dim());

        let mut viewport = div()
            .id(self.id.clone())
            .flex()
            .flex_col()
            .w_full()
            .gap(px(self.gap))
            .overflow_y_scroll()
            .track_scroll(&handle)
            .children(self.children);
        if let Some(h) = self.max_height {
            viewport = viewport.max_h(px(h));
        }

        // The thumb's rectangle in the viewport now: (y, height), or None without overflow.
        let thumb = move |h: &ScrollHandle| -> Option<(Bounds<Pixels>, Pixels)> {
            let b = h.bounds();
            let max = h.max_offset().y;
            if max <= px(0.5) || b.size.height <= px(0.0) {
                return None;
            }
            let track = b.size.height;
            let content = track + max;
            let len = (track * (track / content)).max(px(min_thumb)).min(track);
            let y = (track - len) * ((-h.offset().y) / max).clamp(0.0, 1.0);
            let x = b.right() - px(thick);
            Some((Bounds::new(point(x, b.top() + y), gpui::size(px(thick), len)), track - len))
        };
        let (h1, d1) = (handle.clone(), drag.clone());
        let bar = canvas(
            |_, _, _| {},
            move |_, _, window, _| {
                let Some((thumb_b, run)) = thumb(&h1) else { return };
                let b = h1.bounds();
                let bar_b = Bounds::new(point(b.right() - px(thick), b.top()), gpui::size(px(thick), b.size.height));
                window.paint_quad(fill(bar_b, groove).corner_radii(rs));
                let dragging = d1.from.get().is_some();
                let ink = if dragging || thumb_b.contains(&window.mouse_position()) { hot } else { rest };
                window.paint_quad(fill(thumb_b, ink).corner_radii(rs));

                let (h2, d2) = (h1.clone(), d1.clone());
                window.on_mouse_event(move |ev: &MouseDownEvent, phase, window, cx| {
                    if phase.bubble() && ev.button == MouseButton::Left && thumb_b.contains(&ev.position) {
                        d2.from.set(Some((ev.position.y, h2.offset().y)));
                        cx.stop_propagation();
                        window.refresh();
                    }
                });
                let (h3, d3) = (h1.clone(), d1.clone());
                window.on_mouse_event(move |ev: &MouseMoveEvent, _, window, _| {
                    if let Some((y0, off0)) = d3.from.get() {
                        let max = h3.max_offset().y;
                        let dy = (ev.position.y - y0) * (max / run.max(px(1.0)));
                        let y = (off0 - dy).clamp(-max, px(0.0));
                        h3.set_offset(point(h3.offset().x, y));
                        window.refresh();
                    } else if bar_b.contains(&ev.position) {
                        window.refresh();
                    }
                });
                let d4 = d1.clone();
                window.on_mouse_event(move |_: &MouseUpEvent, _, window, _| {
                    if d4.from.take().is_some() {
                        window.refresh();
                    }
                });
            },
        )
        .absolute()
        .top_0()
        .left_0()
        .size_full();

        let e = div().relative().flex().flex_col().flex_shrink_0().child(viewport).child(bar);
        match self.width {
            Some(w) => e.w(px(w)),
            None => e.w_full(),
        }
    }
}

/// Dim labels beside their values in a grid, `columns` pairs to a line: the details block the
/// power, dropbox and network panels each define (InfoLabel, InfoValue).
#[derive(IntoElement, Default)]
pub struct KeyValue {
    pairs: Vec<(SharedString, SharedString, Tone)>,
    columns: usize,
}

impl KeyValue {
    pub fn new() -> KeyValue {
        KeyValue { pairs: Vec::new(), columns: 1 }
    }
    pub fn pair(mut self, label: impl Into<SharedString>, value: impl Into<SharedString>) -> Self {
        self.pairs.push((label.into(), value.into(), Tone::Neutral));
        self
    }
    /// A pair whose value is tinted (a packet loss in urgent).
    pub fn pair_toned(mut self, label: impl Into<SharedString>, value: impl Into<SharedString>, tone: Tone) -> Self {
        self.pairs.push((label.into(), value.into(), tone));
        self
    }
    /// Pairs per line.
    pub fn columns(mut self, n: usize) -> Self {
        self.columns = n.max(1);
        self
    }
}

impl RenderOnce for KeyValue {
    fn render(self, _: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let size = tk.font.body_small;
        let n = self.columns.max(1);
        // Grid columns, each as wide as its widest cell: label, value, label, value, ...
        let mut grid = div().flex().flex_row().flex_shrink_0().items_start().gap(px(t.space(20.0)));
        for c in 0..n * 2 {
            let mut column = div().flex().flex_col().gap(px(tk.spacing.label_gap as f32));
            for (l, v, tone) in self.pairs.iter().skip(c / 2).step_by(n) {
                column = column.child(if c % 2 == 0 {
                    label(&t, l.clone(), size, false, t.dim())
                } else {
                    label(&t, v.clone(), size, false, color(tone.rgba(tk, tk.palette.foreground)))
                });
            }
            grid = grid.child(column);
        }
        grid
    }
}

/// The head of a panel: a big glyph (`font.display`), a bold title (`font.title`), a dim
/// uppercase status line (urgent on `error`), and a trailing control centered against them.
#[derive(IntoElement)]
pub struct Hero {
    glyph: SharedString,
    title: SharedString,
    status: SharedString,
    error: bool,
    glyph_opacity: f32,
    width: Option<f32>,
    trailing: Option<AnyElement>,
}

impl Hero {
    pub fn new(glyph: impl Into<SharedString>, title: impl Into<SharedString>) -> Hero {
        Hero {
            glyph: glyph.into(),
            title: title.into(),
            status: SharedString::default(),
            error: false,
            glyph_opacity: 1.0,
            width: None,
            trailing: None,
        }
    }
    pub fn status(mut self, s: impl Into<SharedString>) -> Self {
        self.status = s.into();
        self
    }
    pub fn error(mut self, on: bool) -> Self {
        self.error = on;
        self
    }
    /// Half for a radio that is off, as the shell dims its hero glyphs.
    pub fn glyph_opacity(mut self, o: f32) -> Self {
        self.glyph_opacity = o;
        self
    }
    /// A fixed width; by default the hero fills its parent.
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    pub fn trailing(mut self, e: impl IntoElement) -> Self {
        self.trailing = Some(e.into_any_element());
        self
    }
}

/// `s` in bold capitals with Qt's `font.letterSpacing` (1.2 px after every character): one cell per
/// character, its advance plus the spacing wide, since GPUI's text has no letter spacing.
fn spaced(t: &Paint, window: &mut Window, s: &str, size: i64, ink: Hsla) -> Div {
    let mut font = gpui::font(t.family());
    font.weight = FontWeight::BOLD;
    let text = window.text_system().clone();
    // Placed one by one at their exact offsets: laid out as a row, each cell would round to whole
    // device pixels and the error would add up along the line.
    let mut x = px(0.0);
    let mut cells = Vec::new();
    for ch in s.chars() {
        let mut b = [0u8; 4];
        let one = SharedString::from(ch.encode_utf8(&mut b).to_string());
        let run = TextRun { len: one.len(), font: font.clone(), color: ink, background_color: None, underline: None, strikethrough: None };
        let adv = text.layout_line(one.as_ref(), px(size as f32), &[run], None).width;
        cells.push(div().absolute().top_0().left(x).child(label(t, one, size, true, ink)));
        x += adv + px(1.2);
    }
    div().relative().flex_shrink(1.0).min_w_0().overflow_hidden().w(x).h(t.line(size)).children(cells)
}

impl RenderOnce for Hero {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let f = &tk.font;
        let mut labels = div()
            .flex()
            .flex_col()
            .flex_1()
            .min_w_0()
            .gap(px(t.space(2.0)))
            .child(label_elided(&t, self.title, f.title, true, t.fg()));
        if !self.status.is_empty() {
            let ink = if self.error { t.urgent() } else { t.dim() };
            labels = labels.child(spaced(&t, window, &self.status.to_uppercase(), f.caption, ink));
        }
        let mut e = div().flex().flex_row().flex_shrink_0().items_center();
        if !self.glyph.is_empty() {
            let slot_w = f.display as f32;
            let slot_h = (f.display_large as f32).max((f.display as f32 * 1.35).ceil());
            e = e
                .child(super::navigation::optical(window, cx, &t, &self.glyph, f.display, t.fg(), None, slot_w, slot_h).opacity(self.glyph_opacity))
                .child(div().flex_shrink_0().w(px(t.space(14.0))));
        }
        e = e.child(labels);
        if let Some(trailing) = self.trailing {
            e = e.child(div().flex_shrink_0().w(px(t.space(12.0)))).child(trailing);
        }
        match self.width {
            Some(w) => e.w(px(w)),
            None => e.w_full(),
        }
    }
}
