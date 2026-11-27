//! Menu, ContextMenu and MenuItem: the kit's own `Menu.qml`, `ContextMenu.qml`, `MenuItem.qml`,
//! `MenuSeparator.qml` and `MenuSection.qml`, drawn on the [menu] surface.

use omarchy_theme::BorderSpec;
use rune::{Align, El, Key, KeyEvent, Side, Tx, col, el, row, text};

use super::navigation::optical;
use super::{Attach, Look, chrome, section_header};
use crate::{Theme, border, color};

type Open = Box<dyn FnOnce(El, bool) -> El>;
type Close = Box<dyn FnOnce(El, bool, bool) -> El>;

/// The vim keys every menu walks with, as Qt's `Menu.handleKey` does.
const VIM: crate::KeyMap = &[("j", Some(Key::Down)), ("k", Some(Key::Up)), ("l", Some(Key::Right))];

/// One row of a Menu: an optional leading glyph, the label, a dim shortcut hint at the right, and
/// a trailing mark: an accent check (or a radio dot) when checked, or a chevron when the row
/// opens a submenu. Under the pointer, keyboard focus or `cursor` it takes the menu surface's
/// selected background and text. Disabled rows are faint and inert; destructive rows are urgent.
pub struct MenuItem {
    label: String,
    icon: Option<String>,
    shortcut: Option<String>,
    checkable: bool,
    checked: bool,
    exclusive: bool,
    destructive: bool,
    submenu: bool,
    disabled: bool,
    cursor: bool,
    key: Option<String>,
    press: Option<Attach>,
    open: Option<Open>,
}

impl MenuItem {
    pub fn new(label: impl Into<String>) -> MenuItem {
        MenuItem {
            label: label.into(),
            icon: None,
            shortcut: None,
            checkable: false,
            checked: false,
            exclusive: false,
            destructive: false,
            submenu: false,
            disabled: false,
            cursor: false,
            key: None,
            press: None,
            open: None,
        }
    }
    /// A leading glyph (a Nerd Font codepoint).
    pub fn icon(mut self, glyph: impl Into<String>) -> Self {
        self.icon = Some(glyph.into());
        self
    }
    /// The shortcut hint, as plain text ("Ctrl+S").
    pub fn shortcut(mut self, hint: impl Into<String>) -> Self {
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
    /// The row opens a submenu: a chevron, and Right opens it (see `on_open`).
    pub fn submenu(mut self, on: bool) -> Self {
        self.submenu = on;
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
    /// Force the cursor look, as a keyboard-walked menu's cursor does.
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    pub fn key(mut self, key: impl Into<String>) -> Self {
        self.key = Some(key.into());
        self
    }
    /// A press, or Enter or Space while focused.
    pub fn on_press<S: 'static>(mut self, f: impl Fn(&mut S) + 'static) -> Self {
        self.press = Some(Box::new(move |e: El| e.on_press(f)));
        self
    }
    /// Opening the submenu: Right or l while focused, and a press when there is no `on_press`.
    pub fn on_open<S: 'static>(mut self, f: impl Fn(&mut S) + Clone + 'static) -> Self {
        self.submenu = true;
        self.open = Some(Box::new(move |e: El, pressed: bool| {
            let g = f.clone();
            let e = e.on_key(move |s: &mut S, k: &KeyEvent, _: &mut Tx| match &k.key {
                Key::Right => {
                    g(s);
                    true
                }
                Key::Text(c) if c == "l" => {
                    g(s);
                    true
                }
                _ => false,
            });
            if pressed { e } else { e.on_press(move |s: &mut S| f(s)) }
        }));
        self
    }

    fn build(self, t: &Theme, reserve_icon: bool, reserve_mark: bool) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let menu = &tk.surfaces.menu;
        let h = sp.popup_row_height as f32;
        let gap = sp.control_gap as f32;
        let icon_box = tk.font.icon as f32;
        let mark_box = icon_box;
        let enabled = !self.disabled;

        let rest_ink = if !enabled {
            t.faint()
        } else if self.destructive {
            t.urgent()
        } else {
            color(menu.base.text)
        };
        let hot_ink = (enabled && !self.destructive).then(|| color(menu.selected_text));
        let forced = self.cursor && enabled;
        let (ink, hover_ink) = if forced { (hot_ink.unwrap_or(rest_ink), None) } else { (rest_ink, hot_ink) };

