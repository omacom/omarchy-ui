//! Navigation (spec/components/navigation.md): Menu, MenuItem, ContextMenu, Keycap, KeyCombo,
//! ShortcutSheet, Toolbar, Tabs and IconRail, drawn as the Qt kit's `Menu.qml`, `MenuItem.qml`,
//! `MenuSeparator.qml`, `MenuSection.qml`, `ContextMenu.qml`, `Keycap.qml`, `KeyCombo.qml`,
//! `ShortcutSheet.qml`, `Toolbar.qml`, `Tabs.qml` and `IconRail.qml`.

use std::rc::Rc;
use std::sync::Arc;

use gpui::prelude::*;
use gpui::{
    AnyElement, App, ClickEvent, Div, ElementId, FontWeight, Hsla, KeyDownEvent, Pixels, Point, SharedString, TextRun, Window, anchored,
    deferred, div, point, px,
};
use omarchy_theme::BorderSpec;

use super::{GROUP, Handler, IconButton, Look, border, chrome, focus_handle, id_for, keyed_state, label, section_header, tab_keys};
use crate::{Paint, color};

// ------------------------------------------------------------------------------- measuring

fn font_for(t: &Paint, bold: bool) -> gpui::Font {
    let mut f = gpui::font(t.family());
    if bold {
        f.weight = FontWeight::BOLD;
    }
    f
}

/// How wide `s` lays out in the Omarchy font at `size` px.
pub(crate) fn advance(window: &Window, t: &Paint, s: &str, size: i64, bold: bool) -> f32 {
    if s.is_empty() {
        return 0.0;
    }
    let run = TextRun { len: s.len(), font: font_for(t, bold), color: t.fg(), background_color: None, underline: None, strikethrough: None };
    f32::from(window.text_system().shape_line(SharedString::from(s.to_string()), px(size as f32), &[run], None).width)
}

/// Tight ink rect `(x, width, y, height)` of `s` relative to `(x = 0, baseline = 0)`, in points
/// (Qt's `tightBoundingRect`). Rasterized once through GPUI's SVG renderer (which handles CFF
/// fonts and fallback Nerd Fonts) and cached per family, string, size and weight.
fn ink_rect(cx: &App, t: &Paint, s: &str, size: i64, bold: bool) -> Option<(f32, f32, f32, f32)> {
    use std::cell::RefCell;
    use std::collections::HashMap;
    thread_local! {
        static INK: RefCell<HashMap<(String, String, i64, bool), Option<(f32, f32, f32, f32)>>> = RefCell::new(HashMap::new());
    }
    let family = t.family().to_string();
    let key = (family.clone(), s.to_string(), size, bold);
    INK.with(|m| m.borrow().get(&key).copied()).unwrap_or_else(|| {
        let found = raster_ink(cx, &family, s, size, bold);
        INK.with(|m| m.borrow_mut().insert(key, found));
        found
    })
}

/// Where `s`'s ink starts and how wide it is, from the left of its advance box (Qt's
/// `tightBoundingRect`). Falls back to the advance.
pub(crate) fn ink_x(window: &Window, cx: &App, t: &Paint, s: &str, size: i64, bold: bool) -> (f32, f32) {
    let adv = advance(window, t, s, size, bold);
    ink_rect(cx, t, s, size, bold).map(|(x, w, _, _)| (x, w)).unwrap_or((0.0, adv))
}

/// The ink's `(x, width, y, height)` of `s` set at `(x = 0, baseline = 0)`, in points, measured
/// at 4× from a raster.
fn raster_ink(cx: &App, family: &str, s: &str, size: i64, bold: bool) -> Option<(f32, f32, f32, f32)> {
    const SCALE: f32 = 4.0;
    let pad = size as f32 * 2.0;
    let baseline = size as f32 * 2.0;
    let w = pad * 2.0 + size as f32 * (s.chars().count() as f32 + 1.0) * 1.5;
    let h = size as f32 * 3.0;
    let esc = |v: &str| v.replace('&', "&amp;").replace('<', "&lt;").replace('"', "&quot;");
    let svg = format!(
        r##"<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}"><text x="{pad}" y="{baseline}" font-family="{}" font-size="{size}" font-weight="{}" fill="#000" xml:space="preserve">{}</text></svg>"##,
        esc(family),
        if bold { 700 } else { 400 },
        esc(s)
    );
    let image = cx.svg_renderer().render_single_frame(svg.as_bytes(), SCALE).ok()?;
    let bytes = image.as_bytes(0)?;
    let px_w = i32::from(image.size(0).width) as usize;
    let px_h = i32::from(image.size(0).height) as usize;
    if px_w == 0 || px_h == 0 {
        return None;
    }
    let (mut lo_x, mut hi_x, mut lo_y, mut hi_y) = (usize::MAX, 0usize, usize::MAX, 0usize);
    for (i, p) in bytes.chunks_exact(4).enumerate() {
        if p[3] > 8 {
            let x = i % px_w;
            let y = i / px_w;
            lo_x = lo_x.min(x);
            hi_x = hi_x.max(x + 1);
            lo_y = lo_y.min(y);
            hi_y = hi_y.max(y + 1);
        }
    }
    if lo_x >= hi_x || lo_y >= hi_y {
        return None;
    }
    let scale_x = px_w as f32 / w;
    let scale_y = px_h as f32 / h;
    Some((
        lo_x as f32 / scale_x - pad,
        (hi_x - lo_x) as f32 / scale_x,
        lo_y as f32 / scale_y - baseline,
        (hi_y - lo_y) as f32 / scale_y,
    ))
}

/// A glyph boxed the way Qt's `Text` boxes one: its advance, widened by any ink that overhangs
/// it, in whole points. Nerd Font icons in the non-Mono Nerd Fonts overhang their cell to the
/// right, so a bare advance box would leave the icon off its neighbours' spacing.
pub(crate) fn boxed_glyph(window: &Window, cx: &App, t: &Paint, s: &str, size: i64, ink: Hsla) -> Div {
    let adv = advance(window, t, s, size, false);
    let (x, w) = ink_x(window, cx, t, s, size, false);
    let left = (-x).max(0.0).ceil();
    let right = if x + w > adv { (x + w - adv).ceil() } else { 0.0 };
    let e = label(t, s.to_string(), size, false, ink);
    if left == 0.0 && right == 0.0 {
        return e;
    }
    div().flex_shrink_0().w(px(adv + left + right)).pl(px(left)).child(e)
}

