//! Feedback (spec/components/feedback.md): Meter, ProgressBar, Spinner, EmptyState, Banner and
//! Toast, drawn as the Qt kit's `Meter.qml`, `ProgressBar.qml`, `Spinner.qml`, `EmptyState.qml`,
//! `Banner.qml` and `Toast.qml`.

use std::rc::Rc;
use std::time::Duration;

use gpui::prelude::*;
use gpui::{
    Animation, AnimationExt, App, Bounds, ClickEvent, Div, ElementId, Hsla, Pixels, SharedString, TransformationMatrix, Window, canvas,
    div, ease_in_out, linear, px, radians, relative,
};

use super::navigation::{boxed_glyph, ink_x};
use super::{Button, Handler, IconButton, border, id_for, label, paragraph};
use crate::{Paint, color};

/// What a meter's fill or a banner's edge says (spec/components/feedback.md's Tone): `Neutral`
/// is the foreground, `Info` the accent, `Error` the urgent color. Named apart from the lists
/// group's `Tone`, which tints status text and badges.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum FeedbackTone {
    #[default]
    Neutral,
    Info,
    Success,
    Warning,
    Error,
}

impl FeedbackTone {
    pub fn color(self, t: &Paint) -> Hsla {
        match self {
            FeedbackTone::Neutral => t.fg(),
            FeedbackTone::Info => t.accent(),
            FeedbackTone::Success => t.success(),
            FeedbackTone::Warning => t.warning(),
            FeedbackTone::Error => t.urgent(),
        }
    }
    /// The glyph a banner in this tone leads with.
    fn glyph(self) -> &'static str {
        match self {
            FeedbackTone::Success => "\u{f012c}",
            FeedbackTone::Warning => "\u{f002a}",
            FeedbackTone::Error => "\u{f015a}",
            FeedbackTone::Neutral | FeedbackTone::Info => "\u{f02fd}",
        }
    }
}

// ------------------------------------------------------------------------------- Meter

/// A determinate level on a `track`: battery, volume, usage against a limit. The fill is the
/// foreground unless a [`FeedbackTone`] says otherwise; `alarming` turns it urgent. Markers are
/// fractions drawn as ticks across the track. Fills the width it is given unless `width` is set.
#[derive(IntoElement)]
pub struct Meter {
    id: ElementId,
    value: f32,
    tone: FeedbackTone,
    alarming: bool,
    markers: Vec<f32>,
    label: SharedString,
    value_text: SharedString,
    indeterminate: bool,
    width: Option<f32>,
}

impl Meter {
    pub fn new(value: f32) -> Meter {
        Meter {
            id: "omarchy-meter".into(),
            value,
            tone: FeedbackTone::Neutral,
            alarming: false,
            markers: Vec::new(),
            label: SharedString::default(),
            value_text: SharedString::default(),
            indeterminate: false,
            width: None,
        }
    }
    /// The element id (only an indeterminate bar's animation needs one).
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
        self
    }
    pub fn tone(mut self, tone: FeedbackTone) -> Self {
        self.tone = tone;
        self
    }
    /// Urgent, whatever the tone: over a limit, nearly out.
    pub fn alarming(mut self, on: bool) -> Self {
        self.alarming = on;
        self
    }
    /// Ticks at these fractions of the track.
    pub fn markers(mut self, at: impl IntoIterator<Item = f32>) -> Self {
        self.markers = at.into_iter().collect();
        self
    }
    /// A label above the bar, at its start.
    pub fn label(mut self, label: impl Into<SharedString>) -> Self {
        self.label = label.into();
        self
    }
    /// A reading above the bar, at its end ("65%").
    pub fn value_text(mut self, value: impl Into<SharedString>) -> Self {
        self.value_text = value.into();
        self
    }
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
}

/// The indeterminate segment's sweep: a quarter of the track from just off the start to just off
/// the end, eased in and out, every 1.4 s.
const SWEEP: Duration = Duration::from_millis(1400);

