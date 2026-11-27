//! Navigation: Keycap, KeyCombo, ShortcutSheet, Toolbar, Tabs and IconRail (the kit's own
//! `Keycap.qml`, `KeyCombo.qml`, `ShortcutSheet.qml`, `Toolbar.qml`, `Tabs.qml`, `IconRail.qml`).
//! Menus are in `menu.rs`.

use omarchy_theme::BorderSpec;
use rune::{El, Key, TextStyle, col, el, row, text};

use super::{Attach, Look, chrome, chrome_corners, section_header};
use crate::{Theme, border, color};

/// Text centered in a `w` × `h` box by its ink rather than its advance, as the Qt kit's
/// `OpticalGlyph` centers it: horizontally on the outline's middle, vertically on the line box
/// (rounded to whole points).
/// `hot` is the color under the hover group (the control around it), if it changes.
pub(crate) fn optical(s: &str, style: TextStyle, ink: rune::Color, hot: Option<rune::Color>, w: f32, h: f32) -> El {
    if s.is_empty() {
        return el().size(w, h);
    }
    let advance = rune::text::advance(s, style);
    let (ix, iw) = match rune::text::ink(s, style).map(|i| i.bounds) {
        Some([x, _, w, _]) if w > 0.0 => (x, w),
        _ => (0.0, advance),
    };
    let line = style.line_height;
    let x = w / 2.0 - (ix + iw.max(1.0) / 2.0);
    let y = ((h - line) / 2.0).round();
    let mut glyph = text(s.to_string()).font(style).color(ink).single_line().absolute(x, y);
    if let Some(c) = hot {
        glyph = glyph.group_color(c);
    }
    el().size(w, h).child(glyph)
}

/// A key name as every binding draws it: modifiers read "Super", "Ctrl", "Alt", "⇧"; Enter,
/// Backspace and the arrows are glyphs; single letters are upper case; anything else as given.
pub fn key_label(key: &str) -> String {
    let k = key.trim();
    let named = match k.to_lowercase().as_str() {
        "super" | "meta" | "win" | "cmd" | "logo" => "Super",
        "ctrl" | "control" => "Ctrl",
        "alt" | "option" => "Alt",
        "shift" => "\u{21e7}",
        "enter" | "return" => "\u{23ce}",
        "backspace" => "\u{232b}",
        "esc" | "escape" => "Esc",
        "tab" => "Tab",
        "space" => "Space",
        "up" => "\u{2191}",
        "down" => "\u{2193}",
        "left" => "\u{2190}",
        "right" => "\u{2192}",
        "pageup" => "PgUp",
        "pagedown" => "PgDn",
        "delete" | "del" => "Del",
        "home" => "Home",
        "end" => "End",
        _ => "",
    };
    if !named.is_empty() {
        return named.into();
    }
    if k.chars().count() == 1 { k.to_uppercase() } else { k.into() }
}

/// "Ctrl+Shift+S" as its keys; a lone "+" is the plus key ("Ctrl++").
pub fn split_combo(combo: &str) -> Vec<String> {
    let parts: Vec<&str> = combo.split('+').collect();
    let mut out = Vec::new();
    let mut i = 0;
    while i < parts.len() {
        if !parts[i].is_empty() {
            out.push(parts[i].trim().to_string());
        } else if i + 1 < parts.len() && parts[i + 1].is_empty() {
            out.push("+".into());
            i += 1;
        }
        i += 1;
    }
    out
}

fn keycap_height(t: &Theme) -> f32 {
    t.space(18.0)
}

/// How wide a Keycap for `key` draws.
pub fn keycap_width(t: &Theme, key: &str) -> f32 {
    let tk = t.tokens();
    let b = &tk.controls.normal.border.widths;
    let advance = rune::text::advance(&key_label(key), t.text_style(tk.font.caption)).ceil();
    let h = keycap_height(t);
    h.max(advance + (tk.spacing.sm * 2) as f32 + (b[1] + b[3]) as f32)
}

