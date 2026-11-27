//! TrimHandle, CropFrame, DragHandle and DropIndicator: the draggable parts of editors and
//! reorderable lists (`TrimHandle.qml`, `CropFrame.qml`, `DragHandle.qml`, `DropIndicator.qml`).

use std::rc::Rc;

use gpui::prelude::*;
use gpui::{App, CursorStyle, Div, ElementId, Hsla, KeyDownEvent, Window, div, px};

use super::{DragFn, HOT, HandleDrag, dragging, follow_drag, hot_accent};
use crate::ui::{Handler, focus_handle, tab_keys};
use crate::{Paint, color};

/// Which side or corner a handle sits on.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Edge {
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
}

impl Edge {
    /// The eight crop handles, in CropFrame's order.
    pub const ALL: [Edge; 8] =
        [Edge::TopLeft, Edge::Top, Edge::TopRight, Edge::Left, Edge::Right, Edge::BottomLeft, Edge::Bottom, Edge::BottomRight];

    pub fn is_corner(self) -> bool {
        matches!(self, Edge::TopLeft | Edge::TopRight | Edge::BottomLeft | Edge::BottomRight)
    }
    fn is_left(self) -> bool {
        matches!(self, Edge::Left | Edge::TopLeft | Edge::BottomLeft)
    }
    fn is_right(self) -> bool {
        matches!(self, Edge::Right | Edge::TopRight | Edge::BottomRight)
    }
    fn is_top(self) -> bool {
        matches!(self, Edge::Top | Edge::TopLeft | Edge::TopRight)
    }
    fn is_bottom(self) -> bool {
        matches!(self, Edge::Bottom | Edge::BottomLeft | Edge::BottomRight)
    }
    fn cursor(self) -> CursorStyle {
        match self {
            Edge::Left | Edge::Right => CursorStyle::ResizeLeftRight,
            Edge::Top | Edge::Bottom => CursorStyle::ResizeUpDown,
            _ => CursorStyle::Crosshair,
        }
    }
    /// The edge's name, as Qt's `edge` property spells it.
    pub fn key(self) -> &'static str {
        match self {
            Edge::Left => "left",
            Edge::Right => "right",
            Edge::Top => "top",
            Edge::Bottom => "bottom",
            Edge::TopLeft => "top-left",
            Edge::TopRight => "top-right",
            Edge::BottomLeft => "bottom-left",
            Edge::BottomRight => "bottom-right",
        }
    }
}

/// A piece of a part in `rest`, swapped for `hot` under the pointer (or always when `forced`),
/// with per-corner radii `[tl, tr, br, bl]`.
fn piece_radii(x: f32, y: f32, w: f32, h: f32, r: [f32; 4], rest: Hsla, hot: Hsla, forced: bool) -> Div {
    let corners = |d: Div| d.rounded_tl(px(r[0])).rounded_tr(px(r[1])).rounded_br(px(r[2])).rounded_bl(px(r[3]));
    let base = corners(div().absolute().left(px(x)).top(px(y)).w(px(w)).h(px(h)));
    if forced {
        base.bg(hot)
    } else {
        base.bg(rest)
            .child(corners(div().absolute().top_0().left_0().size_full()).bg(hot).opacity(0.0).group_hover(HOT, |s| s.opacity(1.0)))
    }
}

/// A piece of a part in `rest`, swapped for `hot` under the pointer (or always when `forced`).
fn piece(x: f32, y: f32, w: f32, h: f32, r: f32, rest: Hsla, hot: Hsla, forced: bool) -> Div {
    piece_radii(x, y, w, h, [r; 4], rest, hot, forced)
}

/// A draggable in/out handle: a trim cap on a clip's left or right end, or (with `crop`) one of
/// the eight handles of a crop rectangle. Accent, because it belongs to the chosen clip or
/// region; hot (lighter, a longer grip) under the pointer, with focus, while dragged or with
/// `cursor`. Arrow keys nudge it (1 point, 10 with Shift) along the axes it moves.
#[derive(IntoElement)]
pub struct TrimHandle {
    id: ElementId,
    edge: Edge,
    crop: bool,
    height: Option<f32>,
    cursor: bool,
    on_drag: Option<DragFn>,
    on_nudge: Option<Handler<(f32, f32)>>,
}