/// Text centered in a `w` × `h` box by its ink horizontally and its line box vertically, as the
/// Qt kit's `OpticalGlyph` (with `paintedSize: 0`) centers it. `hot` is its color under the
/// pointer, when that changes.
#[allow(clippy::too_many_arguments)]
pub(crate) fn optical(window: &Window, cx: &App, t: &Paint, s: &str, size: i64, ink: Hsla, hot: Option<Hsla>, w: f32, h: f32) -> Div {
    let (ix, iw) = ink_x(window, cx, t, s, size, false);
    let x = w / 2.0 - (ix + iw.max(1.0) / 2.0);
    let line = t.tokens().font.line_px(size) as f32;
    let y = ((h - line) / 2.0).round();
    let mut glyph = label(t, s.to_string(), size, false, ink).absolute().left(px(x)).top(px(y));
    if let Some(c) = hot {
        glyph = glyph.group_hover(GROUP, move |s| s.text_color(c));
    }
    div().relative().flex_shrink_0().w(px(w)).h(px(h)).child(glyph)
}

/// Text centered optically horizontally by ink bounds (snapped to whole points) and vertically on
/// the line box, as Qt's `PanelActionButton` centers icon glyphs. Reads TrueType `glyf` bounds
/// directly when available and falls back to the SVG raster bounds (CFF fonts, fallback Nerd
/// Fonts).
#[allow(clippy::too_many_arguments)]
pub(crate) fn optical_xy(window: &Window, cx: &App, t: &Paint, s: &str, size: i64, ink: Hsla, hot: Option<Hsla>, w: f32, h: f32) -> Div {
    let k = size as f32;
    let line = t.tokens().font.line_px(size) as f32;
    let y = ((h - line) / 2.0).round();
    let single = {
        let mut chars = s.chars();
        chars.next().filter(|_| chars.next().is_none())
    };
    let x = if let (Some(ch), Some(file)) = (single, t.tokens().font.file.as_deref())
        && let Some(g) = super::ink::ink(file, ch)
        && g.x_max > g.x_min
        && g.y_max > g.y_min
    {
        let ix = g.x_min * k;
        let iw = (g.x_max - g.x_min) * k;
        (w / 2.0 - (ix + iw / 2.0)).round()
    } else if let Some((ix, iw, _, _)) = ink_rect(cx, t, s, size, false) {
        (w / 2.0 - (ix + iw / 2.0)).round()
    } else {
        let adv = advance(window, t, s, size, false);
        ((w - adv) / 2.0).round()
    };
    let mut glyph = label(t, s.to_string(), size, false, ink).absolute().left(px(x)).top(px(y));
    if let Some(c) = hot {
        glyph = glyph.group_hover(GROUP, move |s| s.text_color(c));
    }
    div().relative().flex_shrink_0().w(px(w)).h(px(h)).child(glyph)
}

// ------------------------------------------------------------------------------- Keycap

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

fn keycap_height(t: &Paint) -> f32 {
    t.space(18.0)
}

/// How wide a Keycap for `key` draws.
pub fn keycap_width(window: &Window, t: &Paint, key: &str) -> f32 {
    let tk = t.tokens();
    let b = &tk.controls.normal.border.widths;
    let adv = advance(window, t, &key_label(key), tk.font.caption, false).ceil();
    keycap_height(t).max(adv + (tk.spacing.sm * 2) as f32 + (b[1] + b[3]) as f32)
}

/// How wide a KeyCombo for `combo` draws.
pub fn key_combo_width(window: &Window, t: &Paint, combo: &str) -> f32 {
    let keys = split_combo(combo);
    let gap = t.tokens().spacing.xxs as f32;
    keys.iter().map(|k| keycap_width(window, t, k)).sum::<f32>() + gap * keys.len().saturating_sub(1) as f32
}

/// One key as a small cap: normal control glass, caption text centered by its ink.
#[derive(IntoElement)]
pub struct Keycap {
    key: SharedString,
}

impl Keycap {
    pub fn new(key: impl Into<SharedString>) -> Keycap {
        Keycap { key: key.into() }
    }
}

impl RenderOnce for Keycap {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let n = &tk.controls.normal;
        let text = key_label(&self.key);
        let (w, h) = (keycap_width(window, &t, &self.key), keycap_height(&t));
        div()
            .relative()
            .flex_shrink_0()
            .w(px(w))
            .h(px(h))
            .child(super::layer(&Look::new(n.fill, n.border.clone()), t.radius_small()))
            .child(optical(window, cx, &t, &text, tk.font.caption, t.fg(), None, w, h))
    }
}

/// A key combination as Keycaps, xxs apart: "Ctrl+Shift+S" draws Ctrl, ⇧, S.
#[derive(IntoElement)]
pub struct KeyCombo {
    combo: SharedString,
}

impl KeyCombo {
    pub fn new(combo: impl Into<SharedString>) -> KeyCombo {
        KeyCombo { combo: combo.into() }
    }
}

impl RenderOnce for KeyCombo {
    fn render(self, _: &mut Window, cx: &mut App) -> impl IntoElement {
        let gap = Paint::of(cx).tokens().spacing.xxs as f32;
        div().flex().flex_row().flex_shrink_0().gap(px(gap)).children(split_combo(&self.combo).into_iter().map(Keycap::new))
    }
}

/// A tooltip's shortcut after its text: a KeyCombo, as Qt's Tooltip draws it.
pub(crate) fn tooltip_shortcut(_: &Paint, combo: SharedString) -> AnyElement {
    KeyCombo::new(combo).into_any_element()
}

// ------------------------------------------------------------------------------- ShortcutSheet

/// Keyboard shortcuts, grouped: each group a section header over rows of a KeyCombo and what it
/// does, the combos in a column as wide as the group's widest. Groups fill `columns` columns left
/// to right, panelGap × 2 apart, and stack panelGap apart within a column.
#[derive(IntoElement)]
pub struct ShortcutSheet {
    groups: Vec<(SharedString, Vec<(SharedString, SharedString)>)>,
    columns: usize,
}

impl ShortcutSheet {
    pub fn new() -> ShortcutSheet {
        ShortcutSheet { groups: Vec::new(), columns: 2 }
    }
    /// A group: its title and (combo, description) rows.
    pub fn group<C: Into<SharedString>, D: Into<SharedString>>(
        mut self,
        title: impl Into<SharedString>,
        keys: impl IntoIterator<Item = (C, D)>,
    ) -> Self {
        self.groups.push((title.into(), keys.into_iter().map(|(c, d)| (c.into(), d.into())).collect()));
        self
    }
    pub fn columns(mut self, n: usize) -> Self {
        self.columns = n.max(1);
        self
    }
}

impl Default for ShortcutSheet {
    fn default() -> Self {
        ShortcutSheet::new()
    }
}

