//! Sidebar: an app's navigation, sections of SectionHeaders over glyph + label rows, one
//! selected; collapsed, a rail of glyphs.

use std::rc::Rc;

use gpui::prelude::*;
use gpui::{App, KeyDownEvent, MouseButton, SharedString, Window, div, px};

use super::{Walk, row_fill, row_ink, walk_key};
use crate::ui::{Handler, focus_handle, keyed_state, label, label_elided, section_header, tab_keys};
use crate::{Paint, color};

/// One row: an id the app knows it by, a label and a glyph.
#[derive(Clone)]
pub struct SidebarItem {
    id: SharedString,
    label: SharedString,
    glyph: SharedString,
}

impl SidebarItem {
    pub fn new(id: impl Into<SharedString>, label: impl Into<SharedString>, glyph: impl Into<SharedString>) -> SidebarItem {
        SidebarItem { id: id.into(), label: label.into(), glyph: glyph.into() }
    }
}

/// A titled run of rows.
#[derive(Clone)]
pub struct SidebarSection {
    title: SharedString,
    items: Vec<SidebarItem>,
}

impl SidebarSection {
    pub fn new(title: impl Into<SharedString>, items: impl IntoIterator<Item = SidebarItem>) -> SidebarSection {
        SidebarSection { title: title.into(), items: items.into_iter().collect() }
    }
}

/// An app's navigation column. Up/Down walk every row across sections, Return/Space (or a
/// click) choose through `on_select`; `collapsed` folds it to a rail of glyphs.
#[derive(IntoElement)]
pub struct Sidebar {
    id: SharedString,
    sections: Vec<SidebarSection>,
    selected: Option<SharedString>,
    cursor: Option<SharedString>,
    collapsed: bool,
    width: Option<f32>,
    on_select: Option<Handler<SharedString>>,
}

impl Sidebar {
    pub fn new(sections: impl IntoIterator<Item = SidebarSection>) -> Sidebar {
        let sections: Vec<SidebarSection> = sections.into_iter().collect();
        let id = format!("omarchy-sidebar-{}", sections.iter().map(|s| s.title.as_ref()).collect::<Vec<_>>().join("|")).into();
        Sidebar { id, sections, selected: None, cursor: None, collapsed: false, width: None, on_select: None }
    }
    /// The id its cursor and focus are kept under (and its rows' debug selectors start with).
    pub fn id(mut self, id: impl Into<SharedString>) -> Self {
        self.id = id.into();
        self
    }
    pub fn selected(mut self, id: Option<impl Into<SharedString>>) -> Self {
        self.selected = id.map(Into::into);
        self
    }
    /// Force the hover-cursor look on one row (and start the keyboard cursor there).
    pub fn cursor(mut self, id: Option<impl Into<SharedString>>) -> Self {
        self.cursor = id.map(Into::into);
        self
    }
    /// Fold to a rail of glyphs, without headers or labels.
    pub fn collapsed(mut self, on: bool) -> Self {
        self.collapsed = on;
        self
    }
    /// The width when expanded (the parent's by default).
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    pub fn on_select(mut self, f: impl Fn(&SharedString, &mut Window, &mut App) + 'static) -> Self {
        self.on_select = Some(Rc::new(f));
        self
    }
    /// The collapsed width: the padding, the row inset each side, one glyph.
    pub fn rail_width(cx: &App) -> f32 {
        let t = Paint::of(cx);
        let tk = t.tokens();
        (tk.spacing.sm * 2 + tk.spacing.control_padding_x * 2 + tk.font.icon) as f32
    }
}

impl RenderOnce for Sidebar {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let h = px(sp.popup_row_height as f32);
        let inset = px(sp.control_padding_x as f32);
        let offset = px((tk.font.icon + sp.control_gap) as f32);
        let id: gpui::ElementId = self.id.clone().into();
        let focus = focus_handle(&id, window, cx);
        let forced = self.cursor.clone();
        let walk = keyed_state(&id, "sidebar", window, cx, || Walk::new(forced.clone()));
        walk.update(cx, |w, _| w.sync(&forced));
        let (cursor, hovering) = {
            let w = walk.read(cx);
            (w.cursor.clone(), w.hovering)
        };
        let shown = forced.is_some() || hovering || focus.is_focused(window);
        let rail = Sidebar::rail_width(cx);
        let ids: Rc<Vec<SharedString>> = Rc::new(self.sections.iter().flat_map(|s| s.items.iter().map(|i| i.id.clone())).collect());

