//! Slider: the shell's `PanelSlider.qml`. A thin track, a foreground fill, a round knob ringed
//! in the surface color, and optional notches cut into the track.

use std::cell::Cell;
use std::rc::Rc;

use gpui::prelude::*;
use gpui::{App, Bounds, ElementId, KeyDownEvent, MouseButton, MouseMoveEvent, MouseUpEvent, Pixels, Window, canvas, div, px};

use super::{Handler, focus_handle, id_for, keyed_state, tab_keys};
use crate::{Paint, color};

/// A horizontal slider. Dragging the knob or pressing the track calls `on_change` with the new
/// value; Left/Right (h/l) step it by `step`, Home/End jump to the ends, when focused.
#[derive(IntoElement)]
pub struct Slider {
    id: ElementId,
    value: f32,
    min: f32,
    max: f32,
    step: f32,
    ticks: usize,
    width: Option<f32>,
    cursor: bool,
    on_change: Option<Handler<f32>>,
}

impl Slider {
    pub fn new(id: impl Into<ElementId>, value: f32) -> Slider {
        Slider { id: id.into(), value, min: 0.0, max: 1.0, step: 0.05, ticks: 0, width: None, cursor: false, on_change: None }
    }
    pub fn range(mut self, min: f32, max: f32) -> Self {
        self.min = min;
        self.max = max;
        self
    }
    /// The keyboard step (0.05 by default, as Qt's).
    pub fn step(mut self, step: f32) -> Self {
        self.step = step;
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
    pub fn on_change(mut self, f: impl Fn(&f32, &mut Window, &mut App) + 'static) -> Self {
        self.on_change = Some(Rc::new(f));
        self
    }
}

#[derive(Default)]
struct Drag {
    bounds: Rc<Cell<Option<Bounds<Pixels>>>>,
    dragging: Rc<Cell<bool>>,
}

impl RenderOnce for Slider {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let _ = id_for;
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let surface = color(tk.surfaces.popups.background);
        let width = self.width.unwrap_or(t.space(200.0));
        let track_h = 4f32.max((sp.control_height as f32 * 0.11).round());
        let knob = 14f32.max((sp.control_height as f32 * 0.38).round());
        let height = t.space(22.0).max(knob + sp.md as f32);
        let (min, max, value, step) = (self.min, self.max, self.value, self.step);
        let progress = if max > min { ((value - min) / (max - min)).clamp(0.0, 1.0) } else { 0.0 };
        let track_w = width - knob;
        let mid = height / 2.0;
        let focus = focus_handle(&self.id, window, cx);
        let drag = keyed_state(&self.id, "slider", window, cx, Drag::default);
        let (bounds, dragging) = {
            let d = drag.read(cx);
            (d.bounds.clone(), d.dragging.clone())
        };

        let track_color = color(tk.palette.foreground.with_alpha(tk.controls.selected.fill.a as f64 / 255.0));
        let mut e = div()
            .id(self.id)
            .relative()
            .flex_shrink_0()
            .w(px(width))
            .h(px(height))
            .track_focus(&focus)
            .child(
                div()
                    .absolute()
                    .left(px(knob / 2.0))
                    .top(px(mid - track_h / 2.0))
                    .w(px(track_w))
                    .h(px(track_h))
                    .rounded(px(track_h / 2.0))
                    .bg(track_color),
            )
            .child(
                div()
                    .absolute()
                    .left(px(knob / 2.0))
                    .top(px(mid - track_h / 2.0))
                    .w(px(track_w * progress))
                    .h(px(track_h))
                    .rounded(px(track_h / 2.0))
                    .bg(t.fg()),
            );
        if self.ticks > 2 {
            let tw = 1f32.max(t.space(2.0));
            let th = track_h + t.space(4.0);
            for i in 1..(self.ticks - 1) {
                let x = knob / 2.0 + track_w * (i as f32 / (self.ticks - 1) as f32) - tw / 2.0;
                e = e.child(div().absolute().left(px(x)).top(px(mid - th / 2.0)).w(px(tw)).h(px(th)).rounded(px(1.0)).bg(surface));
            }
        }
        let ring = 1f32.max(t.space(2.0));
        let hot = self.cursor || focus.is_focused(window);
        let k = if hot { (knob * 1.15).round() } else { knob };
        e = e.child(
            div()
                .absolute()
                .left(px(knob / 2.0 + track_w * progress - k / 2.0))
                .top(px(mid - k / 2.0))
                .size(px(k))
                .rounded(px(k / 2.0))
                .bg(t.fg())
                .border(px(ring))
                .border_color(surface),
        );

        // Where the slider is, for the pointer; and the drag, followed window-wide.
        let value_at = move |b: Bounds<Pixels>, x: Pixels| {
            let frac = ((f32::from(x - b.left()) - knob / 2.0) / track_w.max(1.0)).clamp(0.0, 1.0);
            min + (max - min) * frac
        };
        let on_change = self.on_change.clone();
        let (b2, d2) = (bounds.clone(), dragging.clone());
        e = e.child(
            canvas(
                move |b, _, _| b2.set(Some(b)),
                move |_, _, window, _| {
                    let (d3, d4, b3) = (d2.clone(), d2.clone(), bounds.clone());
                    let f = on_change.clone();
                    window.on_mouse_event(move |ev: &MouseMoveEvent, _, window, cx| {
                        if d3.get() && ev.pressed_button == Some(MouseButton::Left)
                            && let (Some(b), Some(f)) = (b3.get(), &f)
                        {
                            f(&value_at(b, ev.position.x), window, cx);
                        }
                    });
                    window.on_mouse_event(move |_: &MouseUpEvent, _, _, _| d4.set(false));
                },
            )
            .absolute()
            .size_full(),
        );
        let f = self.on_change.clone();
        let b4 = drag.read(cx).bounds.clone();
        let d5 = dragging;
        e = e.on_mouse_down(MouseButton::Left, move |ev, window, cx| {
            d5.set(true);
            if let (Some(b), Some(f)) = (b4.get(), &f) {
                f(&value_at(b, ev.position.x), window, cx);
            }
        });
        let keys = self.on_change;
        e.on_key_down(move |ev: &KeyDownEvent, window, cx| {
            if tab_keys(ev, window, cx) {
                cx.stop_propagation();
                return;
            }
            let next = match ev.keystroke.key.as_str() {
                "left" | "down" | "h" | "j" => value - step,
                "right" | "up" | "l" | "k" => value + step,
                "home" => min,
                "end" => max,
                _ => return,
            }
            .clamp(min, max);
            if let Some(f) = &keys {
                f(&next, window, cx);
            }
            cx.stop_propagation();
        })
    }
}