impl RenderOnce for ShortcutSheet {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let n = self.columns.min(self.groups.len()).max(1);
        let mut columns: Vec<Vec<AnyElement>> = (0..n).map(|_| Vec::new()).collect();
        for (i, (title, keys)) in self.groups.into_iter().enumerate() {
            let widest = keys.iter().map(|(c, _)| key_combo_width(window, &t, c)).fold(0.0f32, f32::max);
            let rows = keys.into_iter().map(|(c, d)| {
                div()
                    .flex()
                    .flex_row()
                    .items_center()
                    .gap(px(sp.xxl as f32))
                    .child(div().flex_shrink_0().w(px(widest)).child(KeyCombo::new(c)))
                    .child(label(&t, d, tk.font.body, false, t.fg()))
            });
            let group = div()
                .flex()
                .flex_col()
                .gap(px(sp.md as f32))
                .child(section_header(&t, title))
                .child(div().flex().flex_col().gap(px(sp.md as f32)).children(rows));
            columns[i % n].push(group.into_any_element());
        }
        let columns = columns.into_iter().map(|groups| div().flex().flex_col().gap(px(sp.panel_gap as f32)).children(groups));
        div().flex().flex_row().items_start().gap(px((sp.panel_gap * 2) as f32)).children(columns)
    }
}

// ------------------------------------------------------------------------------- Toolbar

/// The vertical rule between groups in a Toolbar: 1px in the track color, sm clear each side.
pub fn toolbar_separator(t: &Paint) -> Div {
    let sm = t.tokens().spacing.sm as f32;
    div().flex_shrink_0().w(px(sm * 2.0 + 1.0)).h(px(t.space(16.0))).child(div().w(px(1.0)).h_full().ml(px(sm)).bg(t.track()))
}

/// [`toolbar_separator`] as an element, under Qt's name.
#[derive(IntoElement, Default)]
pub struct ToolbarSeparator;

impl RenderOnce for ToolbarSeparator {
    fn render(self, _: &mut Window, cx: &mut App) -> impl IntoElement {
        toolbar_separator(&Paint::of(cx))
    }
}

/// An app toolbar: IconButtons and Buttons in three slots (leading at the left, center in the
/// middle of the whole bar, trailing at the right), groups split by [`ToolbarSeparator`]s, an
/// optional overflow "…" IconButton at the end, and a 1px track rule along the bottom. Items in a
/// slot sit xxs apart, vertically centered; the bar is sm inside its edges.
#[derive(IntoElement)]
pub struct Toolbar {
    id: ElementId,
    leading: Vec<AnyElement>,
    center: Vec<AnyElement>,
    trailing: Vec<AnyElement>,
    overflow: bool,
    overflow_cursor: bool,
    divided: bool,
    on_overflow: Option<Handler<ClickEvent>>,
}

impl Toolbar {
    pub fn new() -> Toolbar {
        Toolbar {
            id: "omarchy-toolbar".into(),
            leading: Vec::new(),
            center: Vec::new(),
            trailing: Vec::new(),
            overflow: false,
            overflow_cursor: false,
            divided: true,
            on_overflow: None,
        }
    }
    /// The element id (the overflow button's focus is kept under it).
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
        self
    }
    /// An item at the left (Qt's default children).
    pub fn leading(mut self, item: impl IntoElement) -> Self {
        self.leading.push(item.into_any_element());
        self
    }
    /// An item centered on the whole bar.
    pub fn center(mut self, item: impl IntoElement) -> Self {
        self.center.push(item.into_any_element());
        self
    }
    /// An item at the right, before the overflow.
    pub fn trailing(mut self, item: impl IntoElement) -> Self {
        self.trailing.push(item.into_any_element());
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
    pub fn on_overflow(mut self, f: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static) -> Self {
        self.on_overflow = Some(Rc::new(f));
        self
    }
}

impl Default for Toolbar {
    fn default() -> Self {
        Toolbar::new()
    }
}

impl RenderOnce for Toolbar {
    fn render(self, _: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let sp = &t.tokens().spacing;
        let pad = sp.sm as f32;
        let gap = px(sp.xxs as f32);
        let rule = if self.divided { 1.0 } else { 0.0 };
        let slot = |items: Vec<AnyElement>| div().flex().flex_row().items_center().gap(gap).children(items);
        let mut end = self.trailing;
        if self.overflow {
            let id = ElementId::NamedChild(Arc::new(self.id.clone()), "overflow".into());
            let mut b = IconButton::new("\u{f141}").id(id).cursor(self.overflow_cursor);
            if let Some(f) = self.on_overflow {
                b = b.on_click(move |e, w, cx| f(e, w, cx));
            }
            end.push(b.into_any_element());
        }
        let mut bar = div()
            .relative()
            .flex()
            .flex_row()
            .w_full()
            .items_center()
            .justify_between()
            .pt(px(pad))
            .px(px(pad))
            .pb(px(pad + rule))
            .child(slot(self.leading))
            .child(slot(end));
        if !self.center.is_empty() {
            bar = bar.child(
                div()
                    .absolute()
                    .left_0()
                    .right_0()
                    .top(px(pad))
                    .bottom(px(pad + rule))
                    .flex()
                    .items_center()
                    .justify_center()
                    .child(slot(self.center)),
            );
        }
        if self.divided {
            bar = bar.child(div().absolute().left_0().right_0().bottom_0().h(px(1.0)).bg(t.track()));
        }
        bar
    }
}

// ------------------------------------------------------------------------------- Tabs

/// Text tabs over a 1px track rule, the selected one bold with an accent underline. A tab under
/// the pointer, the keyboard cursor (`cursor`) or the focused control's selection takes the hover
/// fill and full foreground; the rest are dim. One tab stop: Left/Right (h/l) move the selection,
/// Home/End jump, a press chooses. For a small choice inside a form, use ButtonGroup.
#[derive(IntoElement)]
pub struct Tabs {
    id: ElementId,
    labels: Vec<SharedString>,
    current: usize,
    cursor: Option<usize>,
    on_choose: Option<Handler<usize>>,
}

impl Tabs {
    pub fn new<L: Into<SharedString>>(labels: impl IntoIterator<Item = L>, current: usize) -> Tabs {
        let labels: Vec<SharedString> = labels.into_iter().map(Into::into).collect();
        let key = labels.iter().map(|l| l.as_ref()).collect::<Vec<_>>().join("|");
        Tabs { id: id_for("tabs", &key), labels, current, cursor: None, on_choose: None }
    }
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
        self
    }
    /// Force the hover-cursor look on one tab.
    pub fn cursor(mut self, index: Option<usize>) -> Self {
        self.cursor = index;
        self
    }
    /// Called with the chosen tab's index.
    pub fn on_choose(mut self, f: impl Fn(&usize, &mut Window, &mut App) + 'static) -> Self {
        self.on_choose = Some(Rc::new(f));
        self
    }
}