/// How wide a KeyCombo for `combo` draws.
pub fn key_combo_width(t: &Theme, combo: &str) -> f32 {
    let keys = split_combo(combo);
    let gap = t.tokens().spacing.xxs as f32;
    keys.iter().map(|k| keycap_width(t, k)).sum::<f32>() + gap * keys.len().saturating_sub(1) as f32
}

/// One key as a small cap: normal control glass, caption text centered by its ink.
pub struct Keycap {
    key: String,
}

impl Keycap {
    pub fn new(key: impl Into<String>) -> Keycap {
        Keycap { key: key.into() }
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let n = &tk.controls.normal;
        let label = key_label(&self.key);
        let (w, h) = (keycap_width(t, &self.key), keycap_height(t));
        let e = el()
            .size(w, h)
            .radius(t.radius_small())
            .fill(color(n.fill))
            .role("img")
            .label(label.clone())
            .child(optical(&label, t.text_style(tk.font.caption), t.fg(), None, w, h));
        border(e, &n.border)
    }
}

/// A key combination as Keycaps, xxs apart: "Ctrl+Shift+S" draws Ctrl, ⇧, S.
pub struct KeyCombo {
    combo: String,
}

impl KeyCombo {
    pub fn new(combo: impl Into<String>) -> KeyCombo {
        KeyCombo { combo: combo.into() }
    }

    pub fn build(self, t: &Theme) -> El {
        let gap = t.tokens().spacing.xxs as f32;
        row().gap(gap).label(self.combo.clone()).children(split_combo(&self.combo).into_iter().map(|k| Keycap::new(k).build(t)))
    }
}

/// Keyboard shortcuts, grouped: each group a section header over rows of a KeyCombo and what it
/// does, the combos in a column as wide as the group's widest. Groups fill `columns` columns left
/// to right, panelGap × 2 apart, and stack panelGap apart within a column.
pub struct ShortcutSheet {
    groups: Vec<(String, Vec<(String, String)>)>,
    columns: usize,
}

impl ShortcutSheet {
    pub fn new() -> ShortcutSheet {
        ShortcutSheet { groups: Vec::new(), columns: 2 }
    }
    /// A group: its title and (combo, description) rows.
    pub fn group<C: Into<String>, D: Into<String>>(mut self, title: impl Into<String>, keys: impl IntoIterator<Item = (C, D)>) -> Self {
        self.groups.push((title.into(), keys.into_iter().map(|(c, d)| (c.into(), d.into())).collect()));
        self
    }
    pub fn columns(mut self, n: usize) -> Self {
        self.columns = n.max(1);
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let n = self.columns.min(self.groups.len()).max(1);
        let mut columns: Vec<Vec<El>> = (0..n).map(|_| Vec::new()).collect();
        for (i, (title, keys)) in self.groups.into_iter().enumerate() {
            let widest = keys.iter().map(|(c, _)| key_combo_width(t, c)).fold(0.0f32, f32::max);
            let rows = keys.into_iter().map(|(c, d)| {
                row()
                    .items_center()
                    .gap(sp.xxl as f32)
                    .child(el().w(widest).child(KeyCombo::new(c).build(t)))
                    .child(text(d).font(t.text_style(tk.font.body)).color(t.fg()).single_line())
            });
            let group = col().gap(sp.md as f32).child(section_header(t, title)).child(col().gap(sp.md as f32).children(rows));
            columns[i % n].push(group);
        }
        let columns = columns.into_iter().map(|groups| col().gap(sp.panel_gap as f32).children(groups));
        row().items_start().gap((sp.panel_gap * 2) as f32).children(columns)
    }
}

impl Default for ShortcutSheet {
    fn default() -> Self {
        ShortcutSheet::new()
    }
}

/// The vertical rule between groups in a Toolbar: 1px in the track color, sm clear each side.
pub fn toolbar_separator(t: &Theme) -> El {
    let sm = t.tokens().spacing.sm as f32;
    el().w(sm * 2.0 + 1.0).h(t.space(16.0)).child(el().w(1.0).h_full().ml(sm).fill(t.track()))
}

