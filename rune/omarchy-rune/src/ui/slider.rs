//! Slider: the shell's `PanelSlider.qml`. A thin track, a foreground fill, a round knob ringed
//! in the surface color, and optional notches cut into the track.

use rune::{El, el};

use crate::{Theme, color};

pub struct Slider {
    value: f32,
    min: f32,
    max: f32,
    ticks: usize,
    width: Option<f32>,
    cursor: bool,
    change: Option<Box<dyn FnOnce(El, f32, f32) -> El>>,
}

impl Slider {
    pub fn new(value: f32) -> Slider {
        Slider { value, min: 0.0, max: 1.0, ticks: 0, width: None, cursor: false, change: None }
    }
    pub fn range(mut self, min: f32, max: f32) -> Self {
        self.min = min;
        self.max = max;
        self
    }
    /// Evenly spaced notches, the surface color, cut into the track.
    pub fn ticks(mut self, n: usize) -> Self {
        self.ticks = n;
        self
    }
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    /// The hovered look (a slightly larger knob).
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    /// Called with the new value as the knob is dragged or the track pressed.
    pub fn on_change<S: 'static>(mut self, f: impl Fn(&mut S, f32) + 'static) -> Self {
        self.change = Some(Box::new(move |e: El, min: f32, max: f32| {
            e.on_drag(move |s: &mut S, d, _tx| {
                let frac = (d.x / d.w.max(1.0)).clamp(0.0, 1.0);
                f(s, min + (max - min) * frac);
            })
        }));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let surface = tk.surfaces.popups.background;
        let width = self.width.unwrap_or(t.space(200.0));
        let track_h = 4f32.max((sp.control_height as f32 * 0.11).round());
        let knob = 14f32.max((sp.control_height as f32 * 0.38).round());
        let height = t.space(22.0).max(knob + sp.md as f32);
        let progress = if self.max > self.min { ((self.value - self.min) / (self.max - self.min)).clamp(0.0, 1.0) } else { 0.0 };
        let track_x = knob / 2.0;
        let track_w = (width - knob).max(0.0);
        let mid = height / 2.0;
        let ring = 1f32.max(t.space(2.0));
        let inner_knob = 4f32.max(knob - ring * 2.0);
        let knob_cx = track_x + track_w * progress;

        let track_color = tk.palette.foreground.with_alpha(tk.controls.selected.fill.a as f64 / 255.0);
        let mut e = el()
            .w(width)
            .h(height)
            .role("slider")
            .range_value(self.value as f64, self.min as f64, self.max as f64)
            .focusable(true)
            .hit();
        // Split the track around the knob with a transparent gap of `ring`, so the slider sits
        // cleanly on any surface without painting an opaque background halo.
        let fill_end = (knob_cx - knob / 2.0).clamp(track_x, track_x + track_w);
        let fill_w = (fill_end - track_x).max(0.0);
        if fill_w > 0.0 {
            e = e.child(
                el()
                    .key("fill")
                    .absolute(track_x, mid - track_h / 2.0)
                    .size(fill_w, track_h)
                    .radius(track_h / 2.0)
                    .fill(t.fg()),
            );
        }
        let unfilled_start = (knob_cx + knob / 2.0).clamp(track_x, track_x + track_w);
        let unfilled_w = (track_x + track_w - unfilled_start).max(0.0);
        if unfilled_w > 0.0 {
            e = e.child(
                el()
                    .absolute(unfilled_start, mid - track_h / 2.0)
                    .size(unfilled_w, track_h)
                    .radius(track_h / 2.0)
                    .fill(color(track_color)),
            );
        }
        if self.ticks > 2 {
            let tw = 1f32.max(t.space(2.0));
            let th = track_h + t.space(4.0);
            for i in 1..self.ticks - 1 {
                let cx = track_x + track_w * (i as f32 / (self.ticks - 1) as f32);
                if (cx - knob_cx).abs() > knob / 2.0 {
                    e = e.child(el().absolute(cx - tw / 2.0, mid - th / 2.0).size(tw, th).radius(1.0).fill(color(surface)));
                }
            }
        }
        let scale = if self.cursor { 1.15 } else { 1.0 };
        e = e.child(
            el()
                .key("knob")
                .absolute(knob_cx - inner_knob / 2.0, mid - inner_knob / 2.0)
                .size(inner_knob, inner_knob)
                .radius(inner_knob / 2.0)
                .fill(t.fg())
                .scale(scale),
        );
        match self.change {
            Some(attach) => attach(e, self.min, self.max),
            None => e,
        }
    }
}