        let mut kids = Vec::new();
        if reserve_icon {
            kids.push(match &self.icon {
                Some(g) => optical(g, t.text_style(tk.font.icon), ink, hover_ink, icon_box, h),
                None => el().size(icon_box, h),
            });
        }
        let mut label = text(self.label.clone()).font(t.text_style(tk.font.body)).color(ink).single_line();
        if let Some(c) = hover_ink {
            label = label.group_color(c);
        }
        if reserve_icon {
            label = label.ml(gap);
        }
        kids.push(label);
        kids.push(el().grow(1.0));
        if let Some(hint) = &self.shortcut {
            let ink = if enabled { t.dim() } else { t.faint() };
            let body_style = t.text_style(tk.font.body);
            let hint_style = t.text_style(tk.font.body_small);
            let baseline = |style: rune::TextStyle| {
                rune::text::ink("H", style).map(|i| i.bounds[1] + i.bounds[3]).unwrap_or(style.line_height * 0.78)
            };
            let dy = ((baseline(body_style) - baseline(hint_style)) - (body_style.line_height - hint_style.line_height) / 2.0).round();
            kids.push(text(hint.clone()).font(hint_style).color(ink).single_line().ml(sp.huge as f32).offset(0.0, dy));
        }
        if reserve_mark {
            let marked = self.checkable && self.checked;
            let mark = if marked {
                let ink = if enabled { t.accent() } else { t.faint() };
                if self.exclusive {
                    let dot = t.space(6.0);
                    let dx = ((mark_box - dot) / 2.0).round();
                    let dy = ((h - dot) / 2.0).round();
                    el().size(mark_box, h).child(el().absolute(dx, dy).size(dot, dot).radius(dot / 2.0).fill(ink))
                } else {
                    optical("\u{2713}", t.text_style(tk.font.body), ink, None, mark_box, h)
                }
            } else if self.submenu {
                let ink = if enabled { t.dim() } else { t.faint() };
                optical("\u{f054}", t.text_style(tk.font.icon_small), ink, None, mark_box, h)
            } else {
                el().size(mark_box, h)
            };
            kids.push(mark.ml(gap));
        }

        let role = match (self.checkable, self.exclusive) {
            (true, true) => "menuitemradio",
            (true, false) => "menuitemcheckbox",
            _ => "menuitem",
        };
        let mut base = row()
            .h(h)
            .items_center()
            .px(sp.control_padding_x as f32)
            .role(role)
            .label(self.label.clone())
            .focusable(enabled)
            .disabled(!enabled);
        if self.checkable {
            base = base.checked(self.checked);
        }
        if self.submenu {
            base = base.expanded(false);
        }
        if let Some(k) = self.key {
            base = base.key(k);
        }
        if !enabled {
            return base.children(kids);
        }
        let rest = Look::clear();
        let hot = Look::new(menu.selected_background, BorderSpec::none());
        let mut e = chrome(t, base, kids, &rest, &hot, forced, t.radius_small());
        let pressed = self.press.is_some();
        if let Some(attach) = self.press {
            e = attach(e);
        }
        if let Some(attach) = self.open {
            e = attach(e, pressed);
        }
        e
    }
}

enum Entry {
    Item(MenuItem),
    Separator,
    Section(String),
}

/// A menu: MenuItems, separators and section labels on the menu surface ([menu] background and
/// border, Hyprland's rounding), sm inside its border.
///
/// Without `anchor` or `at` it draws in place (held open, for galleries and still renders).
/// With `anchor(key)` it goes below that keyed element: push it into the root's overlay layer.
/// Up and Down (or k and j) walk the rows, Enter runs one, Right (or l) opens a submenu row's
/// `on_open`; Escape and a press outside call `on_close`, and so do Left and h in
/// a menu marked `submenu`.
pub struct Menu {
    entries: Vec<Entry>,
    min_width: Option<f32>,
    anchor: Option<String>,
    at: Option<(f32, f32)>,
    submenu: bool,
    key: Option<String>,
    close: Option<Close>,
}

