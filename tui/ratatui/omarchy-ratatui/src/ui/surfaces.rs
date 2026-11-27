//! Surfaces (spec/terminal.md, Surfaces): boxes with a light border in the surface's border color,
//! corners by the radius, padding 2 columns × 1 row; and the in-window Pane, Heading,
//! SectionHeader, Separator, MenuRow, Tooltip and the dialogs.

use omarchy_theme::Rgba;
use ratatui::buffer::Buffer;
use ratatui::layout::Rect;

use super::glyphs;
use super::navigation::KeyCombo;
use crate::paint::{self, Ink, clear, fill, frame, hline, put, text_center, text_fit, vline, width};
use crate::{Draw, Theme};

type Kids = Vec<Box<dyn Draw>>;

fn stack_size(t: &Theme, kids: &Kids, gap: u16) -> (u16, u16) {
    let mut w = 0;
    let mut h = 0;
    for (i, k) in kids.iter().enumerate() {
        let (cw, ch) = k.size(t);
        w = w.max(cw);
        h += ch + if i > 0 { gap } else { 0 };
    }
    (w, h)
}

fn stack_draw(t: &Theme, kids: &Kids, gap: u16, area: Rect, buf: &mut Buffer) {
    let mut y = area.y;
    for k in kids {
        let (cw, ch) = k.size(t);
        let r = Rect::new(area.x, y, cw.min(area.width), ch).intersection(area);
        if r.height > 0 && r.width > 0 {
            k.draw(t, r, buf);
        }
        y += ch + gap;
    }
}

/// The caption that labels a group of rows: bold, `textDim`.
pub struct SectionHeader(pub String);

/// See [`SectionHeader`].
pub fn section_header(text: impl Into<String>) -> SectionHeader {
    SectionHeader(text.into())
}

impl Draw for SectionHeader {
    fn size(&self, _: &Theme) -> (u16, u16) {
        (width(&self.0), 1)
    }
    fn draw(&self, t: &Theme, area: Rect, buf: &mut Buffer) {
        text_fit(t, buf, area.x, area.y, &self.0, area.width, Ink::new(t.dim()).bold(true));
    }
}

/// A rule of `─` in `track` across its width.
pub struct Separator(pub u16);

impl Draw for Separator {
    fn size(&self, _: &Theme) -> (u16, u16) {
        (self.0, 1)
    }
    fn draw(&self, t: &Theme, area: Rect, buf: &mut Buffer) {
        hline(t, buf, area.x, area.y, area.width, "─", t.track());
    }
}

/// The raised surface panels, popups and flyouts are drawn on: a box in the popups border color
/// (accent), its title a SectionHeader set in the top edge, children one row apart inside a
/// 2-column, 1-row padding.
pub struct Card {
    title: String,
    width: u16,
    children: Kids,
}

impl Card {
    pub fn new() -> Self {
        Card { title: String::new(), width: 45, children: Vec::new() }
    }
    pub fn title(mut self, t: impl Into<String>) -> Self {
        self.title = t.into();
        self
    }
    pub fn width(mut self, w: u16) -> Self {
        self.width = w;
        self
    }
    pub fn child(mut self, d: impl Draw + 'static) -> Self {
        self.children.push(Box::new(d));
        self
    }
    pub fn boxed(mut self, d: Box<dyn Draw>) -> Self {
        self.children.push(d);
        self
    }
}

impl Default for Card {
    fn default() -> Self {
        Self::new()
    }
}

impl Draw for Card {
    fn size(&self, t: &Theme) -> (u16, u16) {
        (self.width, stack_size(t, &self.children, 1).1 + 4)
    }
    fn draw(&self, t: &Theme, area: Rect, buf: &mut Buffer) {
        clear(buf, area);
        let s = &t.shown().surfaces.popups;
        let title = (!self.title.is_empty()).then(|| (self.title.as_str(), Ink::new(t.dim()).bold(true)));
        let inner = frame(t, buf, area, s.border.color, title);
        stack_draw(t, &self.children, 1, paint::inset(inner, 2, 1), buf);
    }
}

/// Which side a Pane's rule is on.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Side {
    Left,
    Top,
    Right,
    Bottom,
}

/// A flat surface inside an app window: padding, an optional Heading with actions, children in a
/// column one row apart. Plain (nothing drawn), divided (a `track` rule on one side) or bordered
/// (the controls' normal glass in a box at the radius).
pub struct Pane {
    title: String,
    subtitle: String,
    actions: Kids,
    divider: Option<Side>,
    bordered: bool,
    width: u16,
    children: Kids,
}