impl RenderOnce for Meter {
    fn render(self, _: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let th = sp.meter_thickness as f32;
        let ink = if self.alarming { t.urgent() } else { self.tone.color(&t) };
        let radius = (tk.radius_small() as f32).min(th / 2.0);
        let markers = if self.indeterminate { Vec::new() } else { self.markers };
        let tick_w = 2f32.max((th * 0.5).round());
        let tick_h = (th * 2.5).round();
        let bar_h = if markers.is_empty() { th } else { th.max(tick_h) };
        let top = ((bar_h - th) / 2.0).round();

        let mut track = div().absolute().left_0().right_0().top(px(top)).h(px(th)).rounded(px(radius)).bg(t.track());
        if self.indeterminate {
            let segment = div().absolute().top_0().h_full().w(relative(0.25)).rounded(px(radius)).bg(ink);
            track = if tk.reduce_motion {
                // Held still in the middle: 3/8 along, as the Qt binding does.
                track.overflow_hidden().child(segment.left(relative(0.375)))
            } else {
                track.overflow_hidden().child(segment.with_animation(
                    self.id.clone(),
                    Animation::new(SWEEP).repeat().with_easing(ease_in_out),
                    |s, d| s.left(relative(-0.25 + 1.25 * d)),
                ))
            };
        } else {
            track = track.child(div().h_full().w(relative(self.value.clamp(0.0, 1.0))).rounded(px(radius)).bg(ink));
        }
        let mut bar = div().relative().w_full().h(px(bar_h)).flex_shrink_0().child(track);
        let tick_radius = (tk.radius_small() as f32).min(tick_w / 2.0);
        for at in markers {
            // Measured so the first and last ticks sit inside the track's ends.
            bar = bar.child(
                div()
                    .absolute()
                    .left_0()
                    .right(px(tick_w))
                    .top(px(((bar_h - tick_h) / 2.0).round()))
                    .h(px(tick_h))
                    .flex()
                    .flex_row()
                    .child(div().h_full().w(relative(at.clamp(0.0, 1.0))))
                    .child(div().flex_shrink_0().w(px(tick_w)).h(px(tick_h)).rounded(px(tick_radius)).bg(ink)),
            );
        }

        let mut e = div().flex().flex_col().gap(px(sp.md as f32));
        e = match self.width {
            Some(w) => e.w(px(w)).flex_shrink_0(),
            None => e.w_full(),
        };
        if !self.label.is_empty() || !self.value_text.is_empty() {
            let size = tk.font.body;
            e = e.child(
                div()
                    .flex()
                    .flex_row()
                    .w_full()
                    .justify_between()
                    .gap(px(sp.control_gap as f32))
                    .child(label(&t, self.label, size, false, t.fg()))
                    .child(label(&t, self.value_text, size, false, t.dim())),
            );
        }
        e.child(bar)
    }
}

/// Progress of a task: a [`Meter`] filled with the accent, with an indeterminate mode for work of
/// unknown length.
#[derive(IntoElement)]
pub struct ProgressBar {
    meter: Meter,
}

impl ProgressBar {
    pub fn new(value: f32) -> ProgressBar {
        ProgressBar { meter: Meter::new(value).tone(FeedbackTone::Info).id("omarchy-progress") }
    }
    /// Work of unknown length: a quarter-track segment sweeps across, still under reduced motion.
    pub fn indeterminate() -> ProgressBar {
        let mut meter = Meter::new(0.0).tone(FeedbackTone::Info).id("omarchy-progress-indeterminate");
        meter.indeterminate = true;
        ProgressBar { meter }
    }
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.meter = self.meter.id(id);
        self
    }
    pub fn tone(mut self, tone: FeedbackTone) -> Self {
        self.meter = self.meter.tone(tone);
        self
    }
    pub fn label(mut self, label: impl Into<SharedString>) -> Self {
        self.meter = self.meter.label(label);
        self
    }
    pub fn value_text(mut self, value: impl Into<SharedString>) -> Self {
        self.meter = self.meter.value_text(value);
        self
    }
    pub fn width(mut self, w: f32) -> Self {
        self.meter = self.meter.width(w);
        self
    }
}

impl RenderOnce for ProgressBar {
    fn render(self, _: &mut Window, _: &mut App) -> impl IntoElement {
        self.meter
    }
}

// ------------------------------------------------------------------------------- Spinner

