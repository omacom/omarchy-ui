//! Tree: FileTree-style rows, each level indented by `huge`, a chevron on branches, an optional
//! glyph, then the label.

use rune::{El, Key, KeyEvent, Tx, col, el, row, text};

use super::row_chrome;
use crate::ui::optical;
use crate::{Theme, color};

/// One node: an id the app knows it by, a label, an optional glyph, and children.
#[derive(Clone)]
pub struct TreeNode {
    id: String,
    label: String,
    glyph: Option<String>,
    children: Vec<TreeNode>,
}

impl TreeNode {
    pub fn new(id: impl Into<String>, label: impl Into<String>) -> TreeNode {
        TreeNode { id: id.into(), label: label.into(), glyph: None, children: Vec::new() }
    }
    pub fn glyph(mut self, g: impl Into<String>) -> Self {
        self.glyph = Some(g.into());
        self
    }
    pub fn children(mut self, nodes: impl IntoIterator<Item = TreeNode>) -> Self {
        self.children = nodes.into_iter().collect();
        self
    }
}

type Pick = Box<dyn Fn(El, String) -> El>;
/// A branch row's Left (fold, when open) and Right (unfold, when folded), as Qt's Tree takes them.
type Fold = Box<dyn Fn(El, String, bool) -> El>;

/// A tree. The app owns which ids are `expanded`, the `selected` id and the `cursor` id; the
/// tree asks with `on_toggle` (the chevron) and `on_select` (the row).
pub struct Tree {
    key: Option<String>,
    nodes: Vec<TreeNode>,
    expanded: Vec<String>,
    selected: Option<String>,
    cursor: Option<String>,
    width: Option<f32>,
    toggle: Option<Pick>,
    fold: Option<Fold>,
    select: Option<Pick>,
}

struct Visible<'a> {
    node: &'a TreeNode,
    depth: usize,
    open: bool,
}

impl Tree {
    pub fn new(nodes: impl IntoIterator<Item = TreeNode>) -> Tree {
        Tree {
            nodes: nodes.into_iter().collect(),
            expanded: Vec::new(),
            selected: None,
            cursor: None,
            width: None,
            toggle: None,
            fold: None,
            select: None,
            key: None,
        }
    }
    pub fn expanded<S: AsRef<str>>(mut self, ids: impl IntoIterator<Item = S>) -> Self {
        self.expanded = ids.into_iter().map(|s| s.as_ref().to_string()).collect();
        self
    }
    pub fn selected(mut self, id: Option<&str>) -> Self {
        self.selected = id.map(str::to_string);
        self
    }
    /// Force the hover-cursor look on one row.
    pub fn cursor(mut self, id: Option<&str>) -> Self {
        self.cursor = id.map(str::to_string);
        self
    }
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    /// Called with a branch's id when its chevron is pressed or its row double-pressed, and when
    /// its row has focus and Left folds it (open) or Right unfolds it (folded), as in Qt's Tree.
    pub fn on_toggle<S: 'static>(mut self, f: impl Fn(&mut S, &str) + Clone + 'static) -> Self {
        let g = f.clone();
        self.toggle = Some(Box::new(move |e: El, id: String| {
            let f = f.clone();
            e.on_press(move |s: &mut S| f(s, &id))
        }));
        self.fold = Some(Box::new(move |e: El, id: String, open: bool| {
            let (f, twice) = (g.clone(), g.clone());
            let again = id.clone();
            e.on_double_press(move |s: &mut S| twice(s, &again)).on_key(move |s: &mut S, k: &KeyEvent, _: &mut Tx| {
                let fold = matches!((&k.key, open), (Key::Left, true) | (Key::Right, false));
                if fold {
                    f(s, &id);
                }
                fold
            })
        }));
        self
    }
    /// Called with a row's id when it is pressed (or chosen with Return/Space).
    pub fn on_select<S: 'static>(mut self, f: impl Fn(&mut S, &str) + Clone + 'static) -> Self {
        self.select = Some(Box::new(move |e: El, id: String| {
            let f = f.clone();
            e.on_press(move |s: &mut S| f(s, &id))
        }));
        self
    }

    /// Key the parts for tests and focus: `<prefix>-<id>` for rows, `<prefix>-<id>-toggle` for chevrons.
    pub fn key(mut self, prefix: impl Into<String>) -> Self {
        self.key = Some(prefix.into());
        self
    }
    fn walk<'a>(&self, nodes: &'a [TreeNode], depth: usize, out: &mut Vec<Visible<'a>>) {
        for n in nodes {
            let open = !n.children.is_empty() && self.expanded.iter().any(|e| *e == n.id);
            out.push(Visible { node: n, depth, open });
            if open {
                self.walk(&n.children, depth + 1, out);
            }
        }
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let h = sp.popup_row_height as f32;
        let slot = sp.xxxl as f32;
        let icon = tk.font.icon;
        let selected_ink = color(tk.controls.selected.text);
        let mut rows = Vec::new();
        self.walk(&self.nodes, 0, &mut rows);

        let mut list = col().role("tree").focus_group();
        for v in rows {
            let n = v.node;
            let is_selected = self.selected.as_deref() == Some(n.id.as_str());
            let branch = !n.children.is_empty();
            let mut line = row().w_full().h_full().items_center().pl(sp.md as f32 + v.depth as f32 * sp.huge as f32).pr(sp.md as f32);
            let mut chevron = if branch {
                optical(if v.open { "\u{f0140}" } else { "\u{f0142}" }, t.text_style(tk.font.icon_small), t.dim(), None, slot, h)
            } else {
                el().w(slot).h_full()
            };
            if branch && let Some(attach) = &self.toggle {
                chevron = attach(chevron, n.id.clone());
            }
            if branch && let Some(k) = &self.key {
                chevron = chevron.key(format!("{k}-{}-toggle", n.id));
            }
            line = line.child(chevron).child(el().w(sp.sm as f32));
            if let Some(g) = &n.glyph {
                let ink = if is_selected { selected_ink } else { t.dim() };
                line = line.child(optical(g, t.text_style(icon), ink, None, icon as f32, h)).child(el().w(sp.md as f32));
            }
            let style = if is_selected { t.text_style_bold(tk.font.body) } else { t.text_style(tk.font.body) };
            line = line.child(text(n.label.clone()).font(style).color(if is_selected { selected_ink } else { t.fg() }).truncate().grow(1.0));
            let mut base = el().w_full().h(h).role("treeitem").focusable(true).focus_selected(is_selected).label(n.label.clone());
            if branch {
                base = base.expanded(v.open);
            }
            let mut e = row_chrome(t, base, vec![line], is_selected, self.cursor.as_deref() == Some(n.id.as_str()));
            if let Some(k) = &self.key {
                e = e.key(format!("{k}-{}", n.id));
            }
            if let Some(attach) = &self.select {
                e = attach(e, n.id.clone());
            }
            if branch && let Some(attach) = &self.fold {
                e = attach(e, n.id.clone(), v.open);
            }
            list = list.child(e);
        }
        match self.width {
            Some(w) => list.w(w),
            None => list.w_full(),
        }
    }
}
