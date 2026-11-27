//! Pickers (spec/components/forms.md): MultiSelect (the shell's `MultiSelect.qml`), ColorWell and
//! SwatchStrip, drawn as the Qt kit draws them.

use std::cell::Cell;
use std::rc::Rc;

use gpui::prelude::*;
use gpui::{
    App, Bounds, ClickEvent, Context, ElementId, Entity, FocusHandle, Focusable, Hsla, KeyDownEvent, Pixels, SharedString, Window, anchored,
    canvas, deferred, div, point, px,
};
use omarchy_theme::Rgba;

use super::field::TextChanged;
use super::forms::{check_mark, input_looks};
use super::{Handler, Look, TextInput, border, chrome, focus_handle, id_for, is_activate, keyed_state, label, label_elided, layer, tab_keys};
use crate::{Paint, color};

// ---------------------------------------------------------------------------------- MultiSelect

/// What a MultiSelect keeps between frames: whether it's open, the keyboard row, the search.
struct Picking {
    open: bool,
    /// The row under the keyboard cursor among those the search shows; `None` while the keys
    /// are in the search.
    highlight: Option<usize>,
    search: Entity<TextInput>,
    list: FocusHandle,
    /// The trigger's bounds, so a press on it doesn't count as a press outside the popup.
    trigger: Rc<Cell<Option<Bounds<Pixels>>>>,
    /// `default_open` applied (once).
    started: bool,
}

impl Picking {
    fn new(cx: &mut Context<Picking>) -> Picking {
        let search = TextInput::new(cx, "");
        cx.observe(&search, |_, _, cx| cx.notify()).detach();
        cx.subscribe(&search, |p: &mut Picking, _, _: &TextChanged, cx| {
            p.highlight = None;
            cx.notify();
        })
        .detach();
        Picking { open: false, highlight: None, search, list: cx.focus_handle(), trigger: Rc::default(), started: false }
    }
}

/// Open, with the search cleared, or closed (clearing it too, as Qt's popup does).
fn set_open(state: &Entity<Picking>, open: bool, cx: &mut App) {
    let search = state.update(cx, |p, cx| {
        p.open = open;
        p.highlight = None;
        cx.notify();
        p.search.clone()
    });
    search.update(cx, |s, cx| {
        if !s.text().is_empty() {
            s.set_text("", cx)
        }
    });
}

/// A searchable choice of any number of options: the Dropdown trigger (reading the chosen labels,
/// or "N selected" past three), and below it while open a popup with a search field and a
/// checkbox row per option. The app owns the chosen `values`: `on_choose` gets the value of a row
/// toggled, to add or remove.
///
/// Keys, as Qt's: on the trigger j, Down, Return or Space opens it and focuses the search; in the
/// search typing filters, Return toggles the first match and Down goes into the list; in the list
/// j/k and Down/Up walk (Up off the first row goes back to the search), Return or Space toggles;
/// Escape closes.
#[derive(IntoElement)]
pub struct MultiSelect {
    id: ElementId,
    label: SharedString,
    options: Vec<(SharedString, SharedString)>,
    values: Vec<SharedString>,
    default_open: bool,
    cursor: bool,
    width: Option<f32>,
    texts: Option<(SharedString, SharedString, SharedString)>,
    on_choose: Option<Handler<SharedString>>,
}