/// Busy: a Nerd Font glyph turning once every 900 ms about its ink's middle, or with `pulse` a
/// glyph breathing between half and full opacity, 420 ms each way. Still under reduced motion (a
/// pulse rests at half), as Qt's `Spinner.qml`.
#[derive(IntoElement)]
pub struct Spinner {
    id: Option<ElementId>,
    glyph: SharedString,
    size: Option<i64>,
    pulse: bool,
    color: Option<Hsla>,
}

impl Spinner {
    /// The turn's period, and the pulse's (one breath in and out: 420 ms each way).
    pub const TURN: Duration = Duration::from_millis(900);
    pub const PULSE: Duration = Duration::from_millis(840);

    pub fn new() -> Spinner {
        Spinner { id: None, glyph: "\u{f0996}".into(), size: None, pulse: false, color: None }
    }
    /// The element id its animation runs under (by default from the glyph, size and mode).
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = Some(id.into());
        self
    }
    pub fn glyph(mut self, g: impl Into<SharedString>) -> Self {
        self.glyph = g.into();
        self
    }
    /// The glyph's size in px, from a font token (`icon` by default).
    pub fn size(mut self, px: i64) -> Self {
        self.size = Some(px);
        self
    }
    pub fn pulse(mut self, on: bool) -> Self {
        self.pulse = on;
        self
    }
    pub fn color(mut self, c: Hsla) -> Self {
        self.color = Some(c);
        self
    }
}

impl Default for Spinner {
    fn default() -> Self {
        Spinner::new()
    }
}

/// The glyph as an SVG `<text>`, for painting turned (GPUI turns SVGs, not text runs): the same
/// family, size and baseline the text would have, in a `side` square.
fn glyph_svg(family: &str, glyph: &str, size: f32, side: f32, x: f32, baseline: f32) -> Vec<u8> {
    let esc = |s: &str| s.replace('&', "&amp;").replace('<', "&lt;").replace('"', "&quot;");
    format!(
        r##"<svg xmlns="http://www.w3.org/2000/svg" width="{side}" height="{side}" viewBox="0 0 {side} {side}"><text x="{x}" y="{baseline}" font-family="{}" font-size="{size}" fill="#000">{}</text></svg>"##,
        esc(family),
        esc(glyph)
    )
    .into_bytes()
}

impl RenderOnce for Spinner {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let size = self.size.unwrap_or(tk.font.icon);
        let ink = self.color.unwrap_or_else(|| t.fg());
        // A square of the glyph's line height, its ink centered across it (as Qt's is, so it
        // turns about its own middle).
        let side = tk.font.line_px(size) as f32;
        let (ix, iw) = ink_x(window, cx, &t, &self.glyph, size, false);
        let left = ((side - iw) / 2.0 - ix).round();
        let id = self.id.unwrap_or_else(|| id_for("spinner", &format!("{}-{size}-{}", self.glyph, self.pulse)));
        let text = label(&t, self.glyph.clone(), size, false, ink).absolute().left(px(left)).top_0();
        let mark = div().relative().flex_shrink_0().size(px(side));
        let still = tk.reduce_motion;
        if self.pulse {
            // Rises from half to full and back, as Qt's SequentialAnimation does from its 0.5.
            if still {
                return mark.child(text.opacity(0.5)).into_any_element();
            }
            return mark
                .child(text)
                .with_animation(id, Animation::new(Self::PULSE).repeat().with_easing(linear), |m, d| {
                    let up = if d < 0.5 { d * 2.0 } else { 2.0 - d * 2.0 };
                    m.opacity(0.5 + 0.5 * ease_in_out(up))
                })
                .into_any_element();
        }
        if still {
            return mark.child(text).into_any_element();
        }
        // Turning: the glyph painted as an SVG text run, rotated about the square's middle.
        let family = t.family();
        let font_id = cx.text_system().resolve_font(&gpui::font(family.clone()));
        let font_size = px(size as f32);
        // As GPUI lays out a line: the ascent and descent (the metric's descent is negative)
        // centered in the line box.
        let ascent = f32::from(cx.text_system().ascent(font_id, font_size));
        let descent = f32::from(cx.text_system().descent(font_id, font_size)).abs();
        let baseline = (side - ascent - descent) / 2.0 + ascent;
        let svg: Rc<[u8]> = glyph_svg(&family, &self.glyph, size as f32, side, left, baseline).into();
        let key: SharedString = format!("omarchy-spinner:{family}:{size}:{}", self.glyph).into();
        mark.with_animation(id, Animation::new(Self::TURN).repeat().with_easing(linear), move |m: Div, d| {
            let (svg, key) = (svg.clone(), key.clone());
            m.child(
                canvas(
                    |_, _, _| {},
                    move |bounds: Bounds<Pixels>, _, window, cx| {
                        let sf = window.scale_factor();
                        let center = bounds.center();
                        let matrix = TransformationMatrix::unit()
                            .translate(center.scale(sf))
                            .rotate(radians(d * std::f32::consts::TAU))
                            .translate(center.scale(-sf));
                        let _ = window.paint_svg(bounds, key, Some(&svg), matrix, ink, cx);
                    },
                )
                .absolute()
                .top_0()
                .left_0()
                .size_full(),
            )
        })
        .into_any_element()
    }
}

