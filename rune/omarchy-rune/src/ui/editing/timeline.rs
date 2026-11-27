//! Timeline and Scrubber: `Timeline.qml` and `Scrubber.qml`.

use std::rc::Rc;

use omarchy_theme::BorderSpec;
use rune::{Cursor, Drag, El, Key, Phase, Tx, el, text};

use super::handles::{Edge, TrimHandle, hot_accent};
use crate::ui::{Look, chrome};
use crate::{Theme, color};

/// One clip on a track, in seconds.
#[derive(Clone, Debug, PartialEq)]
pub struct Clip {
    pub start: f32,
    pub end: f32,
    pub label: String,
}

impl Clip {
    pub fn new(start: f32, end: f32, label: impl Into<String>) -> Clip {
        Clip { start, end, label: label.into() }
    }
}

/// The visible window: what the handlers need to turn points into seconds.
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

/// Attaches the scrubbing handlers: the element, the view, the playhead and the ruler's labeled step.
type Seek = Rc<dyn Fn(El, TimelineView, f32, f32) -> El>;
type Select = Rc<dyn Fn(El, usize, usize) -> El>;
type Trim = Rc<dyn Fn(TrimHandle, usize, usize, Edge, f32, TimelineView) -> TrimHandle>;
type ViewFn = Rc<dyn Fn(El, TimelineView, f32) -> El>;

/// A time ruler over clip tracks, with a playhead. Press or drag on the ruler or an empty lane
/// to scrub, press a clip to select it, drag a trim handle to trim, turn the wheel to zoom
/// around the pointer. Left/Right move the playhead a second (Shift: a major step), Home/End
/// jump to the ends.
pub struct Timeline {
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
    seek: Option<Seek>,
    select: Option<Select>,
    trim: Option<Trim>,
    view: Option<ViewFn>,
}