impl MultiSelect {
    /// `options` are (value, label) pairs; `values` the chosen ones.
    pub fn new<V: Into<SharedString>, L: Into<SharedString>, S: Into<SharedString>>(
        id: impl Into<ElementId>,
        options: impl IntoIterator<Item = (V, L)>,
        values: impl IntoIterator<Item = S>,
    ) -> MultiSelect {
        MultiSelect {
            id: id.into(),
            label: SharedString::default(),
            options: options.into_iter().map(|(v, l)| (v.into(), l.into())).collect(),
            values: values.into_iter().map(Into::into).collect(),
            default_open: false,
            cursor: false,
            width: None,
            texts: None,
            on_choose: None,
        }
    }
    pub fn label(mut self, label: impl Into<SharedString>) -> Self {
        self.label = label.into();
        self
    }
    /// Open the popup when it first shows (focusing its search, as Qt's `open()`); after that it
    /// opens and closes itself.
    pub fn default_open(mut self, open: bool) -> Self {
        self.default_open = open;
        self
    }
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    /// Width; `searchableDropdownWidth` by default.
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    /// The search placeholder, the trigger's text with nothing chosen, and the popup's text with
    /// nothing to show (by default the catalog's `common.search`, `common.noneSelected` and
    /// `common.noOptions`, in the display language).
    pub fn texts(mut self, placeholder: impl Into<SharedString>, none_selected: impl Into<SharedString>, no_options: impl Into<SharedString>) -> Self {
        self.texts = Some((placeholder.into(), none_selected.into(), no_options.into()));
        self
    }
    /// A row toggled (pressed, or Return/Space from the keys): called with its value.
    pub fn on_choose(mut self, f: impl Fn(&SharedString, &mut Window, &mut App) + 'static) -> Self {
        self.on_choose = Some(Rc::new(f));
        self
    }

    fn selection_label(&self, cx: &App) -> SharedString {
        if self.values.is_empty() {
            return SharedString::default();
        }
        if self.values.len() > 3 {
            return crate::Theme::get(cx)
                .string("multiSelect.selectedCount", "%1 selected")
                .replace("%1", &self.values.len().to_string())
                .into();
        }
        let label = |v: &SharedString| self.options.iter().find(|(o, _)| o == v).map_or_else(|| v.clone(), |(_, l)| l.clone());
        self.values.iter().map(label).collect::<Vec<_>>().join(", ").into()
    }
}

impl RenderOnce for MultiSelect {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let c = &tk.controls;
        let pop = &tk.surfaces.popups;
        let theme = crate::Theme::get(cx);
        let (placeholder, none_text, empty_text) = self.texts.clone().unwrap_or_else(|| {
            (
                theme.string("common.search", "Search..."),
                theme.string("common.noneSelected", "None selected"),
                theme.string("common.noOptions", "No options"),
            )
        });
        let focus = focus_handle(&self.id, window, cx);
        let state = window.use_keyed_state(ElementId::NamedChild(std::sync::Arc::new(self.id.clone()), "picking".into()), cx, |_, cx| Picking::new(cx));
        if !state.read(cx).started {
            let open = self.default_open;
            let search = state.update(cx, |p, _| {
                p.started = true;
                p.open = open;
                p.search.clone()
            });
            if open {
                let f = search.read(cx).focus_handle(cx);
                window.defer(cx, move |window, cx| f.focus(window, cx));
            }
        }
        let (open, highlight, search, list_focus, trigger_bounds) = {
            let p = state.read(cx);
            (p.open, p.highlight, p.search.clone(), p.list.clone(), p.trigger.clone())
        };
        let query = search.read(cx).text().to_lowercase();
        let shown: Rc<Vec<(SharedString, SharedString)>> = Rc::new(
            self.options
                .iter()
                .filter(|(v, l)| query.is_empty() || l.to_lowercase().contains(&query) || v.to_lowercase().contains(&query))
                .cloned()
                .collect(),
        );

