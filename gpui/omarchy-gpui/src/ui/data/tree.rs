//! Tree: FileTree-style rows, each level indented by `huge`, a chevron on branches, an optional
//! glyph, then the label.

use std::rc::Rc;

use gpui::prelude::*;
use gpui::{App, KeyDownEvent, MouseButton, SharedString, Window, div, px};

use super::{Walk, row_fill, row_ink, walk_key};
use crate::ui::{Handler, focus_handle, keyed_state, label_elided, tab_keys};
use crate::{Paint, color};

/// One node: an id the app knows it by, a label, an optional glyph, and children.
#[derive(Clone)]
pub struct TreeNode {
    id: SharedString,
    label: SharedString,
    glyph: Option<SharedString>,
    children: Vec<TreeNode>,
}

impl TreeNode {
    pub fn new(id: impl Into<SharedString>, label: impl Into<SharedString>) -> TreeNode {
        TreeNode { id: id.into(), label: label.into(), glyph: None, children: Vec::new() }
    }
    pub fn glyph(mut self, g: impl Into<SharedString>) -> Self {
        self.glyph = Some(g.into());
        self
    }
    pub fn children(mut self, nodes: impl IntoIterator<Item = TreeNode>) -> Self {
        self.children = nodes.into_iter().collect();
        self
    }
}

/// A tree. The app owns which ids are `expanded` and the `selected` id; the tree asks with
/// `on_toggle` (the chevron, a double click, Right on a folded branch, Left on an open one) and
/// `on_select` (a click, Return/Space on the cursor row). Up/Down walk the visible rows.
#[derive(IntoElement)]
pub struct Tree {
    id: SharedString,
    nodes: Vec<TreeNode>,
    expanded: Vec<SharedString>,
    selected: Option<SharedString>,
    cursor: Option<SharedString>,
    width: Option<f32>,
    on_toggle: Option<Handler<SharedString>>,
    on_select: Option<Handler<SharedString>>,
}

#[derive(Clone)]
struct Visible {
    node: TreeNode,
    depth: usize,
    branch: bool,
    open: bool,
}

impl Tree {
    pub fn new(nodes: impl IntoIterator<Item = TreeNode>) -> Tree {
        let nodes: Vec<TreeNode> = nodes.into_iter().collect();
        let id = format!("omarchy-tree-{}", nodes.iter().map(|n| n.id.as_ref()).collect::<Vec<_>>().join("|")).into();
        Tree { id, nodes, expanded: Vec::new(), selected: None, cursor: None, width: None, on_toggle: None, on_select: None }
    }
    /// The id its cursor and focus are kept under (and its parts' debug selectors start with).
    pub fn id(mut self, id: impl Into<SharedString>) -> Self {
        self.id = id.into();
        self
    }
    pub fn expanded<S: Into<SharedString>>(mut self, ids: impl IntoIterator<Item = S>) -> Self {
        self.expanded = ids.into_iter().map(Into::into).collect();
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
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    /// Called with a branch's id to fold or unfold it.
    pub fn on_toggle(mut self, f: impl Fn(&SharedString, &mut Window, &mut App) + 'static) -> Self {
        self.on_toggle = Some(Rc::new(f));
        self
    }
    /// Called with a row's id when it is clicked or chosen with Return/Space.
    pub fn on_select(mut self, f: impl Fn(&SharedString, &mut Window, &mut App) + 'static) -> Self {
        self.on_select = Some(Rc::new(f));
        self
    }

    fn walk(&self, nodes: &[TreeNode], depth: usize, out: &mut Vec<Visible>) {
        for n in nodes {
            let branch = !n.children.is_empty();
            let open = branch && self.expanded.contains(&n.id);
            out.push(Visible { node: n.clone(), depth, branch, open });
            if open {
                self.walk(&n.children, depth + 1, out);
            }
        }
    }
}

impl RenderOnce for Tree {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let row_h = sp.popup_row_height as f32;
        let h = px(row_h);
        let slot = sp.xxxl as f32;
        let icon = tk.font.icon;
        let id: gpui::ElementId = self.id.clone().into();
        let focus = focus_handle(&id, window, cx);
        let forced = self.cursor.clone();
        let walk = keyed_state(&id, "tree", window, cx, || Walk::new(forced.clone()));
        walk.update(cx, |w, _| w.sync(&forced));
        let (cursor, hovering) = {
            let w = walk.read(cx);
            (w.cursor.clone(), w.hovering)
        };
        let shown = forced.is_some() || hovering || focus.is_focused(window);
        let mut rows = Vec::new();
        self.walk(&self.nodes, 0, &mut rows);

