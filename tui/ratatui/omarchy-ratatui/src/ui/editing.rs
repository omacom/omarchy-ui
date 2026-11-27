//! The pieces media and image editors need (spec/terminal.md, Editing). Handles and the playhead
//! are accent; clips, lanes and swatches foreground glass.

use crossterm::event::{KeyCode, KeyEvent, KeyModifiers};
use omarchy_theme::Rgba;
use ratatui::buffer::Buffer;
use ratatui::layout::Rect;

use super::TextField;
use crate::paint::{Ink, edges, fill, hline, put, text, text_fit, width};
use crate::{Draw, Theme, over};

/// m:ss (h:mm:ss from an hour; tenths below a 1 s step).
pub fn clock_label(secs: f64, tenths: bool) -> String {
    if tenths {
        let t = (secs * 10.0).round() as i64;
        return format!("{}:{:02}.{}", t / 600, (t / 10) % 60, t % 10);
    }
    let s = secs.round() as i64;
    if s >= 3600 { format!("{}:{:02}:{:02}", s / 3600, (s / 60) % 60, s % 60) } else { format!("{}:{:02}", s / 60, s % 60) }
}

// ---------------------------------------------------------------------------------------------
// Timeline

#[derive(Clone, Debug)]
pub struct Clip {
    pub start: f64,
    pub end: f64,
    pub label: String,
}

impl Clip {
    pub fn new(start: f64, end: f64, label: impl Into<String>) -> Self {
        Clip { start, end, label: label.into() }
    }
}

/// The labeled step: the first of 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30 s, 1, 2, 5, 10, 15, 30 min,
/// 1 h that keeps major ticks at least 11 columns apart.
pub fn ruler_step(duration: f64, cols: u16) -> f64 {
    const STEPS: [f64; 16] = [0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0, 900.0, 1800.0, 3600.0];
    let per_sec = cols as f64 / duration.max(1e-9);
    STEPS.iter().copied().find(|s| s * per_sec >= 11.0).unwrap_or(3600.0)
}

/// A ruler over clip tracks with a playhead. Ruler: a label row (m:ss in `textDim`, centered on
/// the tick, kept inside) over a tick row (`│` major, `╷` minor at a fifth of a step, `textFaint`).
/// Lanes share the rows under it, filled with the normal fill; clips are the controls' glass with
/// edges, their label on the lane's middle row. The selected clip: the selected fill, a bold label,
/// trim handles (accent cells with a grip) for edges. The playhead: `▼` on the tick row and `│`
/// down the lanes, accent (hot accent with the cursor). x(t) = round(t × w / duration).
pub struct Timeline {
    duration: f64,
    position: f64,
    tracks: Vec<Vec<Clip>>,
    selected: Option<(usize, usize)>,
    cursor: bool,
    width: u16,
    height: u16,
}

impl Timeline {
    pub fn new(duration: f64) -> Self {
        Timeline { duration: duration.max(0.001), position: 0.0, tracks: Vec::new(), selected: None, cursor: false, width: 90, height: 10 }
    }
    pub fn position(mut self, t: f64) -> Self {
        self.position = t;
        self
    }
    pub fn track(mut self, clips: impl IntoIterator<Item = Clip>) -> Self {
        self.tracks.push(clips.into_iter().collect());
        self
    }
    pub fn selected(mut self, s: Option<(usize, usize)>) -> Self {
        self.selected = s;
        self
    }
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    pub fn size_cells(mut self, w: u16, h: u16) -> Self {
        self.width = w;
        self.height = h;
        self
    }
    /// The column of time `t` in a timeline `w` wide.
    pub fn x_of(t: f64, duration: f64, w: u16) -> u16 {
        ((t / duration * w as f64).round().max(0.0) as u16).min(w.saturating_sub(1))
    }
    /// The time at a column.
    pub fn time_at(col: u16, duration: f64, w: u16) -> f64 {
        (col as f64 / w.max(1) as f64 * duration).clamp(0.0, duration)
    }
    /// Left/Right ±1 s (Shift: a labeled step), Home/End; true when handled.
    pub fn handle_key(position: &mut f64, duration: f64, cols: u16, key: &KeyEvent) -> bool {
        let step = if key.modifiers.contains(KeyModifiers::SHIFT) { ruler_step(duration, cols) } else { 1.0 };
        *position = match key.code {
            KeyCode::Left | KeyCode::Char('h') => *position - step,
            KeyCode::Right | KeyCode::Char('l') => *position + step,
            KeyCode::Home => 0.0,
            KeyCode::End => duration,
            _ => return false,
        }
        .clamp(0.0, duration);
        true
    }
}

