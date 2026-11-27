//! TrimHandle, CropFrame, DragHandle and DropIndicator: the draggable parts of editors and
//! reorderable lists (`TrimHandle.qml`, `CropFrame.qml`, `DragHandle.qml`, `DropIndicator.qml`).

use std::rc::Rc;

use omarchy_theme::Rgba;
use rune::{Cursor, Drag, El, Key, Tx, el};

use crate::{Theme, color};

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
    fn cursor(self) -> Cursor {
        match self {
            Edge::Left | Edge::Right => Cursor::ResizeColumn,
            Edge::Top | Edge::Bottom => Cursor::ResizeRow,
            _ => Cursor::Move,
        }
    }
    fn key(self) -> &'static str {
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

/// The hot look of an accent handle: a quarter of the foreground mixed in (Qt's
/// `Qt.tint(accent, alpha(foreground, 0.25))`).
pub(crate) fn hot_accent(t: &Theme, base: Rgba) -> Rgba {
    base.mix(t.tokens().palette.foreground, 0.25)
}

type DragFn = Rc<dyn Fn(El) -> El>;

/// A draggable in/out handle: a trim cap on a clip's left or right end, or (with `crop`) one of
/// the eight handles of a crop rectangle. Accent, because it belongs to the chosen clip or region.
pub struct TrimHandle {
    edge: Edge,
    crop: bool,
    height: Option<f32>,
    cursor: bool,
    drag: Option<DragFn>,
    nudge: Option<DragFn>,
}

