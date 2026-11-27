//! SearchList (spec/components/composed.md): the type-ahead overlay the shell builds seven times
//! (clipboard, emojis, the menu, the app grid, reminders). A filter line, the matching rows under
//! a keyboard cursor, an optional detail pane beside them, and an empty state when nothing
//! matches, on the [menu] surface. The Qt binding's `SearchList.qml` is the reference.

use std::rc::Rc;

use gpui::prelude::*;
use gpui::{AnyElement, App, Context, ElementId, Entity, KeyDownEvent, Pixels, Point, ScrollHandle, SharedString, Window, div, px};

use super::field::{TextChanged, TextInput};
use super::lists::glyph;
use super::{CursorKey, CursorModel, Handler, ListRow, ScrollArea, border, label_elided, paragraph};
use crate::{Paint, color};

/// The CursorModel section a SearchList's rows live in.
pub const SEARCH_SECTION: &str = "results";

/// One result: a title, and optionally a leading glyph, a dim subtitle, a trailing value and the
/// text the detail pane shows for it.
#[derive(Clone, Debug, Default)]
pub struct SearchItem {
    pub title: SharedString,
    pub subtitle: SharedString,
    pub glyph: SharedString,
    pub value: SharedString,
    pub detail: SharedString,
}

impl SearchItem {
    pub fn new(title: impl Into<SharedString>) -> SearchItem {
        SearchItem { title: title.into(), ..Default::default() }
    }
    pub fn subtitle(mut self, s: impl Into<SharedString>) -> Self {
        self.subtitle = s.into();
        self
    }
    pub fn glyph(mut self, g: impl Into<SharedString>) -> Self {
        self.glyph = g.into();
        self
    }
    pub fn value(mut self, v: impl Into<SharedString>) -> Self {
        self.value = v.into();
        self
    }
    pub fn detail(mut self, d: impl Into<SharedString>) -> Self {
        self.detail = d.into();
        self
    }
}

/// What a [`SearchList`] keeps between frames: the query (a [`TextInput`]), the cursor over the
/// results, and the scroll. Keep the entity in your view (`SearchState::new(cx, "")`) and filter
/// your items by [`SearchState::query`]. A new query puts the cursor back on the first match.
pub struct SearchState {
    pub input: Entity<TextInput>,
    pub cursor: CursorModel,
    pub scroll: ScrollHandle,
    _typing: gpui::Subscription,
}

impl SearchState {
    /// A query holding `query`, the cursor showing on the first row (the shell's overlays open
    /// with it there).
    pub fn new(cx: &mut App, query: impl Into<SharedString>) -> Entity<SearchState> {
        let input = TextInput::new(cx, query);
        // The filter line has no caret (spec/components/composed.md).
        input.update(cx, |i, _| i.set_caret(false));
        cx.new(|cx: &mut Context<SearchState>| {
            let typing = cx.subscribe(&input, |s: &mut SearchState, _, _: &TextChanged, cx| {
                s.cursor.set(SEARCH_SECTION, 0);
                s.scroll.set_offset(gpui::point(px(0.), px(0.)));
                cx.notify();
            });
            let mut cursor = CursorModel::new([(SEARCH_SECTION, 0)]);
            cursor.active = true;
            SearchState { input, cursor, scroll: ScrollHandle::new(), _typing: typing }
        })
    }
    /// The query's text.
    pub fn query(&self, cx: &App) -> SharedString {
        self.input.read(cx).text().clone()
    }
}

/// The type-ahead overlay: a filter line, results as ListRows under a [`CursorModel`], an
/// optional detail pane and an empty state.
///
/// ```ignore
/// let q = self.search.read(cx).query(cx);
/// let items = all.iter().filter(|i| SearchList::matches(&i.title, &q)).cloned();
/// SearchList::new("emoji", &self.search)
///     .items(items)
///     .on_activate(cx.listener(|this, i: &usize, _, cx| this.pick(*i, cx)))
///     .on_close(cx.listener(|this, _, _, cx| this.close(cx)))
/// ```
///
/// Filtering is the caller's: pass only the items that match the query ([`SearchList::matches`]
/// is the plain case-insensitive substring test). Typing edits the query (j, k, x and Space are
/// text here); Up/Down, Page Up/Down, Home and End move the cursor; Return activates; Delete asks
/// to delete; Escape clears the query, or asks to close when it is already empty.
#[derive(IntoElement)]
pub struct SearchList {
    id: ElementId,
    state: Entity<SearchState>,
    placeholder: Option<SharedString>,
    items: Vec<SearchItem>,
    detail: bool,
    detail_el: Option<AnyElement>,
    detail_width: Option<f32>,
    empty_glyph: SharedString,
    empty_text: SharedString,
    no_matches: Option<SharedString>,
    size: Option<(f32, f32)>,
    on_activate: Option<Handler<usize>>,
    on_delete: Option<Handler<usize>>,
    on_close: Option<Handler<()>>,
}