// ------------------------------------------------------------------------------- EmptyState

/// What a list or panel shows when it has nothing: glyph, title, optional hint and action,
/// centered in the width it is given. `overlay(false)` (the default) is a panel's quiet line;
/// `overlay(true)` is a picker overlay's: a displayLarge glyph in the menu's selected text and a
/// title at 0.7 opacity.
#[derive(IntoElement)]
pub struct EmptyState {
    glyph: SharedString,
    title: SharedString,
    hint: SharedString,
    action: SharedString,
    overlay: bool,
    cursor: bool,
    on_action: Option<Handler<ClickEvent>>,
}

impl EmptyState {
    pub fn new(title: impl Into<SharedString>) -> EmptyState {
        EmptyState {
            glyph: SharedString::default(),
            title: title.into(),
            hint: SharedString::default(),
            action: SharedString::default(),
            overlay: false,
            cursor: false,
            on_action: None,
        }
    }
    pub fn glyph(mut self, g: impl Into<SharedString>) -> Self {
        self.glyph = g.into();
        self
    }
    pub fn hint(mut self, h: impl Into<SharedString>) -> Self {
        self.hint = h.into();
        self
    }
    /// A bordered button under the text.
    pub fn action(mut self, label: impl Into<SharedString>) -> Self {
        self.action = label.into();
        self
    }
    pub fn overlay(mut self, on: bool) -> Self {
        self.overlay = on;
        self
    }
    /// Force the action's hover-cursor look.
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    pub fn on_action(mut self, f: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static) -> Self {
        self.on_action = Some(Rc::new(f));
        self
    }
}

impl RenderOnce for EmptyState {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let f = &tk.font;
        let (gap, glyph_size, glyph_ink, glyph_opacity, title_size, title_ink, title_opacity, hint_size, hint_ink) = if self.overlay {
            (sp.lg, f.display_large, color(tk.surfaces.menu.selected_text), 0.8, f.title, t.fg(), 0.7, f.body, t.dim())
        } else {
            (sp.sm, f.icon_large, t.dim(), 1.0, f.body, t.dim(), 1.0, f.body_small, t.faint())
        };
        let mut c = div().flex().flex_col().w_full().items_center().gap(px(gap as f32));
        if !self.glyph.is_empty() {
            c = c.child(boxed_glyph(window, cx, &t, &self.glyph, glyph_size, glyph_ink).opacity(glyph_opacity));
        }
        c = c.child(paragraph(&t, self.title, title_size, title_ink).w_full().text_center().opacity(title_opacity));
        if !self.hint.is_empty() {
            c = c.child(paragraph(&t, self.hint, hint_size, hint_ink).w_full().text_center());
        }
        if !self.action.is_empty() {
            let mut b = Button::new(self.action).bordered(true).cursor(self.cursor);
            if let Some(f) = self.on_action {
                b = b.on_click(move |e, w, cx| f(e, w, cx));
            }
            c = c.child(b);
        }
        c
    }
}

// ------------------------------------------------------------------------------- Banner