/// An app toolbar: IconButtons and Buttons in three slots (leading at the left, center in the
/// middle, trailing at the right), groups split by `toolbar_separator`s, an optional overflow
/// "…" IconButton at the end, and a 1px track rule along the bottom. Items in a slot sit xxs
/// apart, vertically centered; the bar is sm inside its edges.
pub struct Toolbar {
    leading: Vec<El>,
    center: Vec<El>,
    trailing: Vec<El>,
    overflow: bool,
    overflow_cursor: bool,
    divided: bool,
    on_overflow: Option<Attach>,
}

impl Toolbar {
    pub fn new() -> Toolbar {
        Toolbar { leading: Vec::new(), center: Vec::new(), trailing: Vec::new(), overflow: false, overflow_cursor: false, divided: true, on_overflow: None }
    }
    pub fn leading(mut self, items: impl IntoIterator<Item = El>) -> Self {
        self.leading.extend(items);
        self
    }
    pub fn center(mut self, items: impl IntoIterator<Item = El>) -> Self {
        self.center.extend(items);
        self
    }
    pub fn trailing(mut self, items: impl IntoIterator<Item = El>) -> Self {
        self.trailing.extend(items);
        self
    }
    /// Show the overflow "…" button at the end.
    pub fn overflow(mut self, on: bool) -> Self {
        self.overflow = on;
        self
    }
    /// Force the hover-cursor look on the overflow button.
    pub fn overflow_cursor(mut self, on: bool) -> Self {
        self.overflow_cursor = on;
        self
    }
    /// The 1px rule along the bottom (on by default).
    pub fn divided(mut self, on: bool) -> Self {
        self.divided = on;
        self
    }
    pub fn on_overflow<S: 'static>(mut self, f: impl Fn(&mut S) + 'static) -> Self {
        self.on_overflow = Some(Box::new(move |e: El| e.on_press(f)));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let sp = &t.tokens().spacing;
        let pad = sp.sm as f32;
        let gap = sp.xxs as f32;
        let rule = if self.divided { 1.0 } else { 0.0 };
        let slot = |items: Vec<El>| row().items_center().gap(gap).children(items);
        let mut end = self.trailing;
        if self.overflow {
            let b = super::IconButton::new("\u{f141}").label("More").cursor(self.overflow_cursor);
            let b = match self.on_overflow {
                Some(attach) => attach(b.build(t)),
                None => b.build(t),
            };
            end.push(b);
        }
        let mut bar = row()
            .w_full()
            .items_center()
            .justify_between()
            .pt(pad)
            .px(pad)
            .pb(pad + rule)
            .role("toolbar")
            .child(slot(self.leading))
            .child(slot(end));
        if !self.center.is_empty() {
            bar = bar.child(
                row()
                    .pin(Some(0.0), Some(pad), Some(0.0), Some(pad + rule))
                    .items_center()
                    .justify_center()
                    .child(slot(self.center)),
            );
        }
        if self.divided {
            bar = bar.child(el().pin(Some(0.0), None, Some(0.0), Some(0.0)).h(1.0).fill(t.track()));
        }
        bar
    }
}

impl Default for Toolbar {
    fn default() -> Self {
        Toolbar::new()
    }
}

type Choose = Box<dyn Fn(El, usize) -> El>;

/// The axis a chooser walks along: Tabs a row, IconRail a column, RadioGroup either.
#[derive(Clone, Copy)]
pub(crate) enum Axis {
    Row,
    Column,
    Both,
}

/// What item `i` of `n` in a chooser swallows so its focus group stops at the ends instead of
/// wrapping, as Qt's clamp: the backward keys on the first, the forward keys on the last.
pub(crate) fn ends(i: usize, n: usize, axis: Axis) -> crate::KeyMap {
    const L: (&str, Option<Key>) = ("Left", None);
    const R: (&str, Option<Key>) = ("Right", None);
    const U: (&str, Option<Key>) = ("Up", None);
    const D: (&str, Option<Key>) = ("Down", None);
    match (axis, i == 0, i + 1 == n) {
        (Axis::Row, true, true) => &[L, R],
        (Axis::Row, true, false) => &[L],
        (Axis::Row, false, true) => &[R],
        (Axis::Column, true, true) => &[U, D],
        (Axis::Column, true, false) => &[U],
        (Axis::Column, false, true) => &[D],
        (Axis::Both, true, true) => &[L, R, U, D],
        (Axis::Both, true, false) => &[L, U],
        (Axis::Both, false, true) => &[R, D],
        _ => &[],
    }
}

