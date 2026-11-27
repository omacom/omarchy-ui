//! Surfaces: Card (the shell's panels and popups), Pane (an app's in-window panels), Heading,
//! section headers, separators, menu rows, tooltips and the confirm dialog.

use omarchy_theme::Rgba;
use rune::{Align, El, Side, col, el, row, text};

use super::Attach;
use crate::{Theme, border, color};

/// A caption-sized bold header in `textDim`: what heads a panel section.
pub fn section_header(t: &Theme, label: impl Into<String>) -> El {
    let size = t.tokens().font.caption;
    text(label.into())
        .font(t.text_style_bold(size))
        .color(t.dim())
        .pt((size as f32 * 0.15).ceil())
        .role("heading")
}

/// A 1px rule in the foreground at 12%.
pub fn separator(t: &Theme) -> El {
    el().w_full().h(1.0).fill(color(t.tokens().palette.foreground.with_alpha(0.12)))
}

/// A raised surface: [popups] background and border (Hyprland's active border by default),
/// Hyprland's rounding, panel padding, children panelGap apart.
pub struct Card {
    title: String,
    width: Option<f32>,
    children: Vec<El>,
}

impl Card {
    pub fn new() -> Card {
        Card { title: String::new(), width: None, children: Vec::new() }
    }
    pub fn title(mut self, title: impl Into<String>) -> Self {
        self.title = title.into();
        self
    }
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
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

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let pop = &tk.surfaces.popups;
        let bw = pop.border.widths;
        let pad = tk.spacing.panel_padding as f32;
        let mut c = col()
            .gap(tk.spacing.panel_gap as f32)
            .pt(pad + bw[0] as f32)
            .pr(pad + bw[1] as f32)
            .pb(pad + bw[2] as f32)
            .pl(pad + bw[3] as f32)
            .fill(color(pop.background))
            .radius(t.radius());
        c = border(c, &pop.border);
        if let Some(w) = self.width {
            c = c.w(w);
        }
        if !self.title.is_empty() {
            c = c.child(section_header(t, self.title));
        }
        c.child(col().w_full().gap(tk.spacing.panel_gap as f32).children(self.children))
    }
}

impl Default for Card {
    fn default() -> Self {
        Card::new()
    }
}

/// Which font token a [`Heading`] is set in: `title` for panes and toolbars, `heading` for
/// sections and dialogs, `display` for pages.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum HeadingLevel {
    #[default]
    Title,
    Heading,
    Display,
}

/// The title of a pane, toolbar, section or page: one bold line at the level's font token, an
/// optional `textDim` subtitle under it, and an optional trailing slot of actions. Not
/// [`section_header`] (the caption-sized dim label over a group of rows) and not
/// [`super::Hero`] (a shell panel's glyph, title and uppercased status). Both lines elide;
/// trailing items sit xxs apart, controlGap after the lines, centered against them.
pub struct Heading {
    text: String,
    subtitle: String,
    level: HeadingLevel,
    centered: bool,
    trailing: Vec<El>,
}

impl Heading {
    pub fn new(text: impl Into<String>) -> Heading {
        Heading { text: text.into(), subtitle: String::new(), level: HeadingLevel::Title, centered: false, trailing: Vec::new() }
    }
    pub fn subtitle(mut self, s: impl Into<String>) -> Self {
        self.subtitle = s.into();
        self
    }
    pub fn level(mut self, level: HeadingLevel) -> Self {
        self.level = level;
        self
    }
    /// Center the lines (a toolbar's title) in the space before the trailing slot.
    pub fn centered(mut self, on: bool) -> Self {
        self.centered = on;
        self
    }
    pub fn trailing(mut self, items: impl IntoIterator<Item = El>) -> Self {
        self.trailing.extend(items);
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let f = &tk.font;
        let sp = &tk.spacing;
        let (size, sub) = match self.level {
            HeadingLevel::Title => (f.title, f.body_small),
            HeadingLevel::Heading => (f.heading, f.body),
            HeadingLevel::Display => (f.display, f.subtitle),
        };
        let align = if self.centered { Align::Center } else { Align::Start };
        let mut lines = col().grow(1.0).min_w(0.0).gap(sp.xxs as f32).child(
            text(self.text.clone()).font(t.text_style_bold(size)).color(t.fg()).single_line().truncate().align(align),
        );
        if !self.subtitle.is_empty() {
            lines = lines.child(text(self.subtitle).font(t.text_style(sub)).color(t.dim()).single_line().truncate().align(align));
        }
        let mut e = row().items_center().role("heading").label(self.text).child(lines);
        if !self.trailing.is_empty() {
            e = e.gap(sp.control_gap as f32).child(row().items_center().gap(sp.xxs as f32).children(self.trailing));
        }
        e
    }
}