impl TrimHandle {
    /// A trim cap on `edge` (`Left` or `Right`).
    pub fn new(edge: Edge) -> TrimHandle {
        TrimHandle { edge, crop: false, height: None, cursor: false, drag: None, nudge: None }
    }
    /// A crop handle: a bar on a side, an L bracket on a corner.
    pub fn crop(edge: Edge) -> TrimHandle {
        TrimHandle { crop: true, ..TrimHandle::new(edge) }
    }
    /// A trim cap's height: the clip's (default `space(40)`).
    pub fn height(mut self, h: f32) -> Self {
        self.height = Some(h);
        self
    }
    /// The hot look (lighter, a longer grip).
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    /// The pointer gesture: Down, Move, Up with `dx`, `dy` since the press. Keep the starting
    /// value in [`Drag::data`] on Down.
    pub fn on_drag<S: 'static>(mut self, f: impl Fn(&mut S, &mut Drag) + 'static) -> Self {
        let f = Rc::new(f);
        self.drag = Some(Rc::new(move |e: El| {
            let f = f.clone();
            e.on_drag(move |s: &mut S, d, _tx| f(s, d))
        }));
        self
    }
    /// Arrow keys while focused: 1 point, 10 with Shift, along the axes the handle moves.
    pub fn on_nudge<S: 'static>(mut self, f: impl Fn(&mut S, f32, f32) + 'static) -> Self {
        let f = Rc::new(f);
        let (crop, edge) = (self.crop, self.edge);
        self.nudge = Some(Rc::new(move |e: El| {
            let f = f.clone();
            e.on_key(move |s: &mut S, k, _tx: &mut Tx| {
                let step = if k.mods.shift { 10.0 } else { 1.0 };
                let (mut dx, mut dy) = match k.key {
                    Key::Left => (-step, 0.0),
                    Key::Right => (step, 0.0),
                    Key::Up => (0.0, -step),
                    Key::Down => (0.0, step),
                    _ => return false,
                };
                if !crop || matches!(edge, Edge::Left | Edge::Right) {
                    dy = 0.0;
                }
                if crop && matches!(edge, Edge::Top | Edge::Bottom) {
                    dx = 0.0;
                }
                if dx == 0.0 && dy == 0.0 {
                    return false;
                }
                f(s, dx, dy);
                true
            })
        }));
        self
    }

    /// The handle's size: trim caps are `space(8)` × the height, crop bars `space(4)` ×
    /// `space(18)`, corners `space(18)` square.
    pub fn size(&self, t: &Theme) -> (f32, f32) {
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

    pub fn build(self, t: &Theme) -> El {
        let (w, h) = self.size(t);
        let small = t.radius_small();
        let accent = t.tokens().palette.accent;
        let ink = color(hot_accent(t, accent));
        let rest = color(accent);
        // Rest and hot colors cross-fade with the hover group (the Qt handle tints under the
        // pointer or keyboard focus).
        let mut e = el().w(w).h(h).hover_group_focus().focusable(true).cursor(self.edge.cursor()).key(format!("handle-{}", self.edge.key()));
        let piece = |x: f32, y: f32, pw: f32, ph: f32, corners: [f32; 4]| -> El {
            let base = el().absolute(x, y).size(pw, ph).corners(corners);
            if self.cursor {
                base.fill(ink)
            } else {
                base.fill(rest).child(el().pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0)).corners(corners).fill(ink).group_opacity(0.0, 1.0))
            }
        };
        if !self.crop {
            // The cap: rounded outside, square inside.
            let half = (w / 2.0).floor();
            e = e.child(piece(0.0, 0.0, w, h, [small; 4]));
            let inner_x = if self.edge == Edge::Right { 0.0 } else { w - half };
            e = e.child(piece(inner_x, 0.0, half, h, [0.0; 4]));
            // The grip line, cut in the background color, longer when hot.
            let gw = 1f32.max(t.space(2.0));
            let grip = |frac: f32| {
                let gh = (h * frac).round();
                el().absolute(((w - gw) / 2.0).round(), ((h - gh) / 2.0).round()).size(gw, gh).radius(small.min(gw / 2.0)).fill(t.bg())
            };
            e = if self.cursor {
                e.child(grip(0.6))
            } else {
                e.child(grip(0.4).group_opacity(1.0, 0.0)).child(grip(0.6).group_opacity(0.0, 1.0))
            };
        } else {
            // Outer corners flush against the 1px crop border stay square at r8 so they don't pull
            // away into pinched undercut notches.
            let thick = t.space(4.0);
            let r = small.min(thick / 2.0);
            let edge = self.edge;
            let bar_corners = [
                if matches!(edge, Edge::Bottom | Edge::Right | Edge::BottomRight) { r } else { 0.0 },
                if matches!(edge, Edge::Bottom | Edge::Left | Edge::BottomLeft) { r } else { 0.0 },
                if matches!(edge, Edge::Top | Edge::Left | Edge::TopLeft) { r } else { 0.0 },
                if matches!(edge, Edge::Top | Edge::Right | Edge::TopRight) { r } else { 0.0 },
            ];
            if edge.is_corner() || matches!(edge, Edge::Top | Edge::Bottom) {
                let y = if edge.is_corner() && edge.is_bottom() { h - thick } else { 0.0 };
                e = e.child(piece(0.0, y, w, thick, bar_corners));
            } else {
                e = e.child(piece(0.0, 0.0, thick, h, bar_corners));
            }
            if edge.is_corner() {
                let x = if edge.is_right() { w - thick } else { 0.0 };
                let leg_corners = [
                    if edge == Edge::BottomRight { r } else { 0.0 },
                    if edge == Edge::BottomLeft { r } else { 0.0 },
                    if edge == Edge::TopLeft { r } else { 0.0 },
                    if edge == Edge::TopRight { r } else { 0.0 },
                ];
                e = e.child(piece(x, 0.0, thick, h, leg_corners));
            }
        }
        if let Some(d) = &self.drag {
            e = d(e);
        }
        if let Some(n) = &self.nudge {
            e = n(e);
        }
        e
    }
}

/// A crop rectangle: a 1px accent outline, rule-of-thirds guides in `track`, and the eight crop
/// [`TrimHandle`]s inside its edges.
pub struct CropFrame {
    width: f32,
    height: f32,
    cursor: Option<Edge>,
    drag: Option<Rc<dyn Fn(TrimHandle, Edge) -> TrimHandle>>,
}

