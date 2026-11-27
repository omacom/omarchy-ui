//! Lists: ListRow, ScrollArea, KeyValue and Hero, plus the CursorModel that drives them and the
//! tile pieces (Badge, StatusDot, Avatar, TileGrid). Drawn as the Qt binding's `ListRow.qml`,
//! `ScrollArea.qml`, `KeyValue.qml`, `Hero.qml`, ... draw them.

mod cursor;
mod tiles;

pub use cursor::{CursorKey, CursorModel, CursorSection, printable};
pub use tiles::{Avatar, AvatarSize, Badge, StatusDot, Tile, TileGrid};

use std::cell::Cell;
use std::rc::Rc;

use omarchy_theme::{BorderSpec, Rgba};
use rune::{El, Live, Phase, ScrollPos, ScrollSpec, col, el, row, text};

use super::{Attach, glyph};
use crate::{Theme, border, color};

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
    pub fn rgba(self, t: &Theme, neutral: Rgba) -> Rgba {
        let p = &t.tokens().palette;
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

/// The width of a ListRow's leading slot: glyphs center in it and a small Avatar fills it, so
/// titles line up down a list whatever leads them.
pub(crate) fn lead_width(t: &Theme) -> f32 {
    t.tokens().font.icon_large as f32
}

/// One row of a keyboard-and-mouse list (the shell's NetworkRow, DeviceRow, ...): a leading glyph
/// or element, a bold title over a dim subtitle and a tinted status line, a trailing value or
/// element, and an end slot holding a trailing glyph at rest and an action in its place under the
/// cursor (the network panel's lock that turns into forget). Like the shell's CursorSurface, the look
/// follows `cursor` and `current` only; the pointer moves the cursor through
/// [`ListRow::on_pointer_move`] and a [`CursorModel`].
pub struct ListRow {
    key: Option<String>,
    glyph: String,
    glyph_color: Option<Rgba>,
    leading: Option<El>,
    title: String,
    subtitle: String,
    status: String,
    tone: Tone,
    value: String,
    trailing: Option<El>,
    trailing_glyph: String,
    trailing_tone: Tone,
    expansion: Option<El>,
    action: String,
    action_urgent: bool,
    action_cursor: bool,
    cursor: bool,
    current: bool,
    disabled: bool,
    width: Option<f32>,
    press: Option<Attach>,
    action_press: Option<Attach>,
    pointer: Option<Attach>,
}

impl ListRow {
    pub fn new(title: impl Into<String>) -> ListRow {
        ListRow {
            key: None,
            glyph: String::new(),
            glyph_color: None,
            leading: None,
            title: title.into(),
            subtitle: String::new(),
            status: String::new(),
            tone: Tone::Neutral,
            value: String::new(),
            trailing: None,
            trailing_glyph: String::new(),
            trailing_tone: Tone::Neutral,
            expansion: None,
            action: String::new(),
            action_urgent: false,
            action_cursor: false,
            cursor: false,
            current: false,
            disabled: false,
            width: None,
            press: None,
            action_press: None,
            pointer: None,
        }
    }
    pub fn key(mut self, key: impl Into<String>) -> Self {
        self.key = Some(key.into());
        self
    }
    /// A leading Nerd Font glyph, at `font.title`.
    pub fn glyph(mut self, g: impl Into<String>) -> Self {
        self.glyph = g.into();
        self
    }
    pub fn glyph_color(mut self, c: Rgba) -> Self {
        self.glyph_color = Some(c);
        self
    }
    /// Any leading element instead of a glyph: an [`Avatar`] (`AvatarSize::Small` fills the slot).
    pub fn leading(mut self, e: El) -> Self {
        self.leading = Some(e);
        self
    }
    pub fn subtitle(mut self, s: impl Into<String>) -> Self {
        self.subtitle = s.into();
        self
    }
    /// A status line under the subtitle ("Connected", "Failed"), dim unless `tone` tints it.
    pub fn status(mut self, s: impl Into<String>, tone: Tone) -> Self {
        self.status = s.into();
        self.tone = tone;
        self
    }
    /// Dim text on the trailing edge ("5 GHz", "80%").
    pub fn value(mut self, v: impl Into<String>) -> Self {
        self.value = v.into();
        self
    }
    /// Any trailing element instead of a value: a [`Badge`], a non-interactive ToggleSwitch.
    pub fn trailing(mut self, e: El) -> Self {
        self.trailing = Some(e);
        self
    }
    /// A glyph that stays in the end slot (a lock, signal bars, a drill-in chevron, a check), dim
    /// unless `tone` tints it. An [`action`](Self::action) takes its place while it shows.
    pub fn trailing_glyph(mut self, g: impl Into<String>, tone: Tone) -> Self {
        self.trailing_glyph = g.into();
        self.trailing_tone = tone;
        self
    }
    /// Opens the row: any element across it under the main line, inside the row's fill (the
    /// network panel's password field and Connect button), `space(4)` below.
    pub fn expansion(mut self, e: El) -> Self {
        self.expansion = Some(e);
        self
    }
    /// An action glyph in the end slot, shown while the row has the cursor or the pointer (or the
    /// action has the cursor), in place of any trailing glyph. `urgent` for a destructive one,
    /// like forget.
    pub fn action(mut self, g: impl Into<String>, urgent: bool) -> Self {
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
    pub fn on_press<S: 'static>(mut self, f: impl Fn(&mut S) + 'static) -> Self {
        self.press = Some(Box::new(move |e: El| e.on_press(f)));
        self
    }
    pub fn on_action<S: 'static>(mut self, f: impl Fn(&mut S) + 'static) -> Self {
        self.action_press = Some(Box::new(move |e: El| e.on_press(f)));
        self
    }
    /// The pointer moved over the row, at a window point: pass it to
    /// [`CursorModel::pointer_moved`].
    pub fn on_pointer_move<S: 'static>(mut self, f: impl Fn(&mut S, f32, f32) + 'static) -> Self {
        self.pointer = Some(Box::new(move |e: El| e.on_pointer_move(move |s: &mut S, p| f(s, p.x, p.y))));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
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

        let mut body = row().w_full().items_center();
        let lead = match (self.leading, self.glyph.is_empty()) {
            (Some(e), _) => Some(e),
            (None, false) => {
                let g = self.glyph_color.filter(|_| !self.disabled).unwrap_or(ink);
                Some(glyph(t, &self.glyph, f.title, color(g)))
            }
            (None, true) => None,
        };
        if let Some(e) = lead {
            body = body.child(row().w(lead_width(t)).justify_center().items_center().child(e)).child(el().w(t.space(10.0)));
        }

        let mut info = col().grow(1.0).min_w(0.0).gap(t.space(1.0)).child(
            text(self.title.clone()).font(t.text_style_bold(f.body)).color(color(ink)).single_line().truncate(),
        );
        if !self.subtitle.is_empty() {
            info = info.child(text(self.subtitle).font(t.text_style(f.caption)).color(color(dim)).single_line().truncate());
        }
        if !self.status.is_empty() {
            let tint = if self.disabled { faint } else { self.tone.rgba(t, tk.roles.text_dim) };
            info = info.child(text(self.status).font(t.text_style(f.caption)).color(color(tint)).single_line().truncate());
        }
        body = body.child(info);

        // Trailing: value or element, then the action slot (kept while hidden, so revealing it
        // never reflows the row).
        let gap = t.space(8.0);
        let mut trail = Vec::new();
        if let Some(e) = self.trailing {
            trail.push(e);
        } else if !self.value.is_empty() {
            trail.push(text(self.value).font(t.text_style(f.body_small)).color(color(dim)).single_line());
        }
        // The end slot: the trailing glyph at rest, the action in its place while it shows (under
        // the cursor, or the pointer through the row's hover group).
        let has_action = !self.action.is_empty();
        if has_action || !self.trailing_glyph.is_empty() {
            let side = t.space(22.0);
            let shown = has_action && !self.disabled && (self.cursor || self.action_cursor);
            let mut slot = row().size(side, side).center().radius(t.radius_small());
            if has_action && self.action_cursor && !self.disabled {
                // The cursor look in the action's own ink, whatever color the theme gives the cursor.
                let tone = if self.action_urgent { tk.palette.urgent } else { tk.palette.foreground };
                let w = c.hover.border.max_width();
                slot = border(slot.fill(color(tone.with_alpha(c.hover.fill.alpha_f() as f64))), &BorderSpec {
                    color: tone.with_alpha(c.hover.border.color.alpha_f() as f64),
                    widths: [w; 4],
                    gradient: None,
                });
            }
            if !self.trailing_glyph.is_empty() {
                let tint = if self.disabled { faint } else { self.trailing_tone.rgba(t, tk.roles.text_dim) };
                let mark = glyph(t, &self.trailing_glyph, f.subtitle, color(tint));
                let rest = if shown { 0.0 } else { 1.0 };
                let hovered = if has_action && !self.disabled { 0.0 } else { rest };
                // Stacked under the action, centered in the slot.
                slot = slot.child(row().pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0)).center().child(mark).group_opacity(rest, hovered));
            }
            if has_action {
                let ink = if self.action_urgent { tk.palette.urgent } else { tk.roles.text_dim };
                let rest = if shown { 1.0 } else { 0.0 };
                let mark = glyph(t, &self.action, f.subtitle, color(ink));
                slot = slot.role("button").child(if self.disabled { mark.opacity(0.0) } else { mark.group_opacity(rest, 1.0) });
                if let Some(attach) = self.action_press {
                    slot = attach(slot);
                }
            }
            trail.push(slot);
        }
        if !trail.is_empty() {
            body = body.child(row().items_center().gap(gap).pl(gap).children(trail));
        }

        let mut e = row()
            .items_center()
            .px(sp.control_padding_x as f32)
            .py(sp.row_padding_x as f32 / 2.0)
            .radius(t.radius())
            .fill(color(fill))
            .role("option")
            .label(self.title)
            .disabled(self.disabled)
            .hover_group()
            .child(match self.expansion {
                Some(x) => col().w_full().gap(t.space(4.0)).child(body).child(x),
                None => body,
            });
        e = border(e, &edge);
        e = match self.width {
            Some(w) => e.w(w),
            None => e.w_full(),
        };
        if let Some(k) = self.key {
            e = e.key(k);
        }
        if self.disabled {
            return e;
        }
        if let Some(attach) = self.pointer {
            e = attach(e);
        }
        match self.press {
            Some(attach) => attach(e),
            None => e,
        }
    }
}

