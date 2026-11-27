//! Feedback: Meter, ProgressBar, Spinner, EmptyState, Banner and Toast. The Qt binding's
//! `Meter.qml`, `ProgressBar.qml`, `Spinner.qml`, `EmptyState.qml`, `Banner.qml` and `Toast.qml`.

use rune::{Align, Color, El, LoopSpec, col, el, row, text};

use super::{Attach, Button, IconButton, glyph};
use crate::{Theme, border, color};

/// What a meter's fill or a banner's edge says. `Neutral` is the foreground, `Info` the accent,
/// `Error` the urgent color.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Tone {
    #[default]
    Neutral,
    Info,
    Success,
    Warning,
    Error,
}

impl Tone {
    pub fn color(self, t: &Theme) -> Color {
        match self {
            Tone::Neutral => t.fg(),
            Tone::Info => t.accent(),
            Tone::Success => t.success(),
            Tone::Warning => t.warning(),
            Tone::Error => t.urgent(),
        }
    }
    /// The glyph a banner in this tone leads with.
    fn glyph(self) -> &'static str {
        match self {
            Tone::Success => "\u{f012c}",
            Tone::Warning => "\u{f002a}",
            Tone::Error => "\u{f015a}",
            Tone::Neutral | Tone::Info => "\u{f02fd}",
        }
    }
}

fn attached(e: El, handler: Option<Attach>) -> El {
    match handler {
        Some(attach) => attach(e),
        None => e,
    }
}

/// A determinate level on a `track`: battery, volume, usage against a limit. The fill is the
/// foreground unless a [`Tone`] says otherwise; `alarming` turns it urgent. Markers are fractions
/// drawn as ticks across the track. Fills the width it is given unless `width` is set.
pub struct Meter {
    value: f32,
    tone: Tone,
    alarming: bool,
    markers: Vec<f32>,
    label: String,
    value_text: String,
    indeterminate: bool,
    width: Option<f32>,
}

impl Meter {
    pub fn new(value: f32) -> Meter {
        Meter {
            value,
            tone: Tone::Neutral,
            alarming: false,
            markers: Vec::new(),
            label: String::new(),
            value_text: String::new(),
            indeterminate: false,
            width: None,
        }
    }
    pub fn tone(mut self, tone: Tone) -> Self {
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
    pub fn label(mut self, label: impl Into<String>) -> Self {
        self.label = label.into();
        self
    }
    /// A reading above the bar, at its end ("65%").
    pub fn value_text(mut self, value: impl Into<String>) -> Self {
        self.value_text = value.into();
        self
    }
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let th = sp.meter_thickness as f32;
        let ink = if self.alarming { t.urgent() } else { self.tone.color(t) };
        let radius = t.radius_small().min(th / 2.0);
        let markers = if self.indeterminate { Vec::new() } else { self.markers };
        let tick_w = 2f32.max((th * 0.5).round());
        let tick_h = (th * 2.5).round();
        let bar_h = if markers.is_empty() { th } else { th.max(tick_h) };

        let mut track = row().w_full().h(th).radius(radius).fill(t.track());
        if self.indeterminate {
            // A quarter of the track sweeps across it and wraps; reduced motion holds it in the
            // middle (3/8 along), as the Qt binding does.
            let segment = el().h_full().w_frac(0.25).radius(radius).fill(ink).looping(LoopSpec::slide(1.4, [-1.0, 0.0], [4.0, 0.0]));
            track = track.clip_rounded(radius).child(segment);
        } else {
            track = track.child(el().h_full().w_frac(self.value.clamp(0.0, 1.0)).radius(radius).fill(ink));
        }
        let top = ((bar_h - th) / 2.0).round();
        let mut bar = el().w_full().h(bar_h).child(track.pin(Some(0.0), Some(top), Some(0.0), None));
        for at in markers {
            // Measured so the first and last ticks sit inside the track's ends.
            let tick = el().w(tick_w).h(tick_h).radius(t.radius_small().min(tick_w / 2.0)).fill(ink);
            bar = bar.child(
                row()
                    .pin(Some(0.0), Some(((bar_h - tick_h) / 2.0).round()), Some(tick_w), None)
                    .h(tick_h)
                    .child(el().w_frac(at.clamp(0.0, 1.0)).h(tick_h))
                    .child(tick),
            );
        }

        let mut e = col().gap(sp.md as f32).role("progressbar").label(self.label.clone());
        if !self.indeterminate {
            let v = self.value.clamp(0.0, 1.0);
            e = e.range_value(v as f64, 0.0, 1.0);
        }
        e = match self.width {
            Some(w) => e.w(w),
            None => e.w_full(),
        };
        if !self.label.is_empty() || !self.value_text.is_empty() {
            let size = tk.font.body;
            e = e.child(
                row()
                    .w_full()
                    .justify_between()
                    .gap(sp.control_gap as f32)
                    .child(text(self.label).font(t.text_style(size)).color(t.fg()).single_line())
                    .child(text(self.value_text).font(t.text_style(size)).color(t.dim()).single_line()),
            );
        }
        e.child(bar)
    }
}