impl Pane {
    pub fn new() -> Self {
        Pane { title: String::new(), subtitle: String::new(), actions: Vec::new(), divider: None, bordered: false, width: 30, children: Vec::new() }
    }
    pub fn title(mut self, t: impl Into<String>) -> Self {
        self.title = t.into();
        self
    }
    pub fn subtitle(mut self, s: impl Into<String>) -> Self {
        self.subtitle = s.into();
        self
    }
    pub fn action(mut self, d: impl Draw + 'static) -> Self {
        self.actions.push(Box::new(d));
        self
    }
    pub fn divider(mut self, side: Side) -> Self {
        self.divider = Some(side);
        self
    }
    pub fn bordered(mut self, on: bool) -> Self {
        self.bordered = on;
        self
    }
    pub fn width(mut self, w: u16) -> Self {
        self.width = w;
        self
    }
    pub fn child(mut self, d: impl Draw + 'static) -> Self {
        self.children.push(Box::new(d));
        self
    }
    pub fn boxed(mut self, d: Box<dyn Draw>) -> Self {
        self.children.push(d);
        self
    }
    fn edge(&self) -> (u16, u16, u16, u16) {
        // Extra cells on (left, top, right, bottom) for the border or the rule.
        if self.bordered {
            return (1, 1, 1, 1);
        }
        match self.divider {
            Some(Side::Left) => (1, 0, 0, 0),
            Some(Side::Top) => (0, 1, 0, 0),
            Some(Side::Right) => (0, 0, 1, 0),
            Some(Side::Bottom) => (0, 0, 0, 1),
            None => (0, 0, 0, 0),
        }
    }
    fn head(&self, w: u16) -> Option<Head<'_>> {
        (!self.title.is_empty()).then(|| Head { text: &self.title, subtitle: &self.subtitle, centered: false, trailing: &self.actions, width: Some(w) })
    }
}

impl Default for Pane {
    fn default() -> Self {
        Self::new()
    }
}

impl Draw for Pane {
    fn size(&self, t: &Theme) -> (u16, u16) {
        let (l, tp, r, b) = self.edge();
        let inner_w = self.width.saturating_sub(l + r + 4);
        let head = self.head(inner_w).map(|h| h.size(t).1 + 1).unwrap_or(0);
        (self.width, tp + b + 2 + head + stack_size(t, &self.children, 1).1)
    }
    fn draw(&self, t: &Theme, area: Rect, buf: &mut Buffer) {
        let c = &t.shown().controls;
        let (l, tp, r, b) = self.edge();
        if self.bordered {
            fill(t, buf, paint::inset(area, 1, 1), c.normal.fill);
            frame(t, buf, area, c.normal.border.color, None);
        } else {
            match self.divider {
                Some(Side::Left) => vline(t, buf, area.x, area.y, area.height, "│", t.track()),
                Some(Side::Right) => vline(t, buf, area.right() - 1, area.y, area.height, "│", t.track()),
                Some(Side::Top) => hline(t, buf, area.x, area.y, area.width, "─", t.track()),
                Some(Side::Bottom) => hline(t, buf, area.x, area.bottom() - 1, area.width, "─", t.track()),
                None => {}
            }
        }
        let inner = Rect {
            x: area.x + l + 2,
            y: area.y + tp + 1,
            width: area.width.saturating_sub(l + r + 4),
            height: area.height.saturating_sub(tp + b + 2),
        };
        let mut y = inner.y;
        if let Some(h) = self.head(inner.width) {
            let hh = h.size(t).1;
            h.draw(t, Rect { y, height: hh, ..inner }, buf);
            y += hh + 1;
        }
        stack_draw(t, &self.children, 1, Rect { y, height: inner.bottom().saturating_sub(y), ..inner }, buf);
    }
}

/// Which font token a [`Heading`] stands for. The terminal has one size: every level is a bold
/// title line with a `textDim` subtitle under it.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum HeadingLevel {
    #[default]
    Title,
    Heading,
    Display,
}

/// The title of a pane, toolbar, section or page: a bold line, an optional `textDim` subtitle
/// under it, an optional trailing slot (one column apart) at the end of its width.
pub struct Heading {
    text: String,
    subtitle: String,
    level: HeadingLevel,
    centered: bool,
    width: Option<u16>,
    pub(crate) trailing: Kids,
}