        let (rest, hot) = input_looks(&t);
        let width = self.width.unwrap_or(sp.searchable_dropdown_width as f32);
        let bw = c.normal.border.max_width() as f32;
        let chosen = self.selection_label(cx);
        let (words, words_ink) = if chosen.is_empty() { (none_text, t.dim()) } else { (chosen, color(pop.text)) };
        let base = div()
            .id(self.id.clone())
            .flex()
            .flex_row()
            .flex_shrink_0()
            .w(px(width))
            .h(px(sp.control_height as f32))
            .items_center()
            .gap(px(sp.md as f32))
            .pl(px(sp.control_padding_x as f32 + bw))
            .pr(px(sp.control_gap as f32 + bw));
        let st = state.clone();
        let st_keys = state.clone();
        let tb = trigger_bounds.clone();
        let search_for_keys = search.clone();
        let trigger = chrome(base, &t, &rest, &hot, self.cursor || focus.is_focused(window), t.radius())
            .child(label_elided(&t, words, tk.font.body, false, words_ink).flex_1())
            .child(label(&t, "\u{f0140}", tk.font.body, false, t.dim()))
            .child(canvas(move |b, _, _| tb.set(Some(b)), |_, _, _, _| {}).absolute().size_full())
            .track_focus(&focus)
            .on_click(move |_, window, cx| {
                let open = !st.read(cx).open;
                set_open(&st, open, cx);
                if open {
                    let f = st.read(cx).search.read(cx).focus_handle(cx);
                    f.focus(window, cx);
                }
            })
            .on_key_down(move |ev: &KeyDownEvent, window, cx| {
                if tab_keys(ev, window, cx) {
                    cx.stop_propagation();
                    return;
                }
                let is_open = st_keys.read(cx).open;
                match (is_open, ev.keystroke.key.as_str()) {
                    (false, "enter" | "kp_enter" | "space" | "down" | "j") => {
                        set_open(&st_keys, true, cx);
                        let f = search_for_keys.read(cx).focus_handle(cx);
                        f.focus(window, cx);
                    }
                    (true, "escape") => set_open(&st_keys, false, cx),
                    _ => return,
                }
                cx.stop_propagation();
            });

        let mut out = div().relative().flex().flex_col().flex_shrink_0().w(px(width)).gap(px(sp.label_gap as f32));
        if !self.label.is_empty() {
            out = out.child(label(&t, self.label.clone(), tk.font.caption, true, t.dim()));
        }
        out = out.child(trigger);
        if !open {
            return out;
        }

        // The popup: its rows (at most six) plus the search header, rule and vertical padding.
        let row_h = sp.popup_row_height as f32;
        let gap = sp.label_gap as f32;
        let line = c.normal.border.max_width().max(1.0);
        let hair = sp.hairline as f32;
        let header_h = row_h + sp.control_padding_x as f32;
        let chrome_h = header_h + 1.0 + (line as f32 + hair) * 2.0;
        let n = shown.len() as f32;
        let content = if shown.is_empty() { row_h } else { n * row_h + (n - 1.0) * gap };
        let height = (content + chrome_h).min(row_h * 6.0 + 5.0 * gap + chrome_h);
        let md = sp.md as f32;
        let fbw = c.normal.border.max_width().max(c.hover.border.max_width()) as f32;

        // The search field: TextField's chrome at the header's height; it holds the keys while
        // open, so it wears the focus look.
        let search_base = div()
            .id(ElementId::NamedChild(std::sync::Arc::new(self.id.clone()), "search".into()))
            .flex()
            .flex_row()
            .items_center()
            .w_full()
            .h(px(header_h - 2.0 * md))
            .px(px(sp.control_padding_x as f32 + fbw))
            .font_family(t.family())
            .text_size(px(tk.font.body as f32))
            .line_height(t.line(tk.font.body))
            .text_color(color(pop.text));
        search.update(cx, |s, _| s.set_placeholder(placeholder));
        let (st_search, shown_search, choose_search, list_for_search) = (state.clone(), shown.clone(), self.on_choose.clone(), list_focus.clone());
        let field = chrome(search_base, &t, &rest, &hot, true, t.radius())
            .child(div().flex_1().min_w_0().overflow_hidden().child(search.clone()))
            .capture_key_down(move |ev: &KeyDownEvent, window, cx| {
                match ev.keystroke.key.as_str() {
                    "escape" => {
                        set_open(&st_search, false, cx);
                    }
                    "down" => {
                        if !shown_search.is_empty() {
                            st_search.update(cx, |p, cx| {
                                p.highlight = Some(0);
                                cx.notify();
                            });
                            list_for_search.focus(window, cx);
                        }
                    }
                    "enter" | "kp_enter" => {
                        if let (Some((v, _)), Some(f)) = (shown_search.first(), &choose_search) {
                            f(v, window, cx);
                        }
                    }
                    _ => return,
                }
                cx.stop_propagation();
            });
        let header = div().w_full().h(px(header_h)).flex_shrink_0().p(px(md)).child(field);
        let rule = div().w_full().h(px(1.0)).flex_shrink_0().bg(color(pop.text.with_alpha(0.10)));