/// Progress of a task: a [`Meter`] filled with the accent, with an indeterminate mode for work of
/// unknown length.
pub struct ProgressBar {
    meter: Meter,
}

impl ProgressBar {
    pub fn new(value: f32) -> ProgressBar {
        ProgressBar { meter: Meter::new(value).tone(Tone::Info) }
    }
    /// Work of unknown length: a quarter-track segment sweeps across, still under reduced motion.
    pub fn indeterminate() -> ProgressBar {
        let mut meter = Meter::new(0.0).tone(Tone::Info);
        meter.indeterminate = true;
        ProgressBar { meter }
    }
    pub fn tone(mut self, tone: Tone) -> Self {
        self.meter = self.meter.tone(tone);
        self
    }
    pub fn label(mut self, label: impl Into<String>) -> Self {
        self.meter = self.meter.label(label);
        self
    }
    pub fn value_text(mut self, value: impl Into<String>) -> Self {
        self.meter = self.meter.value_text(value);
        self
    }
    pub fn width(mut self, w: f32) -> Self {
        self.meter = self.meter.width(w);
        self
    }
    pub fn build(self, t: &Theme) -> El {
        self.meter.build(t)
    }
}

/// Busy: a Nerd Font glyph turning once every 900 ms about its ink's middle, or with `pulse` a
/// glyph breathing between half and full opacity, 420 ms each way. Still under reduced motion
/// (a pulse rests at half), as Qt's `Spinner.qml`.
pub struct Spinner {
    glyph: String,
    size: Option<i64>,
    pulse: bool,
    color: Option<Color>,
}

impl Spinner {
    pub fn new() -> Spinner {
        Spinner { glyph: "\u{f0996}".into(), size: None, pulse: false, color: None }
    }
    pub fn glyph(mut self, g: impl Into<String>) -> Self {
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
    pub fn color(mut self, c: Color) -> Self {
        self.color = Some(c);
        self
    }

    /// The turn's period in seconds, and the pulse's (one breath in and out: 420 ms each way).
    pub const TURN: f64 = 0.9;
    pub const PULSE: f64 = 0.84;

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let size = self.size.unwrap_or(tk.font.icon);
        let ink = self.color.unwrap_or_else(|| t.fg());
        // A square of the glyph's line height, its ink centered across it (as the Qt one is, so
        // it turns about its own middle).
        let style = t.text_style(size);
        let (_, h) = rune::text::measure(&self.glyph, &[], style, None);
        let side = h.round();
        let bounds = rune::text::ink(&self.glyph, style).map(|i| i.bounds);
        let left = match bounds {
            Some([x, _, w, _]) => ((side - w) / 2.0 - x).round(),
            None => ((side - rune::text::advance(&self.glyph, style)) / 2.0).round(),
        };
        let top = ((side - h) / 2.0).round();
        let mark = el().size(side, side).role("progressbar").label("Busy");
        let still = tk.reduce_motion;
        if self.pulse {
            let text = text(self.glyph).font(style).color(ink).single_line().absolute(left, top);
            // Rises from half to full and back, as Qt's SequentialAnimation does from its 0.5.
            return if still {
                mark.child(text.opacity(0.5))
            } else {
                mark.child(text).looping(LoopSpec::opacity(Self::PULSE, 0.0, 0.5, 1.0))
            };
        }
        // Rune repeats a turn only for an icon, so the glyph turns as its own outline, its ink
        // where the text would draw it. Still, or without an outline, it is the text itself.
        let outline = bounds.filter(|_| !still).and_then(|[x, y, w, h]| {
            let centre = [left + x + w / 2.0, top + y + h / 2.0];
            super::turn::glyph_icon(&self.glyph, &tk.font.family, size as f32, side, centre)
        });
        match outline {
            Some(glyph) => mark.child(
                rune::icon(glyph, side).absolute(0.0, 0.0).color(ink).looping(LoopSpec::rotation(Self::TURN)),
            ),
            None => mark.child(text(self.glyph).font(style).color(ink).single_line().absolute(left, top)),
        }
    }
}

impl Default for Spinner {
    fn default() -> Self {
        Spinner::new()
    }
}