impl Heading {
    pub fn new(text: impl Into<String>) -> Self {
        Heading { text: text.into(), subtitle: String::new(), level: HeadingLevel::Title, centered: false, width: None, trailing: Vec::new() }
    }
    pub fn subtitle(mut self, s: impl Into<String>) -> Self {
        self.subtitle = s.into();
        self
    }
    pub fn level(mut self, l: HeadingLevel) -> Self {
        self.level = l;
        self
    }
    pub fn centered(mut self, on: bool) -> Self {
        self.centered = on;
        self
    }
    /// A fixed width (the trailing slot sits at its end); by default the widest line + trailing.
    pub fn width(mut self, w: u16) -> Self {
        self.width = Some(w);
        self
    }
    pub fn trailing(mut self, d: impl Draw + 'static) -> Self {
        self.trailing.push(Box::new(d));
        self
    }
    fn head(&self) -> Head<'_> {
        Head { text: &self.text, subtitle: &self.subtitle, centered: self.centered, trailing: &self.trailing, width: self.width }
    }
}

impl Draw for Heading {
    fn size(&self, t: &Theme) -> (u16, u16) {
        let _ = self.level;
        self.head().size(t)
    }
    fn draw(&self, t: &Theme, area: Rect, buf: &mut Buffer) {
        self.head().draw(t, area, buf)
    }
}

/// A Heading over borrowed parts (a Pane's header lends its actions).
struct Head<'a> {
    text: &'a str,
    subtitle: &'a str,
    centered: bool,
    trailing: &'a [Box<dyn Draw>],
    width: Option<u16>,
}

impl Head<'_> {
    fn trailing_w(&self, t: &Theme) -> u16 {
        let n = self.trailing.len() as u16;
        self.trailing.iter().map(|d| d.size(t).0).sum::<u16>() + n.saturating_sub(1) + if n > 0 { 1 } else { 0 }
    }
    fn size(&self, t: &Theme) -> (u16, u16) {
        let tw = self.trailing_w(t);
        let trail_h = self.trailing.iter().map(|d| d.size(t).1).max().unwrap_or(0);
        let lines_h = if self.subtitle.is_empty() { 1 } else { 2 };
        let h = lines_h.max(trail_h);
        let natural = if trail_h > 1 || self.centered {
            width(self.text).max(width(self.subtitle)) + tw
        } else {
            (width(self.text) + tw).max(width(self.subtitle))
        };
        (self.width.unwrap_or(natural), h)
    }
    fn draw(&self, t: &Theme, area: Rect, buf: &mut Buffer) {
        let tw = self.trailing_w(t);
        let lines_h = if self.subtitle.is_empty() { 1 } else { 2 };
        let title_w = area.width.saturating_sub(tw);
        let trail_on_sub = self.trailing.iter().any(|d| {
            let h = d.size(t).1;
            let dy = (lines_h.max(h) - h) / 2;
            dy + h > 1
        });
        let sub_w = if self.centered || trail_on_sub { title_w } else { area.width };
        let put_line = |buf: &mut Buffer, y: u16, w: u16, s: &str, ink: Ink| {
            if self.centered {
                text_center(t, buf, area.x, y, w, s, ink);
            } else {
                text_fit(t, buf, area.x, y, s, w, ink);
            }
        };
        put_line(buf, area.y, title_w, self.text, Ink::new(t.fg()).bold(true));
        if !self.subtitle.is_empty() {
            put_line(buf, area.y + 1, sub_w, self.subtitle, Ink::new(t.dim()));
        }
        let mut x = area.right().saturating_sub(tw) + if tw > 0 { 1 } else { 0 };
        for d in self.trailing {
            let (w, h) = d.size(t);
            let dy = (lines_h.max(h) - h) / 2;
            d.draw(t, Rect::new(x, area.y + dy, w, h).intersection(area), buf);
            x += w + 1;
        }
    }
}

/// A list or menu row: the label, an accent check at the end when `checked`, the hover fill when
/// `highlighted`.
pub struct MenuRow {
    label: String,
    checked: bool,
    highlighted: bool,
    width: u16,
    key: Option<String>,
}

impl MenuRow {
    pub fn new(label: impl Into<String>) -> Self {
        MenuRow { label: label.into(), checked: false, highlighted: false, width: 30, key: None }
    }
    pub fn checked(mut self, on: bool) -> Self {
        self.checked = on;
        self
    }
    pub fn highlighted(mut self, on: bool) -> Self {
        self.highlighted = on;
        self
    }
    pub fn width(mut self, w: u16) -> Self {
        self.width = w;
        self
    }
    pub fn key(mut self, k: impl Into<String>) -> Self {
        self.key = Some(k.into());
        self
    }
}