        let mut list = div()
            .id(ElementId::NamedChild(std::sync::Arc::new(self.id.clone()), "list".into()))
            .flex()
            .flex_col()
            .w_full()
            .flex_1()
            .min_h_0()
            .gap(px(gap))
            .overflow_y_scroll()
            .track_focus(&list_focus);
        if shown.is_empty() {
            list = list.items_center().justify_center().child(label(&t, empty_text, tk.font.body, false, t.faint()));
        }
        let box_size = t.space(16.0);
        let box_radius = t.radius_small();
        for (row_index, (value, text)) in shown.iter().enumerate() {
            let selected = self.values.contains(value);
            let look = if selected {
                Look::new(c.selected.fill, c.selected.border.clone())
            } else {
                Look::new(Rgba::TRANSPARENT, c.normal.border.clone())
            };
            let mut check = div().relative().flex().flex_shrink_0().items_center().justify_center().size(px(box_size)).child(layer(&look, box_radius));
            if selected {
                check = check.child(check_mark(&t, box_size, color(c.selected.text)));
            }
            let lit = highlight == Some(row_index);
            let ink = if lit { color(c.hover.text) } else { color(pop.text) };
            let mut r = div()
                .id(ElementId::NamedChild(std::sync::Arc::new(self.id.clone()), value.to_string().into()))
                .flex()
                .flex_row()
                .flex_shrink_0()
                .w_full()
                .h(px(row_h))
                .items_center()
                .gap(px(sp.row_gap as f32))
                .px(px(sp.control_padding_x as f32))
                .rounded(t.radius_small())
                .cursor_pointer()
                .when(lit, |r| r.bg(color(c.hover.fill)))
                .child(check)
                .child(label_elided(&t, text.clone(), tk.font.body, false, ink).flex_1());
            let st = state.clone();
            r = r.on_mouse_move(move |_, _, cx| {
                if st.read(cx).highlight != Some(row_index) {
                    st.update(cx, |p, cx| {
                        p.highlight = Some(row_index);
                        cx.notify();
                    });
                }
            });
            if let Some(f) = self.on_choose.clone() {
                let v = value.clone();
                r = r.on_click(move |_: &ClickEvent, window, cx| f(&v, window, cx));
            }
            list = list.child(r);
        }
        let (st_list, shown_list, choose_list, search_for_list) = (state.clone(), shown.clone(), self.on_choose.clone(), search.clone());
        let list = list.on_key_down(move |ev: &KeyDownEvent, window, cx| {
            let at = st_list.read(cx).highlight;
            let last = shown_list.len().saturating_sub(1);
            let set = |hl: Option<usize>, cx: &mut App| {
                st_list.update(cx, |p, cx| {
                    p.highlight = hl;
                    cx.notify();
                })
            };
            match ev.keystroke.key.as_str() {
                "escape" => set_open(&st_list, false, cx),
                "down" | "j" => set(Some(at.map_or(0, |i| (i + 1).min(last))), cx),
                "up" | "k" => match at {
                    Some(i) if i > 0 => set(Some(i - 1), cx),
                    _ => {
                        set(None, cx);
                        let f = search_for_list.read(cx).focus_handle(cx);
                        f.focus(window, cx);
                    }
                },
                "enter" | "kp_enter" | "space" => {
                    if let (Some(i), Some(f)) = (at, &choose_list)
                        && let Some((v, _)) = shown_list.get(i)
                    {
                        f(v, window, cx);
                    }
                }
                _ => return,
            }
            cx.stop_propagation();
        });