/// What a list or panel shows when it has nothing: glyph, title, optional hint and action,
/// centered in the width it is given. `overlay(false)` (the default) is a panel's quiet line;
/// `overlay(true)` is a picker overlay's: a displayLarge glyph in the menu's selected text and a
/// title at 0.7 opacity.
pub struct EmptyState {
    glyph: String,
    title: String,
    hint: String,
    action: String,
    overlay: bool,
    cursor: bool,
    on_action: Option<Attach>,
}

impl EmptyState {
    pub fn new(title: impl Into<String>) -> EmptyState {
        EmptyState {
            glyph: String::new(),
            title: title.into(),
            hint: String::new(),
            action: String::new(),
            overlay: false,
            cursor: false,
            on_action: None,
        }
    }
    pub fn glyph(mut self, g: impl Into<String>) -> Self {
        self.glyph = g.into();
        self
    }
    pub fn hint(mut self, h: impl Into<String>) -> Self {
        self.hint = h.into();
        self
    }
    /// A bordered button under the text.
    pub fn action(mut self, label: impl Into<String>) -> Self {
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
    pub fn on_action<S: 'static>(mut self, f: impl Fn(&mut S) + 'static) -> Self {
        self.on_action = Some(Box::new(move |e: El| e.on_press(f)));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let f = &tk.font;
        let (gap, glyph_size, glyph_ink, glyph_opacity, title_size, title_ink, title_opacity, hint_size, hint_ink) = if self.overlay {
            (sp.lg, f.display_large, color(tk.surfaces.menu.selected_text), 0.8, f.title, t.fg(), 0.7, f.body, t.dim())
        } else {
            (sp.sm, f.icon_large, t.dim(), 1.0, f.body, t.dim(), 1.0, f.body_small, t.faint())
        };
        let mut c = col().w_full().items_center().gap(gap as f32);
        if !self.glyph.is_empty() {
            c = c.child(glyph(t, &self.glyph, glyph_size, glyph_ink).opacity(glyph_opacity));
        }
        c = c.child(
            text(self.title).font(t.text_style(title_size)).color(title_ink).align(Align::Center).wrap().w_full().opacity(title_opacity),
        );
        if !self.hint.is_empty() {
            c = c.child(text(self.hint).font(t.text_style(hint_size)).color(hint_ink).align(Align::Center).wrap().w_full());
        }
        if !self.action.is_empty() {
            c = c.child(attached(Button::new(self.action).bordered(true).cursor(self.cursor).build(t), self.on_action));
        }
        c
    }
}

/// An inline message: a tone glyph, the text, an optional action and dismiss. A foreground-glass
/// fill with a border in the tone; never a filled saturated background.
pub struct Banner {
    text: String,
    tone: Tone,
    glyph: Option<String>,
    action: String,
    dismissible: bool,
    cursor: bool,
    on_action: Option<Attach>,
    on_dismiss: Option<Attach>,
}

impl Banner {
    pub fn new(text: impl Into<String>) -> Banner {
        Banner {
            text: text.into(),
            tone: Tone::Info,
            glyph: None,
            action: String::new(),
            dismissible: false,
            cursor: false,
            on_action: None,
            on_dismiss: None,
        }
    }
    /// Info (the default), Success, Warning or Error. Neutral draws as Info.
    pub fn tone(mut self, tone: Tone) -> Self {
        self.tone = tone;
        self
    }
    /// Replaces the tone's own glyph.
    pub fn glyph(mut self, g: impl Into<String>) -> Self {
        self.glyph = Some(g.into());
        self
    }
    pub fn action(mut self, label: impl Into<String>) -> Self {
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
    pub fn on_action<S: 'static>(mut self, f: impl Fn(&mut S) + 'static) -> Self {
        self.on_action = Some(Box::new(move |e: El| e.on_press(f)));
        self
    }
    pub fn on_dismiss<S: 'static>(mut self, f: impl Fn(&mut S) + 'static) -> Self {
        self.on_dismiss = Some(Box::new(move |e: El| e.on_press(f)));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let tone = if self.tone == Tone::Neutral { Tone::Info } else { self.tone };
        let ink = tone.color(t);
        let line = tk.controls.normal.border.max_width().max(1.0) as f32;
        let btn_bw = tk.controls.normal.border.max_width().max(tk.controls.hover.border.max_width()) as f32;
        let btn_min_h = tk.font.line_px(tk.font.body) as f32 + (sp.control_padding_y as f32 + btn_bw) * 2.0;
        let inner_min_h = (sp.control_height as f32).max(btn_min_h);
        let trailing = !self.action.is_empty() || self.dismissible;
        let mark = self.glyph.unwrap_or_else(|| tone.glyph().into());

        let mut r = row()
            .w_full()
            .items_center()
            .gap(sp.control_gap as f32)
            .min_h(inner_min_h + (sp.sm as f32 + line) * 2.0)
            .pl(sp.control_padding_x as f32 + line)
            .pr(if trailing { sp.sm } else { sp.control_padding_x } as f32 + line)
            .py(sp.sm as f32 + line)
            .fill(color(tk.controls.normal.fill))
            .border(line, ink)
            .radius(t.radius())
            .role("alert")
            .label(self.text.clone())
            .child(glyph(t, &mark, tk.font.icon, ink))
            .child(text(self.text).font(t.text_style(tk.font.body)).color(t.fg()).wrap().grow(1.0).min_w(0.0));
        if !self.action.is_empty() {
            r = r.child(attached(Button::new(self.action).bordered(true).cursor(self.cursor).build(t), self.on_action));
        }
        if self.dismissible {
            r = r.child(attached(IconButton::new("\u{f0156}").label("Dismiss").build(t), self.on_dismiss));
        }
        r
    }
}