impl Draw for MenuRow {
    fn size(&self, _: &Theme) -> (u16, u16) {
        (self.width, 1)
    }
    fn draw(&self, t: &Theme, area: Rect, buf: &mut Buffer) {
        if self.highlighted {
            fill(t, buf, area, t.shown().controls.hover.fill);
        }
        text_fit(t, buf, area.x + 1, area.y, &self.label, area.width.saturating_sub(4), t.fg());
        if self.checked {
            put(t, buf, area.right().saturating_sub(2), area.y, glyphs::CHECK, Ink::new(t.accent()).bold(true));
        }
        if let Some(k) = &self.key {
            t.hit(k.clone(), area);
        }
    }
}

/// The tooltip surface: a box in the tooltip border color holding the text and, with a shortcut,
/// a KeyCombo one column after it. Three rows.
pub struct Tooltip {
    text: String,
    shortcut: String,
}

impl Tooltip {
    pub fn new(text: impl Into<String>) -> Self {
        Tooltip { text: text.into(), shortcut: String::new() }
    }
    pub fn shortcut(mut self, s: impl Into<String>) -> Self {
        self.shortcut = s.into();
        self
    }
}

impl Draw for Tooltip {
    fn size(&self, t: &Theme) -> (u16, u16) {
        let combo = if self.shortcut.is_empty() { 0 } else { 1 + KeyCombo::new(self.shortcut.clone()).size(t).0 };
        (width(&self.text) + combo + 4, 3)
    }
    fn draw(&self, t: &Theme, area: Rect, buf: &mut Buffer) {
        clear(buf, area);
        let s = &t.shown().surfaces.tooltip;
        let inner = frame(t, buf, area, s.border.color, None);
        let w = text_fit(t, buf, inner.x + 1, inner.y, &self.text, inner.width.saturating_sub(2), s.text);
        if !self.shortcut.is_empty() {
            let kc = KeyCombo::new(self.shortcut.clone());
            let kw = kc.size(t).0;
            kc.draw(t, Rect::new(inner.x + 2 + w, inner.y, kw, 1).intersection(inner), buf);
        }
    }
}

/// A modal card on the scrim: title (bold), message, body (those present a blank row apart), then
/// a blank row and the buttons at the bottom right (secondary, primary), the selected one in the
/// menu's selected look (urgent when destructive).
pub struct Dialog {
    title: String,
    message: String,
    body: Option<Box<dyn Draw>>,
    secondary: String,
    primary: String,
    destructive: bool,
    selected: usize,
    width: u16,
    scrim: bool,
    key: Option<String>,
}

impl Dialog {
    pub fn new(title: impl Into<String>) -> Self {
        Dialog {
            title: title.into(),
            message: String::new(),
            body: None,
            secondary: String::new(),
            primary: String::new(),
            destructive: false,
            selected: 1,
            width: 46,
            scrim: true,
            key: None,
        }
    }
    pub fn message(mut self, m: impl Into<String>) -> Self {
        self.message = m.into();
        self
    }
    pub fn body(mut self, d: impl Draw + 'static) -> Self {
        self.body = Some(Box::new(d));
        self
    }
    /// Button labels; empty takes the catalog's Cancel / Confirm.
    pub fn labels(mut self, secondary: impl Into<String>, primary: impl Into<String>) -> Self {
        self.secondary = secondary.into();
        self.primary = primary.into();
        self
    }
    pub fn destructive(mut self, on: bool) -> Self {
        self.destructive = on;
        self
    }
    /// 0 the secondary button, 1 the primary.
    pub fn selected(mut self, i: usize) -> Self {
        self.selected = i;
        self
    }
    pub fn width(mut self, w: u16) -> Self {
        self.width = w;
        self
    }
    /// Draw the scrim over the whole area given (default on); the card centers in it.
    pub fn scrim(mut self, on: bool) -> Self {
        self.scrim = on;
        self
    }
    /// Buttons keyed `<k>-cancel` and `<k>-accept`.
    pub fn key(mut self, k: impl Into<String>) -> Self {
        self.key = Some(k.into());
        self
    }
    /// Left/Right/h/l/Tab move the selection; true when handled. (Escape and Return are the
    /// app's: cancel, and the selected button.)
    pub fn handle_key(selected: &mut usize, key: &crossterm::event::KeyEvent) -> bool {
        use crossterm::event::KeyCode::*;
        match key.code {
            Left | Char('h') => *selected = 0,
            Right | Char('l') => *selected = 1,
            Tab | BackTab => *selected = 1 - (*selected).min(1),
            _ => return false,
        }
        true
    }
    fn card_size(&self, t: &Theme) -> (u16, u16) {
        let inner = self.width.saturating_sub(6);
        let mut h = 0;
        if !self.title.is_empty() {
            h += 1;
        }
        let msg = super::wrap(&self.message, inner).len() as u16;
        if !self.message.is_empty() {
            h += msg + if h > 0 { 1 } else { 0 };
        }
        if let Some(b) = &self.body {
            h += b.size(t).1 + if h > 0 { 1 } else { 0 };
        }
        // A blank row, the buttons, the padding and the border.
        (self.width, h + 2 + 4)
    }
}