        let mut p = div()
            .id(ElementId::NamedChild(std::sync::Arc::new(self.id.clone()), "popup".into()))
            .occlude()
            .flex()
            .flex_col()
            .w(px(width))
            .h(px(height))
            .p(px(hair))
            .bg(color(pop.background))
            .rounded(t.radius());
        p = border(p, &pop.border.with_width(line));
        let st = state.clone();
        let p = p.child(header).child(rule).child(list).on_mouse_down_out(move |ev, _, cx| {
            if trigger_bounds.get().is_some_and(|b| b.contains(&ev.position)) {
                return;
            }
            set_open(&st, false, cx);
        });
        let below = px(sp.xxs as f32);
        out.child(div().absolute().left_0().bottom_0().child(deferred(anchored().offset(point(px(0.), below)).child(p)).with_priority(1)))
    }
}

// ----------------------------------------------------------------------------------- ColorWell

/// A button showing a color, for opening the app's picker: the swatch inset in a bordered well.
/// The cursor (pointer, focus, `cursor`) is a ring outside the well, as ToggleSwitch draws it.
/// Space or Return presses it when focused.
#[derive(IntoElement)]
pub struct ColorWell {
    id: ElementId,
    swatch: Hsla,
    cursor: bool,
    on_click: Option<Handler<ClickEvent>>,
}

impl ColorWell {
    pub fn new(swatch: impl Into<Hsla>) -> ColorWell {
        let swatch = swatch.into();
        ColorWell { id: id_for("color-well", &format!("{swatch:?}")), swatch, cursor: false, on_click: None }
    }
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
        self
    }
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    pub fn on_click(mut self, f: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static) -> Self {
        self.on_click = Some(Rc::new(f));
        self
    }
}

impl RenderOnce for ColorWell {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let c = &tk.controls;
        let sp = &tk.spacing;
        let (w, h) = (t.space(44.0), t.space(26.0));
        let pad = sp.sm as f32;
        let r = t.radius_small();
        let r_f = f32::from(r);
        let ring_r = if r_f > 0.0 { (tk.radius as f32).min(r_f + pad) } else { 0.0 };
        let xxs = sp.xxs as f32;
        let inset = c.normal.border.widths[0] as f32 + xxs;
        let focus = focus_handle(&self.id, window, cx);
        let hot_now = self.cursor || focus.is_focused(window);
        let ring = layer(&Look::new(Rgba::TRANSPARENT, c.hover.border.clone()), px(ring_r));
        let ring = if hot_now { ring } else { ring.opacity(0.0).group_hover(super::GROUP, |s| s.opacity(1.0)) };
        let well = div()
            .absolute()
            .left(px(pad))
            .top(px(pad))
            .w(px(w))
            .h(px(h))
            .child(layer(&Look::new(c.normal.fill, c.normal.border.clone()), r))
            .child(
                div()
                    .absolute()
                    .left(px(inset))
                    .top(px(inset))
                    .w(px(w - 2.0 * inset))
                    .h(px(h - 2.0 * inset))
                    .rounded(px((r_f - inset).max(0.0)))
                    .bg(self.swatch),
            );
        let e = div()
            .id(self.id)
            .relative()
            .flex_shrink_0()
            .w(px(w + pad * 2.0))
            .h(px(h + pad * 2.0))
            .group(super::GROUP)
            .cursor_pointer()
            .child(ring)
            .child(well);
        super::button::pressable(e, &focus, self.on_click)
    }
}

// --------------------------------------------------------------------------------- SwatchStrip

const SWATCH: &str = "omarchy-swatch";

/// A row of preset colors, one chosen. The chosen swatch wears an accent ring; the keyboard
/// cursor and the pointer the hover-cursor border in the same place. Left/Right (h/l) walk the
/// cursor, Space or Return pick, when focused.
#[derive(IntoElement)]
pub struct SwatchStrip {
    id: ElementId,
    colors: Vec<Hsla>,
    current: Option<usize>,
    cursor: Option<usize>,
    on_pick: Option<Handler<usize>>,
}