/// Keys a one-stop chooser of `n` items takes: back and forward (clamped), Home and End. `back`
/// and `forward` are the key names.
fn choose_key(ev: &KeyDownEvent, current: usize, n: usize, back: &[&str], forward: &[&str]) -> Option<usize> {
    if n == 0 || ev.keystroke.modifiers.modified() {
        return None;
    }
    let k = ev.keystroke.key.as_str();
    let last = n - 1;
    Some(if back.contains(&k) {
        current.saturating_sub(1).min(last)
    } else if forward.contains(&k) {
        (current + 1).min(last)
    } else if k == "home" {
        0
    } else if k == "end" {
        last
    } else {
        return None;
    })
}

impl RenderOnce for Tabs {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let focus = focus_handle(&self.id, window, cx);
        let focused = focus.is_focused(window);
        let hover_fill = color(tk.controls.hover.fill);
        let pressed_fill = color(tk.controls.pressed.fill);
        let r = t.radius_small();
        let selected_ink = color(tk.controls.selected.text);
        let n = self.labels.len();
        let gap = sp.xxs as f32;
        let mut cursor_x = 0.0f32;
        let mut underline: Option<(f32, f32)> = None;
        let mut row = div().flex().flex_row().gap(px(gap));
        for (i, text) in self.labels.into_iter().enumerate() {
            let selected = i == self.current;
            let forced = self.cursor == Some(i) || (focused && selected);
            let ink = if selected {
                selected_ink
            } else if forced {
                t.fg()
            } else {
                t.dim()
            };
            // Qt sizes the tab to the label's whole-pixel width and centers the label in it.
            let width = advance(window, &t, &text, tk.font.body, selected).ceil() + (sp.control_padding_x * 2) as f32;
            if selected {
                underline = Some((cursor_x, width));
            }
            cursor_x += width + gap;
            let selector = format!("tab-{text}");
            let base = div()
                .id(ElementId::NamedChild(Arc::new(self.id.clone()), format!("tab-{i}").into()))
                .debug_selector(move || selector)
                .relative()
                .group(GROUP)
                .rounded_t(r)
                .flex()
                .flex_shrink_0()
                .items_center()
                .justify_center()
                .w(px(width))
                .py(px(sp.control_padding_y as f32));
            let mut word = label(&t, text, tk.font.body, selected, ink);
            if !selected && !forced {
                let fg = t.fg();
                word = word.group_hover(GROUP, move |s| s.text_color(fg));
            }
            let hover_layer = div().absolute().top_0().left_0().size_full().rounded_t(r).bg(hover_fill);
            let hover_layer = if forced { hover_layer } else { hover_layer.opacity(0.0).group_hover(GROUP, |s| s.opacity(1.0)) };
            let pressed_layer = div()
                .id("pressed")
                .absolute()
                .top_0()
                .left_0()
                .size_full()
                .rounded_t(r)
                .group_active(GROUP, move |s| s.bg(pressed_fill));
            let mut tab = base.child(hover_layer).child(pressed_layer).child(word);
            let (f, focus) = (self.on_choose.clone(), focus.clone());
            let current = self.current;
            tab = tab.on_click(move |_, window, cx| {
                focus.focus(window, cx);
                if i != current
                    && let Some(f) = &f
                {
                    f(&i, window, cx);
                }
            });
            row = row.child(tab);
        }
        let (f, current) = (self.on_choose, self.current);
        let mut out = div()
            .id(self.id)
            .relative()
            .w_full()
            .track_focus(&focus)
            .on_key_down(move |ev: &KeyDownEvent, window, cx| {
                if tab_keys(ev, window, cx) {
                    cx.stop_propagation();
                    return;
                }
                let Some(next) = choose_key(ev, current, n, &["left", "h"], &["right", "l"]) else { return };
                if next != current
                    && let Some(f) = &f
                {
                    f(&next, window, cx);
                }
                cx.stop_propagation();
            })
            .child(row)
            .child(div().absolute().left_0().right_0().bottom_0().h(px(1.0)).bg(t.track()));
        if let Some((ux, uw)) = underline {
            out = out.child(div().absolute().left(px(ux)).bottom_0().w(px(uw)).h(px(sp.xxs as f32)).bg(t.accent()));
        }
        out
    }
}

// ------------------------------------------------------------------------------- IconRail

/// A vertical rail of icon tabs, as an app's side-panel switcher: square buttons sm apart, the
/// selected glyph in the accent with an accent bar at the rail's leading edge, a 1px track rule
/// down the trailing edge. Under the pointer, the keyboard cursor (`cursor`) or the focused rail's
/// selection a button takes the hover fill; the rest are dim. One tab stop: Up/Down (k/j) move
/// the selection, Home/End jump, a press chooses.
#[derive(IntoElement)]
pub struct IconRail {
    id: ElementId,
    items: Vec<(SharedString, SharedString)>,
    current: usize,
    cursor: Option<usize>,
    on_choose: Option<Handler<usize>>,
}

impl IconRail {
    /// `items` are (glyph, label) pairs; the label is the accessible name.
    pub fn new<G: Into<SharedString>, L: Into<SharedString>>(items: impl IntoIterator<Item = (G, L)>, current: usize) -> IconRail {
        let items: Vec<(SharedString, SharedString)> = items.into_iter().map(|(g, l)| (g.into(), l.into())).collect();
        let key = items.iter().map(|(_, l)| l.as_ref()).collect::<Vec<_>>().join("|");
        IconRail { id: id_for("rail", &key), items, current, cursor: None, on_choose: None }
    }
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
        self
    }
    pub fn cursor(mut self, index: Option<usize>) -> Self {
        self.cursor = index;
        self
    }
    /// Called with the chosen item's index.
    pub fn on_choose(mut self, f: impl Fn(&usize, &mut Window, &mut App) + 'static) -> Self {
        self.on_choose = Some(Rc::new(f));
        self
    }
}

/// The side of an IconRail's square items: controlHeight plus sm each side.
pub fn icon_rail_item_size(t: &Paint) -> f32 {
    let sp = &t.tokens().spacing;
    (sp.control_height + sp.sm * 2) as f32
}