impl CropFrame {
    pub fn new(width: f32, height: f32) -> CropFrame {
        CropFrame { width, height, cursor: None, drag: None }
    }
    /// Draw this handle hot, as if under the pointer.
    pub fn cursor(mut self, edge: Option<Edge>) -> Self {
        self.cursor = edge;
        self
    }
    /// A handle's gesture, as [`TrimHandle::on_drag`], with the handle's edge.
    pub fn on_drag<S: 'static>(mut self, f: impl Fn(&mut S, Edge, &mut Drag) + 'static) -> Self {
        let f = Rc::new(f);
        self.drag = Some(Rc::new(move |h: TrimHandle, edge: Edge| {
            let f = f.clone();
            h.on_drag(move |s: &mut S, d: &mut Drag| f(s, edge, d))
        }));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let (w, h) = (self.width, self.height);
        let track = t.track();
        let mut e = el().w(w).h(h);
        for i in 1..=2 {
            e = e.child(el().absolute((w * i as f32 / 3.0).round(), 0.0).size(1.0, h).fill(track));
            e = e.child(el().absolute(0.0, (h * i as f32 / 3.0).round()).size(w, 1.0).fill(track));
        }
        e = e.child(el().pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0)).border(1.0, t.accent()));
        for edge in Edge::ALL {
            let mut handle = TrimHandle::crop(edge).cursor(self.cursor == Some(edge));
            if let Some(d) = &self.drag {
                handle = d(handle, edge);
            }
            let (hw, hh) = handle.size(t);
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
            e = e.child(handle.build(t).absolute(x, y));
        }
        e
    }
}

/// The grip a row is dragged by to reorder it: a 2×3 grid of `space(3)` dots, textDim at rest
/// and the foreground under the pointer, the keyboard cursor or focus.
pub struct DragHandle {
    cursor: bool,
    drag: Option<DragFn>,
    step: Option<DragFn>,
}

impl DragHandle {
    pub fn new() -> DragHandle {
        DragHandle { cursor: false, drag: None, step: None }
    }
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    /// The pointer gesture (Down, Move, Up; `dy` is travel since the press).
    pub fn on_drag<S: 'static>(mut self, f: impl Fn(&mut S, &mut Drag) + 'static) -> Self {
        let f = Rc::new(f);
        self.drag = Some(Rc::new(move |e: El| {
            let f = f.clone();
            e.on_drag(move |s: &mut S, d, _tx| f(s, d))
        }));
        self
    }
    /// Up/Down while focused: move the row by -1 or +1.
    pub fn on_move<S: 'static>(mut self, f: impl Fn(&mut S, i32) + 'static) -> Self {
        let f = Rc::new(f);
        self.step = Some(Rc::new(move |e: El| {
            let f = f.clone();
            e.on_key(move |s: &mut S, k, _tx: &mut Tx| match k.key {
                Key::Up => {
                    f(s, -1);
                    true
                }
                Key::Down => {
                    f(s, 1);
                    true
                }
                _ => false,
            })
        }));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let (w, h) = (t.space(16.0), t.space(22.0));
        let dot = t.space(3.0);
        let gap = t.space(3.0);
        let r = t.radius_small().min(dot / 2.0);
        let x0 = (w - (dot * 2.0 + gap)) / 2.0;
        let y0 = (h - (dot * 3.0 + gap * 2.0)) / 2.0;
        let mut e = el().w(w).h(h).hover_group_focus().focusable(true).cursor(Cursor::Grab).key("drag-handle");
        for i in 0..6 {
            let (c, r_) = ((i % 2) as f32, (i / 2) as f32);
            let d = el().absolute(x0 + c * (dot + gap), y0 + r_ * (dot + gap)).size(dot, dot).radius(r);
            e = e.child(if self.cursor {
                d.fill(t.fg())
            } else {
                d.fill(t.dim()).child(el().pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0)).radius(r).fill(t.fg()).group_opacity(0.0, 1.0))
            });
        }
        if let Some(d) = &self.drag {
            e = d(e);
        }
        if let Some(s) = &self.step {
            e = s(e);
        }
        e
    }
}

impl Default for DragHandle {
    fn default() -> Self {
        DragHandle::new()
    }
}

/// Where a dragged row will land: an accent line `max(1, space(2))` thick. Place it centered on
/// the boundary between two rows, over them (it takes no layout space).
pub struct DropIndicator {
    width: f32,
}

impl DropIndicator {
    pub fn new(width: f32) -> DropIndicator {
        DropIndicator { width }
    }
    /// The line's thickness.
    pub fn thickness(t: &Theme) -> f32 {
        1f32.max(t.space(2.0))
    }
    pub fn build(self, t: &Theme) -> El {
        let h = DropIndicator::thickness(t);
        el().w(self.width).h(h).radius(t.radius_small().min(h / 2.0)).fill(t.accent())
    }
}
