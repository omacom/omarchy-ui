//! Surfaces: Card (the shell's panels and popups), Pane (an app's in-window panels), Heading,
//! section headers, separators, menu rows, setting rows, tooltips and dialogs.

use std::rc::Rc;

use gpui::prelude::*;
use gpui::{AnyElement, App, ClickEvent, Div, ElementId, SharedString, Window, div, px};
use omarchy_theme::{BorderSpec, Rgba};

use super::button::glyph;
use super::{Handler, border, id_for, label, label_elided, paragraph};
use crate::{Paint, color};

/// A caption-sized bold header in `textDim`: what heads a panel section.
pub fn section_header(t: &Paint, text: impl Into<SharedString>) -> Div {
    let size = t.tokens().font.caption;
    div()
        .flex()
        .flex_shrink_0()
        .pt(px((size as f32 * 0.15).ceil()))
        .child(label_elided(t, text, size, true, t.dim()))
}

/// A 1px rule in the foreground at 12%.
pub fn separator(t: &Paint) -> Div {
    div().w_full().h(px(1.0)).flex_shrink_0().bg(color(t.tokens().palette.foreground.with_alpha(0.12)))
}

/// A raised surface: [popups] background and border (Hyprland's active border by default),
/// Hyprland's rounding, panel padding, children panelGap apart.
#[derive(IntoElement)]
pub struct Card {
    title: SharedString,
    width: Option<f32>,
    children: Vec<AnyElement>,
}

impl Card {
    pub fn new() -> Card {
        Card { title: SharedString::default(), width: None, children: Vec::new() }
    }
    pub fn title(mut self, title: impl Into<SharedString>) -> Self {
        self.title = title.into();
        self
    }
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
}

impl Default for Card {
    fn default() -> Self {
        Card::new()
    }
}

impl ParentElement for Card {
    fn extend(&mut self, elements: impl IntoIterator<Item = AnyElement>) {
        self.children.extend(elements);
    }
}