impl SwatchStrip {
    pub fn new(colors: impl IntoIterator<Item = Hsla>, current: Option<usize>) -> SwatchStrip {
        let colors: Vec<Hsla> = colors.into_iter().collect();
        SwatchStrip { id: id_for("swatches", &colors.len().to_string()), colors, current, cursor: None, on_pick: None }
    }
    pub fn id(mut self, id: impl Into<ElementId>) -> Self {
        self.id = id.into();
        self
    }
    /// Force the cursor ring on one swatch.
    pub fn cursor(mut self, index: Option<usize>) -> Self {
        self.cursor = index;
        self
    }
    /// Called with the picked swatch's index.
    pub fn on_pick(mut self, f: impl Fn(&usize, &mut Window, &mut App) + 'static) -> Self {
        self.on_pick = Some(Rc::new(f));
        self
    }
}

impl RenderOnce for SwatchStrip {
    fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement {
        let t = Paint::of(cx);
        let tk = t.tokens();
        let sp = &tk.spacing;
        let size = t.space(22.0);
        let ring_w = t.space(2.0).max(1.0);
        let gap = sp.xxs as f32;
        let cell = size + (gap + ring_w) * 2.0;
        let r = f32::from(t.radius_small());
        let ring_r = if r > 0.0 { r + gap + ring_w } else { 0.0 };
        let focus = focus_handle(&self.id, window, cx);
        let keyed = keyed_state(&self.id, "swatch-cursor", window, cx, || None::<usize>);
        let cursor = self.cursor.or(if focus.is_focused(window) { *keyed.read(cx) } else { None });
        let hover = color(tk.controls.hover.border.color);
        let mut strip = div().id(self.id.clone()).flex().flex_row().flex_shrink_0().gap(px(gap)).track_focus(&focus);
        for (i, swatch) in self.colors.iter().enumerate() {
            let chosen = self.current == Some(i);
            let ring = div().absolute().top_0().left_0().size_full().rounded(px(ring_r)).border(px(ring_w));
            let ring = if chosen {
                ring.border_color(t.accent())
            } else if cursor == Some(i) {
                ring.border_color(hover)
            } else {
                ring.border_color(hover).opacity(0.0).group_hover(SWATCH, |s| s.opacity(1.0))
            };
            let chip = div().size(px(size)).rounded(px(r)).bg(*swatch).border_1().border_color(t.track());
            let mut e = div()
                .id(i)
                .relative()
                .flex()
                .flex_shrink_0()
                .items_center()
                .justify_center()
                .size(px(cell))
                .group(SWATCH)
                .cursor_pointer()
                .child(ring)
                .child(chip);
            let (k, f) = (keyed.clone(), self.on_pick.clone());
            e = e.on_click(move |_, window, cx| {
                k.update(cx, |c, cx| {
                    *c = Some(i);
                    cx.notify();
                });
                if let Some(f) = &f {
                    f(&i, window, cx);
                }
            });
            strip = strip.child(e);
        }
        let n = self.colors.len();
        let current = self.current;
        let f = self.on_pick;
        strip.on_key_down(move |ev: &KeyDownEvent, window, cx| {
            if tab_keys(ev, window, cx) {
                cx.stop_propagation();
                return;
            }
            if n == 0 {
                return;
            }
            let from = keyed.read(cx).unwrap_or(current.unwrap_or(0));
            let key = ev.keystroke.key.as_str();
            let to = match key {
                "right" | "l" => (from + 1).min(n - 1),
                "left" | "h" => from.saturating_sub(1),
                _ if is_activate(ev) => {
                    if let Some(f) = &f {
                        f(&from, window, cx);
                    }
                    cx.stop_propagation();
                    return;
                }
                _ => return,
            };
            keyed.update(cx, |c, cx| {
                *c = Some(to);
                cx.notify();
            });
            cx.stop_propagation();
        })
    }
}