impl RenderOnce for IconRail {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let size = icon_rail_item_size(&t);
        let pad = sp.sm as f32;
        let gap = sp.sm as f32;
        let glyph_size = tk.font.icon_large;
        let focus = focus_handle(&self.id, window, cx);
        let focused = focus.is_focused(window);
        let rest = Look::clear();
        let hot = Look::new(tk.controls.hover.fill, BorderSpec::none());
        let n = self.items.len();
        let mut list = div().absolute().left(px(pad)).top(px(pad)).flex().flex_col().gap(px(gap));
        for (i, (glyph, _)) in self.items.into_iter().enumerate() {
            let selected = i == self.current;
            let forced = self.cursor == Some(i) || (focused && selected);
            let ink = if selected {
                t.accent()
            } else if forced {
                t.fg()
            } else {
                t.dim()
            };
            let hover_ink = (!selected && !forced).then(|| t.fg());
            let mark = optical(window, cx, &t, &glyph, glyph_size, ink, hover_ink, size, size);
            let base = div()
                .id(ElementId::NamedChild(Arc::new(self.id.clone()), format!("rail-{i}").into()))
                .debug_selector(move || format!("rail-{i}"))
                .flex_shrink_0()
                .size(px(size));
            let mut item = chrome(base, &t, &rest, &hot, forced, t.radius()).child(mark.absolute().top_0().left_0());
            let (f, focus, current) = (self.on_choose.clone(), focus.clone(), self.current);
            item = item.on_click(move |_, window, cx| {
                focus.focus(window, cx);
                if i != current
                    && let Some(f) = &f
                {
                    f(&i, window, cx);
                }
            });
            list = list.child(item);
        }
        let height = pad * 2.0 + n as f32 * size + n.saturating_sub(1) as f32 * gap;
        let (f, current) = (self.on_choose, self.current);
        let mut rail = div()
            .id(self.id)
            .relative()
            .flex_shrink_0()
            .w(px(size + pad * 2.0 + 1.0))
            .h(px(height))
            .track_focus(&focus)
            .on_key_down(move |ev: &KeyDownEvent, window, cx| {
                if tab_keys(ev, window, cx) {
                    cx.stop_propagation();
                    return;
                }
                let Some(next) = choose_key(ev, current, n, &["up", "k"], &["down", "j"]) else { return };
                if next != current
                    && let Some(f) = &f
                {
                    f(&next, window, cx);
                }
                cx.stop_propagation();
            })
            .child(div().absolute().top_0().right_0().bottom_0().w(px(1.0)).bg(t.track()))
            .child(list);
        if self.current < n {
            let bar = glyph_size as f32;
            let y = pad + self.current as f32 * (size + gap) + ((size - bar) / 2.0).round();
            rail = rail.child(
                div().absolute().left_0().top(px(y)).w(px(sp.xxs as f32)).h(px(bar)).rounded(px((tk.radius as f32).min(1.0))).bg(t.accent()),
            );
        }
        rail
    }
}

// ------------------------------------------------------------------------------- Menu

/// One row of a [`Menu`]: an optional leading glyph, the label, a dim shortcut hint at the right,
/// and a trailing mark: an accent check (or a radio dot) when checked, or a chevron when the row
/// opens a submenu. Under the pointer or the menu's keyboard cursor it takes the menu surface's
/// selected background and text. Disabled rows are faint and inert; destructive rows are urgent.
pub struct MenuItem {
    label: SharedString,
    icon: Option<SharedString>,
    shortcut: Option<SharedString>,
    checkable: bool,
    checked: bool,
    exclusive: bool,
    destructive: bool,
    has_submenu: bool,
    submenu: Option<Menu>,
    disabled: bool,
    cursor: bool,
    on_click: Option<Handler<ClickEvent>>,
}

impl MenuItem {
    pub fn new(label: impl Into<SharedString>) -> MenuItem {
        MenuItem {
            label: label.into(),
            icon: None,
            shortcut: None,
            checkable: false,
            checked: false,
            exclusive: false,
            destructive: false,
            has_submenu: false,
            submenu: None,
            disabled: false,
            cursor: false,
            on_click: None,
        }
    }
    /// A leading glyph (a Nerd Font codepoint).
    pub fn icon(mut self, glyph: impl Into<SharedString>) -> Self {
        self.icon = Some(glyph.into());
        self
    }
    /// The shortcut hint, as plain text ("Ctrl+S").
    pub fn shortcut(mut self, hint: impl Into<SharedString>) -> Self {
        self.shortcut = Some(hint.into());
        self
    }
    /// A checkable row, checked or not.
    pub fn check(mut self, checked: bool) -> Self {
        self.checkable = true;
        self.checked = checked;
        self
    }
    /// One of a set of exclusive choices: a radio dot when chosen.
    pub fn radio(mut self, checked: bool) -> Self {
        self.checkable = true;
        self.exclusive = true;
        self.checked = checked;
        self
    }
    /// Draw the submenu chevron without a submenu of the kit's (the app opens its own).
    pub fn has_submenu(mut self, on: bool) -> Self {
        self.has_submenu = on;
        self
    }
    /// The menu this row opens to its right: Right or l (or a press) opens it with its first row
    /// under the cursor; Left, h or Escape goes back out; running one of its rows closes it.
    pub fn submenu(mut self, menu: Menu) -> Self {
        self.has_submenu = true;
        self.submenu = Some(menu);
        self
    }
    pub fn disabled(mut self, on: bool) -> Self {
        self.disabled = on;
        self
    }
    pub fn destructive(mut self, on: bool) -> Self {
        self.destructive = on;
        self
    }
    /// Force the cursor look, as a keyboard-walked menu's cursor does (and for stills).
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    /// A press, or Enter or Space with the menu's cursor on the row.
    pub fn on_click(mut self, f: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static) -> Self {
        self.on_click = Some(Rc::new(f));
        self
    }
}

enum Entry {
    Item(MenuItem),
    Separator,
    Section(SharedString),
}

/// The keyboard cursor of a menu and its open submenu, kept across frames.
#[derive(Default)]
struct MenuState {
    cursor: Option<usize>,
    sub: Option<usize>,
    sub_cursor: Option<usize>,
    focused_once: bool,
}

/// Where a menu draws.
#[derive(Clone, Copy)]
enum Placement {
    /// In place, held open (galleries, stills, a panel's own menu).
    Held,
    /// Floating below the bottom-left of the element it's a child of (put it in the trigger's
    /// relative container), `xs` away.
    Below,
    /// Floating with its top-left at a point in the window.
    At(Point<Pixels>),
}