impl TrimHandle {
    /// A trim cap on `edge` (`Left` or `Right`).
    pub fn new(edge: Edge) -> TrimHandle {
        TrimHandle {
            id: ElementId::Name(format!("omarchy-trim-{}", edge.key()).into()),
            edge,
            crop: false,
            height: None,
            cursor: false,
            on_drag: None,
            on_nudge: None,
        }
    }
    /// A crop handle: a bar on a side, an L bracket on a corner.
    pub fn crop(edge: Edge) -> TrimHandle {
        TrimHandle { crop: true, id: ElementId::Name(format!("omarchy-crop-{}", edge.key()).into()), ..TrimHandle::new(edge) }
    }
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
        self
    }
    /// A trim cap's height: the clip's (default `space(40)`).
    pub fn height(mut self, h: f32) -> Self {
        self.height = Some(h);
        self
    }
    /// The hot look.
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    /// The pointer gesture: Down, Move, Up with the travel since the press. Keep the starting
    /// value in [`HandleDrag::data`] on Down.
    pub fn on_drag(mut self, f: impl Fn(&HandleDrag, &mut Window, &mut App) + 'static) -> Self {
        self.on_drag = Some(Rc::new(f));
        self
    }
    /// Arrow keys while focused: `(dx, dy)`, 1 point or 10 with Shift, along the axes the handle
    /// moves.
    pub fn on_nudge(mut self, f: impl Fn(&(f32, f32), &mut Window, &mut App) + 'static) -> Self {
        self.on_nudge = Some(Rc::new(f));
        self
    }

    /// The handle's size: trim caps are `space(8)` × the height, crop bars `space(4)` ×
    /// `space(18)`, corners `space(18)` square.
    pub fn size(&self, t: &Paint) -> (f32, f32) {
        let thick = if self.crop { t.space(4.0) } else { t.space(8.0) };
        let len = t.space(18.0);
        if !self.crop {
            return (thick, self.height.unwrap_or(t.space(40.0)));
        }
        match self.edge {
            e if e.is_corner() => (len, len),
            Edge::Top | Edge::Bottom => (len, thick),
            _ => (thick, len),
        }
    }
}

impl RenderOnce for TrimHandle {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let (w, h) = self.size(&t);
        let small = t.tokens().radius_small() as f32;
        let accent = t.tokens().palette.accent;
        let (rest, ink) = (color(accent), color(hot_accent(&t, accent)));
        let focus = focus_handle(&self.id, window, cx);
        let hot = self.cursor || focus.is_focused(window) || dragging(&self.id, window, cx);
        let mut e = div()
            .id(self.id.clone())
            .relative()
            .flex_shrink_0()
            .w(px(w))
            .h(px(h))
            .group(HOT)
            .cursor(self.edge.cursor())
            .track_focus(&focus);
        if !self.crop {
            // The cap: rounded outside, square inside.
            let half = (w / 2.0).floor();
            e = e.child(piece(0.0, 0.0, w, h, small, rest, ink, hot));
            let inner_x = if self.edge == Edge::Right { 0.0 } else { w - half };
            e = e.child(piece(inner_x, 0.0, half, h, 0.0, rest, ink, hot));
            // The grip line, cut in the background color, longer when hot.
            let gw = 1f32.max(t.space(2.0));
            let grip = |frac: f32| {
                let gh = (h * frac).round();
                div()
                    .absolute()
                    .left(px(((w - gw) / 2.0).round()))
                    .top(px(((h - gh) / 2.0).round()))
                    .w(px(gw))
                    .h(px(gh))
                    .rounded(px(small.min(gw / 2.0)))
                    .bg(t.bg())
            };
            e = if hot {
                e.child(grip(0.6))
            } else {
                e.child(grip(0.4).group_hover(HOT, |s| s.opacity(0.0))).child(grip(0.6).opacity(0.0).group_hover(HOT, |s| s.opacity(1.0)))
            };
        } else {
            let thick = t.space(4.0);
            let r = small.min(thick / 2.0);
            let edge = self.edge;
            let r1 = [
                if matches!(edge, Edge::Bottom | Edge::Right | Edge::BottomRight) { r } else { 0.0 },
                if matches!(edge, Edge::Bottom | Edge::Left | Edge::BottomLeft) { r } else { 0.0 },
                if matches!(edge, Edge::Top | Edge::Left | Edge::TopLeft) { r } else { 0.0 },
                if matches!(edge, Edge::Top | Edge::Right | Edge::TopRight) { r } else { 0.0 },
            ];
            if edge.is_corner() || matches!(edge, Edge::Top | Edge::Bottom) {
                let y = if edge.is_corner() && edge.is_bottom() { h - thick } else { 0.0 };
                e = e.child(piece_radii(0.0, y, w, thick, r1, rest, ink, hot));
            } else {
                e = e.child(piece_radii(0.0, 0.0, thick, h, r1, rest, ink, hot));
            }
            if edge.is_corner() {
                let x = if edge.is_right() { w - thick } else { 0.0 };
                let r2 = [
                    if edge == Edge::BottomRight { r } else { 0.0 },
                    if edge == Edge::BottomLeft { r } else { 0.0 },
                    if edge == Edge::TopLeft { r } else { 0.0 },
                    if edge == Edge::TopRight { r } else { 0.0 },
                ];
                e = e.child(piece_radii(x, 0.0, thick, h, r2, rest, ink, hot));
            }
        }
        let (crop, edge) = (self.crop, self.edge);
        let nudge = self.on_nudge;
        e = e.on_key_down(move |ev: &KeyDownEvent, window, cx| {
            if tab_keys(ev, window, cx) {
                cx.stop_propagation();
                return;
            }
            let step = if ev.keystroke.modifiers.shift { 10.0 } else { 1.0 };
            let (mut dx, mut dy) = match ev.keystroke.key.as_str() {
                "left" => (-step, 0.0),
                "right" => (step, 0.0),
                "up" => (0.0, -step),
                "down" => (0.0, step),
                _ => return,
            };
            if !crop || matches!(edge, Edge::Left | Edge::Right) {
                dy = 0.0;
            }
            if crop && matches!(edge, Edge::Top | Edge::Bottom) {
                dx = 0.0;
            }
            if dx == 0.0 && dy == 0.0 {
                return;
            }
            if let Some(f) = &nudge {
                f(&(dx, dy), window, cx);
            }
            cx.stop_propagation();
        });
        let f = self.on_drag;
        let fh = focus.clone();
        let drag: DragFn = Rc::new(move |d, window, cx| {
            if d.phase == super::DragPhase::Down {
                fh.focus(window, cx);
            }
            if let Some(f) = &f {
                f(d, window, cx);
            }
        });
        follow_drag(e, &self.id, true, drag, window, cx)
    }
}