/// A transient in-app message on the [notifications] surface: optional glyph, bold title, text,
/// optional action, and a countdown line in `notifications.countdown`. It draws one state; the
/// app owns when it shows and the timer behind `countdown`. Desktop notifications go through
/// `omarchy-notification-send`, not this.
pub struct Toast {
    glyph: String,
    title: String,
    text: String,
    action: String,
    countdown: Option<f32>,
    cursor: bool,
    width: Option<f32>,
    on_action: Option<Attach>,
}

impl Toast {
    pub fn new(title: impl Into<String>) -> Toast {
        Toast {
            glyph: String::new(),
            title: title.into(),
            text: String::new(),
            action: String::new(),
            countdown: None,
            cursor: false,
            width: None,
            on_action: None,
        }
    }
    pub fn glyph(mut self, g: impl Into<String>) -> Self {
        self.glyph = g.into();
        self
    }
    pub fn text(mut self, s: impl Into<String>) -> Self {
        self.text = s.into();
        self
    }
    pub fn action(mut self, label: impl Into<String>) -> Self {
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
    pub fn on_action<S: 'static>(mut self, f: impl Fn(&mut S) + 'static) -> Self {
        self.on_action = Some(Box::new(move |e: El| e.on_press(f)));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let s = &tk.surfaces.notifications;
        let bw = s.base.border.widths.map(|w| w as f32);
        let ink = color(s.base.text);
        let body = t.text_style(tk.font.body);

        let mut words = col().grow(1.0).min_w(0.0).gap(t.space(3.0)).child(
            text(self.title.clone()).font(t.text_style_bold(tk.font.body)).color(ink).single_line(),
        );
        if !self.text.is_empty() {
            words = words.child(text(self.text).font(body).color(color(s.base.text.with_alpha(0.78))).wrap());
        }
        let mut content = row().w_full().items_center().gap(t.space(12.0));
        if !self.glyph.is_empty() {
            content = content.child(glyph(t, &self.glyph, tk.font.icon_large, ink));
        }
        content = content.child(words);
        if !self.action.is_empty() {
            content = content.child(attached(Button::new(self.action).bordered(true).build(t), self.on_action));
        }

        let line = t.space(2.0).max(1.0);
        let shift = if self.countdown.is_some() { (line / 2.0).round() } else { 0.0 };
        let mut e = col()
            .justify_center()
            .pt(t.space(10.0) + bw[0] - shift)
            .pr(t.space(12.0) + bw[1])
            .pb(t.space(10.0) + bw[2] + shift)
            .pl(t.space(12.0) + bw[3])
            .fill(color(s.base.background))
            .radius(t.radius())
            .role("alert")
            .label(self.title);
        e = border(e, &s.base.border);
        e = e.w(self.width.unwrap_or_else(|| t.space(360.0)));
        if self.cursor {
            let hover = color(tk.controls.hover.fill);
            let inner_r = (t.radius() - bw[3].max(bw[0])).max(0.0);
            e = e.child(el().pin(Some(bw[3]), Some(bw[0]), Some(bw[1]), Some(bw[2])).radius(inner_r).fill(hover));
        }
        e = e.child(content);
        if let Some(left) = self.countdown {
            let r = t.radius();
            e = e.child(
                row()
                    .pin(Some(bw[3] + r), None, Some(bw[1] + r), Some(bw[2]))
                    .h(line)
                    .child(el().h_full().w_frac(left.clamp(0.0, 1.0)).fill(color(s.countdown))),
            );
        }
        e
    }
}