/// A trim cap at (x, y), `h` rows: accent cells, the grip `│` in the background on the middle
/// row(s) (40% of the height, at least one row; 60% hot).
fn trim_cap(t: &Theme, buf: &mut Buffer, x: u16, y: u16, h: u16, hot: bool) {
    let ink = if hot { t.hot_accent() } else { t.accent() };
    fill(t, buf, Rect::new(x, y, 1, h), ink);
    let g = ((h as f64 * if hot { 0.6 } else { 0.4 }).round() as u16).clamp(1, h);
    let top = y + (h - g) / 2;
    for dy in 0..g {
        put(t, buf, x, top + dy, "│", t.bg());
    }
}

impl Draw for Timeline {
    fn size(&self, _: &Theme) -> (u16, u16) {
        (self.width, self.height)
    }
    fn draw(&self, t: &Theme, area: Rect, buf: &mut Buffer) {
        let c = &t.shown().controls;
        let w = area.width;
        let step = ruler_step(self.duration, w);
        let tenths = step < 1.0;
        let minor = step / 5.0;
        // Ticks first, then labels over the label row.
        let n_minor = (self.duration / minor).floor() as i64;
        for i in 0..=n_minor {
            let tm = i as f64 * minor;
            let x = area.x + Timeline::x_of(tm, self.duration, w);
            put(t, buf, x, area.y + 1, if i % 5 == 0 { "│" } else { "╷" }, t.faint());
        }
        let n_major = (self.duration / step).floor() as i64;
        for i in 0..=n_major {
            let tm = i as f64 * step;
            let label = clock_label(tm, tenths);
            let lw = width(&label);
            let cx = Timeline::x_of(tm, self.duration, w) as i32 - (lw as i32 - 1) / 2;
            let lx = cx.clamp(0, (w - lw) as i32) as u16;
            text(t, buf, area.x + lx, area.y, &label, lw, t.dim());
        }
        let lanes_h = area.height.saturating_sub(2);
        let n = self.tracks.len().max(1) as u16;
        let lane_h = lanes_h / n;
        for (ti, clips) in self.tracks.iter().enumerate() {
            let ly = area.y + 2 + ti as u16 * lane_h;
            let lh = if ti as u16 == n - 1 { lanes_h - lane_h * (n - 1) } else { lane_h };
            if lh == 0 || w < 2 {
                continue;
            }
            fill(t, buf, Rect::new(area.x, ly, w, lh), c.normal.fill);
            for (ci, clip) in clips.iter().enumerate() {
                let x0 = Timeline::x_of(clip.start, self.duration, w);
                let x1 = (Timeline::x_of(clip.end, self.duration, w)).max(x0 + 2);
                let cw = (x1 - x0).min(w - x0);
                let selected = self.selected == Some((ti, ci));
                let r = Rect::new(area.x + x0, ly, cw, lh);
                if selected {
                    fill(t, buf, r, c.selected.fill);
                    trim_cap(t, buf, r.x, ly, lh, false);
                    trim_cap(t, buf, r.right() - 1, ly, lh, false);
                } else {
                    fill(t, buf, r, c.normal.fill);
                    for dy in 0..lh {
                        edges(t, buf, r.x, ly + dy, cw, Some(c.normal.border.color));
                    }
                }
                text_fit(t, buf, r.x + 2, ly + (lh - 1) / 2, &clip.label, cw.saturating_sub(4), Ink::new(t.fg()).bold(selected));
                t.hit(format!("clip-{ti}-{ci}"), r);
            }
        }
        let px = area.x + Timeline::x_of(self.position, self.duration, w);
        let head = if self.cursor { t.hot_accent() } else { t.accent() };
        put(t, buf, px, area.y + 1, "▼", head);
        for dy in 2..area.height {
            put(t, buf, px, area.y + dy, "│", head);
        }
        t.hit("timeline-ruler", Rect::new(area.x, area.y, w, 2));
    }
}