/// Text tabs over a 1px track rule, the selected one bold with an accent underline. A tab under
/// the pointer, keyboard focus or `cursor` takes the hover fill and full foreground; the rest are
/// dim. Left and Right walk the tabs (a focus group); Enter or a press chooses. For a small
/// choice inside a form, use ButtonGroup.
pub struct Tabs {
    labels: Vec<String>,
    current: usize,
    cursor: Option<usize>,
    choose: Option<Choose>,
}

impl Tabs {
    pub fn new<L: Into<String>>(labels: impl IntoIterator<Item = L>, current: usize) -> Tabs {
        Tabs { labels: labels.into_iter().map(Into::into).collect(), current, cursor: None, choose: None }
    }
    /// Force the hover-cursor look on one tab.
    pub fn cursor(mut self, index: Option<usize>) -> Self {
        self.cursor = index;
        self
    }
    pub fn on_choose<S: 'static>(mut self, f: impl Fn(&mut S, usize) + Clone + 'static) -> Self {
        self.choose = Some(Box::new(move |e: El, i: usize| {
            let f = f.clone();
            e.on_press(move |s: &mut S| f(s, i))
        }));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let rest = Look::clear();
        let hot = Look::new(tk.controls.hover.fill, BorderSpec::none());
        let selected_ink = color(tk.controls.selected.text);
        let gap = sp.xxs as f32;
        let mut tabs = row().gap(gap).role("tablist").focus_group();
        let n = self.labels.len();
        let mut cursor_x = 0.0f32;
        let mut selected_span: Option<(f32, f32)> = None;
        for (i, label) in self.labels.into_iter().enumerate() {
            let selected = i == self.current;
            let forced = self.cursor == Some(i);
            let style = if selected { t.text_style_bold(tk.font.body) } else { t.text_style(tk.font.body) };
            let mut ink = text(label.clone()).font(style).single_line();
            ink = if selected {
                ink.color(selected_ink)
            } else if forced {
                ink.color(t.fg())
            } else {
                ink.color(t.dim()).group_color(t.fg())
            };
            // Qt sizes the tab to the label's whole-pixel width and centers it on whole pixels,
            // which puts the label at the padding.
            let width = rune::text::advance(&label, style).ceil() + (sp.control_padding_x * 2) as f32;
            if selected {
                selected_span = Some((cursor_x, width));
            }
            cursor_x += width + gap;
            let base = row()
                .key(format!("tab-{i}"))
                .w(width)
                .items_center()
                .px(sp.control_padding_x as f32)
                .py(sp.control_padding_y as f32)
                .role("tab")
                .label(label)
                .checked(selected)
                .focusable(true)
                .focus_selected(selected);
            let r = t.radius_small();
            let tab = chrome_corners(t, base, vec![ink], &rest, &hot, forced, [r, r, 0.0, 0.0]);
            let tab = t.key_map(tab, ends(i, n, Axis::Row));
            tabs = tabs.child(match &self.choose {
                Some(attach) => attach(tab, i),
                None => tab,
            });
        }
        // As Qt's Tabs: h and l walk too, Up and Down don't, and the ends don't wrap (above).
        let tabs = t.key_map(tabs, &[("h", Some(Key::Left)), ("l", Some(Key::Right)), ("Up", None), ("Down", None)]);
        let mut out = el()
            .w_full()
            .child(tabs)
            .child(el().pin(Some(0.0), None, Some(0.0), Some(0.0)).h(1.0).fill(t.track()));
        if let Some((sx, sw)) = selected_span {
            out = out.child(el().pin(Some(sx), None, None, Some(0.0)).w(sw).h(sp.xxs as f32).fill(t.accent()));
        }
        out
    }
}