impl RenderOnce for Card {
    fn render(self, _: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let pop = &tk.surfaces.popups;
        let bw = pop.border.widths;
        let pad = tk.spacing.panel_padding as f32;
        let gap = px(tk.spacing.panel_gap as f32);
        let mut c = div()
            .flex()
            .flex_col()
            .flex_shrink_0()
            .gap(gap)
            .pt(px(pad))
            .pr(px(pad))
            .pb(px(pad))
            .pl(px(pad))
            .bg(color(pop.background))
            .rounded(t.radius());
        let _ = bw;
        c = border(c, &pop.border);
        if let Some(w) = self.width {
            c = c.w(px(w));
        }
        if !self.title.is_empty() {
            c = c.child(section_header(&t, self.title));
        }
        c.child(div().flex().flex_col().w_full().gap(gap).children(self.children))
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
/// optional `textDim` subtitle under it, and an optional trailing slot of actions. Both lines
/// elide; trailing items sit xxs apart, controlGap after the lines, centered against them.
#[derive(IntoElement)]
pub struct Heading {
    text: SharedString,
    subtitle: SharedString,
    level: HeadingLevel,
    centered: bool,
    trailing: Vec<AnyElement>,
}

impl Heading {
    pub fn new(text: impl Into<SharedString>) -> Heading {
        Heading { text: text.into(), subtitle: SharedString::default(), level: HeadingLevel::Title, centered: false, trailing: Vec::new() }
    }
    pub fn subtitle(mut self, s: impl Into<SharedString>) -> Self {
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
    pub fn trailing(mut self, item: impl IntoElement) -> Self {
        self.trailing.push(item.into_any_element());
        self
    }
}

impl RenderOnce for Heading {
    fn render(self, _: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let f = &tk.font;
        let sp = &tk.spacing;
        let (size, sub) = match self.level {
            HeadingLevel::Title => (f.title, f.body_small),
            HeadingLevel::Heading => (f.heading, f.body),
            HeadingLevel::Display => (f.display, f.subtitle),
        };
        let line = |d: Div| if self.centered { d.flex().justify_center() } else { d };
        let mut lines = div()
            .flex()
            .flex_col()
            .flex_1()
            .min_w_0()
            .gap(px(sp.xxs as f32))
            .child(line(div()).child(label_elided(&t, self.text, size, true, t.fg())));
        if !self.subtitle.is_empty() {
            lines = lines.child(line(div()).child(label_elided(&t, self.subtitle, sub, false, t.dim())));
        }
        let mut e = div().flex().flex_row().w_full().items_center().child(lines);
        if !self.trailing.is_empty() {
            e = e.gap(px(sp.control_gap as f32)).child(div().flex().flex_row().items_center().gap(px(sp.xxs as f32)).children(self.trailing));
        }
        e
    }
}

/// A side of a box, for [`Pane::divider`].
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Side {
    Top,
    Right,
    Bottom,
    Left,
}

/// A flat surface inside an app window: an inspector, a side panel, a settings group. Not a
/// [`Card`]: by default it draws nothing but its padding, plus a hairline track rule on one side
/// against its neighbour (`divider`). `bordered` gives it the controls' normal glass instead.
/// The header is a [`Heading`] with `actions` as its trailing slot; children stack under it,
/// panelGap apart, panelPadding inside the edges.
#[derive(IntoElement)]
pub struct Pane {
    title: SharedString,
    subtitle: SharedString,
    actions: Vec<AnyElement>,
    bordered: bool,
    divider: Option<Side>,
    width: Option<f32>,
    children: Vec<AnyElement>,
}

impl Pane {
    pub fn new() -> Pane {
        Pane {
            title: SharedString::default(),
            subtitle: SharedString::default(),
            actions: Vec::new(),
            bordered: false,
            divider: None,
            width: None,
            children: Vec::new(),
        }
    }
    pub fn title(mut self, title: impl Into<SharedString>) -> Self {
        self.title = title.into();
        self
    }
    pub fn subtitle(mut self, s: impl Into<SharedString>) -> Self {
        self.subtitle = s.into();
        self
    }
    /// An IconButton or Button at the end of the header.
    pub fn action(mut self, item: impl IntoElement) -> Self {
        self.actions.push(item.into_any_element());
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
}

impl Default for Pane {
    fn default() -> Self {
        Pane::new()
    }
}

impl ParentElement for Pane {
    fn extend(&mut self, elements: impl IntoIterator<Item = AnyElement>) {
        self.children.extend(elements);
    }
}

impl RenderOnce for Pane {
    fn render(self, _: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let normal = &tk.controls.normal;
        let pad = sp.panel_padding as f32;
        let gap = px(sp.panel_gap as f32);
        let hair = sp.hairline as f32;
        let rule = |side: Side| if self.divider == Some(side) { hair } else { 0.0 };
        let mut c = div()
            .relative()
            .flex()
            .flex_col()
            .flex_shrink_0()
            .gap(gap)
            .pt(px(pad + rule(Side::Top)))
            .pr(px(pad + rule(Side::Right)))
            .pb(px(pad + rule(Side::Bottom)))
            .pl(px(pad + rule(Side::Left)));
        if self.bordered {
            c = border(c.bg(color(normal.fill)).rounded(t.radius()), &normal.border);
        }
        if let Some(w) = self.width {
            c = c.w(px(w));
        }
        if let Some(side) = self.divider {
            let line = div().absolute().bg(t.track());
            let line = match side {
                Side::Top => line.top_0().left_0().right_0().h(px(hair)),
                Side::Right => line.top_0().right_0().bottom_0().w(px(hair)),
                Side::Bottom => line.left_0().right_0().bottom_0().h(px(hair)),
                Side::Left => line.top_0().left_0().bottom_0().w(px(hair)),
            };
            c = c.child(line);
        }
        if !self.title.is_empty() {
            let mut h = Heading::new(self.title).subtitle(self.subtitle);
            for a in self.actions {
                h = h.trailing(a);
            }
            c = c.child(h);
        }
        c.child(div().flex().flex_col().w_full().gap(gap).children(self.children))
    }
}

/// One row of a menu or list: label, a check in the accent when `checked`, the hover fill under
/// the pointer or when `highlighted` (a keyboard-walked menu).
#[derive(IntoElement)]
pub struct MenuRow {
    id: ElementId,
    label: SharedString,
    checked: bool,
    highlighted: bool,
    small_radius: bool,
    on_click: Option<Handler<ClickEvent>>,
}

impl MenuRow {
    pub fn new(label: impl Into<SharedString>) -> MenuRow {
        let label = label.into();
        MenuRow { id: id_for("menu-row", &label), label, checked: false, highlighted: false, small_radius: false, on_click: None }
    }
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
        self
    }
    pub fn checked(mut self, on: bool) -> Self {
        self.checked = on;
        self
    }
    pub fn highlighted(mut self, on: bool) -> Self {
        self.highlighted = on;
        self
    }
    pub(crate) fn small_radius(mut self, on: bool) -> Self {
        self.small_radius = on;
        self
    }
    pub fn on_click(mut self, f: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static) -> Self {
        self.on_click = Some(Rc::new(f));
        self
    }
}

impl RenderOnce for MenuRow {
    fn render(self, _: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let hover = color(tk.controls.hover.fill);
        let mut r = div()
            .id(self.id)
            .flex()
            .flex_row()
            .w_full()
            .flex_shrink_0()
            .h(px(sp.popup_row_height as f32))
            .items_center()
            .justify_between()
            .px(px(sp.control_padding_x as f32))
            .rounded(if self.small_radius { t.radius_small() } else { t.radius() })
            .bg(if self.highlighted { hover } else { color(Rgba::TRANSPARENT) })
            .hover(move |s| s.bg(hover))
            .child(label_elided(&t, self.label, tk.font.body, false, color(tk.surfaces.popups.text)));
        if self.checked {
            r = r.child(glyph(&t, "\u{2713}", tk.font.body, t.accent()));
        }
        match self.on_click {
            Some(f) => r.on_click(move |e, w, cx| f(e, w, cx)),
            None => r,
        }
    }
}

/// A row in a settings list: a label (and optional dim description) at the start, a control at
/// the end. The row's hover fill with `cursor`; `disabled` dims the words.
#[derive(IntoElement)]
pub struct SettingRow {
    label: SharedString,
    description: SharedString,
    control: Option<AnyElement>,
    cursor: bool,
    disabled: bool,
    width: Option<f32>,
}

impl SettingRow {
    pub fn new(label: impl Into<SharedString>) -> SettingRow {
        SettingRow { label: label.into(), description: SharedString::default(), control: None, cursor: false, disabled: false, width: None }
    }
    pub fn description(mut self, d: impl Into<SharedString>) -> Self {
        self.description = d.into();
        self
    }
    /// The control at the end: a ToggleSwitch, Dropdown, NumberField, Button...
    pub fn control(mut self, e: impl IntoElement) -> Self {
        self.control = Some(e.into_any_element());
        self
    }
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
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
}

impl RenderOnce for SettingRow {
    fn render(self, _: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let (ink, dim) = if self.disabled { (t.faint(), t.faint()) } else { (t.fg(), t.dim()) };
        let pad_y = sp.md as f32;
        let mut words = div()
            .flex()
            .flex_col()
            .flex_1()
            .min_w_0()
            .py(px(pad_y))
            .gap(px(sp.xxs as f32))
            .child(label_elided(&t, self.label, tk.font.body, false, ink));
        if !self.description.is_empty() {
            words = words.child(paragraph(&t, self.description, tk.font.caption, dim));
        }
        let mut r = div()
            .flex()
            .flex_row()
            .items_center()
            .gap(px(sp.xxl as f32))
            .px(px(sp.row_padding_x as f32))
            .min_h(px(sp.control_height as f32 + pad_y * 2.0))
            .rounded(t.radius())
            .child(words);
        if self.cursor {
            r = r.bg(color(tk.controls.hover.fill));
        }
        if let Some(c) = self.control {
            r = r.child(div().flex().flex_row().items_center().py(px(sp.xxs as f32)).gap(px(sp.md as f32)).child(c));
        }
        match self.width {
            Some(w) => r.w(px(w)).flex_shrink_0(),
            None => r.w_full(),
        }
    }
}

/// A tooltip as drawn (the app owns when it shows): [tooltip] surface with the theme's corners and
/// small text. For GPUI's own hover tooltips, `.tooltip(Tooltip::text("…"))`.
#[derive(IntoElement, Clone)]
pub struct Tooltip {
    label: SharedString,
    shortcut: Option<SharedString>,
}

impl Tooltip {
    pub fn new(label: impl Into<SharedString>) -> Tooltip {
        Tooltip { label: label.into(), shortcut: None }
    }
    /// A key combination shown after the text, as a KeyCombo ("Ctrl+S").
    pub fn shortcut(mut self, combo: impl Into<SharedString>) -> Self {
        self.shortcut = Some(combo.into());
        self
    }
    /// A builder for GPUI's `.tooltip(..)`: shows this tooltip on hover.
    pub fn text(label: impl Into<SharedString>) -> impl Fn(&mut Window, &mut App) -> gpui::AnyView + 'static {
        let label = label.into();
        move |_, cx| cx.new(|_| TooltipView(Tooltip::new(label.clone()))).into()
    }
}

struct TooltipView(Tooltip);

impl Render for TooltipView {
    fn render(&mut self, _: &mut Window, _: &mut gpui::Context<Self>) -> impl IntoElement {
        self.0.clone()
    }
}

impl RenderOnce for Tooltip {
    fn render(self, _: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let s = &tk.surfaces.tooltip;
        let sp = &tk.spacing;
        let mut e = div()
            .flex()
            .flex_row()
            .flex_shrink_0()
            .items_center()
            .px(px(sp.control_padding_x as f32))
            .py(px(sp.control_padding_y as f32))
            .bg(color(s.background))
            .rounded(t.radius())
            .child(label(&t, self.label, tk.font.body_small, false, color(s.text)));
        if let Some(combo) = self.shortcut {
            e = e.gap(px(sp.control_gap as f32)).child(super::shortcut_hint(&t, combo));
        }
        border(e, &s.border.with_width(s.border.max_width().max(tk.controls.normal.border.max_width()).max(1.0)))
    }
}

/// A modal card: title, message or any body, then a secondary and a primary button (urgent when
/// `destructive`), one of them selected. With [`Dialog::scrim`] it fills its parent with the
/// scrim and centers the card on it. Return presses the selected button; Left/Right move the
/// selection; Escape cancels.
#[derive(IntoElement)]
pub struct Dialog {
    id: ElementId,
    title: SharedString,
    message: SharedString,
    body: Vec<AnyElement>,
    secondary: Option<SharedString>,
    primary: Option<SharedString>,
    destructive: bool,
    selected: usize,
    width: Option<f32>,
    scrim: bool,
    on_cancel: Option<Handler<ClickEvent>>,
    on_accept: Option<Handler<ClickEvent>>,
}

impl Dialog {
    pub fn new(title: impl Into<SharedString>) -> Dialog {
        let title = title.into();
        Dialog {
            id: id_for("dialog", &title),
            title,
            message: SharedString::default(),
            body: Vec::new(),
            secondary: None,
            primary: None,
            destructive: false,
            selected: 1,
            width: None,
            scrim: false,
            on_cancel: None,
            on_accept: None,
        }
    }
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
        self
    }
    /// Body text under the title, in `body` size.
    pub fn message(mut self, m: impl Into<SharedString>) -> Self {
        self.message = m.into();
        self
    }
    /// Anything under the title and message.
    pub fn body(mut self, e: impl IntoElement) -> Self {
        self.body.push(e.into_any_element());
        self
    }
    /// The secondary and primary labels (the catalog's `common.cancel` and `common.confirm` by
    /// default, in the display language).
    pub fn labels(mut self, secondary: impl Into<SharedString>, primary: impl Into<SharedString>) -> Self {
        self.secondary = Some(secondary.into());
        self.primary = Some(primary.into());
        self
    }
    /// Draw the primary button in urgent, for actions that destroy something.
    pub fn destructive(mut self, on: bool) -> Self {
        self.destructive = on;
        self
    }
    /// 0 for the secondary button, 1 for the primary (the default).
    pub fn selected(mut self, index: usize) -> Self {
        self.selected = index;
        self
    }
    /// The card's width; `space(370)` by default.
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    /// Fill the parent with the scrim and center the card on it.
    pub fn scrim(mut self, on: bool) -> Self {
        self.scrim = on;
        self
    }
    pub fn on_cancel(mut self, f: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static) -> Self {
        self.on_cancel = Some(Rc::new(f));
        self
    }
    pub fn on_accept(mut self, f: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static) -> Self {
        self.on_accept = Some(Rc::new(f));
        self
    }
}

impl RenderOnce for Dialog {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let theme = crate::Theme::get(cx);
        let tk = t.tokens();
        let secondary = self.secondary.unwrap_or_else(|| theme.string("common.cancel", "Cancel"));
        let primary = self.primary.unwrap_or_else(|| theme.string("common.confirm", "Confirm"));
        let p = &tk.palette;
        let line = tk.controls.normal.border.max_width().max(1.0) as f32;
        let accent = tk.surfaces.menu.selected_text;
        let selected_index = self.selected;
        let destructive = self.destructive;
        let choice = |text: SharedString, index: usize, handler: Option<Handler<ClickEvent>>| {
            let selected = selected_index == index;
            let urgent = destructive && index == 1;
            let (fill, edge, ink) = if urgent {
                (
                    if selected { p.urgent.with_alpha(0.22) } else { Rgba::TRANSPARENT },
                    if selected { p.urgent } else { p.urgent.with_alpha(0.56) },
                    if selected { p.urgent } else { p.foreground },
                )
            } else {
                (
                    if selected { tk.surfaces.menu.selected_background } else { Rgba::TRANSPARENT },
                    if selected { accent } else { p.foreground.with_alpha(0.38) },
                    if selected { accent } else { p.foreground },
                )
            };
            let look = super::Look::new(fill, BorderSpec { color: edge, widths: [line as f64; 4], gradient: None });
            let w = t
                .space(88.0)
                .max(super::navigation::advance(window, &t, &text, tk.font.caption, false).ceil() + (tk.spacing.control_padding_x * 2) as f32);
            let b = div()
                .id(text.clone())
                .relative()
                .flex()
                .flex_shrink_0()
                .items_center()
                .justify_center()
                .w(px(w))
                .h(px(t.space(34.0)))
                .rounded(t.radius())
                .child(super::layer(&look, t.radius()))
                .child(label(&t, text, tk.font.caption, false, color(ink)));
            match handler {
                Some(f) => b.on_click(move |e, w, cx| f(e, w, cx)),
                None => b,
            }
        };