        let mut list = div().id(id).flex().flex_col().flex_shrink_0().track_focus(&focus);
        for v in rows.iter() {
            let n = &v.node;
            let is_selected = self.selected.as_ref() == Some(&n.id);
            let hot = shown && cursor.as_ref() == Some(&n.id);
            let ink = row_ink(&t, is_selected);
            let mut chevron = div()
                .id("chevron")
                .flex()
                .flex_row()
                .flex_shrink_0()
                .w(px(slot))
                .h_full()
                .items_center()
                .justify_center();
            if v.branch {
                let chev = if v.open { "\u{f0140}" } else { "\u{f0142}" };
                chevron = chevron.child(crate::ui::navigation::optical(window, cx, &t, chev, tk.font.icon_small, t.dim(), None, slot, row_h));
                let selector = format!("{}-{}-toggle", self.id, n.id);
                chevron = chevron.debug_selector(|| selector).cursor_pointer();
                if let Some(f) = self.on_toggle.clone() {
                    let nid = n.id.clone();
                    chevron = chevron.on_click(move |_, window, cx| {
                        cx.stop_propagation();
                        f(&nid, window, cx)
                    });
                }
            }
            let mut line = div()
                .flex()
                .flex_row()
                .w_full()
                .h_full()
                .items_center()
                .pl(px(sp.md as f32 + v.depth as f32 * sp.huge as f32))
                .pr(px(sp.md as f32))
                .child(chevron)
                .child(div().w(px(sp.sm as f32)).flex_shrink_0());
            if let Some(g) = &n.glyph {
                let gi = if is_selected { color(tk.controls.selected.text) } else { t.dim() };
                line = line
                    .child(crate::ui::navigation::optical(window, cx, &t, g, icon, gi, None, icon as f32, row_h))
                    .child(div().w(px(sp.md as f32)).flex_shrink_0());
            }
            line = line.child(label_elided(&t, n.label.clone(), tk.font.body, is_selected, ink).flex_1());

            let (w1, w2, fo) = (walk.clone(), walk.clone(), focus.clone());
            let (sel, tog) = (self.on_select.clone(), self.on_toggle.clone());
            let (nid, nid2, branch) = (n.id.clone(), n.id.clone(), v.branch);
            let selector = format!("{}-{}", self.id, n.id);
            let row = div()
                .id(SharedString::from(format!("row-{}", n.id)))
                .debug_selector(|| selector)
                .w_full()
                .h(h)
                .flex_shrink_0()
                .rounded(t.radius())
                .bg(row_fill(&t, is_selected, hot))
                .cursor_pointer()
                .child(line)
                .on_hover(move |on, _, cx| {
                    let nid = nid.clone();
                    w1.update(cx, |w, cx| {
                        w.hovering = *on;
                        if *on {
                            w.cursor = Some(nid);
                        }
                        cx.notify();
                    })
                })
                .on_mouse_down(MouseButton::Left, move |_, window, cx| {
                    fo.focus(window, cx);
                    let nid = nid2.clone();
                    w2.update(cx, |w, cx| {
                        w.cursor = Some(nid);
                        cx.notify();
                    });
                });
            let nid3 = n.id.clone();
            let row = row.on_click(move |ev, window, cx| {
                if ev.click_count() == 2 && branch {
                    if let Some(f) = &tog {
                        f(&nid3, window, cx);
                    }
                } else if let Some(f) = &sel {
                    f(&nid3, window, cx);
                }
            });
            list = list.child(row);
        }

        let order: Rc<Vec<Visible>> = Rc::new(rows);
        let (w3, sel, tog) = (walk.clone(), self.on_select.clone(), self.on_toggle.clone());
        let list = list.on_key_down(move |ev: &KeyDownEvent, window, cx| {
            if tab_keys(ev, window, cx) {
                cx.stop_propagation();
                return;
            }
            let Some(key) = walk_key(ev) else { return };
            let n = order.len();
            if n == 0 {
                return;
            }
            let at = w3.read(cx).cursor.clone().and_then(|c| order.iter().position(|v| v.node.id == c));
            let moved = |d: isize| {
                let from = match at {
                    Some(a) => a as isize,
                    None if d > 0 => -1,
                    None => n as isize,
                };
                (from + d).clamp(0, n as isize - 1) as usize
            };
            let current = at.map(|i| &order[i]);
            match key {
                "up" | "down" => {
                    let next = order[moved(if key == "up" { -1 } else { 1 })].node.id.clone();
                    w3.update(cx, |w, cx| {
                        w.cursor = Some(next);
                        cx.notify();
                    });
                }
                "home" | "end" => {
                    let next = order[if key == "home" { 0 } else { n - 1 }].node.id.clone();
                    w3.update(cx, |w, cx| {
                        w.cursor = Some(next);
                        cx.notify();
                    });
                }
                "right" | "left" => {
                    if let (Some(v), Some(f)) = (current, &tog)
                        && v.branch
                        && v.open == (key == "left")
                    {
                        f(&v.node.id, window, cx);
                    }
                }
                "choose" => {
                    if let (Some(v), Some(f)) = (current, &sel) {
                        f(&v.node.id, window, cx);
                    }
                }
                _ => return,
            }
            cx.stop_propagation();
        });
        match self.width {
            Some(w) => list.w(px(w)),
            None => list.w_full(),
        }
    }
}