impl Timeline {
    pub fn new(duration: f32) -> Timeline {
        Timeline {
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
            seek: None,
            select: None,
            trim: None,
            view: None,
        }
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
    pub fn on_seek<S: 'static>(mut self, f: impl Fn(&mut S, f32) + 'static) -> Self {
        let f = Rc::new(f);
        self.seek = Some(Rc::new(move |e: El, view: TimelineView, position: f32, major: f32| {
            let drag = f.clone();
            let key = f.clone();
            e.on_drag(move |s: &mut S, d: &mut Drag, _tx| drag(s, view.time_at(d.x))).on_key(move |s: &mut S, k, _tx: &mut Tx| {
                let step = if k.mods.shift { major } else { 1.0 };
                let to = match k.key {
                    Key::Left => position - step,
                    Key::Right => position + step,
                    Key::Home => 0.0,
                    Key::End => view.duration,
                    _ => return false,
                };
                key(s, to.clamp(0.0, view.duration));
                true
            })
        }));
        self
    }
    /// A clip was pressed: its track and index.
    pub fn on_select<S: 'static>(mut self, f: impl Fn(&mut S, usize, usize) + 'static) -> Self {
        let f = Rc::new(f);
        self.select = Some(Rc::new(move |e: El, track: usize, clip: usize| {
            let f = f.clone();
            e.on_press(move |s: &mut S| f(s, track, clip))
        }));
        self
    }
    /// A trim handle of the selected clip was dragged or nudged with the arrow keys (a point a
    /// press): its track, index, edge (`Left` or `Right`) and the new time for that edge.
    pub fn on_trim<S: 'static>(mut self, f: impl Fn(&mut S, usize, usize, Edge, f32) + 'static) -> Self {
        let f = Rc::new(f);
        self.trim = Some(Rc::new(move |h: TrimHandle, track: usize, clip: usize, edge: Edge, origin: f32, view: TimelineView| {
            let (drag, nudge) = (f.clone(), f.clone());
            h.on_drag(move |s: &mut S, d: &mut Drag| {
                if d.phase == Phase::Down {
                    d.data = origin;
                    return;
                }
                let to = (d.data + d.dx / view.pixels_per_second()).clamp(0.0, view.duration);
                drag(s, track, clip, edge, to);
            })
            .on_nudge(move |s: &mut S, dx, _dy| {
                nudge(s, track, clip, edge, (origin + dx / view.pixels_per_second()).clamp(0.0, view.duration));
            })
        }));
        self
    }
    /// The wheel zoomed: the new zoom and left-edge time.
    pub fn on_view<S: 'static>(mut self, f: impl Fn(&mut S, f32, f32) + 'static) -> Self {
        let f = Rc::new(f);
        self.view = Some(Rc::new(move |e: El, view: TimelineView, max_zoom: f32| {
            let f = f.clone();
            e.on_wheel(move |s: &mut S, w, _tx| {
                let steps = w.dy / 40.0;
                if steps == 0.0 {
                    return false;
                }
                let anchor = view.time_at(w.x);
                let z = (view.zoom * 1.25f32.powf(steps)).clamp(1.0, max_zoom);
                let next = view.clamped(z, anchor - w.x / (view.width * z / view.duration.max(0.001)), max_zoom);
                f(s, next.zoom, next.start);
                true
            })
        }));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let w = self.width.unwrap_or(t.space(720.0));
        let h = self.height.unwrap_or(t.space(160.0));
        let view = TimelineView { duration: self.duration, zoom: self.zoom, start: self.start, width: w };
        let pps = view.pixels_per_second();
        let ruler_h = t.space(24.0);
        let lane_gap = sp.xs as f32;
        let inset = sp.xxs as f32;
        let small = t.radius_small();
        let n = self.tracks.len();
        let lane_h = if n > 0 { ((h - ruler_h - lane_gap * (n as f32 - 1.0)) / n as f32).floor() } else { 0.0 };
        let step = tick_step(pps, t.space(90.0));
        let end = self.start + view.visible();

        let mut e = el().w(w).h(h).focusable(true).role("slider").key("timeline");

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
            e = e.child(el().absolute(x, ruler_h - th).size(1.0, th).fill(t.faint()));
        }
        let style = t.text_style(tk.font.caption);
        let first = ((self.start / step) - 1e-4).ceil().max(0.0) as i64;
        let last = ((end.min(self.duration) / step) + 1e-4).floor() as i64;
        for i in first..=last {
            let time = i as f32 * step;
            let label = format_ruler(time, step);
            let lw = rune::text::advance(&label, style).ceil();
            let x = (view.x_at(time) - (lw / 2.0).round()).min(w - lw).max(0.0);
            e = e.child(text(label).font(style).color(t.dim()).single_line().absolute(x, sp.xxs as f32));
        }

        // ---- lanes and clips (clipped below the ruler so the playhead head stays unclipped)
        let c = &tk.controls;
        let label_size = tk.font.body_small;
        let label_h = tk.font.line_px(label_size) as f32;
        let pad = sp.control_padding_x as f32;
        let mut lanes = el().absolute(0.0, ruler_h).size(w, (h - ruler_h).max(0.0)).clip_rounded(0.0);
        for (ti, clips) in self.tracks.iter().enumerate() {
            let ly = ti as f32 * (lane_h + lane_gap);
            lanes = lanes.child(el().absolute(0.0, ly).size(w, lane_h).radius(small).fill(color(c.normal.fill)));
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
                let label_style = if selected { t.text_style_bold(label_size) } else { t.text_style(label_size) };
                let label_inset = if selected { t.space(8.0) + pad } else { pad };
                let label = text(clip.label.clone())
                    .font(label_style)
                    .color(t.fg())
                    .single_line()
                    .truncate()
                    .absolute(label_inset, ((ch - label_h) / 2.0).floor())
                    .w((cw - label_inset * 2.0).max(0.0));
                let base = el().absolute(x, ly + inset).size(cw, ch).cursor(Cursor::Pointer).key(format!("clip-{ti}-{ci}"));
                let base = match &self.select {
                    Some(sel) => sel(base, ti, ci),
                    None => base,
                };
                let mut children = vec![label];
                if selected {
                    for edge in [Edge::Left, Edge::Right] {
                        let mut handle = TrimHandle::new(edge).height(ch);
                        if let Some(trim) = &self.trim {
                            let origin = if edge == Edge::Left { clip.start } else { clip.end };
                            handle = trim(handle, ti, ci, edge, origin, view);
                        }
                        let hw = handle.size(t).0;
                        let hx = if edge == Edge::Left { 0.0 } else { cw - hw };
                        children.push(handle.build(t).absolute(hx, 0.0));
                    }
                }
                lanes = lanes.child(chrome(t, base, children, &rest, &hot, selected, small));
            }
        }
        e = e.child(lanes);

        // ---- playhead
        if self.position >= self.start && self.position <= end {
            let line_w = 1f32.max(t.space(2.0));
            let head_w = t.space(10.0);
            let head_h = t.space(10.0);
            let accent = tk.palette.accent;
            let ink = if self.cursor { color(hot_accent(t, accent)) } else { color(accent) };
            let x = view.x_at(self.position) - (head_w / 2.0).round();
            let y = ruler_h - head_h;
            let playhead_bottom = if n > 0 { ruler_h + (n - 1) as f32 * (lane_h + lane_gap) + lane_h } else { h };
            e = e
                .child(el().absolute(x, y).size(head_w, head_h).radius(small).fill(ink))
                .child(el().absolute(x + ((head_w - line_w) / 2.0).round(), ruler_h).size(line_w, (playhead_bottom - ruler_h).max(0.0)).fill(ink));
        }

        if let Some(seek) = &self.seek {
            // Shift+arrow steps by the labeled ruler step, as Qt's tickStep.
            e = seek(e, view, self.position, tick_step(view.pixels_per_second(), t.space(90.0)));
        }
        if let Some(v) = &self.view {
            e = v(e, view, self.max_zoom);
        }
        e
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