        let mut content = div().flex().flex_col().w_full().gap(px(t.space(10.0)));
        if !self.title.is_empty() {
            content = content.child(paragraph(&t, self.title.clone(), tk.font.title, t.fg()).font_weight(gpui::FontWeight::BOLD));
        }
        if !self.message.is_empty() {
            content = content.child(paragraph(&t, self.message, tk.font.body, t.fg()));
        }
        content = content.children(self.body);

        let pad = t.space(18.0);
        let card_border = BorderSpec { color: accent, widths: [line as f64; 4], gradient: None };
        let (cancel, accept) = (self.on_cancel.clone(), self.on_accept.clone());
        let mut card = div()
            .id(self.id)
            .flex()
            .flex_col()
            .flex_shrink_0()
            .gap(px(t.space(20.0)))
            .p(px(pad))
            .bg(color(p.background))
            .rounded(t.radius())
            .w(px(self.width.unwrap_or_else(|| t.space(370.0))));
        card = border(card, &card_border);
        card = card
            .child(content)
            .child(
                div()
                    .flex()
                    .flex_row()
                    .w_full()
                    .justify_end()
                    .gap(px(t.space(10.0)))
                    .child(choice(secondary, 0, self.on_cancel))
                    .child(choice(primary, 1, self.on_accept)),
            )
            .on_key_down(move |ev: &gpui::KeyDownEvent, window, cx| {
                let f = match ev.keystroke.key.as_str() {
                    "escape" => &cancel,
                    "enter" | "kp_enter" => {
                        if selected_index == 0 {
                            &cancel
                        } else {
                            &accept
                        }
                    }
                    _ => return,
                };
                if let Some(f) = f {
                    f(&ClickEvent::default(), window, cx);
                }
                cx.stop_propagation();
            });
        if !self.scrim {
            return card.into_any_element();
        }
        div().absolute().inset_0().flex().items_center().justify_center().bg(t.scrim()).child(card).into_any_element()
    }
}