/// A vertical rail of icon tabs, as an app's side-panel switcher: square buttons sm apart, the
/// selected glyph in the accent with an accent bar at the rail's leading edge, a 1px track rule
/// down the trailing edge. Under the pointer, keyboard focus or `cursor` a button takes the hover
/// fill; the rest are dim. Up and Down walk it (a focus group); Enter or a press chooses.
pub struct IconRail {
    items: Vec<(String, String)>,
    current: usize,
    cursor: Option<usize>,
    choose: Option<Choose>,
}

impl IconRail {
    /// `items` are (glyph, label) pairs; the label is the accessible name.
    pub fn new<G: Into<String>, L: Into<String>>(items: impl IntoIterator<Item = (G, L)>, current: usize) -> IconRail {
        IconRail { items: items.into_iter().map(|(g, l)| (g.into(), l.into())).collect(), current, cursor: None, choose: None }
    }
    pub fn cursor(mut self, index: Option<usize>) -> Self {
        self.cursor = index;
        self
    }
    pub fn on_choose<S: 'static>(mut self, f: impl Fn(&mut S, usize) + Clone + 'static) -> Self {
        self.choose = Some(Box::new(move |e: El, i: usize| {
            let f = f.clone();
            e.on_press(move |s: &mut S| f(s, i))
        }));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let size = (sp.control_height + sp.sm * 2) as f32;
        let pad = sp.sm as f32;
        let gap = sp.sm as f32;
        let glyph_size = tk.font.icon_large;
        let rest = Look::clear();
        let hot = Look::new(tk.controls.hover.fill, BorderSpec::none());
        let n = self.items.len();
        let mut list = col().gap(gap).role("tablist").focus_group();
        for (i, (glyph, label)) in self.items.into_iter().enumerate() {
            let selected = i == self.current;
            let forced = self.cursor == Some(i);
            let ink = if selected {
                t.accent()
            } else if forced {
                t.fg()
            } else {
                t.dim()
            };
            let hover_ink = (!selected && !forced).then(|| t.fg());
            let mark = optical(&glyph, t.text_style(glyph_size), ink, hover_ink, size, size);
            let base = el()
                .key(format!("rail-{i}"))
                .size(size, size)
                .role("tab")
                .label(label)
                .checked(selected)
                .focusable(true)
                .focus_selected(selected);
            let item = chrome(t, base, vec![mark], &rest, &hot, forced, t.radius());
            let item = t.key_map(item, ends(i, n, Axis::Column));
            list = list.child(match &self.choose {
                Some(attach) => attach(item, i),
                None => item,
            });
        }
        let height = pad * 2.0 + n as f32 * size + n.saturating_sub(1) as f32 * gap;
        let mut rail = el()
            .size(size + pad * 2.0 + 1.0, height)
            .child(el().pin(None, Some(0.0), Some(0.0), Some(0.0)).w(1.0).fill(t.track()))
            // As Qt's IconRail: k and j walk too, Left and Right don't, and the ends don't wrap.
            .child(t.key_map(list, &[("k", Some(Key::Up)), ("j", Some(Key::Down)), ("Left", None), ("Right", None)]).absolute(pad, pad));
        if self.current < n {
            let bar = glyph_size as f32;
            let y = pad + self.current as f32 * (size + gap) + ((size - bar) / 2.0).round();
            rail = rail.child(el().absolute(0.0, y).size(sp.xxs as f32, bar).radius(t.radius().min(1.0)).fill(t.accent()));
        }
        rail
    }
}


#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn keys_split_and_name_as_in_qt() {
        assert_eq!(split_combo("Ctrl+Shift+S"), ["Ctrl", "Shift", "S"]);
        assert_eq!(split_combo("Ctrl++"), ["Ctrl", "+"]);
        assert_eq!(split_combo("+"), ["+"]);
        let labels: Vec<String> = ["super", "ctrl", "Shift", "enter", "backspace", "up", "esc", "k", "F11"].iter().map(|k| key_label(k)).collect();
        assert_eq!(labels, ["Super", "Ctrl", "\u{21e7}", "\u{23ce}", "\u{232b}", "\u{2191}", "Esc", "K", "F11"]);
    }
}