/// A media transport scrub: elapsed time, a one-track position bar (the Slider's metrics, the
/// unplayed part in `track`, the played part in the foreground), total time.
pub struct Scrubber {
    position: f32,
    duration: f32,
    width: Option<f32>,
    cursor: bool,
    seek: Option<Rc<dyn Fn(El, f32, f32, f32) -> El>>,
}

impl Scrubber {
    pub fn new(position: f32, duration: f32) -> Scrubber {
        Scrubber { position, duration, width: None, cursor: false, seek: None }
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
    /// Pressing or dragging the bar, or Left/Right (5 s, Shift 30 s), Home/End.
    pub fn on_seek<S: 'static>(mut self, f: impl Fn(&mut S, f32) + 'static) -> Self {
        let f = Rc::new(f);
        self.seek = Some(Rc::new(move |e: El, inset: f32, duration: f32, position: f32| {
            let drag = f.clone();
            let key = f.clone();
            e.on_drag(move |s: &mut S, d: &mut Drag, _tx| {
                let frac = ((d.x - inset) / (d.w - inset * 2.0).max(1.0)).clamp(0.0, 1.0);
                drag(s, frac * duration)
            })
            .on_key(move |s: &mut S, k, _tx: &mut Tx| {
                let step = if k.mods.shift { 30.0 } else { 5.0 };
                let to = match k.key {
                    Key::Left => position - step,
                    Key::Right => position + step,
                    Key::Home => 0.0,
                    Key::End => duration,
                    _ => return false,
                };
                key(s, to.clamp(0.0, duration));
                true
            })
        }));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let w = self.width.unwrap_or(t.space(360.0));
        let track_h = 4f32.max((sp.control_height as f32 * 0.11).round());
        let knob = 14f32.max((sp.control_height as f32 * 0.38).round());
        let h = t.space(22.0).max(knob + sp.md as f32);
        let style = t.text_style(tk.font.body_small);
        let line = tk.font.line_px(tk.font.body_small) as f32;
        let elapsed = format_clock(self.position);
        let total = format_clock(self.duration);
        let ew = rune::text::advance(&elapsed, style);
        let tw = rune::text::advance(&total, style);
        let gap = sp.control_gap as f32;
        let bar_x = ew + gap;
        let bar_w = w - tw - gap - bar_x;
        let track_w = bar_w - knob;
        let progress = if self.duration > 0.0 { (self.position / self.duration).clamp(0.0, 1.0) } else { 0.0 };
        let mid = h / 2.0;
        let ty = ((h - line) / 2.0).floor();

        let ring = 1f32.max(t.space(2.0));
        let inner_knob = 4f32.max(knob - ring * 2.0);
        let track_x = knob / 2.0;
        let knob_cx = track_x + (track_w * progress).round();
        let fill_w = (knob_cx - knob / 2.0 - track_x).clamp(0.0, track_w);
        let unfilled_start = (knob_cx + knob / 2.0).clamp(track_x, track_x + track_w);
        let unfilled_w = (track_x + track_w - unfilled_start).max(0.0);
        let knob_at = |scale: f32| {
            el().absolute(knob_cx - inner_knob / 2.0, mid - inner_knob / 2.0)
                .size(inner_knob, inner_knob)
                .radius(inner_knob / 2.0)
                .fill(t.fg())
                .scale(scale)
        };
        let mut bar = el()
            .absolute(bar_x, 0.0)
            .size(bar_w, h)
            .hover_group_focus()
            .focusable(true)
            .role("slider")
            .range_value(self.position as f64, 0.0, self.duration as f64)
            .cursor(Cursor::Pointer)
            .key("scrubber");
        if fill_w > 0.0 {
            bar = bar.child(el().absolute(track_x, mid - track_h / 2.0).size(fill_w, track_h).radius(track_h / 2.0).fill(t.fg()));
        }
        if unfilled_w > 0.0 {
            bar = bar.child(el().absolute(unfilled_start, mid - track_h / 2.0).size(unfilled_w, track_h).radius(track_h / 2.0).fill(t.track()));
        }
        bar = bar
            .child(if self.cursor { knob_at(1.15) } else { knob_at(1.0).group_opacity(1.0, 0.0) })
            .child(if self.cursor { el() } else { knob_at(1.15).group_opacity(0.0, 1.0) });
        if let Some(seek) = &self.seek {
            bar = seek(bar, knob / 2.0, self.duration, self.position);
        }
        el().w(w)
            .h(h)
            .child(text(elapsed).font(style).color(t.dim()).single_line().absolute(0.0, ty))
            .child(bar)
            .child(text(total).font(style).color(t.dim()).single_line().absolute(w - tw, ty))
    }
}
