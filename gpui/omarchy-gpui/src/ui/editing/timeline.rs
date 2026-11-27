//! Timeline and Scrubber: `Timeline.qml` and `Scrubber.qml`.

use std::rc::Rc;

use gpui::prelude::*;
use gpui::{App, ClickEvent, CursorStyle, ElementId, KeyDownEvent, ScrollDelta, ScrollEvent, SharedString, Window, div, px};
use omarchy_theme::BorderSpec;

use super::handles::{Edge, TrimHandle};
use super::{DragFn, DragPhase, HOT, dragging, follow_drag, hot_accent, text_width};
use crate::ui::{Handler, Look, chrome, focus_handle, label, label_elided, tab_keys};
use crate::{Paint, color};

/// One clip on a track, in seconds.
#[derive(Clone, Debug, PartialEq)]
pub struct Clip {
    pub start: f32,
    pub end: f32,
    pub label: SharedString,
}

impl Clip {
    pub fn new(start: f32, end: f32, label: impl Into<SharedString>) -> Clip {
        Clip { start, end, label: label.into() }
    }
}

/// The visible window: what turns points into seconds.
#[derive(Clone, Copy, Debug)]
pub struct TimelineView {
    pub duration: f32,
    pub zoom: f32,
    pub start: f32,
    pub width: f32,
}

impl TimelineView {
    pub fn pixels_per_second(&self) -> f32 {
        self.width * self.zoom / self.duration.max(0.001)
    }
    pub fn x_at(&self, time: f32) -> f32 {
        ((time - self.start) * self.pixels_per_second()).round()
    }
    pub fn time_at(&self, x: f32) -> f32 {
        (self.start + x / self.pixels_per_second()).clamp(0.0, self.duration)
    }
    fn visible(&self) -> f32 {
        self.duration / self.zoom
    }
    /// Clamp a zoom and start so the window stays inside the duration.
    pub fn clamped(self, zoom: f32, start: f32, max_zoom: f32) -> TimelineView {
        let zoom = zoom.clamp(1.0, max_zoom);
        let visible = self.duration / zoom;
        TimelineView { zoom, start: start.clamp(0.0, (self.duration - visible).max(0.0)), ..self }
    }
}

/// A trim: a selected clip's edge (`Left` or `Right`) dragged or nudged to `time`.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Trim {
    pub track: usize,
    pub clip: usize,
    pub edge: Edge,
    pub time: f32,
}

/// A time ruler over clip tracks, with a playhead. Press or drag on the ruler or an empty lane to
/// scrub, press a clip to select it, drag a trim handle to trim, turn the wheel to zoom around
/// the pointer. Left/Right move the playhead a second (Shift: a major step), Home/End jump to
/// the ends.
#[derive(IntoElement)]
pub struct Timeline {
    id: ElementId,
    duration: f32,
    position: f32,
    tracks: Vec<Vec<Clip>>,
    selected: Option<(usize, usize)>,
    zoom: f32,
    start: f32,
    max_zoom: f32,
    width: Option<f32>,
    height: Option<f32>,
    cursor: bool,
    on_seek: Option<Handler<f32>>,
    on_select: Option<Handler<(usize, usize)>>,
    on_trim: Option<Handler<Trim>>,
    on_view: Option<Handler<(f32, f32)>>,
}