// ---------------------------------------------------------------------------------------------
// Scrubber

/// `0:42 ━━━━━━●────── 2:10`: the times in `textDim`, the Slider's track (no edges) a column from
/// each. The knob is never bold: the cursor has no cell form (the GUI knob's 1.15 scale).
pub struct Scrubber {
    position: f64,
    duration: f64,
    width: u16,
}

impl Scrubber {
    pub fn new(position: f64, duration: f64) -> Self {
        Scrubber { position, duration: duration.max(0.001), width: 45 }
    }
    pub fn width(mut self, w: u16) -> Self {
        self.width = w;
        self
    }
    /// Taken for the GUI bindings' API; draws nothing (the hot knob's scale has no cell form).
    pub fn cursor(self, _on: bool) -> Self {
        self
    }
    /// Left/Right ±5 s (Shift ±30 s), Home/End; true when handled.
    pub fn handle_key(position: &mut f64, duration: f64, key: &KeyEvent) -> bool {
        let step = if key.modifiers.contains(KeyModifiers::SHIFT) { 30.0 } else { 5.0 };
        *position = match key.code {
            KeyCode::Left | KeyCode::Char('h') => *position - step,
            KeyCode::Right | KeyCode::Char('l') => *position + step,
            KeyCode::Home => 0.0,
            KeyCode::End => duration,
            _ => return false,
        }
        .clamp(0.0, duration);
        true
    }
}

impl Draw for Scrubber {
    fn size(&self, _: &Theme) -> (u16, u16) {
        (self.width, 1)
    }
    fn draw(&self, t: &Theme, area: Rect, buf: &mut Buffer) {
        let a = clock_label(self.position, false);
        let b = clock_label(self.duration, false);
        let (aw, bw) = (width(&a), width(&b));
        text(t, buf, area.x, area.y, &a, aw, t.dim());
        text(t, buf, area.right().saturating_sub(bw), area.y, &b, bw, t.dim());
        let bar = Rect::new(area.x + aw + 1, area.y, area.width.saturating_sub(aw + bw + 2), 1);
        let w = bar.width;
        if w < 2 {
            return;
        }
        let k = ((self.position / self.duration).clamp(0.0, 1.0) * (w - 1) as f64).round() as u16;
        for i in 0..w {
            if i < k {
                put(t, buf, bar.x + i, bar.y, "━", t.fg());
            } else if i > k {
                put(t, buf, bar.x + i, bar.y, "─", t.track());
            }
        }
        put(t, buf, bar.x + k, bar.y, "●", t.fg());
        t.hit("scrubber", bar);
    }
}

// ---------------------------------------------------------------------------------------------
// TrimHandle, CropFrame, DragHandle, DropIndicator

/// Which edge or corner a handle is on.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
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

/// A draggable in/out handle. Trim (left/right): an accent cap a column wide, `height` rows, with
/// the grip cut in the background. Crop: the heavy accent glyphs a CropFrame draws (`┏━`, `━━`,
/// `┃`).
pub struct TrimHandle {
    edge: Edge,
    crop: bool,
    cursor: bool,
    height: u16,
}

impl TrimHandle {
    pub fn new(edge: Edge) -> Self {
        TrimHandle { edge, crop: false, cursor: false, height: 3 }
    }
    pub fn crop(edge: Edge) -> Self {
        TrimHandle { edge, crop: true, cursor: false, height: 1 }
    }
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    pub fn height(mut self, h: u16) -> Self {
        self.height = h;
        self
    }
}