        let mut sections = div().flex().flex_col().w_full().gap(px(sp.md as f32));
        for s in self.sections {
            let mut section = div().flex().flex_col().w_full();
            if !self.collapsed && !s.title.is_empty() {
                section = section.child(section_header(&t, s.title).pl(inset).pb(px(sp.xxs as f32)).w_full());
            }
            for item in s.items {
                let is_selected = self.selected.as_ref() == Some(&item.id);
                let hot = shown && cursor.as_ref() == Some(&item.id);
                let ink = row_ink(&t, is_selected);
                let gi = if is_selected { color(tk.controls.selected.text) } else { t.dim() };
                let mut line = div()
                    .flex()
                    .flex_row()
                    .w_full()
                    .h_full()
                    .items_center()
                    .px(inset)
                    .child(div().flex().flex_row().flex_shrink_0().w(offset).h_full().items_center().child(label(&t, item.glyph.clone(), tk.font.icon, false, gi)));
                if !self.collapsed {
                    line = line.child(label_elided(&t, item.label.clone(), tk.font.body, is_selected, ink).flex_1());
                }
                let (w1, w2, fo, f) = (walk.clone(), walk.clone(), focus.clone(), self.on_select.clone());
                let (i1, i2, i3) = (item.id.clone(), item.id.clone(), item.id.clone());
                let selector = format!("{}-{}", self.id, item.id);
                section = section.child(
                    div()
                        .id(SharedString::from(format!("row-{}", item.id)))
                        .debug_selector(|| selector)
                        .w_full()
                        .h(h)
                        .flex_shrink_0()
                        .rounded(t.radius())
                        .bg(row_fill(&t, is_selected, hot))
                        .cursor_pointer()
                        .child(line)
                        .on_hover(move |on, _, cx| {
                            let i1 = i1.clone();
                            w1.update(cx, |w, cx| {
                                w.hovering = *on;
                                if *on {
                                    w.cursor = Some(i1);
                                }
                                cx.notify();
                            })
                        })
                        .on_mouse_down(MouseButton::Left, move |_, window, cx| {
                            fo.focus(window, cx);
                            let i2 = i2.clone();
                            w2.update(cx, |w, cx| {
                                w.cursor = Some(i2);
                                cx.notify();
                            });
                        })
                        .on_click(move |_, window, cx| {
                            if let Some(f) = &f {
                                f(&i3, window, cx);
                            }
                        }),
                );
            }
            sections = sections.child(section);
        }

        let (w3, f) = (walk.clone(), self.on_select.clone());
        let bar = div()
            .id(id)
            .flex()
            .flex_col()
            .flex_shrink_0()
            .h_full()
            .p(px(sp.sm as f32))
            .track_focus(&focus)
            .child(sections)
            .on_key_down(move |ev: &KeyDownEvent, window, cx| {
                if tab_keys(ev, window, cx) {
                    cx.stop_propagation();
                    return;
                }
                let Some(key) = walk_key(ev) else { return };
                let n = ids.len();
                if n == 0 {
                    return;
                }
                let at = w3.read(cx).cursor.clone().and_then(|c| ids.iter().position(|i| *i == c));
                let d: isize = match key {
                    "up" => -1,
                    "down" => 1,
                    "choose" => {
                        if let (Some(i), Some(f)) = (at, &f) {
                            f(&ids[i], window, cx);
                        }
                        cx.stop_propagation();
                        return;
                    }
                    _ => return,
                };
                let from = match at {
                    Some(a) => a as isize,
                    None if d > 0 => -1,
                    None => n as isize,
                };
                let next = ids[(from + d).clamp(0, n as isize - 1) as usize].clone();
                w3.update(cx, |w, cx| {
                    w.cursor = Some(next);
                    cx.notify();
                });
                cx.stop_propagation();
            });
        if self.collapsed {
            bar.w(px(rail))
        } else {
            match self.width {
                Some(w) => bar.w(px(w)),
                None => bar.w_full(),
            }
        }
    }
}