impl Timeline {
    pub fn new(duration: f32) -> Timeline {
        Timeline {
            id: ElementId::Name("omarchy-timeline".into()),
            duration,
            position: 0.0,
            tracks: Vec::new(),
            selected: None,
            zoom: 1.0,
            start: 0.0,
            max_zoom: 64.0,
            width: None,
            height: None,
            cursor: false,
            on_seek: None,
            on_select: None,
            on_trim: None,
            on_view: None,
        }
    }
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
        self
    }
    /// The playhead, in seconds.
    pub fn position(mut self, t: f32) -> Self {
        self.position = t;
        self
    }
    pub fn track(mut self, clips: impl IntoIterator<Item = Clip>) -> Self {
        self.tracks.push(clips.into_iter().collect());
        self
    }
    /// The selected clip: its track and index.
    pub fn selected(mut self, sel: Option<(usize, usize)>) -> Self {
        self.selected = sel;
        self
    }
    /// 1 shows the whole duration; more zooms in. `start` is the time at the left edge.
    pub fn zoom(mut self, zoom: f32, start: f32) -> Self {
        self.zoom = zoom;
        self.start = start;
        self
    }
    pub fn max_zoom(mut self, z: f32) -> Self {
        self.max_zoom = z;
        self
    }
    /// Default `space(720)` × `space(160)`.
    pub fn size(mut self, w: f32, h: f32) -> Self {
        self.width = Some(w);
        self.height = Some(h);
        self
    }
    /// The hot playhead look.
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    /// Scrubbing: the time under the pointer on the ruler or an empty lane, or a key's step.
    pub fn on_seek(mut self, f: impl Fn(&f32, &mut Window, &mut App) + 'static) -> Self {
        self.on_seek = Some(Rc::new(f));
        self
    }
    /// A clip was pressed: its track and index.
    pub fn on_select(mut self, f: impl Fn(&(usize, usize), &mut Window, &mut App) + 'static) -> Self {
        self.on_select = Some(Rc::new(f));
        self
    }
    /// A trim handle of the selected clip was dragged, or nudged with the arrow keys (a point a
    /// press).
    pub fn on_trim(mut self, f: impl Fn(&Trim, &mut Window, &mut App) + 'static) -> Self {
        self.on_trim = Some(Rc::new(f));
        self
    }
    /// The wheel zoomed: the new zoom and left-edge time.
    pub fn on_view(mut self, f: impl Fn(&(f32, f32), &mut Window, &mut App) + 'static) -> Self {
        self.on_view = Some(Rc::new(f));
        self
    }
}

/// The labeled tick step: the first of 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30 s, 1, 2, 5, 10, 15,
/// 30 min, 1 h at least `min_px` apart.
fn tick_step(pps: f32, min_px: f32) -> f32 {
    let target = min_px / pps.max(0.001);
    const STEPS: [f32; 16] = [0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0, 900.0, 1800.0, 3600.0];
    STEPS.into_iter().find(|s| *s >= target).unwrap_or(3600.0)
}

/// m:ss, with tenths when the step is under a second.
fn format_ruler(seconds: f32, step: f32) -> String {
    let s = (seconds.max(0.0) * 10.0).round() / 10.0;
    let m = (s / 60.0).floor();
    let rest = s - m * 60.0;
    let whole = rest.floor();
    let mut out = format!("{}:{:02}", m as i64, whole as i64);
    if step < 1.0 {
        out.push_str(&format!(".{}", ((rest - whole) * 10.0).round() as i64));
    }
    out
}

/// m:ss, or h:mm:ss from an hour.
pub(crate) fn format_clock(seconds: f32) -> String {
    let s = seconds.max(0.0).floor() as i64;
    let (h, m, sec) = (s / 3600, (s % 3600) / 60, s % 60);
    if h > 0 { format!("{h}:{m:02}:{sec:02}") } else { format!("{m}:{sec:02}") }
}

impl RenderOnce for Timeline {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let w = self.width.unwrap_or(t.space(720.0));
        let h = self.height.unwrap_or(t.space(160.0));
        let view = TimelineView { duration: self.duration, zoom: self.zoom, start: self.start, width: w };
        let pps = view.pixels_per_second();
        let ruler_h = t.space(24.0);
        let lane_gap = sp.xs as f32;
        let inset = sp.xxs as f32;
        let small = tk.radius_small() as f32;
        let n = self.tracks.len();
        let lane_h = if n > 0 { ((h - ruler_h - lane_gap * (n as f32 - 1.0)) / n as f32).floor() } else { 0.0 };
        let step = tick_step(pps, t.space(90.0));
        let end = self.start + view.visible();
        let focus = focus_handle(&self.id, window, cx);

        let mut e = div().id(self.id.clone()).relative().flex_shrink_0().w(px(w)).h(px(h)).overflow_hidden().track_focus(&focus);
        let at = |x: f32, y: f32, ww: f32, hh: f32| div().absolute().left(px(x)).top(px(y)).w(px(ww)).h(px(hh));