/// A crop handle's cells relative to its anchor (the corner cell, or the middle of a side).
fn crop_cells(edge: Edge) -> Vec<(i32, i32, &'static str)> {
    match edge {
        Edge::TopLeft => vec![(0, 0, "┏"), (1, 0, "━"), (0, 1, "┃")],
        Edge::TopRight => vec![(0, 0, "┓"), (-1, 0, "━"), (0, 1, "┃")],
        Edge::BottomLeft => vec![(0, 0, "┗"), (1, 0, "━"), (0, -1, "┃")],
        Edge::BottomRight => vec![(0, 0, "┛"), (-1, 0, "━"), (0, -1, "┃")],
        Edge::Top | Edge::Bottom => vec![(0, 0, "━"), (1, 0, "━")],
        Edge::Left | Edge::Right => vec![(0, 0, "┃")],
    }
}

impl Draw for TrimHandle {
    fn size(&self, _: &Theme) -> (u16, u16) {
        if self.crop {
            match self.edge {
                Edge::Top | Edge::Bottom => (2, 1),
                Edge::Left | Edge::Right => (1, 1),
                _ => (2, 2),
            }
        } else {
            (1, self.height)
        }
    }
    fn draw(&self, t: &Theme, area: Rect, buf: &mut Buffer) {
        if !self.crop {
            return trim_cap(t, buf, area.x, area.y, area.height, self.cursor);
        }
        let ink = if self.cursor { t.hot_accent() } else { t.accent() };
        let (ax, ay) = match self.edge {
            Edge::TopRight => (area.right() as i32 - 1, area.y as i32),
            Edge::BottomLeft => (area.x as i32, area.bottom() as i32 - 1),
            Edge::BottomRight => (area.right() as i32 - 1, area.bottom() as i32 - 1),
            _ => (area.x as i32, area.y as i32),
        };
        for (dx, dy, s) in crop_cells(self.edge) {
            put(t, buf, (ax + dx) as u16, (ay + dy) as u16, s, Ink::new(ink).bold(true));
        }
    }
}

/// A crop rectangle: a light accent outline, rule-of-thirds guides (`┆`, `┄` in `track`) and the
/// eight crop handles in heavy accent strokes inside its edges.
pub struct CropFrame {
    width: u16,
    height: u16,
    cursor: Option<Edge>,
}

impl CropFrame {
    pub fn new(width: u16, height: u16) -> Self {
        CropFrame { width, height, cursor: None }
    }
    pub fn cursor(mut self, e: Option<Edge>) -> Self {
        self.cursor = e;
        self
    }
}

impl Draw for CropFrame {
    fn size(&self, _: &Theme) -> (u16, u16) {
        (self.width, self.height)
    }
    fn draw(&self, t: &Theme, area: Rect, buf: &mut Buffer) {
        let (w, h) = (area.width, area.height);
        if w < 4 || h < 3 {
            return;
        }
        for i in 1..3u16 {
            let gx = area.x + (w * i) / 3;
            for dy in 1..h - 1 {
                put(t, buf, gx, area.y + dy, "┆", t.track());
            }
        }
        for i in 1..3u16 {
            let gy = area.y + (h * i) / 3;
            for dx in 1..w - 1 {
                let x = area.x + dx;
                let cross = (1..3u16).any(|j| area.x + (w * j) / 3 == x);
                put(t, buf, x, gy, if cross { "┼" } else { "┄" }, t.track());
            }
        }
        // The outline is square: its corners are the corner handles.
        hline(t, buf, area.x, area.y, w, "─", t.accent());
        hline(t, buf, area.x, area.bottom() - 1, w, "─", t.accent());
        for dy in 0..h {
            put(t, buf, area.x, area.y + dy, "│", t.accent());
            put(t, buf, area.right() - 1, area.y + dy, "│", t.accent());
        }
        let hot = |e: Edge| self.cursor == Some(e);
        let (mx, my) = (area.x + (w - 2) / 2, area.y + (h - 1) / 2);
        let place = |buf: &mut Buffer, e: Edge, r: Rect| TrimHandle::crop(e).cursor(hot(e)).draw(t, r, buf);
        place(buf, Edge::TopLeft, area);
        place(buf, Edge::TopRight, area);
        place(buf, Edge::BottomLeft, area);
        place(buf, Edge::BottomRight, area);
        place(buf, Edge::Top, Rect::new(mx, area.y, 2, 1));
        place(buf, Edge::Bottom, Rect::new(mx, area.bottom() - 1, 2, 1));
        place(buf, Edge::Left, Rect::new(area.x, my, 1, 1));
        place(buf, Edge::Right, Rect::new(area.right() - 1, my, 1, 1));
    }
}