/// A flat surface inside an app window: an inspector, a side panel, a settings group. Not a
/// [`Card`] (the raised [popups] surface with the accent border): a pane sits on the window, so
/// by default it draws nothing but its padding, plus a hairline track rule on one side against
/// its neighbour (`divider`). `bordered` gives it the controls' normal glass instead, with the
/// theme's corners, for a group set off inside a page. Never a shadow.
///
/// The header is a [`Heading`] (title level) with `actions` as its trailing slot. Children stack
/// in a column under it, panelGap apart, panelPadding inside the edges.
pub struct Pane {
    title: String,
    subtitle: String,
    actions: Vec<El>,
    bordered: bool,
    divider: Option<Side>,
    width: Option<f32>,
    children: Vec<El>,
}

impl Pane {
    pub fn new() -> Pane {
        Pane { title: String::new(), subtitle: String::new(), actions: Vec::new(), bordered: false, divider: None, width: None, children: Vec::new() }
    }
    pub fn title(mut self, title: impl Into<String>) -> Self {
        self.title = title.into();
        self
    }
    pub fn subtitle(mut self, s: impl Into<String>) -> Self {
        self.subtitle = s.into();
        self
    }
    /// IconButtons or Buttons at the end of the header.
    pub fn actions(mut self, items: impl IntoIterator<Item = El>) -> Self {
        self.actions.extend(items);
        self
    }
    /// The controls' normal fill and border, and the theme's corners.
    pub fn bordered(mut self, on: bool) -> Self {
        self.bordered = on;
        self
    }
    /// A hairline track rule along one side, against the pane's neighbour.
    pub fn divider(mut self, side: Side) -> Self {
        self.divider = Some(side);
        self
    }
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
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

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let normal = &tk.controls.normal;
        let pad = sp.panel_padding as f32;
        let gap = sp.panel_gap as f32;
        let hair = sp.hairline as f32;
        let bw = if self.bordered { normal.border.widths.map(|w| w as f32) } else { [0.0; 4] };
        let rule = |side: Side| if self.divider == Some(side) { hair } else { 0.0 };
        let mut c = col()
            .gap(gap)
            .pt(pad + bw[0] + rule(Side::Top))
            .pr(pad + bw[1] + rule(Side::Right))
            .pb(pad + bw[2] + rule(Side::Bottom))
            .pl(pad + bw[3] + rule(Side::Left));
        if self.bordered {
            c = border(c.fill(color(normal.fill)).radius(t.radius()), &normal.border);
        }
        if let Some(w) = self.width {
            c = c.w(w);
        }
        if let Some(side) = self.divider {
            let line = match side {
                Side::Top => el().pin(Some(0.0), Some(0.0), Some(0.0), None).h(hair),
                Side::Right => el().pin(None, Some(0.0), Some(0.0), Some(0.0)).w(hair),
                Side::Bottom => el().pin(Some(0.0), None, Some(0.0), Some(0.0)).h(hair),
                Side::Left => el().pin(Some(0.0), Some(0.0), None, Some(0.0)).w(hair),
            };
            c = c.child(line.fill(t.track()));
        }
        if !self.title.is_empty() {
            c = c.child(Heading::new(self.title).subtitle(self.subtitle).trailing(self.actions).build(t).w_full());
        }
        c.child(col().w_full().gap(gap).children(self.children))
    }
}

impl Default for Pane {
    fn default() -> Self {
        Pane::new()
    }
}

/// One row of a menu or list: label, a check in the accent when `checked`, the hover fill under
/// the pointer or when `highlighted` (a keyboard-walked menu).
pub struct MenuRow {
    label: String,
    checked: bool,
    highlighted: bool,
    radius: Option<f32>,
    press: Option<Attach>,
}