/// An inline message: a tone glyph, the text, an optional action and dismiss. A foreground-glass
/// fill with a border in the tone; never a filled saturated background.
#[derive(IntoElement)]
pub struct Banner {
    id: ElementId,
    text: SharedString,
    tone: FeedbackTone,
    glyph: Option<SharedString>,
    action: SharedString,
    dismissible: bool,
    cursor: bool,
    on_action: Option<Handler<ClickEvent>>,
    on_dismiss: Option<Handler<ClickEvent>>,
}

impl Banner {
    pub fn new(text: impl Into<SharedString>) -> Banner {
        let text = text.into();
        Banner {
            id: id_for("banner", &text),
            text,
            tone: FeedbackTone::Info,
            glyph: None,
            action: SharedString::default(),
            dismissible: false,
            cursor: false,
            on_action: None,
            on_dismiss: None,
        }
    }
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
        self
    }
    /// Info (the default), Success, Warning or Error. Neutral draws as Info.
    pub fn tone(mut self, tone: FeedbackTone) -> Self {
        self.tone = tone;
        self
    }
    /// Replaces the tone's own glyph.
    pub fn glyph(mut self, g: impl Into<SharedString>) -> Self {
        self.glyph = Some(g.into());
        self
    }
    pub fn action(mut self, label: impl Into<SharedString>) -> Self {
        self.action = label.into();
        self
    }
    pub fn dismissible(mut self, on: bool) -> Self {
        self.dismissible = on;
        self
    }
    /// Force the action's hover-cursor look.
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    pub fn on_action(mut self, f: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static) -> Self {
        self.on_action = Some(Rc::new(f));
        self
    }
    pub fn on_dismiss(mut self, f: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static) -> Self {
        self.on_dismiss = Some(Rc::new(f));
        self
    }
}

impl RenderOnce for Banner {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let tone = if self.tone == FeedbackTone::Neutral { FeedbackTone::Info } else { self.tone };
        let ink = tone.color(&t);
        let line = tk.controls.normal.border.max_width().max(1.0) as f32;
        let btn_border = tk.controls.normal.border.max_width().max(tk.controls.hover.border.max_width()) as f32;
        let btn_min_h = tk.font.line_px(tk.font.body) as f32 + (sp.control_padding_y as f32 + btn_border) * 2.0;
        let inner_min_h = (sp.control_height as f32).max(btn_min_h);
        let trailing = !self.action.is_empty() || self.dismissible;
        let mark = self.glyph.unwrap_or_else(|| tone.glyph().into());
        let child_id = |name: &str| ElementId::NamedChild(std::sync::Arc::new(self.id.clone()), name.to_string().into());

        let mut r = div()
            .flex()
            .flex_row()
            .w_full()
            .items_center()
            .gap(px(sp.control_gap as f32))
            .min_h(px(inner_min_h + (sp.sm as f32 + line) * 2.0))
            .pl(px(sp.control_padding_x as f32))
            .pr(px(if trailing { sp.sm } else { sp.control_padding_x } as f32))
            .py(px(sp.sm as f32))
            .bg(color(tk.controls.normal.fill))
            .border(px(line))
            .border_color(ink)
            .rounded(t.radius())
            .child(boxed_glyph(window, cx, &t, &mark, tk.font.icon, ink))
            .child(paragraph(&t, self.text, tk.font.body, t.fg()).flex_1().min_w_0());
        if !self.action.is_empty() {
            let mut b = Button::new(self.action).id(child_id("action")).bordered(true).cursor(self.cursor);
            if let Some(f) = self.on_action {
                b = b.on_click(move |e, w, cx| f(e, w, cx));
            }
            r = r.child(b);
        }
        if self.dismissible {
            let mut b = IconButton::new("\u{f0156}").id(child_id("dismiss"));
            if let Some(f) = self.on_dismiss {
                b = b.on_click(move |e, w, cx| f(e, w, cx));
            }
            r = r.child(b);
        }
        r
    }
}

// ------------------------------------------------------------------------------- Toast

