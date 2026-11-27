//! SearchList: the type-ahead overlay the shell builds seven times (clipboard, emojis, the menu,
//! the app grid, reminders). A filter line, the matching rows under a keyboard cursor, an
//! optional detail pane beside them, and an empty state when nothing matches, on the [menu]
//! surface. The Qt binding's `SearchList.qml` is the reference.

use std::any::Any;
use std::rc::Rc;

use rune::{El, Key, KeyEvent, TextEdit, Tx, col, el, row, text, text_input};

use super::{CursorKey, CursorModel, EmptyState, ListRow, ScrollArea, printable};
use crate::{Theme, border, color};

/// The CursorModel section a SearchList's rows live in.
pub const SEARCH_SECTION: &str = "results";

/// One result: a title, and optionally a leading glyph, a dim subtitle, a trailing value and the
/// text the detail pane shows for it.
#[derive(Clone, Debug, Default)]
pub struct SearchItem {
    pub title: String,
    pub subtitle: String,
    pub glyph: String,
    pub value: String,
    pub detail: String,
}

impl SearchItem {
    pub fn new(title: impl Into<String>) -> SearchItem {
        SearchItem { title: title.into(), ..Default::default() }
    }
    pub fn subtitle(mut self, s: impl Into<String>) -> Self {
        self.subtitle = s.into();
        self
    }
    pub fn glyph(mut self, g: impl Into<String>) -> Self {
        self.glyph = g.into();
        self
    }
    pub fn value(mut self, v: impl Into<String>) -> Self {
        self.value = v.into();
        self
    }
    pub fn detail(mut self, d: impl Into<String>) -> Self {
        self.detail = d.into();
        self
    }
}

type Callback = Rc<dyn Fn(&mut dyn Any, usize)>;

#[derive(Clone, Default)]
struct Callbacks {
    activate: Option<Callback>,
    delete: Option<Callback>,
    close: Option<Callback>,
}

/// Wires the filter line and the rows to the app's state, typed when `on_state` was called.
trait Wire {
    fn input(&self, e: El, list: &str, count: usize, cb: Callbacks) -> El;
    fn row(&self, e: El, index: usize, count: usize, cb: Callbacks) -> El;
}

type Access<S> = Rc<dyn for<'a> Fn(&'a mut S) -> (&'a mut TextEdit, &'a mut CursorModel)>;

struct Wiring<S> {
    access: Access<S>,
}

impl<S: 'static> Wire for Wiring<S> {
    fn input(&self, e: El, list: &str, count: usize, cb: Callbacks) -> El {
        let (edit, keys, change) = (self.access.clone(), self.access.clone(), self.access.clone());
        let list = list.to_string();
        e.on_edit(move |s: &mut S| edit(s).0)
            .on_input_change(move |s: &mut S, _| {
                // A new query starts the cursor at the first match again.
                change(s).1.set(SEARCH_SECTION, 0);
            })
            .on_input_key(move |s: &mut S, k: &KeyEvent, tx: &mut Tx| {
                let (edit, cursor) = keys(s);
                cursor.set_count(SEARCH_SECTION, count);
                if k.key == Key::Escape {
                    if !edit.text.is_empty() {
                        edit.clear();
                        cursor.set(SEARCH_SECTION, 0);
                    } else if let Some(f) = &cb.close {
                        f(s, 0);
                    }
                    return true;
                }
                // Typed text (Space too) and Backspace are the filter line's.
                let typed = printable(k).is_some() || matches!(&k.key, Key::Text(t) if t == " ");
                if typed || k.key == Key::Backspace {
                    return false;
                }
                let index = cursor.index();
                let (call, at) = match cursor.apply_key(k) {
                    CursorKey::Moved => {
                        ScrollArea::ensure_visible(tx, format!("{list}-row-{}", cursor.index()));
                        return true;
                    }
                    CursorKey::Activate => (&cb.activate, index),
                    CursorKey::Delete => (&cb.delete, index),
                    CursorKey::Close => (&cb.close, 0),
                    CursorKey::Text(_) | CursorKey::Ignored => return false,
                };
                if count > 0 && let Some(f) = call {
                    f(s, at);
                }
                true
            })
    }

    fn row(&self, e: El, index: usize, count: usize, cb: Callbacks) -> El {
        let (hover, press) = (self.access.clone(), self.access.clone());
        e.on_pointer_move(move |s: &mut S, p| {
            let cursor = hover(s).1;
            cursor.set_count(SEARCH_SECTION, count);
            cursor.pointer_moved(p.x, p.y, SEARCH_SECTION, index);
        })
        .on_press(move |s: &mut S| {
            let cursor = press(s).1;
            cursor.set_count(SEARCH_SECTION, count);
            cursor.set(SEARCH_SECTION, index);
            if let Some(f) = &cb.activate {
                f(s, index);
            }
        })
    }
}