impl MenuRow {
    pub fn new(label: impl Into<String>) -> MenuRow {
        MenuRow { label: label.into(), checked: false, highlighted: false, radius: None, press: None }
    }
    pub fn checked(mut self, on: bool) -> Self {
        self.checked = on;
        self
    }
    pub fn highlighted(mut self, on: bool) -> Self {
        self.highlighted = on;
        self
    }
    pub(crate) fn row_radius(mut self, r: f32) -> Self {
        self.radius = Some(r);
        self
    }
    pub fn on_press<S: 'static>(mut self, f: impl Fn(&mut S) + 'static) -> Self {
        self.press = Some(Box::new(move |e: El| e.on_press(f)));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let hover = color(tk.controls.hover.fill);
        let mut r = row()
            .w_full()
            .h(sp.popup_row_height as f32)
            .items_center()
            .justify_between()
            .px(sp.control_padding_x as f32)
            .radius(self.radius.unwrap_or_else(|| t.radius()))
            .fill(if self.highlighted { hover } else { color(Rgba::TRANSPARENT) })
            .hover(hover)
            .role("menuitem")
            .label(self.label.clone())
            .child(text(self.label).font(t.text_style(tk.font.body)).color(color(tk.surfaces.popups.text)).single_line());
        if self.checked {
            r = r.checked(true).child(super::glyph(t, "\u{2713}", tk.font.body, t.accent()));
        }
        match self.press {
            Some(attach) => attach(r),
            None => r,
        }
    }
}

/// A tooltip as drawn (the app owns when it shows): [tooltip] surface with the theme's corners,
/// small text, and an optional shortcut as Keycaps after it.
pub struct Tooltip {
    label: String,
    shortcut: Option<String>,
}

impl Tooltip {
    pub fn new(label: impl Into<String>) -> Tooltip {
        Tooltip { label: label.into(), shortcut: None }
    }
    /// A key combination shown after the text, as a KeyCombo ("Ctrl+S").
    pub fn shortcut(mut self, combo: impl Into<String>) -> Self {
        self.shortcut = Some(combo.into());
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let s = &tk.surfaces.tooltip;
        let sp = &tk.spacing;
        let bw = s.border.max_width() as f32;
        let e = row()
            .px(sp.control_padding_x as f32 + bw)
            .py(sp.control_padding_y as f32 + bw)
            .fill(color(s.background))
            .radius(t.radius())
            .child(text(self.label).font(t.text_style(tk.font.body_small)).color(color(s.text)));
        let e = match self.shortcut {
            Some(combo) => e.items_center().gap(sp.control_gap as f32).child(super::KeyCombo::new(combo).build(t)),
            None => e,
        };
        border(e, &s.border)
    }
}

/// A yes/no prompt card: message, then Cancel and a destructive Confirm, one of them selected
/// (arrow keys move it in the shell; here the app owns `selected`). A [`super::Dialog`] preset:
/// no title, the message in `title` size.
pub struct ConfirmDialog {
    message: String,
    /// `None`: Dialog's defaults, the catalog's `common.cancel` and `common.confirm`.
    labels: Option<(String, String)>,
    selected: usize,
    width: Option<f32>,
    on_cancel: Option<Attach>,
    on_confirm: Option<Attach>,
}

impl ConfirmDialog {
    pub fn new(message: impl Into<String>) -> ConfirmDialog {
        ConfirmDialog {
            message: message.into(),
            labels: None,
            selected: 1,
            width: None,
            on_cancel: None,
            on_confirm: None,
        }
    }
    pub fn labels(mut self, cancel: impl Into<String>, confirm: impl Into<String>) -> Self {
        self.labels = Some((cancel.into(), confirm.into()));
        self
    }
    /// 0 for Cancel, 1 for Confirm (the default).
    pub fn selected(mut self, index: usize) -> Self {
        self.selected = index;
        self
    }
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    pub fn on_cancel<S: 'static>(mut self, f: impl Fn(&mut S) + 'static) -> Self {
        self.on_cancel = Some(Box::new(move |e: El| e.on_press(f)));
        self
    }
    pub fn on_confirm<S: 'static>(mut self, f: impl Fn(&mut S) + 'static) -> Self {
        self.on_confirm = Some(Box::new(move |e: El| e.on_press(f)));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let message = text(self.message).font(t.text_style(tk.font.title)).color(t.fg()).wrap();
        let mut d = super::Dialog::new("").body(message).destructive(true).selected(self.selected);
        if let Some((cancel, confirm)) = self.labels {
            d = d.labels(cancel, confirm);
        }
        if let Some(w) = self.width {
            d = d.width(w);
        }
        d.attach(self.on_cancel, self.on_confirm).build(t)
    }
}