/// A crop rectangle: a 1px accent outline, rule-of-thirds guides in `track`, and the eight crop
/// [`TrimHandle`]s inside its edges. No fill, so it sits over the image.
#[derive(IntoElement)]
pub struct CropFrame {
    id: ElementId,
    width: Option<f32>,
    height: Option<f32>,
    cursor: Option<Edge>,
    on_drag: Option<Rc<dyn Fn(&(Edge, HandleDrag), &mut Window, &mut App)>>,
    on_nudge: Option<Rc<dyn Fn(&(Edge, f32, f32), &mut Window, &mut App)>>,
}

impl CropFrame {
    /// A frame `width` × `height` (`space(240)` × `space(140)` with [`CropFrame::default`]).
    pub fn new(width: f32, height: f32) -> CropFrame {
        CropFrame { width: Some(width), height: Some(height), ..CropFrame::default() }
    }
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
        self
    }
    /// Draw this handle hot, as if under the pointer.
    pub fn cursor(mut self, edge: Option<Edge>) -> Self {
        self.cursor = edge;
        self
    }
    /// A handle's gesture, as [`TrimHandle::on_drag`], with the handle's edge.
    pub fn on_drag(mut self, f: impl Fn(&(Edge, HandleDrag), &mut Window, &mut App) + 'static) -> Self {
        self.on_drag = Some(Rc::new(f));
        self
    }
    /// A handle's arrow keys, as [`TrimHandle::on_nudge`], with the handle's edge.
    pub fn on_nudge(mut self, f: impl Fn(&(Edge, f32, f32), &mut Window, &mut App) + 'static) -> Self {
        self.on_nudge = Some(Rc::new(f));
        self
    }
}

impl Default for CropFrame {
    fn default() -> Self {
        CropFrame { id: ElementId::Name("omarchy-crop-frame".into()), width: None, height: None, cursor: None, on_drag: None, on_nudge: None }
    }
}

impl RenderOnce for CropFrame {
    fn render(self, _: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let w = self.width.unwrap_or(t.space(240.0));
        let h = self.height.unwrap_or(t.space(140.0));
        let track = t.track();
        let mut e = div().relative().flex_shrink_0().w(px(w)).h(px(h));
        for i in 1..=2 {
            e = e.child(div().absolute().left(px((w * i as f32 / 3.0).round())).top_0().w(px(1.0)).h(px(h)).bg(track));
            e = e.child(div().absolute().left_0().top(px((h * i as f32 / 3.0).round())).w(px(w)).h(px(1.0)).bg(track));
        }
        e = e.child(div().absolute().top_0().left_0().size_full().border_1().border_color(t.accent()));
        for edge in Edge::ALL {
            let mut handle = TrimHandle::crop(edge)
                .id(gpui::ElementId::NamedChild(std::sync::Arc::new(self.id.clone()), edge.key().into()))
                .cursor(self.cursor == Some(edge));
            if let Some(f) = self.on_drag.clone() {
                handle = handle.on_drag(move |d, window, cx| f(&(edge, d.clone()), window, cx));
            }
            if let Some(f) = self.on_nudge.clone() {
                handle = handle.on_nudge(move |(dx, dy), window, cx| f(&(edge, *dx, *dy), window, cx));
            }
            let (hw, hh) = handle.size(&t);
            let x = if edge.is_left() {
                0.0
            } else if edge.is_right() {
                w - hw
            } else {
                ((w - hw) / 2.0).round()
            };
            let y = if edge.is_top() {
                0.0
            } else if edge.is_bottom() {
                h - hh
            } else {
                ((h - hh) / 2.0).round()
            };
            e = e.child(div().absolute().left(px(x)).top(px(y)).child(handle));
        }
        e
    }
}