/// The type-ahead overlay: a filter line, results as ListRows under a [`CursorModel`], an
/// optional detail pane and an EmptyState.
///
/// ```ignore
/// let items: Vec<SearchItem> = all.iter().filter(|i| SearchList::matches(&i.title, &s.query.text)).cloned().collect();
/// SearchList::new("emoji", &s.query, &s.cursor)
///     .items(items)
///     .size(560.0, 400.0)
///     .on_state(|s: &mut App| (&mut s.query, &mut s.cursor))
///     .on_activate(|s: &mut App, i| s.pick(i))
///     .on_close(|s: &mut App| s.open = false)
///     .build(&t)
/// ```
///
/// Filtering is the caller's: pass only the items that match the query ([`SearchList::matches`]
/// is the plain case-insensitive substring test). Typing edits the query (j and k are text here);
/// Up/Down, Page Up/Down, Home and End move the cursor; Return activates; Delete asks to delete;
/// Escape clears the query, or closes when it is already empty.
pub struct SearchList {
    key: String,
    query: TextEdit,
    cursor: Option<usize>,
    placeholder: Option<String>,
    items: Vec<SearchItem>,
    detail: bool,
    detail_el: Option<El>,
    detail_width: Option<f32>,
    empty_glyph: String,
    empty_text: String,
    no_matches: Option<String>,
    size: Option<(f32, f32)>,
    wire: Option<Box<dyn Wire>>,
    cb: Callbacks,
}