/// The grip a row is dragged by: `⠿` (a 2×3 grid of dots), `textDim`, the foreground when hot.
pub struct DragHandle {
    cursor: bool,
}

impl Default for DragHandle {
    fn default() -> Self {
        Self::new()
    }
}

impl DragHandle {
    pub fn new() -> Self {
        DragHandle { cursor: false }
    }
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
}

impl Draw for DragHandle {
    fn size(&self, _: &Theme) -> (u16, u16) {
        (1, 1)
    }
    fn draw(&self, t: &Theme, area: Rect, buf: &mut Buffer) {
        put(t, buf, area.x, area.y, "⠿", if self.cursor { t.fg() } else { t.dim() });
    }
}

/// Where a dragged row will land: `━` in accent across the list. Cells can't draw between rows,
/// so it takes a row of its own at the drop slot.
pub struct DropIndicator(pub u16);

impl Draw for DropIndicator {
    fn size(&self, _: &Theme) -> (u16, u16) {
        (self.0, 1)
    }
    fn draw(&self, t: &Theme, area: Rect, buf: &mut Buffer) {
        hline(t, buf, area.x, area.y, area.width, "━", t.accent());
    }
}

// ---------------------------------------------------------------------------------------------
// ColorPicker

/// Hue (0–1 turns), saturation, value, alpha, all 0–1.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Hsva {
    pub h: f64,
    pub s: f64,
    pub v: f64,
    pub a: f64,
}

impl Hsva {
    pub fn new(h: f64, s: f64, v: f64, a: f64) -> Self {
        Hsva { h, s, v, a }
    }
    /// The 0–255 channels, by the same arithmetic as every binding's picker.
    pub fn rgb8(self) -> [u8; 3] {
        let h6 = (self.h - self.h.floor()) * 6.0;
        let i = h6.floor();
        let f = h6 - i;
        let (s, v) = (self.s, self.v);
        let (p, q, t) = (v * (1.0 - s), v * (1.0 - s * f), v * (1.0 - s * (1.0 - f)));
        let c = match (i as i32).rem_euclid(6) {
            0 => [v, t, p],
            1 => [q, v, p],
            2 => [p, v, t],
            3 => [p, q, v],
            4 => [t, p, v],
            _ => [v, p, q],
        };
        c.map(|x| (x * 255.0).round().clamp(0.0, 255.0) as u8)
    }
    pub fn rgba(self) -> Rgba {
        let [r, g, b] = self.rgb8();
        Rgba { r, g, b, a: (self.a * 255.0).round() as u8 }
    }
    /// `#rrggbb`, or `#rrggbbaa` below full alpha.
    pub fn hex(self) -> String {
        let [r, g, b] = self.rgb8();
        let mut out = format!("#{r:02x}{g:02x}{b:02x}");
        if self.a < 1.0 {
            out.push_str(&format!("{:02x}", (self.a * 255.0).round() as u8));
        }
        out
    }
    /// From `#rrggbb[aa]`; the hue kept from `keep` when the color is gray.
    pub fn from_hex(hex: &str, keep: f64) -> Option<Hsva> {
        let c = omarchy_theme::color::parse_color(hex, 1.0)?;
        let (r, g, b) = (c.r as f64 / 255.0, c.g as f64 / 255.0, c.b as f64 / 255.0);
        let max = r.max(g).max(b);
        let min = r.min(g).min(b);
        let d = max - min;
        let h = if d == 0.0 {
            keep
        } else if max == r {
            ((g - b) / d).rem_euclid(6.0) / 6.0
        } else if max == g {
            ((b - r) / d + 2.0) / 6.0
        } else {
            ((r - g) / d + 4.0) / 6.0
        };
        Some(Hsva { h, s: if max == 0.0 { 0.0 } else { d / max }, v: max, a: c.a as f64 / 255.0 })
    }
}