impl Menu {
    pub fn new() -> Menu {
        Menu { entries: Vec::new(), min_width: None, anchor: None, at: None, submenu: false, key: None, close: None }
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
    pub fn section(mut self, label: impl Into<String>) -> Self {
        self.entries.push(Entry::Section(label.into()));
        self
    }
    /// The narrowest the menu gets, inside its border and padding (default `space(160)`).
    pub fn min_width(mut self, w: f32) -> Self {
        self.min_width = Some(w);
        self
    }
    /// Open below the element keyed `key` (the trigger).
    pub fn anchor(mut self, key: impl Into<String>) -> Self {
        self.anchor = Some(key.into());
        self
    }
    /// Open with the top-left corner at a point in the window, as a context menu does.
    pub fn at(mut self, x: f32, y: f32) -> Self {
        self.at = Some((x, y));
        self
    }
    /// This menu is another's submenu: Left and h close it too.
    pub fn submenu(mut self, on: bool) -> Self {
        self.submenu = on;
        self
    }
    pub fn key(mut self, key: impl Into<String>) -> Self {
        self.key = Some(key.into());
        self
    }
    /// Closing: Escape, a press outside (when anchored or at a point), Left or h in a submenu.
    /// (j, k and l map to Down, Up and Right with or without it.)
    pub fn on_close<S: 'static>(mut self, f: impl Fn(&mut S) + Clone + 'static) -> Self {
        self.close = Some(Box::new(move |e: El, popup: bool, submenu: bool| {
            let g = f.clone();
            let e = e.on_key(move |s: &mut S, k: &KeyEvent, tx: &mut Tx| {
                let send = |tx: &mut Tx, key: Key| tx.send_key(KeyEvent { key, mods: k.mods });
                match &k.key {
                    Key::Text(c) if c == "j" => send(tx, Key::Down),
                    Key::Text(c) if c == "k" => send(tx, Key::Up),
                    Key::Text(c) if c == "l" => send(tx, Key::Right),
                    Key::Text(c) if c == "h" && submenu => g(s),
                    Key::Left | Key::Escape if submenu => g(s),
                    Key::Escape if !popup => g(s),
                    _ => return false,
                }
                true
            });
            if popup { e.modal(true, move |s: &mut S| f(s)) } else { e }
        }));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let menu = &tk.surfaces.menu;
        let bw = menu.base.border.widths.map(|w| w as f32);
        let pad = sp.sm as f32;
        let min_w = self.min_width.unwrap_or_else(|| t.space(160.0));
        let reserve_icon = self.entries.iter().any(|e| matches!(e, Entry::Item(i) if i.icon.is_some()));
        let reserve_mark = self.entries.iter().any(|e| matches!(e, Entry::Item(i) if i.checkable || i.submenu));

        let mut frame = col()
            .items_stretch()
            .min_w(min_w + pad * 2.0 + bw[1] + bw[3])
            .pt(pad + bw[0])
            .pr(pad + bw[1])
            .pb(pad + bw[2])
            .pl(pad + bw[3])
            .fill(color(menu.base.background))
            .radius(t.radius())
            .role("menu")
            .focus_group();
        frame = border(frame, &menu.base.border);
        if let Some(k) = self.key {
            frame = frame.key(k);
        }
        for entry in self.entries {
            frame = frame.child(match entry {
                Entry::Item(item) => item.build(t, reserve_icon, reserve_mark),
                Entry::Separator => col().py(pad).items_stretch().child(el().h(1.0).fill(t.track())).a11y_hidden(),
                Entry::Section(label) => col().pt(pad).pb(sp.xxs as f32).px(sp.control_padding_x as f32).child(section_header(t, label)),
            });
        }
        let popup = self.anchor.is_some() || self.at.is_some();
        if let Some(key) = self.anchor {
            frame = frame.anchored(key, Side::Bottom, Align::Start, sp.xs as f32);
        } else if let Some((x, y)) = self.at {
            frame = frame.absolute(x, y);
        }
        match self.close {
            Some(attach) => attach(frame, popup, self.submenu),
            None => t.key_map(frame, VIM),
        }
    }
}

impl Default for Menu {
    fn default() -> Self {
        Menu::new()
    }
}

/// A Menu that opens at a point (the pointer) instead of under a trigger: `at(x, y)` in window
/// points, from an `on_secondary_press` handler's `PointerPress::window_point()`. Without `at` it
/// draws in place, held open.
pub struct ContextMenu {
    menu: Menu,
}

impl ContextMenu {
    pub fn new() -> ContextMenu {
        ContextMenu { menu: Menu::new() }
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
    pub fn section(mut self, label: impl Into<String>) -> Self {
        self.menu = self.menu.section(label);
        self
    }
    pub fn min_width(mut self, w: f32) -> Self {
        self.menu = self.menu.min_width(w);
        self
    }
    pub fn at(mut self, x: f32, y: f32) -> Self {
        self.menu = self.menu.at(x, y);
        self
    }
    pub fn key(mut self, key: impl Into<String>) -> Self {
        self.menu = self.menu.key(key);
        self
    }
    pub fn on_close<S: 'static>(mut self, f: impl Fn(&mut S) + Clone + 'static) -> Self {
        self.menu = self.menu.on_close(f);
        self
    }
    pub fn build(self, t: &Theme) -> El {
        self.menu.build(t)
    }
}

impl Default for ContextMenu {
    fn default() -> Self {
        ContextMenu::new()
    }
}
