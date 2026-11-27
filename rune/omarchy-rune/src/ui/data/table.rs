//! Table: a header row of dim bold captions (a sort chevron on the sorted column), then rows of
//! plain text cut with "…", hairline rules between them, one selected row and one cursor row.

use rune::{Align, El, col, el, row, text};

use super::{bottom_rule, row_chrome};
use crate::ui::glyph;
use crate::{Theme, color};

/// A table column: a title, a fixed width (or a share of what fixed columns leave), and
/// whether its cells align right (numbers).
#[derive(Clone)]
pub struct Column {
    title: String,
    width: Option<f32>,
    right: bool,
}

impl Column {
    pub fn new(title: impl Into<String>) -> Column {
        Column { title: title.into(), width: None, right: false }
    }
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    /// Right-align the column: for numbers.
    pub fn right(mut self) -> Self {
        self.right = true;
        self
    }
}

type Pick = Box<dyn Fn(El, usize) -> El>;

/// A data table. The app sorts: the table shows `sort(column, ascending)` and asks for another
/// order with `on_sort`. `selected` is the chosen row (selected fill, bold), `cursor` the
/// keyboard cursor (hover fill, as the pointer's hover).
pub struct Table {
    key: Option<String>,
    columns: Vec<Column>,
    rows: Vec<Vec<String>>,
    sort: Option<(usize, bool)>,
    selected: Option<usize>,
    cursor: Option<usize>,
    separators: bool,
    width: Option<f32>,
    select: Option<Pick>,
    sort_by: Option<Pick>,
}

impl Table {
    pub fn new(columns: impl IntoIterator<Item = Column>) -> Table {
        Table {
            columns: columns.into_iter().collect(),
            rows: Vec::new(),
            sort: None,
            selected: None,
            cursor: None,
            separators: true,
            width: None,
            select: None,
            sort_by: None,
            key: None,
        }
    }
    /// One `Vec` of cell strings per row, one string per column.
    pub fn rows<R: IntoIterator<Item = S>, S: Into<String>>(mut self, rows: impl IntoIterator<Item = R>) -> Self {
        self.rows = rows.into_iter().map(|r| r.into_iter().map(Into::into).collect()).collect();
        self
    }
    /// The sorted column and direction, shown as a chevron in its header.
    pub fn sort(mut self, column: usize, ascending: bool) -> Self {
        self.sort = Some((column, ascending));
        self
    }
    pub fn selected(mut self, row: Option<usize>) -> Self {
        self.selected = row;
        self
    }
    /// Force the hover-cursor look on one row.
    pub fn cursor(mut self, row: Option<usize>) -> Self {
        self.cursor = row;
        self
    }
    /// Hairline rules between rows (on by default).
    pub fn separators(mut self, on: bool) -> Self {
        self.separators = on;
        self
    }
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    /// Called with the row pressed (or chosen with Return/Space).
    pub fn on_select<S: 'static>(mut self, f: impl Fn(&mut S, usize) + Clone + 'static) -> Self {
        self.select = Some(Box::new(move |e: El, i: usize| {
            let f = f.clone();
            e.on_press(move |s: &mut S| f(s, i))
        }));
        self
    }
    /// Called with the column whose header was pressed.
    pub fn on_sort<S: 'static>(mut self, f: impl Fn(&mut S, usize) + Clone + 'static) -> Self {
        self.sort_by = Some(Box::new(move |e: El, i: usize| {
            let f = f.clone();
            e.on_press(move |s: &mut S| f(s, i))
        }));
        self
    }

    /// Key the parts for tests and focus: `<prefix>-<row>` for rows, `<prefix>-col-<column>` for headers.
    pub fn key(mut self, prefix: impl Into<String>) -> Self {
        self.key = Some(prefix.into());
        self
    }
    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let h = sp.popup_row_height as f32;
        let pad = sp.control_padding_x as f32;
        let selected_ink = color(tk.controls.selected.text);

        // Column widths as Qt computes them: fixed as given, the rest share what is left.
        let fixed: f32 = self.columns.iter().filter_map(|c| c.width).sum();
        let flex = self.columns.iter().filter(|c| c.width.is_none()).count();
        let share = self.width.map(|w| if flex > 0 { ((w - fixed) / flex as f32).floor().max(0.0) } else { 0.0 });
        let sized = |c: &Column, e: El| match (c.width, share) {
            (Some(w), _) | (None, Some(w)) => e.w(w),
            (None, None) => e.grow(1.0).min_w(0.0),
        };

        // ---- Header
        let mut header = row().w_full().h(h);
        for (i, c) in self.columns.iter().enumerate() {
            let title = text(c.title.clone()).font(t.text_style_bold(tk.font.caption)).color(t.dim()).truncate();
            let mut content = row().items_center().gap(sp.xs as f32).min_w(0.0);
            let chevron = self.sort.filter(|(col, _)| *col == i).map(|(_, up)| {
                glyph(t, if up { "\u{f0143}" } else { "\u{f0140}" }, tk.font.caption, t.dim())
            });
            content = match (c.right, chevron) {
                (true, Some(g)) => content.child(g).child(title),
                (false, Some(g)) => content.child(title).child(g),
                (_, None) => content.child(title),
            };
            let mut cell = sized(c, row().h_full().items_center().px(pad));
            if c.right {
                cell = cell.justify_end();
            }
            let mut cell = cell.child(content).role("columnheader").label(c.title.clone());
            if let Some(k) = &self.key {
                cell = cell.key(format!("{k}-col-{i}"));
            }
            if let Some(attach) = &self.sort_by {
                cell = attach(cell, i);
            }
            header = header.child(cell);
        }
        let header = el().w_full().h(h).child(header).child(bottom_rule(t));

        // ---- Body
        let last = self.rows.len().saturating_sub(1);
        let mut body = col().w_full().role("grid").focus_group();
        for (r, cells) in self.rows.iter().enumerate() {
            let is_selected = self.selected == Some(r);
            let is_cursor = self.cursor == Some(r);
            let next_highlighted = self.selected == Some(r + 1) || self.cursor == Some(r + 1);
            let style = if is_selected { t.text_style_bold(tk.font.body) } else { t.text_style(tk.font.body) };
            let ink = if is_selected { selected_ink } else { t.fg() };
            let mut kids = Vec::new();
            let mut line = row().w_full().h_full().items_center();
            for (i, c) in self.columns.iter().enumerate() {
                let value = cells.get(i).cloned().unwrap_or_default();
                let mut label = text(value).font(style).color(ink).truncate().w_full();
                if c.right {
                    label = label.align(Align::End);
                }
                line = line.child(sized(c, row().h_full().items_center().px(pad)).child(label));
            }
            kids.push(line);
            if self.separators && r < last && !is_selected && !is_cursor && !next_highlighted {
                kids.push(bottom_rule(t));
            }
            let base = el().w_full().h(h).role("row").focusable(true).focus_selected(is_selected).label(cells.first().cloned().unwrap_or_default());
            let mut e = row_chrome(t, base, kids, is_selected, is_cursor);
            if let Some(k) = &self.key {
                e = e.key(format!("{k}-{r}"));
            }
            if let Some(attach) = &self.select {
                e = attach(e, r);
            }
            body = body.child(e);
        }

        let mut table = col().child(header).child(body);
        table = match self.width {
            Some(w) => table.w(w),
            None => table.w_full(),
        };
        table
    }
}