/// A menu: MenuItems, separators and section labels on the menu surface ([menu] background and
/// border, Hyprland's rounding), sm inside its border. One tab stop; Up/Down (k/j) walk the
/// enabled rows and wrap, Home/End jump, Enter or Space runs the row under the cursor, Right (l)
/// opens its submenu, Left/h/Escape leave a submenu, Escape (and a press outside a floating menu)
/// calls `on_close`. Running a row closes a floating menu.
///
/// Held in place by default; [`Menu::popup`] floats it below its parent, [`Menu::at`] at a point
/// (a context menu). A floating menu takes the keyboard when it opens.
#[derive(IntoElement)]
pub struct Menu {
    id: Option<ElementId>,
    entries: Vec<Entry>,
    min_width: Option<f32>,
    placement: Placement,
    on_close: Option<Handler<()>>,
}

impl Menu {
    pub fn new() -> Menu {
        Menu { id: None, entries: Vec::new(), min_width: None, placement: Placement::Held, on_close: None }
    }
    /// The element id its focus and cursor are kept under (by default from its rows' labels).
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = Some(id.into());
        self
    }
    pub fn item(mut self, item: MenuItem) -> Self {
        self.entries.push(Entry::Item(item));
        self
    }
    pub fn items(mut self, items: impl IntoIterator<Item = MenuItem>) -> Self {
        self.entries.extend(items.into_iter().map(Entry::Item));
        self
    }
    /// A 1px rule in the track color between groups of rows.
    pub fn separator(mut self) -> Self {
        self.entries.push(Entry::Separator);
        self
    }
    /// A label heading a group of rows, in the section header style.
    pub fn section(mut self, label: impl Into<SharedString>) -> Self {
        self.entries.push(Entry::Section(label.into()));
        self
    }
    /// The narrowest the menu gets, inside its border and padding (default `space(160)`).
    pub fn min_width(mut self, w: f32) -> Self {
        self.min_width = Some(w);
        self
    }
    /// Float below the bottom-left of the element this menu is a child of, `xs` away.
    pub fn popup(mut self) -> Self {
        self.placement = Placement::Below;
        self
    }
    /// Float with the top-left at `position` in the window (a context menu, at the pointer).
    pub fn at(mut self, position: Point<Pixels>) -> Self {
        self.placement = Placement::At(position);
        self
    }
    /// Escape, a press outside a floating menu, and running a row of a floating menu.
    pub fn on_close(mut self, f: impl Fn(&(), &mut Window, &mut App) + 'static) -> Self {
        self.on_close = Some(Rc::new(f));
        self
    }

    fn default_id(&self) -> ElementId {
        let key = self
            .entries
            .iter()
            .filter_map(|e| match e {
                Entry::Item(i) => Some(i.label.as_ref()),
                _ => None,
            })
            .collect::<Vec<_>>()
            .join("|");
        id_for("menu", &key)
    }
}

impl Default for Menu {
    fn default() -> Self {
        Menu::new()
    }
}

/// The next enabled row from `from` by `step`, wrapping; from nothing, the first (or last).
fn walk(enabled: &[bool], from: Option<usize>, step: isize) -> Option<usize> {
    let n = enabled.len() as isize;
    if n == 0 {
        return None;
    }
    let mut i = match from {
        Some(i) => i as isize,
        None => {
            if step > 0 {
                -1
            } else {
                n
            }
        }
    };
    for _ in 0..n {
        i = (i + step).rem_euclid(n);
        if enabled[i as usize] {
            return Some(i as usize);
        }
    }
    None
}

/// What a row does when run: its handler, and whether it opens a submenu instead.
struct RowAction {
    enabled: bool,
    opens: bool,
    run: Option<Handler<ClickEvent>>,
}

fn actions(entries: &[Entry]) -> Vec<RowAction> {
    entries
        .iter()
        .filter_map(|e| match e {
            Entry::Item(i) => Some(RowAction { enabled: !i.disabled, opens: i.submenu.is_some(), run: i.on_click.clone() }),
            _ => None,
        })
        .collect()
}

#[allow(clippy::too_many_arguments)]
fn menu_row(
    window: &Window,
    cx: &App,
    t: &Paint,
    item: MenuItem,
    id: ElementId,
    reserve_icon: bool,
    reserve_mark: bool,
    under_cursor: bool,
) -> gpui::Stateful<Div> {
    let tk = t.tokens();
    let sp = &tk.spacing;
    let menu = &tk.surfaces.menu;
    let h = sp.popup_row_height as f32;
    let gap = sp.control_gap as f32;
    let icon_box = tk.font.icon as f32;
    let enabled = !item.disabled;

    let rest_ink = if !enabled {
        t.faint()
    } else if item.destructive {
        t.urgent()
    } else {
        color(menu.base.text)
    };
    let hot_ink = (enabled && !item.destructive).then(|| color(menu.selected_text));
    let forced = (item.cursor || under_cursor) && enabled;
    let (ink, hover_ink) = if forced { (hot_ink.unwrap_or(rest_ink), None) } else { (rest_ink, hot_ink) };

    let selector = format!("menu-row-{}", item.label);
    let base = div()
        .id(id)
        .debug_selector(move || selector)
        .flex()
        .flex_row()
        .w_full()
        .h(px(h))
        .items_center()
        .px(px(sp.control_padding_x as f32));
    let mut kids: Vec<AnyElement> = Vec::new();
    if reserve_icon {
        kids.push(match &item.icon {
            Some(g) => optical(window, cx, t, g, tk.font.icon, ink, hover_ink, icon_box, h).into_any_element(),
            None => div().flex_shrink_0().w(px(icon_box)).h(px(h)).into_any_element(),
        });
    }
    let mut word = label(t, item.label.clone(), tk.font.body, false, ink);
    if let Some(c) = hover_ink {
        word = word.group_hover(GROUP, move |s| s.text_color(c));
    }
    if reserve_icon {
        word = word.ml(px(gap));
    }
    kids.push(word.into_any_element());
    kids.push(div().flex_1().into_any_element());
    if let Some(hint) = item.shortcut {
        let ink = if enabled { t.dim() } else { t.faint() };
        let font_id = cx.text_system().resolve_font(&font_for(t, false));
        let baseline_y = |size: i64| {
            let fs = px(size as f32);
            let asc = f32::from(cx.text_system().ascent(font_id, fs));
            let desc = f32::from(cx.text_system().descent(font_id, fs)).abs();
            let line = tk.font.line_px(size) as f32;
            (h - line) / 2.0 + (t.exact_line(size) + asc - desc) / 2.0
        };
        let dy = baseline_y(tk.font.body) - baseline_y(tk.font.body_small);
        kids.push(label(t, hint, tk.font.body_small, false, ink).relative().top(px(dy)).ml(px(sp.huge as f32)).into_any_element());
    }
    if reserve_mark {
        let mark = if item.checkable && item.checked {
            let ink = if enabled { t.accent() } else { t.faint() };
            if item.exclusive {
                let dot = t.space(6.0);
                div()
                    .flex()
                    .flex_shrink_0()
                    .items_center()
                    .justify_center()
                    .w(px(icon_box))
                    .h(px(h))
                    .child(div().size(px(dot)).rounded(px(dot / 2.0)).bg(ink))
            } else {
                optical(window, cx, t, "\u{2713}", tk.font.body, ink, None, icon_box, h)
            }
        } else if item.has_submenu {
            let ink = if enabled { t.dim() } else { t.faint() };
            optical(window, cx, t, "\u{f054}", tk.font.icon_small, ink, None, icon_box, h)
        } else {
            div().flex_shrink_0().w(px(icon_box)).h(px(h))
        };
        kids.push(mark.ml(px(gap)).into_any_element());
    }
    if !enabled {
        return base.children(kids);
    }
    let rest = Look::clear();
    let hot = Look::new(menu.selected_background, BorderSpec::none());
    chrome(base, t, &rest, &hot, forced, t.radius_small()).children(kids)
}