impl SearchList {
    /// `id` names the list (rows are `(id, "row-<i>")` under it); `state` keeps the query, cursor
    /// and scroll.
    pub fn new(id: impl Into<ElementId>, state: &Entity<SearchState>) -> SearchList {
        SearchList {
            id: id.into(),
            state: state.clone(),
            placeholder: None,
            items: Vec::new(),
            detail: false,
            detail_el: None,
            detail_width: None,
            empty_glyph: "\u{F0349}".into(),
            empty_text: SharedString::default(),
            no_matches: None,
            size: None,
            on_activate: None,
            on_delete: None,
            on_close: None,
        }
    }
    /// The plain filter: `query` appears in `text`, ignoring case.
    pub fn matches(text: &str, query: &str) -> bool {
        text.to_lowercase().contains(&query.to_lowercase())
    }
    /// The filter line's text while the query is empty (default `common.search`, "Search...",
    /// in the display language).
    pub fn placeholder(mut self, p: impl Into<SharedString>) -> Self {
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
    pub fn detail_el(mut self, e: impl IntoElement) -> Self {
        self.detail = true;
        self.detail_el = Some(e.into_any_element());
        self
    }
    /// The detail pane's width (default 45% of the body).
    pub fn detail_width(mut self, w: f32) -> Self {
        self.detail_width = Some(w);
        self
    }
    /// The empty state's glyph, and its text when the query is empty (nothing to list at all).
    pub fn empty(mut self, glyph: impl Into<SharedString>, text: impl Into<SharedString>) -> Self {
        self.empty_glyph = glyph.into();
        self.empty_text = text.into();
        self
    }
    /// The no-matches text; `{}` is the query (default `common.noMatchesFor`, "No matches for
    /// “{}”", in the display language).
    pub fn no_matches(mut self, t: impl Into<SharedString>) -> Self {
        self.no_matches = Some(t.into());
        self
    }
    /// The overlay's outer size (default `space(480)` × `space(360)`).
    pub fn size(mut self, w: f32, h: f32) -> Self {
        self.size = Some((w, h));
        self
    }
    /// Return on the cursor row, or a click on a row: its index in the items.
    pub fn on_activate(mut self, f: impl Fn(&usize, &mut Window, &mut App) + 'static) -> Self {
        self.on_activate = Some(Rc::new(f));
        self
    }
    /// Delete on the cursor row.
    pub fn on_delete(mut self, f: impl Fn(&usize, &mut Window, &mut App) + 'static) -> Self {
        self.on_delete = Some(Rc::new(f));
        self
    }
    /// Escape on an empty query.
    pub fn on_close(mut self, f: impl Fn(&(), &mut Window, &mut App) + 'static) -> Self {
        self.on_close = Some(Rc::new(f));
        self
    }
}

/// The empty state's overlay look (feedback's EmptyState, `overlay`): the glyph in the menu's
/// selected text at 0.8, the title in `title` at 0.7, `lg` apart, centered.
fn empty_state(t: &Paint, window: &mut Window, g: SharedString, title: SharedString) -> gpui::Div {
    let tk = t.tokens();
    let f = &tk.font;
    div()
        .flex()
        .flex_col()
        .w_full()
        .items_center()
        .gap(px(tk.spacing.lg as f32))
        .child(glyph(t, window, g, f.display_large, color(tk.surfaces.menu.selected_text)).opacity(0.8))
        .child(paragraph(t, title, f.title, t.fg()).w_full().text_center().opacity(0.7))
}

impl RenderOnce for SearchList {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens().clone();
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

        let (input, scroll, query) = {
            let state = self.state.clone();
            state.update(cx, |s, cx| {
                s.cursor.set_count(SEARCH_SECTION, n);
                (s.input.clone(), s.scroll.clone(), s.input.read(cx).text().clone())
            })
        };
        let cursor = {
            let c = &self.state.read(cx).cursor;
            c.cursor_in(SEARCH_SECTION).filter(|_| n > 0)
        };
        let placeholder = self.placeholder.unwrap_or_else(|| crate::Theme::get(cx).string("common.search", "Search..."));
        input.update(cx, |i, _| i.set_placeholder(placeholder));

        // The filter line: heading-sized menu text, the placeholder faint.
        let header = div()
            .flex()
            .flex_row()
            .w_full()
            .h(px(header_h))
            .items_center()
            .font_family(t.family())
            .text_size(px(f.heading as f32))
            .line_height(t.line(f.heading))
            .text_color(color(menu.text))
            .child(div().flex_1().min_w_0().overflow_hidden().child(input.clone()));