/// A yes/no prompt card: message, then Cancel and a destructive Confirm, one of them selected.
/// A [`Dialog`] preset: no title, the message in `title` size.
#[derive(IntoElement)]
pub struct ConfirmDialog {
    message: SharedString,
    labels: Option<(SharedString, SharedString)>,
    selected: usize,
    width: Option<f32>,
    on_cancel: Option<Handler<ClickEvent>>,
    on_confirm: Option<Handler<ClickEvent>>,
}

impl ConfirmDialog {
    pub fn new(message: impl Into<SharedString>) -> ConfirmDialog {
        ConfirmDialog { message: message.into(), labels: None, selected: 1, width: None, on_cancel: None, on_confirm: None }
    }
    pub fn labels(mut self, cancel: impl Into<SharedString>, confirm: impl Into<SharedString>) -> Self {
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
    pub fn on_cancel(mut self, f: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static) -> Self {
        self.on_cancel = Some(Rc::new(f));
        self
    }
    pub fn on_confirm(mut self, f: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static) -> Self {
        self.on_confirm = Some(Rc::new(f));
        self
    }
}

impl RenderOnce for ConfirmDialog {
    fn render(self, _: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let size = t.tokens().font.title;
        let mut d = Dialog::new("").id(id_for("confirm", &self.message)).body(paragraph(&t, self.message, size, t.fg())).destructive(true).selected(self.selected);
        if let Some((cancel, confirm)) = self.labels {
            d = d.labels(cancel, confirm);
        }
        if let Some(w) = self.width {
            d = d.width(w);
        }
        d.on_cancel = self.on_cancel;
        d.on_accept = self.on_confirm;
        d
    }
}