/// The surface and its rows. `cursor` is the row under the keyboard cursor; `open` the row whose
/// submenu shows, with that submenu drawn beside it from `sub`.
#[allow(clippy::too_many_arguments)]
fn surface(
    window: &Window,
    cx: &App,
    t: &Paint,
    id: &ElementId,
    entries: Vec<Entry>,
    min_width: Option<f32>,
    cursor: Option<usize>,
    open: Option<usize>,
    mut sub: Option<(Vec<Entry>, Option<f32>, Option<usize>)>,
    in_sub: bool,
    run: Rc<dyn Fn(bool, usize, &mut Window, &mut App)>,
) -> Div {
    let tk = t.tokens();
    let sp = &tk.spacing;
    let menu = &tk.surfaces.menu;
    let bw = menu.base.border.widths.map(|w| w as f32);
    let pad = sp.sm as f32;
    let min_w = min_width.unwrap_or_else(|| t.space(160.0));
    let reserve_icon = entries.iter().any(|e| matches!(e, Entry::Item(i) if i.icon.is_some()));
    let reserve_mark = entries.iter().any(|e| matches!(e, Entry::Item(i) if i.checkable || i.has_submenu));

    let mut frame = div()
        .flex()
        .flex_col()
        .flex_shrink_0()
        .min_w(px(min_w + pad * 2.0 + bw[1] + bw[3]))
        .p(px(pad))
        .bg(color(menu.base.background))
        .rounded(t.radius());
    frame = border(frame, &menu.base.border);
    let mut row_index = 0;
    for entry in entries {
        frame = frame.child(match entry {
            Entry::Item(item) => {
                let i = row_index;
                row_index += 1;
                let rid = ElementId::NamedChild(Arc::new(id.clone()), format!("row-{i}").into());
                let enabled = !item.disabled;
                let mut r = menu_row(window, cx, t, item, rid, reserve_icon, reserve_mark, cursor == Some(i));
                if enabled {
                    let run = run.clone();
                    r = r.on_click(move |_, window, cx| run(in_sub, i, window, cx));
                }
                let mut wrap = div().relative().w_full().child(r);
                if open == Some(i)
                    && let Some((entries, min_w, sub_cursor)) = sub.take()
                {
                    // Beside the row, its first row level with this one.
                    let sid = ElementId::NamedChild(Arc::new(id.clone()), "submenu".into());
                    let inner = surface(window, cx, t, &sid, entries, min_w, sub_cursor, None, None, true, run.clone());
                    wrap = wrap.child(
                        div()
                            .absolute()
                            .top(px(-(pad + bw[0])))
                            .right(px(-(pad + bw[1])))
                            .child(deferred(anchored().child(div().occlude().child(inner))).with_priority(2)),
                    );
                }
                wrap.into_any_element()
            }
            Entry::Separator => div().w_full().py(px(pad)).child(div().w_full().h(px(1.0)).bg(t.track())).into_any_element(),
            Entry::Section(text) => div()
                .pt(px(pad))
                .pb(px(sp.xxs as f32))
                .px(px(sp.control_padding_x as f32))
                .child(section_header(t, text))
                .into_any_element(),
        });
    }
    frame
}

