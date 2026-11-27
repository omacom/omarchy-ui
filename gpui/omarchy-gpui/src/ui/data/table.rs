//! Table: a header row of dim bold captions (a sort chevron on the sorted column), then rows of
//! plain text cut with "…", hairline rules between them, one selected row and one cursor row.

use std::rc::Rc;

use gpui::prelude::*;
use gpui::{App, KeyDownEvent, MouseButton, SharedString, Window, div, px};

use super::{Walk, bottom_rule, row_fill, row_ink, walk_key};
use crate::Paint;
use crate::ui::{Handler, focus_handle, keyed_state, label, label_elided, tab_keys};

/// A table column: a title, a fixed width (or a share of what fixed columns leave), and
/// whether its cells align right (numbers).
#[derive(Clone)]
pub struct Column {
    title: SharedString,
    width: Option<f32>,
    right: bool,
}

impl Column {
    pub fn new(title: impl Into<SharedString>) -> Column {
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

/// A data table. The app sorts: the table shows `sort(column, ascending)` and asks for another
/// order with `on_sort` (a header click). `selected` is the chosen row (selected fill, bold);
/// the keyboard cursor (hover fill) moves with Up/Down/Home/End/PageUp/PageDown and the
/// pointer; Return or Space (or a click) choose it through `on_select`.
#[derive(IntoElement)]
pub struct Table {
    id: SharedString,
    columns: Vec<Column>,
    rows: Vec<Vec<SharedString>>,
    sort: Option<(usize, bool)>,
    selected: Option<usize>,
    cursor: Option<usize>,
    separators: bool,
    width: Option<f32>,
    height: Option<f32>,
    on_select: Option<Handler<usize>>,
    on_sort: Option<Handler<usize>>,
}

impl Table {
    pub fn new(columns: impl IntoIterator<Item = Column>) -> Table {
        let columns: Vec<Column> = columns.into_iter().collect();
        let id = format!("omarchy-table-{}", columns.iter().map(|c| c.title.as_ref()).collect::<Vec<_>>().join("|")).into();
        Table {
            id,
            columns,
            rows: Vec::new(),
            sort: None,
            selected: None,
            cursor: None,
            separators: true,
            width: None,
            height: None,
            on_select: None,
            on_sort: None,
        }
    }
    /// The id its cursor and focus are kept under (and its parts' debug selectors start with).
    pub fn id(mut self, id: impl Into<SharedString>) -> Self {
        self.id = id.into();
        self
    }
    /// One row of cell strings per row, one string per column.
    pub fn rows<R: IntoIterator<Item = S>, S: Into<SharedString>>(mut self, rows: impl IntoIterator<Item = R>) -> Self {
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
    /// Force the hover-cursor look on one row (and start the keyboard cursor there).
    pub fn cursor(mut self, row: Option<usize>) -> Self {
        self.cursor = row;
        self
    }
    /// Hairline rules between rows (on by default).
    pub fn separators(mut self, on: bool) -> Self {
        self.separators = on;
        self
    }
    /// The width; flexible columns share what the fixed ones leave. The parent's by default.
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    /// A fixed height: the rows scroll under the header. As tall as its rows by default.
    pub fn height(mut self, h: f32) -> Self {
        self.height = Some(h);
        self
    }
    /// Called with the row clicked, or chosen with Return/Space.
    pub fn on_select(mut self, f: impl Fn(&usize, &mut Window, &mut App) + 'static) -> Self {
        self.on_select = Some(Rc::new(f));
        self
    }
    /// Called with the column whose header was clicked.
    pub fn on_sort(mut self, f: impl Fn(&usize, &mut Window, &mut App) + 'static) -> Self {
        self.on_sort = Some(Rc::new(f));
        self
    }
}

impl RenderOnce for Table {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let h = px(sp.popup_row_height as f32);
        let pad = px(sp.control_padding_x as f32);
        let id: gpui::ElementId = self.id.clone().into();
        let focus = focus_handle(&id, window, cx);
        let forced = self.cursor;
        let walk = keyed_state(&id, "table", window, cx, || Walk::new(forced));
        walk.update(cx, |w, _| w.sync(&forced));
        let (cursor, hovering) = {
            let w = walk.read(cx);
            (w.cursor, w.hovering)
        };
        let shown = forced.is_some() || hovering || focus.is_focused(window);

        // Column widths as Qt computes them: fixed as given, the rest share what is left.
        let fixed: f32 = self.columns.iter().filter_map(|c| c.width).sum();
        let flex = self.columns.iter().filter(|c| c.width.is_none()).count();
        let share = self.width.map(|w| if flex > 0 { ((w - fixed) / flex as f32).floor().max(0.0) } else { 0.0 });
        let sized = |c: &Column, e: gpui::Div| match (c.width, share) {
            (Some(w), _) | (None, Some(w)) => e.w(px(w)).flex_shrink_0(),
            (None, None) => e.flex_1().min_w_0(),
        };

        // ---- Header
        let mut header = div().relative().flex().flex_row().w_full().h(h).flex_shrink_0();
        for (i, c) in self.columns.iter().enumerate() {
            let title = label_elided(&t, c.title.clone(), tk.font.caption, true, t.dim());
            let chevron = self
                .sort
                .filter(|(col, _)| *col == i)
                .map(|(_, up)| label(&t, if up { "\u{f0143}" } else { "\u{f0140}" }, tk.font.caption, false, t.dim()));
            let mut content = div().flex().flex_row().items_center().min_w_0().gap(px(sp.xs as f32));
            content = match (c.right, chevron) {
                (true, Some(g)) => content.child(g).child(title),
                (false, Some(g)) => content.child(title).child(g),
                (_, None) => content.child(title),
            };
            let mut cell = sized(c, div().flex().flex_row().h_full().items_center().px(pad));
            if c.right {
                cell = cell.justify_end();
            }
            let selector = format!("{}-col-{i}", self.id);
            let mut cell = cell.id(i).debug_selector(|| selector).cursor_pointer().child(content);
            if let Some(f) = self.on_sort.clone() {
                cell = cell.on_click(move |_, window, cx| f(&i, window, cx));
            }
            header = header.child(cell);
        }
        header = header.child(bottom_rule(&t));

        // ---- Body
        let last = self.rows.len().saturating_sub(1);
        let mut body = div().id("rows").flex().flex_col().w_full();
        if self.height.is_some() {
            body = body.flex_1().min_h_0().overflow_y_scroll();
        }
        for (r, cells) in self.rows.iter().enumerate() {
            let is_selected = self.selected == Some(r);
            let hot = shown && cursor == Some(r);
            let next_highlighted = self.selected == Some(r + 1) || (shown && cursor == Some(r + 1));
            let ink = row_ink(&t, is_selected);
            let mut line = div().relative().flex().flex_row().w_full().h(h).flex_shrink_0().items_center().rounded(t.radius()).bg(row_fill(&t, is_selected, hot));
            for (i, c) in self.columns.iter().enumerate() {
                let value = cells.get(i).cloned().unwrap_or_default();
                let mut text = label_elided(&t, value, tk.font.body, is_selected, ink).w_full();
                if c.right {
                    text = text.text_right();
                }
                line = line.child(sized(c, div().flex().flex_row().h_full().items_center().px(pad)).child(text));
            }
            if self.separators && r < last && !is_selected && !hot && !next_highlighted {
                line = line.child(bottom_rule(&t));
            }
            let (w1, w2) = (walk.clone(), walk.clone());
            let (f, fo) = (self.on_select.clone(), focus.clone());
            let selector = format!("{}-{r}", self.id);
            let row = line
                .id(r)
                .debug_selector(|| selector)
                .cursor_pointer()
                .on_hover(move |on, _, cx| {
                    w1.update(cx, |w, cx| {
                        w.hovering = *on;
                        if *on {
                            w.cursor = Some(r);
                        }
                        cx.notify();
                    })
                })
                .on_mouse_down(MouseButton::Left, move |_, window, cx| {
                    fo.focus(window, cx);
                    w2.update(cx, |w, cx| {
                        w.cursor = Some(r);
                        cx.notify();
                    });
                })
                .on_click(move |_, window, cx| {
                    if let Some(f) = &f {
                        f(&r, window, cx);
                    }
                });
            body = body.child(row);
        }

        let n = self.rows.len();
        let page = self.height.map(|hh| ((hh - h.as_f32()) / h.as_f32()).floor().max(1.0) as isize).unwrap_or(n.max(1) as isize);
        let (w3, f) = (walk.clone(), self.on_select.clone());
        let mut table = div()
            .id(id)
            .flex()
            .flex_col()
            .flex_shrink_0()
            .track_focus(&focus)
            .on_key_down(move |ev: &KeyDownEvent, window, cx| {
                if tab_keys(ev, window, cx) {
                    cx.stop_propagation();
                    return;
                }
                let Some(key) = walk_key(ev) else { return };
                if n == 0 {
                    return;
                }
                let at = w3.read(cx).cursor;
                let step = |d: isize| {
                    let from = match at {
                        Some(a) => a as isize,
                        None if d > 0 => -1,
                        None => n as isize,
                    };
                    Some((from + d).clamp(0, n as isize - 1) as usize)
                };
                let next = match key {
                    "up" => step(-1),
                    "down" => step(1),
                    "pageup" => step(-page),
                    "pagedown" => step(page),
                    "home" => Some(0),
                    "end" => Some(n - 1),
                    "choose" => {
                        if let (Some(row), Some(f)) = (at, &f) {
                            f(&row, window, cx);
                        }
                        cx.stop_propagation();
                        return;
                    }
                    _ => return,
                };
                w3.update(cx, |w, cx| {
                    w.cursor = next;
                    cx.notify();
                });
                cx.stop_propagation();
            })
            .child(header)
            .child(body);
        table = match self.width {
            Some(w) => table.w(px(w)),
            None => table.w_full(),
        };
        if let Some(hh) = self.height {
            table = table.h(px(hh));
        }
        table
    }
}