impl SearchList {
    /// `key` names the list (rows are keyed `<key>-row-<i>`); `query` is the filter line's text;
    /// `cursor` the model the keys move (make it with [`SearchList::cursor`]).
    pub fn new(key: impl Into<String>, query: &TextEdit, cursor: &CursorModel) -> SearchList {
        SearchList {
            key: key.into(),
            query: query.clone(),
            cursor: cursor.active.then(|| cursor.index()),
            placeholder: None,
            items: Vec::new(),
            detail: false,
            detail_el: None,
            detail_width: None,
            empty_glyph: "\u{F0349}".into(),
            empty_text: String::new(),
            no_matches: None,
            size: None,
            wire: None,
            cb: Callbacks::default(),
        }
    }
    /// A CursorModel for a SearchList: one section, the cursor showing on the first row, as the
    /// shell's overlays open.
    pub fn cursor() -> CursorModel {
        let mut c = CursorModel::new([(SEARCH_SECTION, 0)]);
        c.active = true;
        c
    }
    /// The plain filter: `query` appears in `text`, ignoring case.
    pub fn matches(text: &str, query: &str) -> bool {
        text.to_lowercase().contains(&query.to_lowercase())
    }
    /// The filter line's text while the query is empty (default `common.search`, "Search...",
    /// in the display language).
    pub fn placeholder(mut self, p: impl Into<String>) -> Self {
        self.placeholder = Some(p.into());
        self
    }
    /// The results to show, already filtered by the query.
    pub fn items(mut self, items: impl IntoIterator<Item = SearchItem>) -> Self {
        self.items = items.into_iter().collect();
        self
    }
    /// Show the detail pane: the cursor item's title and detail text.
    pub fn detail(mut self, on: bool) -> Self {
        self.detail = on;
        self
    }
    /// Show this in the detail pane instead.
    pub fn detail_el(mut self, e: El) -> Self {
        self.detail = true;
        self.detail_el = Some(e);
        self
    }
    /// The detail pane's width (default 45% of the body).
    pub fn detail_width(mut self, w: f32) -> Self {
        self.detail_width = Some(w);
        self
    }
    /// The empty state's glyph, and its text when the query is empty (nothing to list at all).
    pub fn empty(mut self, glyph: impl Into<String>, text: impl Into<String>) -> Self {
        self.empty_glyph = glyph.into();
        self.empty_text = text.into();
        self
    }
    /// The no-matches text; `{}` is the query (default `common.noMatchesFor`, "No matches for
    /// “{}”", in the display language).
    pub fn no_matches(mut self, t: impl Into<String>) -> Self {
        self.no_matches = Some(t.into());
        self
    }
    /// The overlay's outer size (default `space(480)` × `space(360)`).
    pub fn size(mut self, w: f32, h: f32) -> Self {
        self.size = Some((w, h));
        self
    }
    /// Where the app keeps the query and the cursor, so typing and the keys reach them.
    pub fn on_state<S: 'static>(mut self, f: impl for<'a> Fn(&'a mut S) -> (&'a mut TextEdit, &'a mut CursorModel) + 'static) -> Self {
        self.wire = Some(Box::new(Wiring::<S> { access: Rc::new(f) }));
        self
    }
    pub fn on_activate<S: 'static>(mut self, f: impl Fn(&mut S, usize) + 'static) -> Self {
        self.cb.activate = Some(erase(f));
        self
    }
    pub fn on_delete<S: 'static>(mut self, f: impl Fn(&mut S, usize) + 'static) -> Self {
        self.cb.delete = Some(erase(f));
        self
    }
    pub fn on_close<S: 'static>(mut self, f: impl Fn(&mut S) + 'static) -> Self {
        self.cb.close = Some(erase(move |s: &mut S, _| f(s)));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let f = &tk.font;
        let menu = &tk.surfaces.menu.base;
        let bw = menu.border.widths.map(|w| w as f32);
        let pad = sp.panel_padding as f32;
        let (w, h) = self.size.unwrap_or((t.space(480.0), t.space(360.0)));
        let header_h = t.space(34.0).max((f.heading + sp.control_padding_y * 2) as f32);
        let inner_w = w - bw[1] - bw[3] - pad * 2.0;
        let body_h = h - bw[0] - bw[2] - pad * 2.0 - header_h - sp.md as f32;
        let n = self.items.len();
        let cursor = self.cursor.map(|i| i.min(n.saturating_sub(1))).filter(|_| n > 0);

        // The filter line: heading-sized menu text, the placeholder dim, no caret (the list owns
        // Left and Right, so the line only grows at its end).
        let style = t.text_style(f.heading);
        let mut input = text_input(&self.query, style)
            .single_line()
            .key(format!("{}-filter", self.key))
            .grow(1.0)
            .min_w(0.0)
            .color(color(menu.text))
            .placeholder(self.placeholder.unwrap_or_else(|| tk.string("common.search", "Search...")), t.faint())
            .caret(rune::Color::TRANSPARENT, color(tk.controls.selection.fill));
        if let Some(wire) = &self.wire {
            input = wire.input(input, &self.key, n, self.cb.clone());
        }
        let header = row().w_full().h(header_h).items_center().child(input);

        let body = if n == 0 {
            let title = if self.query.text.is_empty() {
                self.empty_text
            } else {
                // The catalog's line takes the query as `%1`, as Qt's `arg` does; a caller's as `{}`.
                let line = self.no_matches.unwrap_or_else(|| tk.string("common.noMatchesFor", "No matches for “%1”"));
                line.replace("{}", &self.query.text).replace("%1", &self.query.text)
            };
            el().w_full().h(body_h).center().child(EmptyState::new(title).glyph(self.empty_glyph).overlay(true).build(t))
        } else {
            let gap = sp.xxl as f32;
            let detail_w = self.detail_width.unwrap_or((inner_w * 0.45).floor());
            let list_w = if self.detail { inner_w - detail_w - gap * 2.0 - 1.0 } else { inner_w };
            let rows = self.items.iter().enumerate().map(|(i, item)| {
                let mut r = ListRow::new(item.title.clone()).key(format!("{}-row-{i}", self.key)).cursor(cursor == Some(i));
                if !item.glyph.is_empty() {
                    r = r.glyph(item.glyph.clone());
                }
                if !item.subtitle.is_empty() {
                    r = r.subtitle(item.subtitle.clone());
                }
                if !item.value.is_empty() {
                    r = r.value(item.value.clone());
                }
                let e = r.build(t);
                match &self.wire {
                    Some(wire) => wire.row(e, i, n, self.cb.clone()),
                    None => e,
                }
            });
            let list = ScrollArea::new(format!("{}-scroll", self.key)).width(list_w).max_height(body_h).gap(sp.xxs as f32).children(rows).build(t);
            let mut b = row().w_full().h(body_h).items_start().child(list);
            if self.detail {
                let pane = match self.detail_el {
                    Some(e) => e,
                    None => {
                        let item = cursor.map(|i| self.items[i].clone()).unwrap_or_default();
                        col()
                            .w_full()
                            .pt(sp.row_padding_x as f32 / 2.0)
                            .gap(sp.sm as f32)
                            .child(text(item.title).font(t.text_style_bold(f.title)).color(color(menu.text)).single_line().truncate())
                            .child(text(item.detail).font(t.text_style(f.body)).color(t.dim()).wrap().w_full())
                    }
                };
                b = b
                    .child(el().w(gap))
                    .child(el().w(1.0).h(body_h).fill(t.track()))
                    .child(el().w(gap))
                    .child(col().w(detail_w).h(body_h).clip_rounded(0.0).child(pane));
            }
            b
        };

        let card = col()
            .size(w, h)
            .pt(pad + bw[0])
            .pr(pad + bw[1])
            .pb(pad + bw[2])
            .pl(pad + bw[3])
            .gap(sp.md as f32)
            .fill(color(menu.background))
            .radius(t.radius())
            .key(self.key)
            .child(header)
            .child(body);
        border(card, &menu.border)
    }
}

fn erase<S: 'static>(f: impl Fn(&mut S, usize) + 'static) -> Callback {
    Rc::new(move |s: &mut dyn Any, i| {
        if let Some(s) = s.downcast_mut::<S>() {
            f(s, i);
        }
    })
}