impl RenderOnce for Menu {
    fn render(mut self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let id = self.id.clone().unwrap_or_else(|| self.default_id());
        let focus = focus_handle(&id, window, cx);
        let state = keyed_state(&id, "menu", window, cx, MenuState::default);
        let floating = !matches!(self.placement, Placement::Held);
        // A floating menu takes the keyboard when it first shows.
        if floating && !state.read(cx).focused_once {
            state.update(cx, |s, _| s.focused_once = true);
            let f = focus.clone();
            window.defer(cx, move |window, cx| f.focus(window, cx));
        }
        let (cursor, open, sub_cursor) = {
            let s = state.read(cx);
            (s.cursor, s.sub, s.sub_cursor)
        };

        let rows = actions(&self.entries);
        let enabled: Rc<Vec<bool>> = Rc::new(rows.iter().map(|r| r.enabled).collect());
        let opens: Rc<Vec<bool>> = Rc::new(rows.iter().map(|r| r.opens).collect());
        // Each row's submenu's enabled rows, for where its cursor starts when it opens.
        let firsts: Rc<Vec<Option<usize>>> = Rc::new(
            self.entries
                .iter()
                .filter_map(|e| match e {
                    Entry::Item(i) => Some(i.submenu.as_ref().and_then(|m| {
                        let en: Vec<bool> = actions(&m.entries).iter().map(|r| r.enabled).collect();
                        walk(&en, None, 1)
                    })),
                    _ => None,
                })
                .collect(),
        );
        let runs: Rc<Vec<Option<Handler<ClickEvent>>>> = Rc::new(rows.into_iter().map(|r| r.run).collect());
        // The open submenu's rows: what they do, and their entries to draw.
        let mut sub_draw = None;
        let mut sub_rows: Vec<RowAction> = Vec::new();
        if let Some(o) = open {
            let mut k = 0;
            for e in self.entries.iter_mut() {
                if let Entry::Item(item) = e {
                    if k == o
                        && let Some(m) = item.submenu.take()
                    {
                        sub_rows = actions(&m.entries);
                        sub_draw = Some((m.entries, m.min_width, sub_cursor));
                    }
                    k += 1;
                }
            }
        }
        let sub_enabled: Rc<Vec<bool>> = Rc::new(sub_rows.iter().map(|r| r.enabled).collect());
        let sub_runs: Rc<Vec<Option<Handler<ClickEvent>>>> = Rc::new(sub_rows.into_iter().map(|r| r.run).collect());
        let on_close = self.on_close.clone();

        // Running row `i` of the menu (`sub` false) or of its open submenu.
        let run: Rc<dyn Fn(bool, usize, &mut Window, &mut App)> = {
            let (state, runs, sub_runs, opens, firsts, on_close) =
                (state.clone(), runs.clone(), sub_runs.clone(), opens.clone(), firsts.clone(), on_close.clone());
            Rc::new(move |in_sub: bool, i: usize, window: &mut Window, cx: &mut App| {
                if !in_sub && opens.get(i).copied().unwrap_or(false) {
                    let first = firsts.get(i).copied().flatten();
                    state.update(cx, |s, cx| {
                        s.cursor = Some(i);
                        s.sub = Some(i);
                        s.sub_cursor = first;
                        cx.notify();
                    });
                    return;
                }
                let f = if in_sub { sub_runs.get(i).cloned().flatten() } else { runs.get(i).cloned().flatten() };
                state.update(cx, |s, cx| {
                    s.sub = None;
                    s.sub_cursor = None;
                    cx.notify();
                });
                if let Some(f) = f {
                    f(&ClickEvent::default(), window, cx);
                }
                if floating && let Some(close) = &on_close {
                    close(&(), window, cx);
                }
            })
        };

        let frame = surface(window, cx, &t, &id, self.entries, self.min_width, cursor, open, sub_draw, false, run.clone());

        let keys = {
            let (state, enabled, sub_enabled, opens, on_close) = (state.clone(), enabled.clone(), sub_enabled.clone(), opens.clone(), on_close.clone());
            move |ev: &KeyDownEvent, window: &mut Window, cx: &mut App| {
                if tab_keys(ev, window, cx) {
                    cx.stop_propagation();
                    return;
                }
                if ev.keystroke.modifiers.modified() {
                    return;
                }
                let k = ev.keystroke.key.as_str();
                let (cursor, sub, sub_cursor) = {
                    let s = state.read(cx);
                    (s.cursor, s.sub, s.sub_cursor)
                };
                let in_sub = sub.is_some();
                let (rows, at) = if in_sub { (&sub_enabled, sub_cursor) } else { (&enabled, cursor) };
                let set = |to: Option<usize>, cx: &mut App| {
                    state.update(cx, |s, cx| {
                        if in_sub {
                            s.sub_cursor = to;
                        } else {
                            s.cursor = to;
                        }
                        cx.notify();
                    })
                };
                match k {
                    "down" | "j" => set(walk(rows, at, 1), cx),
                    "up" | "k" => set(walk(rows, at, -1), cx),
                    "home" => set(walk(rows, None, 1), cx),
                    "end" => set(walk(rows, None, -1), cx),
                    "enter" | "kp_enter" | "space" => {
                        if let Some(i) = at {
                            run(in_sub, i, window, cx);
                        }
                    }
                    "right" | "l" => {
                        if !in_sub
                            && let Some(i) = cursor
                            && opens.get(i).copied().unwrap_or(false)
                        {
                            run(false, i, window, cx);
                        }
                    }
                    "left" | "h" | "escape" => {
                        if in_sub {
                            state.update(cx, |s, cx| {
                                s.sub = None;
                                s.sub_cursor = None;
                                cx.notify();
                            });
                        } else if k == "escape"
                            && let Some(close) = &on_close
                        {
                            close(&(), window, cx);
                        }
                    }
                    _ => return,
                }
                cx.stop_propagation();
            }
        };
        let menu = div().id(id).track_focus(&focus).on_key_down(keys).child(frame);
        match self.placement {
            Placement::Held => menu.into_any_element(),
            Placement::Below => {
                let gap = px(t.tokens().spacing.xs as f32);
                let close = on_close.clone();
                let menu = menu.occlude().on_mouse_down_out(move |_, window, cx| {
                    if let Some(f) = &close {
                        f(&(), window, cx);
                    }
                });
                div()
                    .absolute()
                    .left_0()
                    .bottom_0()
                    .child(deferred(anchored().offset(point(px(0.), gap)).child(menu)).with_priority(1))
                    .into_any_element()
            }
            Placement::At(p) => {
                let close = on_close.clone();
                let menu = menu.occlude().on_mouse_down_out(move |_, window, cx| {
                    if let Some(f) = &close {
                        f(&(), window, cx);
                    }
                });
                deferred(anchored().position(p).child(menu)).with_priority(1).into_any_element()
            }
        }
    }
}

/// A [`Menu`] that opens at a point (the pointer) rather than under a trigger: `at(position)` in
/// window coordinates, from a right-click's `event.position`. Without `at` it draws in place,
/// held open.
#[derive(IntoElement)]
pub struct ContextMenu {
    menu: Menu,
}

impl ContextMenu {
    pub fn new() -> ContextMenu {
        ContextMenu { menu: Menu::new() }
    }
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.menu = self.menu.id(id);
        self
    }
    pub fn item(mut self, item: MenuItem) -> Self {
        self.menu = self.menu.item(item);
        self
    }
    pub fn items(mut self, items: impl IntoIterator<Item = MenuItem>) -> Self {
        self.menu = self.menu.items(items);
        self
    }
    pub fn separator(mut self) -> Self {
        self.menu = self.menu.separator();
        self
    }
    pub fn section(mut self, label: impl Into<SharedString>) -> Self {
        self.menu = self.menu.section(label);
        self
    }
    pub fn min_width(mut self, w: f32) -> Self {
        self.menu = self.menu.min_width(w);
        self
    }
    pub fn at(mut self, position: Point<Pixels>) -> Self {
        self.menu = self.menu.at(position);
        self
    }
    pub fn on_close(mut self, f: impl Fn(&(), &mut Window, &mut App) + 'static) -> Self {
        self.menu = self.menu.on_close(f);
        self
    }
}

impl Default for ContextMenu {
    fn default() -> Self {
        ContextMenu::new()
    }
}

impl RenderOnce for ContextMenu {
    fn render(self, _: &mut Window, _: &mut App) -> impl IntoElement {
        self.menu
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

    #[test]
    fn walk_skips_disabled_and_wraps() {
        let rows = [true, true, false, true];
        assert_eq!(walk(&rows, None, 1), Some(0));
        assert_eq!(walk(&rows, None, -1), Some(3));
        assert_eq!(walk(&rows, Some(1), 1), Some(3));
        assert_eq!(walk(&rows, Some(3), 1), Some(0));
        assert_eq!(walk(&rows, Some(0), -1), Some(3));
    }
}