/// An HSV picker in half-block pixels (two per cell, `▀`): the square (saturation across, value
/// down) with its knob `●` in the picked color on the background; a hue strip and an alpha strip,
/// 2 columns each, a column apart, with knob rows `◀▶`… drawn as `●`; a row under them: the
/// preview `██` and a TextField with the hex; a blank row; the theme palette's swatches `██` a
/// column apart, the current one marked `▔▔` in the foreground on the row under.
pub struct ColorPicker {
    color: Hsva,
    alpha: bool,
    width: u16,
    height: u16,
    palette: Vec<String>,
    cursor: bool,
    hex: Option<String>,
}

impl ColorPicker {
    pub fn new(color: Hsva) -> Self {
        ColorPicker {
            color,
            alpha: true,
            width: 38,
            height: 16,
            palette: ["red", "orange", "yellow", "green", "cyan", "blue", "magenta", "foreground"].map(String::from).to_vec(),
            cursor: false,
            hex: None,
        }
    }
    pub fn alpha(mut self, on: bool) -> Self {
        self.alpha = on;
        self
    }
    pub fn size_cells(mut self, w: u16, h: u16) -> Self {
        self.width = w;
        self.height = h;
        self
    }
    pub fn palette(mut self, names: impl IntoIterator<Item = impl Into<String>>) -> Self {
        self.palette = names.into_iter().map(Into::into).collect();
        self
    }
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    /// The hex field's text (else the color's hex).
    pub fn hex(mut self, s: impl Into<String>) -> Self {
        self.hex = Some(s.into());
        self
    }
    fn swatches(&self, t: &Theme) -> Vec<Rgba> {
        let p = &t.shown().palette;
        self.palette
            .iter()
            .filter_map(|n| match n.as_str() {
                "foreground" => Some(p.foreground),
                "background" => Some(p.background),
                "accent" => Some(p.accent),
                other => p.named.get(other).copied(),
            })
            .collect()
    }
    /// Arrows move the square's knob 1% (Shift 10%); true when handled.
    pub fn handle_key(c: &mut Hsva, key: &KeyEvent) -> bool {
        let d = if key.modifiers.contains(KeyModifiers::SHIFT) { 0.1 } else { 0.01 };
        match key.code {
            KeyCode::Left | KeyCode::Char('h') => c.s = (c.s - d).clamp(0.0, 1.0),
            KeyCode::Right | KeyCode::Char('l') => c.s = (c.s + d).clamp(0.0, 1.0),
            KeyCode::Up | KeyCode::Char('k') => c.v = (c.v + d).clamp(0.0, 1.0),
            KeyCode::Down | KeyCode::Char('j') => c.v = (c.v - d).clamp(0.0, 1.0),
            _ => return false,
        }
        true
    }
}