/// A dialog button's width: 11 columns, or the label and its frame when wider.
fn dialog_button_w(label: &str) -> u16 {
    (width(label) + 4).max(11)
}

/// A dialog button, label centered.
fn dialog_button(t: &Theme, buf: &mut Buffer, x: u16, y: u16, label: &str, selected: bool, destructive: bool) {
    let s = t.shown();
    let (fill_c, border, ink) = match (selected, destructive) {
        (true, true) => (t.urgent().with_alpha(0.22), t.urgent(), t.urgent()),
        (true, false) => (s.surfaces.menu.selected_background, s.surfaces.menu.selected_text, s.surfaces.menu.selected_text),
        (false, true) => (Rgba::TRANSPARENT, t.urgent().with_alpha(0.56), t.fg()),
        (false, false) => (Rgba::TRANSPARENT, t.fg().with_alpha(0.38), t.fg()),
    };
    let w = dialog_button_w(label);
    super::controls::control(t, buf, x, y, w, &super::Look { fill: fill_c, border: Some(border), text: ink, bold: false });
    text_center(t, buf, x + 1, y, w - 2, label, Ink::new(ink).bold(selected));
}

impl Draw for Dialog {
    fn size(&self, t: &Theme) -> (u16, u16) {
        self.card_size(t)
    }
    fn draw(&self, t: &Theme, area: Rect, buf: &mut Buffer) {
        let s = t.shown();
        if self.scrim {
            paint::scrim(t, buf, area, s.roles.scrim);
        }
        let (cw, ch) = self.card_size(t);
        let (cw, ch) = (cw.min(area.width), ch.min(area.height));
        let card = Rect::new(area.x + (area.width - cw) / 2, area.y + (area.height - ch) / 2, cw, ch);
        clear(buf, card);
        let inner = paint::inset(frame(t, buf, card, s.surfaces.menu.selected_text, None), 2, 1);
        let mut y = inner.y;
        if !self.title.is_empty() {
            text_fit(t, buf, inner.x, y, &self.title, inner.width, Ink::new(t.fg()).bold(true));
            y += 1;
        }
        if !self.message.is_empty() {
            if y > inner.y {
                y += 1;
            }
            for line in super::wrap(&self.message, inner.width) {
                text_fit(t, buf, inner.x, y, &line, inner.width, t.fg());
                y += 1;
            }
        }
        if let Some(b) = &self.body {
            let (bw, bh) = b.size(t);
            if y > inner.y {
                y += 1;
            }
            b.draw(t, Rect::new(inner.x, y, bw.min(inner.width), bh), buf);
        }
        let secondary = if self.secondary.is_empty() { t.string("common.cancel", "Cancel") } else { self.secondary.clone() };
        let primary = if self.primary.is_empty() { t.string("common.confirm", "Confirm") } else { self.primary.clone() };
        let by = inner.bottom().saturating_sub(1);
        let (sw, pw) = (dialog_button_w(&secondary), dialog_button_w(&primary));
        let px = inner.right().saturating_sub(pw);
        let sx = px.saturating_sub(sw + 1);
        dialog_button(t, buf, sx, by, &secondary, self.selected == 0, false);
        dialog_button(t, buf, px, by, &primary, self.selected == 1, self.destructive);
        if let Some(k) = &self.key {
            t.hit(format!("{k}-cancel"), Rect::new(sx, by, sw, 1));
            t.hit(format!("{k}-accept"), Rect::new(px, by, pw, 1));
        }
    }
}

/// A Dialog with no title, the message and a destructive Confirm, selected.
pub struct ConfirmDialog;

impl ConfirmDialog {
    #[allow(clippy::new_ret_no_self)]
    pub fn new(message: impl Into<String>) -> Dialog {
        Dialog::new("").message(message).destructive(true).selected(1)
    }
}