/// A transient in-app message on the [notifications] surface: optional glyph, bold title, text,
/// optional action, and a countdown line in `notifications.countdown`. It draws one state; the
/// app owns when it shows and the timer behind `countdown`. Desktop notifications go through
/// `omarchy-notification-send`, not this.
#[derive(IntoElement)]
pub struct Toast {
    id: ElementId,
    glyph: SharedString,
    title: SharedString,
    text: SharedString,
    action: SharedString,
    countdown: Option<f32>,
    cursor: bool,
    width: Option<f32>,
    on_action: Option<Handler<ClickEvent>>,
}

impl Toast {
    pub fn new(title: impl Into<SharedString>) -> Toast {
        let title = title.into();
        Toast {
            id: id_for("toast", &title),
            glyph: SharedString::default(),
            title,
            text: SharedString::default(),
            action: SharedString::default(),
            countdown: None,
            cursor: false,
            width: None,
            on_action: None,
        }
    }
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
        self
    }
    pub fn glyph(mut self, g: impl Into<SharedString>) -> Self {
        self.glyph = g.into();
        self
    }
    pub fn text(mut self, s: impl Into<SharedString>) -> Self {
        self.text = s.into();
        self
    }
    pub fn action(mut self, label: impl Into<SharedString>) -> Self {
        self.action = label.into();
        self
    }
    /// Time left, 1 → 0, drawn as a line along the bottom.
    pub fn countdown(mut self, left: f32) -> Self {
        self.countdown = Some(left);
        self
    }
    /// The keyboard's toast: tinted with the hover fill.
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    pub fn on_action(mut self, f: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static) -> Self {
        self.on_action = Some(Rc::new(f));
        self
    }
}

impl RenderOnce for Toast {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let s = &tk.surfaces.notifications;
        let ink = color(s.base.text);

        let mut words = div()
            .flex()
            .flex_col()
            .flex_1()
            .min_w_0()
            .gap(px(t.space(3.0)))
            .child(label(&t, self.title, tk.font.body, true, ink));
        if !self.text.is_empty() {
            words = words.child(paragraph(&t, self.text, tk.font.body, color(s.base.text.with_alpha(0.78))));
        }
        let mut content = div().relative().flex().flex_row().w_full().items_center().gap(px(t.space(12.0)));
        if !self.glyph.is_empty() {
            content = content.child(boxed_glyph(window, cx, &t, &self.glyph, tk.font.icon_large, ink));
        }
        content = content.child(words);
        if !self.action.is_empty() {
            let id = ElementId::NamedChild(std::sync::Arc::new(self.id.clone()), "action".into());
            let mut b = Button::new(self.action).id(id).bordered(true);
            if let Some(f) = self.on_action {
                b = b.on_click(move |e, w, cx| f(e, w, cx));
            }
            content = content.child(b);
        }

        // The border spec is drawn by the box itself, so the padding is inside it.
        // Center the content in the height above the countdown bar when present.
        let pad_x = t.space(12.0);
        let pad_y = t.space(10.0);
        let line = t.space(2.0).max(1.0);
        let (pt, pb) = if self.countdown.is_some() {
            let shift = (line / 2.0).round();
            ((pad_y - shift).max(0.0), pad_y + shift)
        } else {
            (pad_y, pad_y)
        };
        let mut e = div()
            .relative()
            .flex()
            .flex_col()
            .justify_center()
            .flex_shrink_0()
            .px(px(pad_x))
            .pt(px(pt))
            .pb(px(pb))
            .bg(color(s.base.background))
            .rounded(t.radius())
            .w(px(self.width.unwrap_or_else(|| t.space(360.0))));
        e = border(e, &s.base.border);
        if self.cursor {
            let bw = s.base.border.widths[3].max(s.base.border.widths[0]) as f32;
            let inner_r = (tk.radius as f32 - bw).max(0.0);
            e = e.child(div().absolute().top_0().left_0().size_full().rounded(px(inner_r)).bg(color(tk.controls.hover.fill)));
        }
        e = e.child(content);
        if let Some(left) = self.countdown {
            let r = tk.radius as f32;
            e = e.child(
                div()
                    .absolute()
                    .left(px(r))
                    .right(px(r))
                    .bottom_0()
                    .h(px(line))
                    .child(div().h_full().w(relative(left.clamp(0.0, 1.0))).bg(color(s.countdown))),
            );
        }
        e
    }
}