impl Draw for ColorPicker {
    fn size(&self, _: &Theme) -> (u16, u16) {
        (self.width, self.height)
    }
    fn draw(&self, t: &Theme, area: Rect, buf: &mut Buffer) {
        let strips = if self.alpha { 6 } else { 3 };
        let sq_w = area.width.saturating_sub(strips);
        let sq_h = area.height.saturating_sub(5);
        if sq_w < 2 || sq_h < 1 {
            return;
        }
        let ph = sq_h as u32 * 2;
        let pixel = |i: u16, j: u32| -> Rgba {
            let s = i as f64 / (sq_w - 1).max(1) as f64;
            let v = 1.0 - j as f64 / (ph - 1).max(1) as f64;
            Hsva::new(self.color.h, s, v, 1.0).rgba()
        };
        for row in 0..sq_h {
            for i in 0..sq_w {
                let (x, y) = (area.x + i, area.y + row);
                fill(t, buf, Rect::new(x, y, 1, 1), pixel(i, row as u32 * 2 + 1));
                put(t, buf, x, y, "▀", pixel(i, row as u32 * 2));
            }
        }
        let picked = Hsva { a: 1.0, ..self.color }.rgba();
        let kx = area.x + (self.color.s.clamp(0.0, 1.0) * (sq_w - 1) as f64).round() as u16;
        let ky = area.y + ((1.0 - self.color.v.clamp(0.0, 1.0)) * (sq_h - 1) as f64).round() as u16;
        fill(t, buf, Rect::new(kx, ky, 1, 1), t.bg());
        put(t, buf, kx, ky, "●", picked);
        // Hue strip: red at the top through the hues back to red.
        let hx = area.x + sq_w + 1;
        for row in 0..sq_h {
            for (k, half) in [(0u32, true), (1, false)] {
                let j = row as u32 * 2 + k;
                let c = Hsva::new(j as f64 / (ph - 1).max(1) as f64, 1.0, 1.0, 1.0).rgba();
                for dx in 0..2 {
                    if half {
                        put(t, buf, hx + dx, area.y + row, "▀", c);
                    } else {
                        fill(t, buf, Rect::new(hx + dx, area.y + row, 1, 1), c);
                    }
                }
            }
        }
        let hy = area.y + (self.color.h.rem_euclid(1.0) * (sq_h - 1) as f64).round() as u16;
        fill(t, buf, Rect::new(hx, hy, 2, 1), t.bg());
        put(t, buf, hx, hy, "◀", t.fg());
        put(t, buf, hx + 1, hy, "▶", t.fg());
        if self.alpha {
            let ax = hx + 3;
            for row in 0..sq_h {
                for (k, half) in [(0u32, true), (1, false)] {
                    let j = row as u32 * 2 + k;
                    let a = 1.0 - j as f64 / (ph - 1).max(1) as f64;
                    let c = over(picked.with_alpha(a), over(t.track(), t.bg()));
                    for dx in 0..2 {
                        if half {
                            put(t, buf, ax + dx, area.y + row, "▀", c);
                        } else {
                            fill(t, buf, Rect::new(ax + dx, area.y + row, 1, 1), c);
                        }
                    }
                }
            }
            let ay = area.y + ((1.0 - self.color.a.clamp(0.0, 1.0)) * (sq_h - 1) as f64).round() as u16;
            fill(t, buf, Rect::new(ax, ay, 2, 1), t.bg());
            put(t, buf, ax, ay, "◀", t.fg());
            put(t, buf, ax + 1, ay, "▶", t.fg());
        }
        // Preview and hex.
        let fy = area.y + sq_h + 1;
        let shown = over(self.color.rgba(), t.bg());
        put(t, buf, area.x, fy, "█", shown);
        put(t, buf, area.x + 1, fy, "█", shown);
        let hex = self.hex.clone().unwrap_or_else(|| self.color.hex());
        TextField::new(hex).width(area.width.saturating_sub(3)).cursor(self.cursor).draw(t, Rect::new(area.x + 3, fy, area.width.saturating_sub(3), 1), buf);
        // Palette.
        let py = fy + 2;
        for (i, c) in self.swatches(t).iter().enumerate() {
            let x = area.x + i as u16 * 3;
            if x + 2 > area.right() {
                break;
            }
            put(t, buf, x, py, "█", *c);
            put(t, buf, x + 1, py, "█", *c);
            if c.r == picked.r && c.g == picked.g && c.b == picked.b {
                put(t, buf, x, py + 1, "▔", t.fg());
                put(t, buf, x + 1, py + 1, "▔", t.fg());
            }
            t.hit(format!("swatch-{i}"), Rect::new(x, py, 2, 1));
        }
        t.hit("picker-square", Rect::new(area.x, area.y, sq_w, sq_h));
    }
}