        // ---- ruler: minor and major ticks, then labels
        let minor = step / 5.0;
        let first = (self.start / minor).floor().max(0.0) as i64;
        let last = (end / minor).ceil() as i64;
        for i in first..=last {
            let time = i as f32 * minor;
            if time > self.duration + 1e-4 {
                break;
            }
            let th = if i % 5 == 0 { t.space(6.0) } else { t.space(3.0) };
            let x = view.x_at(time).min(w - 1.0);
            e = e.child(at(x, ruler_h - th, 1.0, th).bg(t.faint()));
        }
        let caption = tk.font.caption;
        let first = ((self.start / step) - 1e-4).ceil().max(0.0) as i64;
        let last = ((end.min(self.duration) / step) + 1e-4).floor() as i64;
        for i in first..=last {
            let time = i as f32 * step;
            let text = format_ruler(time, step);
            let lw = text_width(window, &t, &text, caption).ceil();
            let x = (view.x_at(time) - (lw / 2.0).round()).min(w - lw).max(0.0);
            e = e.child(label(&t, text, caption, false, t.dim()).absolute().left(px(x)).top(px(sp.xxs as f32)));
        }

        // ---- lanes and clips
        let c = &tk.controls;
        let label_size = tk.font.body_small;
        let label_h = tk.font.line_px(label_size) as f32;
        let pad = sp.control_padding_x as f32;
        for (ti, clips) in self.tracks.iter().enumerate() {
            let ly = ruler_h + ti as f32 * (lane_h + lane_gap);
            e = e.child(at(0.0, ly, w, lane_h).rounded(px(small)).bg(color(c.normal.fill)));
            for (ci, clip) in clips.iter().enumerate() {
                let selected = self.selected == Some((ti, ci));
                let x = view.x_at(clip.start);
                let cw = view.x_at(clip.end) - x;
                let ch = lane_h - inset * 2.0;
                let (rest, hot) = if selected {
                    let look = Look::new(
                        c.selected.fill,
                        BorderSpec { color: tk.palette.accent, widths: [1f64.max(t.space(2.0) as f64); 4], gradient: None },
                    );
                    (look.clone(), look)
                } else {
                    (Look::new(c.normal.fill, c.normal.border.clone()), Look::new(c.hover.fill, c.hover.border.clone()))
                };
                let label_inset = if selected { t.space(8.0) + sp.md as f32 } else { pad };
                let text = label_elided(&t, clip.label.clone(), label_size, selected, t.fg())
                    .absolute()
                    .left(px(label_inset))
                    .top(px(((ch - label_h) / 2.0).floor()))
                    .w(px((cw - label_inset * 2.0).max(0.0)));
                let clip_id = ElementId::NamedChild(std::sync::Arc::new(self.id.clone()), format!("clip-{ti}-{ci}").into());
                let base = div().id(clip_id).size_full().cursor(CursorStyle::PointingHand);
                let mut body = chrome(base, &t, &rest, &hot, selected, px(small)).child(text);
                // A press on a clip selects it rather than seeking.
                body = body.on_mouse_down(gpui::MouseButton::Left, |_, _, cx| cx.stop_propagation());
                if let Some(f) = self.on_select.clone() {
                    body = body.on_click(move |_: &ClickEvent, window, cx| f(&(ti, ci), window, cx));
                }
                if selected {
                    for edge in [Edge::Left, Edge::Right] {
                        let origin = if edge == Edge::Left { clip.start } else { clip.end };
                        let mut handle = TrimHandle::new(edge)
                            .id(ElementId::NamedChild(std::sync::Arc::new(self.id.clone()), format!("trim-{ti}-{ci}-{}", edge.key()).into()))
                            .height(ch);
                        if let Some(f) = self.on_trim.clone() {
                            let duration = view.duration;
                            let nudge = f.clone();
                            handle = handle
                                .on_drag(move |d, window, cx| {
                                    if d.phase == DragPhase::Down {
                                        d.data.set(origin);
                                        return;
                                    }
                                    let time = (d.data.get() + d.dx / pps).clamp(0.0, duration);
                                    f(&Trim { track: ti, clip: ci, edge, time }, window, cx);
                                })
                                .on_nudge(move |(dx, _), window, cx| {
                                    let time = (origin + dx / pps).clamp(0.0, duration);
                                    nudge(&Trim { track: ti, clip: ci, edge, time }, window, cx);
                                });
                        }
                        let hw = handle.size(&t).0;
                        let hx = if edge == Edge::Left { 0.0 } else { cw - hw };
                        body = body.child(div().absolute().left(px(hx)).top_0().child(handle));
                    }
                }
                e = e.child(at(x, ly + inset, cw, ch).child(body));
            }
        }