        let body: AnyElement = if n == 0 {
            let title: SharedString = if query.is_empty() {
                self.empty_text
            } else {
                // The catalog's line takes the query as `%1`, as Qt's `arg` does; a caller's as `{}`.
                let line = self.no_matches.unwrap_or_else(|| crate::Theme::get(cx).string("common.noMatchesFor", "No matches for “%1”"));
                line.replace("{}", &query).replace("%1", &query).into()
            };
            let (sel_id, sel_title) = (self.id.clone(), title.clone());
            div()
                .flex()
                .w_full()
                .h(px(body_h))
                .items_center()
                .justify_center()
                .debug_selector(move || format!("{sel_id}-empty: {sel_title}"))
                .child(empty_state(&t, window, self.empty_glyph, title))
                .into_any_element()
        } else {
            let gap = sp.xxl as f32;
            let detail_w = self.detail_width.unwrap_or((inner_w * 0.45).floor());
            let list_w = if self.detail { inner_w - detail_w - gap * 2.0 - 1.0 } else { inner_w };
            let mut list = ScrollArea::new(ElementId::NamedChild(std::sync::Arc::new(self.id.clone()), "scroll".into()))
                .track_scroll(&scroll)
                .width(list_w)
                .max_height(body_h)
                .gap(sp.xxs as f32);
            for (i, item) in self.items.iter().enumerate() {
                let mut r = ListRow::new(item.title.clone())
                    .id(ElementId::NamedChild(std::sync::Arc::new(self.id.clone()), format!("row-{i}").into()))
                    .cursor(cursor == Some(i));
                if !item.glyph.is_empty() {
                    r = r.glyph(item.glyph.clone());
                }
                if !item.subtitle.is_empty() {
                    r = r.subtitle(item.subtitle.clone());
                }
                if !item.value.is_empty() {
                    r = r.value(item.value.clone());
                }
                let st = self.state.clone();
                r = r.on_pointer_move(move |p: &Point<Pixels>, _, cx| {
                    st.update(cx, |s, cx| {
                        if s.cursor.pointer_moved(p.x.into(), p.y.into(), SEARCH_SECTION, i) {
                            cx.notify();
                        }
                    })
                });
                let (st, act) = (self.state.clone(), self.on_activate.clone());
                r = r.on_click(move |_, window, cx| {
                    st.update(cx, |s, cx| {
                        s.cursor.set(SEARCH_SECTION, i);
                        cx.notify();
                    });
                    if let Some(f) = &act {
                        f(&i, window, cx);
                    }
                });
                list = list.child(r);
            }
            let mut b = div().flex().flex_row().w_full().h(px(body_h)).items_start().child(list);
            if self.detail {
                let pane = match self.detail_el {
                    Some(e) => e,
                    None => {
                        let item = cursor.map(|i| self.items[i].clone()).unwrap_or_default();
                        div()
                            .flex()
                            .flex_col()
                            .w_full()
                            .pt(px(sp.row_padding_x as f32 / 2.0))
                            .gap(px(sp.sm as f32))
                            .child(label_elided(&t, item.title, f.title, true, color(menu.text)))
                            .child(paragraph(&t, item.detail, f.body, t.dim()).w_full())
                            .into_any_element()
                    }
                };
                b = b
                    .child(div().flex_shrink_0().w(px(gap)))
                    .child(div().flex_shrink_0().w(px(1.0)).h(px(body_h)).bg(t.track()))
                    .child(div().flex_shrink_0().w(px(gap)))
                    .child(div().flex_shrink_0().w(px(detail_w)).h(px(body_h)).overflow_hidden().child(pane));
            }
            b.into_any_element()
        };

        // Every key reaches the list first (capture), and the filter line takes what's left:
        // typed text, Space and Backspace.
        let (st, act, del, close) = (self.state.clone(), self.on_activate, self.on_delete, self.on_close);
        let (inp, scr) = (input.clone(), scroll.clone());
        let card = div()
            .id(self.id)
            .flex()
            .flex_col()
            .flex_shrink_0()
            .w(px(w))
            .h(px(h))
            .p(px(pad))
            .gap(px(sp.md as f32))
            .bg(color(menu.background))
            .rounded(t.radius())
            .capture_key_down(move |ev: &KeyDownEvent, window, cx| {
                let k = &ev.keystroke;
                if k.key == "escape" {
                    if inp.read(cx).text().is_empty() {
                        if let Some(f) = &close {
                            f(&(), window, cx);
                        }
                    } else {
                        inp.update(cx, |i, cx| i.set_text("", cx));
                    }
                    cx.stop_propagation();
                    return;
                }
                let typed = super::printable(k).is_some() || k.key == "space";
                if typed || k.key == "backspace" || k.key == "tab" {
                    return;
                }
                let (key, index, count) = st.update(cx, |s, cx| {
                    let index = s.cursor.index();
                    let key = s.cursor.apply_key(k);
                    if key == CursorKey::Moved {
                        ScrollArea::ensure_visible(&scr, s.cursor.index());
                        cx.notify();
                    }
                    (key, index, n)
                });
                let call = match key {
                    CursorKey::Moved => {
                        cx.stop_propagation();
                        return;
                    }
                    CursorKey::Activate => &act,
                    CursorKey::Delete => &del,
                    CursorKey::Close | CursorKey::Text(_) | CursorKey::Ignored => return,
                };
                cx.stop_propagation();
                if count > 0
                    && let Some(f) = call
                {
                    f(&index, window, cx);
                }
            })
            .child(header)
            .child(body);
        border(card, &menu.border)
    }
}