/// The grip a row is dragged by to reorder it: a 2×3 grid of `space(3)` dots, textDim at rest
/// and the foreground under the pointer, with focus, while dragged or with `cursor`. Up/Down
/// while focused ask to move the row by one.
#[derive(IntoElement)]
pub struct DragHandle {
    id: ElementId,
    cursor: bool,
    on_drag: Option<DragFn>,
    on_move: Option<Handler<i32>>,
}

impl DragHandle {
    pub fn new(id: impl Into<ElementId>) -> DragHandle {
        DragHandle { id: id.into(), cursor: false, on_drag: None, on_move: None }
    }
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    /// The pointer gesture (Down, Move, Up; `dy` is travel since the press).
    pub fn on_drag(mut self, f: impl Fn(&HandleDrag, &mut Window, &mut App) + 'static) -> Self {
        self.on_drag = Some(Rc::new(f));
        self
    }
    /// Up/Down while focused: move the row by -1 or +1.
    pub fn on_move(mut self, f: impl Fn(&i32, &mut Window, &mut App) + 'static) -> Self {
        self.on_move = Some(Rc::new(f));
        self
    }
}

impl RenderOnce for DragHandle {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let (w, h) = (t.space(16.0), t.space(22.0));
        let dot = t.space(3.0);
        let gap = t.space(3.0);
        let r = (t.tokens().radius_small() as f32).min(dot / 2.0);
        let x0 = (w - (dot * 2.0 + gap)) / 2.0;
        let y0 = (h - (dot * 3.0 + gap * 2.0)) / 2.0;
        let focus = focus_handle(&self.id, window, cx);
        let held = dragging(&self.id, window, cx);
        let hot = self.cursor || held || focus.is_focused(window);
        let mut e = div()
            .id(self.id.clone())
            .relative()
            .flex_shrink_0()
            .w(px(w))
            .h(px(h))
            .group(HOT)
            .cursor(if held { CursorStyle::ClosedHand } else { CursorStyle::OpenHand })
            .track_focus(&focus);
        for i in 0..6 {
            let (c, row) = ((i % 2) as f32, (i / 2) as f32);
            e = e.child(piece(x0 + c * (dot + gap), y0 + row * (dot + gap), dot, dot, r, t.dim(), t.fg(), hot));
        }
        let mv = self.on_move;
        e = e.on_key_down(move |ev: &KeyDownEvent, window, cx| {
            if tab_keys(ev, window, cx) {
                cx.stop_propagation();
                return;
            }
            let delta = match ev.keystroke.key.as_str() {
                "up" => -1,
                "down" => 1,
                _ => return,
            };
            if let Some(f) = &mv {
                f(&delta, window, cx);
            }
            cx.stop_propagation();
        });
        let f = self.on_drag;
        let fh = focus.clone();
        let drag: DragFn = Rc::new(move |d, window, cx| {
            if d.phase == super::DragPhase::Down {
                fh.focus(window, cx);
            }
            if let Some(f) = &f {
                f(d, window, cx);
            }
        });
        follow_drag(e, &self.id, true, drag, window, cx)
    }
}

/// Where a dragged row will land: an accent line `max(1, space(2))` thick. Place it centered on
/// the boundary between two rows, over them (it takes no layout space: position it absolutely).
#[derive(IntoElement)]
pub struct DropIndicator {
    width: f32,
}

impl DropIndicator {
    pub fn new(width: f32) -> DropIndicator {
        DropIndicator { width }
    }
    /// The line's thickness.
    pub fn thickness(t: &Paint) -> f32 {
        1f32.max(t.space(2.0))
    }
}

impl RenderOnce for DropIndicator {
    fn render(self, _: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let h = DropIndicator::thickness(&t);
        div().w(px(self.width)).h(px(h)).rounded(px((t.tokens().radius_small() as f32).min(h / 2.0))).bg(t.accent())
    }
}