        // ---- playhead
        if self.position >= self.start && self.position <= end {
            let line_w = 1f32.max(t.space(2.0));
            let head = t.space(10.0);
            let accent = tk.palette.accent;
            let hot = self.cursor || focus.is_focused(window);
            let ink = if hot { color(hot_accent(&t, accent)) } else { color(accent) };
            let x = view.x_at(self.position) - (head / 2.0).round();
            e = e
                .child(at(x, ruler_h - head, head, head).rounded(px(small)).bg(ink))
                .child(at(x + ((head - line_w) / 2.0).round(), ruler_h, line_w, h - ruler_h).bg(ink));
        }

        // ---- keys, wheel, scrubbing
        let (seek, position, duration) = (self.on_seek.clone(), self.position, self.duration);
        e = e.on_key_down(move |ev: &KeyDownEvent, window, cx| {
            if tab_keys(ev, window, cx) {
                cx.stop_propagation();
                return;
            }
            // Shift: the labeled step, as Qt's `tickStep`.
            let step = if ev.keystroke.modifiers.shift { step } else { 1.0 };
            let to = match ev.keystroke.key.as_str() {
                "left" => position - step,
                "right" => position + step,
                "home" => 0.0,
                "end" => duration,
                _ => return,
            };
            if let Some(f) = &seek {
                f(&to.clamp(0.0, duration), window, cx);
            }
            cx.stop_propagation();
        });
        if let Some(f) = self.on_view.clone() {
            let max_zoom = self.max_zoom;
            let bounds = super::drag_bounds(&self.id, window, cx);
            e = e.on_scroll(move |ev: &ScrollEvent, window, cx| {
                let steps = match ev.delta {
                    ScrollDelta::Lines(p) => p.y,
                    ScrollDelta::Pixels(p) => f32::from(p.y) / 40.0,
                };
                if steps == 0.0 {
                    return;
                }
                let Some(b) = bounds.get() else { return };
                let x = f32::from(ev.position.x - b.left());
                let anchor = view.time_at(x);
                let z = (view.zoom * 1.25f32.powf(steps)).clamp(1.0, max_zoom);
                let next = view.clamped(z, anchor - x / (view.width * z / view.duration.max(0.001)), max_zoom);
                f(&(next.zoom, next.start), window, cx);
                cx.stop_propagation();
            });
        }
        let seek = self.on_seek.clone();
        let fh = focus.clone();
        let drag: DragFn = Rc::new(move |d, window, cx| {
            if d.phase == DragPhase::Down {
                fh.focus(window, cx);
            }
            if d.phase != DragPhase::Up
                && let Some(f) = &seek
            {
                f(&view.time_at(d.x), window, cx);
            }
        });
        follow_drag(e, &self.id, false, drag, window, cx)
    }
}

/// A media transport scrub: elapsed time, a one-track position bar (the Slider's metrics, the
/// unplayed part in `track`, the played part in the foreground), total time. Pressing or
/// dragging the bar seeks; Left/Right step 5 s (Shift 30 s), Home/End jump to the ends.
#[derive(IntoElement)]
pub struct Scrubber {
    id: ElementId,
    position: f32,
    duration: f32,
    width: Option<f32>,
    cursor: bool,
    on_seek: Option<Handler<f32>>,
}

impl Scrubber {
    pub fn new(position: f32, duration: f32) -> Scrubber {
        Scrubber { id: ElementId::Name("omarchy-scrubber".into()), position, duration, width: None, cursor: false, on_seek: None }
    }
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
        self
    }
    /// Default `space(360)`.
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    /// The hot knob.
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    pub fn on_seek(mut self, f: impl Fn(&f32, &mut Window, &mut App) + 'static) -> Self {
        self.on_seek = Some(Rc::new(f));
        self
    }
}