/// A vertical scroll viewport capped at `max_height`, with Omarchy's thin scrollbar beside it: a
/// `track`-colored groove `scrollbarThickness` wide and a foreground-glass thumb, both
/// `radius_small`. The bar shows only while the content overflows and overlays the content's
/// right edge instead of reserving a gutter (as the shell's AsNeeded bars do), so a short list is
/// as wide as its siblings and nothing reflows when the bar appears; rows keep clear of it with
/// their own padding. The thumb drags. Scrolling stops at the ends. Reveal a row with
/// `Tx::scroll_into_view(row_key)` (see [`ScrollArea::ensure_visible`]).
pub struct ScrollArea {
    key: String,
    max_height: Option<f32>,
    width: Option<f32>,
    gap: f32,
    children: Vec<El>,
}

impl ScrollArea {
    /// `key` names the viewport, for `Tx::scroll_to_y` and the like.
    pub fn new(key: impl Into<String>) -> ScrollArea {
        ScrollArea { key: key.into(), max_height: None, width: None, gap: 0.0, children: Vec::new() }
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
    pub fn child(mut self, e: El) -> Self {
        self.children.push(e);
        self
    }
    pub fn children(mut self, es: impl IntoIterator<Item = El>) -> Self {
        self.children.extend(es);
        self
    }

    /// Scroll just far enough to show the row keyed `row` (the cursor's row after a key).
    pub fn ensure_visible(tx: &mut rune::Tx, row: impl Into<String>) {
        tx.scroll_into_view(row);
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let thick = tk.spacing.scrollbar_thickness as f32;
        let rs = t.radius_small();
        let min_thumb = t.space(24.0);
        let thumb = Live::new([0.0f32; 4]);
        let shown = Live::new(0.0f32);
        let pos = Rc::new(Cell::new(ScrollPos::default()));

        let (thumb_w, shown_w, pos_w) = (thumb.clone(), shown.clone(), pos.clone());
        let mut viewport = col()
            .key(self.key.clone())
            .w_full()
            .gap(self.gap)
            .children(self.children)
            .scroll(ScrollSpec { stick_bottom: false })
            .on_scroll_offset(move |at: ScrollPos| {
                pos_w.set(at);
                let track = at.viewport[1];
                let content = track + at.max[1];
                let overflow = at.max[1] > 0.5;
                shown_w.set(if overflow { 1.0 } else { 0.0 });
                if !overflow || content <= 0.0 {
                    thumb_w.set([0.0, 0.0, thick, 0.0]);
                    return;
                }
                let h = (track * track / content).max(min_thumb).min(track);
                let y = (track - h) * (at.offset[1] / at.max[1]).clamp(0.0, 1.0);
                thumb_w.set([0.0, y, thick, h]);
            });
        if let Some(h) = self.max_height {
            viewport = viewport.max_h(h);
        }

        let key = self.key;
        let drag_pos = pos;
        let thumb_el = el()
            .absolute(0.0, 0.0)
            .live_rect(&thumb)
            .radius(rs)
            .fill(color(tk.controls.normal.border.color))
            .hover(color(tk.roles.text_dim))
            .on_drag_any(move |d, tx| {
                let at = drag_pos.get();
                if d.phase == Phase::Down {
                    d.data = at.offset[1];
                    return;
                }
                let track = at.viewport[1];
                let content = track + at.max[1];
                let h = (track * track / content.max(1.0)).max(min_thumb).min(track);
                let run = (track - h).max(1.0);
                tx.scroll_to_y(key.clone(), d.data + d.dy * at.max[1] / run);
            });
        // Over the content's right edge, the full height of the viewport.
        let bar = el()
            .pin(None, Some(0.0), Some(0.0), Some(0.0))
            .w(thick)
            .radius(rs)
            .fill(t.track())
            .live_opacity(&shown)
            .a11y_hidden()
            .child(thumb_el);

        let mut e = col().child(viewport).child(bar);
        e = match self.width {
            Some(w) => e.w(w),
            None => e.w_full(),
        };
        e
    }
}

/// Dim labels beside their values in a grid, `columns` pairs to a line: the details block the
/// power, dropbox and network panels each define (InfoLabel, InfoValue).
pub struct KeyValue {
    pairs: Vec<(String, String, Tone)>,
    columns: usize,
}

impl KeyValue {
    pub fn new() -> KeyValue {
        KeyValue { pairs: Vec::new(), columns: 1 }
    }
    pub fn pair(mut self, label: impl Into<String>, value: impl Into<String>) -> Self {
        self.pairs.push((label.into(), value.into(), Tone::Neutral));
        self
    }
    /// A pair whose value is tinted (a packet loss in urgent).
    pub fn pair_toned(mut self, label: impl Into<String>, value: impl Into<String>, tone: Tone) -> Self {
        self.pairs.push((label.into(), value.into(), tone));
        self
    }
    /// Pairs per line.
    pub fn columns(mut self, n: usize) -> Self {
        self.columns = n.max(1);
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let size = tk.font.body_small;
        let n = self.columns;
        // Grid columns, each as wide as its widest cell: label, value, label, value, ...
        let mut grid = row().items_start().gap(t.space(20.0));
        for c in 0..n * 2 {
            let mut column = col().gap(tk.spacing.label_gap as f32);
            for (label, value, tone) in self.pairs.iter().skip(c / 2).step_by(n) {
                column = column.child(if c % 2 == 0 {
                    text(label.clone()).font(t.text_style(size)).color(t.dim()).single_line()
                } else {
                    text(value.clone()).font(t.text_style(size)).color(color(tone.rgba(t, tk.palette.foreground))).single_line()
                });
            }
            grid = grid.child(column);
        }
        grid
    }
}

impl Default for KeyValue {
    fn default() -> Self {
        KeyValue::new()
    }
}

/// The head of a panel: a big glyph (`font.display`), a bold title (`font.title`), a dim
/// uppercase status line (urgent on `error`), and a trailing control centered against them.
pub struct Hero {
    glyph: String,
    title: String,
    status: String,
    error: bool,
    glyph_opacity: f32,
    trailing: Option<El>,
}

impl Hero {
    pub fn new(glyph: impl Into<String>, title: impl Into<String>) -> Hero {
        Hero { glyph: glyph.into(), title: title.into(), status: String::new(), error: false, glyph_opacity: 1.0, trailing: None }
    }
    pub fn status(mut self, s: impl Into<String>) -> Self {
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
    pub fn trailing(mut self, e: El) -> Self {
        self.trailing = Some(e);
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let f = &tk.font;
        let mut labels = col().grow(1.0).min_w(0.0).gap(t.space(2.0)).child(
            text(self.title).font(t.text_style_bold(f.title)).color(t.fg()).single_line().truncate(),
        );
        if !self.status.is_empty() {
            let ink = if self.error { t.urgent() } else { t.dim() };
            let mut style = t.text_style_bold(f.caption);
            style.tracking = 1.2;
            labels = labels.child(text(self.status.to_uppercase()).font(style).color(ink).single_line().truncate());
        }
        let mut e = row()
            .w_full()
            .items_center()
            .child(glyph(t, &self.glyph, f.display, t.fg()).opacity(self.glyph_opacity))
            .child(el().w(t.space(14.0)))
            .child(labels);
        if let Some(trailing) = self.trailing {
            e = e.child(el().w(t.space(12.0))).child(trailing);
        }
        e
    }
}
