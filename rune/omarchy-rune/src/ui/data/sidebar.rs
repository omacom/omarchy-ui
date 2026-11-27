//! Sidebar: an app's navigation, sections of SectionHeaders over glyph + label rows, one
//! selected; collapsed, a rail of glyphs.

use rune::{El, col, el, row, text};

use super::row_chrome;
use crate::ui::{optical, section_header};
use crate::{Theme, color};

/// One row: an id the app knows it by, a label and a glyph.
#[derive(Clone)]
pub struct SidebarItem {
    id: String,
    label: String,
    glyph: String,
}

impl SidebarItem {
    pub fn new(id: impl Into<String>, label: impl Into<String>, glyph: impl Into<String>) -> SidebarItem {
        SidebarItem { id: id.into(), label: label.into(), glyph: glyph.into() }
    }
}

/// A titled run of rows.
#[derive(Clone)]
pub struct SidebarSection {
    title: String,
    items: Vec<SidebarItem>,
}

impl SidebarSection {
    pub fn new(title: impl Into<String>, items: impl IntoIterator<Item = SidebarItem>) -> SidebarSection {
        SidebarSection { title: title.into(), items: items.into_iter().collect() }
    }
}

type Pick = Box<dyn Fn(El, String) -> El>;

pub struct Sidebar {
    key: Option<String>,
    sections: Vec<SidebarSection>,
    selected: Option<String>,
    cursor: Option<String>,
    collapsed: bool,
    width: Option<f32>,
    select: Option<Pick>,
}

impl Sidebar {
    pub fn new(sections: impl IntoIterator<Item = SidebarSection>) -> Sidebar {
        Sidebar { sections: sections.into_iter().collect(), selected: None, cursor: None, collapsed: false, width: None, select: None, key: None }
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
    pub fn on_select<S: 'static>(mut self, f: impl Fn(&mut S, &str) + Clone + 'static) -> Self {
        self.select = Some(Box::new(move |e: El, id: String| {
            let f = f.clone();
            e.on_press(move |s: &mut S| f(s, &id))
        }));
        self
    }

    /// Key the parts for tests and focus: `<prefix>-<id>` for rows.
    pub fn key(mut self, prefix: impl Into<String>) -> Self {
        self.key = Some(prefix.into());
        self
    }
    /// The collapsed width: the padding, the row inset each side, one glyph.
    pub fn rail_width(t: &Theme) -> f32 {
        let tk = t.tokens();
        (tk.spacing.sm * 2 + tk.spacing.control_padding_x * 2 + tk.font.icon) as f32
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let h = sp.popup_row_height as f32;
        let inset = sp.control_padding_x as f32;
        let icon_w = tk.font.icon as f32;
        let selected_ink = color(tk.controls.selected.text);

        let mut sections = col().w_full().gap(sp.md as f32);
        for s in self.sections {
            let mut section = col().w_full();
            if !self.collapsed && !s.title.is_empty() {
                section = section.child(section_header(t, s.title).pl(inset).pb(sp.xxs as f32).truncate());
            }
            for item in s.items {
                let is_selected = self.selected.as_deref() == Some(item.id.as_str());
                let ink = if is_selected { selected_ink } else { t.fg() };
                let mut line = row().w_full().h_full().items_center().px(inset);
                line = line.child(optical(&item.glyph, t.text_style(tk.font.icon), if is_selected { ink } else { t.dim() }, None, icon_w, h));
                if !self.collapsed {
                    let style = if is_selected { t.text_style_bold(tk.font.body) } else { t.text_style(tk.font.body) };
                    line = line.child(el().w(sp.control_gap as f32)).child(text(item.label.clone()).font(style).color(ink).truncate().grow(1.0));
                }
                let base = el().w_full().h(h).role("listitem").focusable(true).focus_selected(is_selected).label(item.label.clone());
                let mut e = row_chrome(t, base, vec![line], is_selected, self.cursor.as_deref() == Some(item.id.as_str()));
                if let Some(k) = &self.key {
                    e = e.key(format!("{k}-{}", item.id));
                }
                if let Some(attach) = &self.select {
                    e = attach(e, item.id.clone());
                }
                section = section.child(e);
            }
            sections = sections.child(section);
        }
        let mut bar = col().h_full().padding(sp.sm as f32).role("list").focus_group().child(sections);
        bar = if self.collapsed {
            bar.w(Sidebar::rail_width(t))
        } else {
            match self.width {
                Some(w) => bar.w(w),
                None => bar.w_full(),
            }
        };
        bar
    }
}