impl RenderOnce for Scrubber {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let w = self.width.unwrap_or(t.space(360.0));
        let track_h = 4f32.max((sp.control_height as f32 * 0.11).round());
        let knob = 14f32.max((sp.control_height as f32 * 0.38).round());
        let h = t.space(22.0).max(knob + sp.md as f32);
        let size = tk.font.body_small;
        let line = tk.font.line_px(size) as f32;
        let elapsed = format_clock(self.position);
        let total = format_clock(self.duration);
        let ew = text_width(window, &t, &elapsed, size);
        let tw = text_width(window, &t, &total, size);
        let gap = sp.control_gap as f32;
        let bar_x = ew + gap;
        let bar_w = w - tw - gap - bar_x;
        let track_w = bar_w - knob;
        let progress = if self.duration > 0.0 { (self.position / self.duration).clamp(0.0, 1.0) } else { 0.0 };
        let mid = h / 2.0;
        let ty = ((h - line) / 2.0).floor();
        let focus = focus_handle(&self.id, window, cx);
        let hot = self.cursor || focus.is_focused(window) || dragging(&self.id, window, cx);

        let ring = 1f32.max(t.space(2.0));
        let surface = color(tk.surfaces.popups.background);
        let played = (track_w * progress).round();
        let knob_at = |scale: f32| {
            let k = (knob * scale).round();
            div()
                .absolute()
                .left(px(played + (knob - k) / 2.0))
                .top(px(mid - k / 2.0))
                .size(px(k))
                .rounded(px(k / 2.0))
                .bg(t.fg())
                .border(px(ring))
                .border_color(surface)
        };
        let bar_line = |ww: f32, c| div().absolute().left(px(knob / 2.0)).top(px(mid - track_h / 2.0)).w(px(ww)).h(px(track_h)).rounded(px(track_h / 2.0)).bg(c);
        let mut bar = div()
            .id(self.id.clone())
            .absolute()
            .left(px(bar_x))
            .top_0()
            .w(px(bar_w))
            .h(px(h))
            .group(HOT)
            .cursor(CursorStyle::PointingHand)
            .track_focus(&focus)
            .child(bar_line(track_w, t.track()))
            .child(bar_line(played, t.fg()));
        bar = if hot {
            bar.child(knob_at(1.15))
        } else {
            bar.child(knob_at(1.0).group_hover(HOT, |s| s.opacity(0.0))).child(knob_at(1.15).opacity(0.0).group_hover(HOT, |s| s.opacity(1.0)))
        };
        let (seek, position, duration) = (self.on_seek.clone(), self.position, self.duration);
        bar = bar.on_key_down(move |ev: &KeyDownEvent, window, cx| {
            if tab_keys(ev, window, cx) {
                cx.stop_propagation();
                return;
            }
            let step = if ev.keystroke.modifiers.shift { 30.0 } else { 5.0 };
            let to = match ev.keystroke.key.as_str() {
                "left" => position - step,
                "right" => position + step,
                "home" => 0.0,
                "end" => duration,
                _ => return,
            };
            if let Some(f) = &seek {
                f(&to.clamp(0.0, duration), window, cx);
            }
            cx.stop_propagation();
        });
        let seek = self.on_seek.clone();
        let fh = focus.clone();
        let inset = knob / 2.0;
        let drag: DragFn = Rc::new(move |d, window, cx| {
            if d.phase == DragPhase::Down {
                fh.focus(window, cx);
            }
            if d.phase != DragPhase::Up
                && let Some(f) = &seek
            {
                let frac = ((d.x - inset) / (d.w - inset * 2.0).max(1.0)).clamp(0.0, 1.0);
                f(&(frac * duration), window, cx);
            }
        });
        let bar = follow_drag(bar, &self.id, false, drag, window, cx);
        div()
            .relative()
            .flex_shrink_0()
            .w(px(w))
            .h(px(h))
            .child(label(&t, elapsed, size, false, t.dim()).absolute().left_0().top(px(ty)))
            .child(bar)
            .child(label(&t, total, size, false, t.dim()).absolute().left(px(w - tw)).top(px(ty)))
    }
}
