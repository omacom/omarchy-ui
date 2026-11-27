//! Pickers: MultiSelect (the shell's `MultiSelect.qml`), ColorWell and SwatchStrip, drawn as
//! the Qt binding draws them.

use std::any::Any;
use std::rc::Rc;

use omarchy_theme::Rgba;
use rune::{Align, Color, El, Key, KeyEvent, Side, TextEdit, Tx, col, el, row, text};

use super::forms::{bold_mark, input_looks, layer, bare_input, state_layers};
use super::{Attach, Look, glyph};
use crate::{Theme, border, color};

// ---------------------------------------------------------------------------------- MultiSelect

/// A searchable choice of any number of options: the Dropdown trigger (reading the chosen
/// labels, or "N selected" past three), and while `open` a popup below it with a search field
/// and a checkbox row per option. The popup goes in the overlay `layer`, as Dropdown's list does.
///
/// The app owns `open`, the search text and the highlighted row, as Qt's MultiSelect keeps them.
/// With [`MultiSelect::on_state`] the keys work as Qt's do: on the trigger, j, Down, Return or
/// Space opens it; in the search (`highlighted` is `None`) typing filters, Return toggles the
/// first match and Down goes into the list; in the list j/k and Down/Up walk (Up off the first
/// row goes back to the search) and Return or Space toggles; Escape closes. The trigger keeps
/// keyboard focus, so typing there lands in the search.
pub struct MultiSelect {
    key: String,
    label: String,
    options: Vec<(String, String)>,
    values: Vec<String>,
    open: bool,
    cursor: bool,
    highlighted: Option<usize>,
    width: Option<f32>,
    search: Option<TextEdit>,
    search_access: Option<Box<dyn FnOnce(El) -> El>>,
    /// `None`: the catalog's `common.search`, `common.noneSelected` and `common.noOptions`.
    texts: Option<(String, String, String)>,
    toggle: Option<Attach>,
    choose: Option<Box<dyn Fn(El, String) -> El>>,
    chooser: Option<Chooser>,
    wire: Option<Box<dyn Wire>>,
}

/// `on_choose`'s handler, callable from a key handler typed for the same state.
type Chooser = Rc<dyn Fn(&mut dyn Any, &str)>;

/// Wires the trigger and the search field to the app's open flag, search and highlighted row,
/// typed when `on_state` was called.
trait Wire {
    fn trigger(&self, e: El, shown: Rc<Vec<String>>, choose: Option<Chooser>) -> El;
    fn input(&self, e: El, shown: Rc<Vec<String>>, choose: Option<Chooser>) -> El;
}

type Access<S> = Rc<dyn for<'a> Fn(&'a mut S) -> (&'a mut bool, &'a mut TextEdit, &'a mut Option<usize>)>;

struct Wiring<S> {
    access: Access<S>,
}

/// Closed, with the search cleared and nothing highlighted, as Qt's popup closes.
fn close(open: &mut bool, search: &mut TextEdit, highlighted: &mut Option<usize>) {
    *open = false;
    search.clear();
    *highlighted = None;
}

/// One key on the trigger (`in_field` false) or the search field: whether it was taken, and the
/// value to toggle.
fn multiselect_key(
    (open, search, highlighted): (&mut bool, &mut TextEdit, &mut Option<usize>),
    k: &KeyEvent,
    shown: &[String],
    in_field: bool,
) -> (bool, Option<String>) {
    let typed = match &k.key {
        Key::Text(t) if !k.mods.ctrl && !k.mods.alt && !k.mods.logo && !k.mods.cmd => Some(t.as_str()),
        _ => None,
    };
    if !*open {
        if matches!(k.key, Key::Down | Key::Enter) || matches!(typed, Some("j" | " ")) {
            *open = true;
            search.clear();
            *highlighted = None;
            return (true, None);
        }
        return (false, None);
    }
    if k.key == Key::Escape {
        close(open, search, highlighted);
        return (true, None);
    }
    let last = shown.len().checked_sub(1);
    match *highlighted {
        // In the search.
        None => match (&k.key, typed) {
            (Key::Down, _) => {
                *highlighted = last.map(|_| 0);
                (true, None)
            }
            (Key::Enter, _) => (true, shown.first().cloned()),
            (Key::Backspace, _) if !in_field => {
                let mut t = search.text.clone();
                t.pop();
                search.replace_text(t);
                (true, None)
            }
            (_, Some(t)) if !in_field => {
                search.set_caret(search.text.len());
                search.insert(t);
                (true, None)
            }
            _ => (false, None),
        },
        // In the list.
        Some(i) => match (&k.key, typed) {
            (Key::Down, _) | (_, Some("j")) => {
                *highlighted = Some(last.map_or(0, |l| (i + 1).min(l)));
                (true, None)
            }
            (Key::Up, _) | (_, Some("k")) => {
                *highlighted = i.checked_sub(1);
                (true, None)
            }
            (Key::Enter, _) | (_, Some(" ")) => (true, shown.get(i).cloned()),
            _ => (false, None),
        },
    }
}

impl<S: 'static> Wire for Wiring<S> {
    fn trigger(&self, e: El, shown: Rc<Vec<String>>, choose: Option<Chooser>) -> El {
        let (press, keys) = (self.access.clone(), self.access.clone());
        e.on_press(move |s: &mut S| {
            let (open, search, highlighted) = press(s);
            if *open {
                close(open, search, highlighted);
            } else {
                *open = true;
                *highlighted = None;
            }
        })
        .on_key(move |s: &mut S, k: &KeyEvent, _: &mut Tx| {
            let (taken, pick) = multiselect_key(keys(s), k, &shown, false);
            if let (Some(v), Some(f)) = (pick, &choose) {
                f(s, &v);
            }
            taken
        })
    }

    fn input(&self, e: El, shown: Rc<Vec<String>>, choose: Option<Chooser>) -> El {
        let (edit, change, keys) = (self.access.clone(), self.access.clone(), self.access.clone());
        e.on_edit(move |s: &mut S| edit(s).1)
            .on_input_change(move |s: &mut S, _| *change(s).2 = None)
            .on_input_key(move |s: &mut S, k: &KeyEvent, _: &mut Tx| {
                let (taken, pick) = multiselect_key(keys(s), k, &shown, true);
                if let (Some(v), Some(f)) = (pick, &choose) {
                    f(s, &v);
                }
                taken
            })
    }
}

impl MultiSelect {
    /// `key` names the trigger (the popup anchors to it); `options` are (value, label) pairs;
    /// `values` the chosen ones.
    pub fn new<V: Into<String>, L: Into<String>, S: Into<String>>(
        key: impl Into<String>,
        options: impl IntoIterator<Item = (V, L)>,
        values: impl IntoIterator<Item = S>,
    ) -> MultiSelect {
        MultiSelect {
            key: key.into(),
            label: String::new(),
            options: options.into_iter().map(|(v, l)| (v.into(), l.into())).collect(),
            values: values.into_iter().map(Into::into).collect(),
            open: false,
            cursor: false,
            highlighted: None,
            width: None,
            search: None,
            search_access: None,
            texts: None,
            toggle: None,
            choose: None,
            chooser: None,
            wire: None,
        }
    }
    pub fn label(mut self, label: impl Into<String>) -> Self {
        self.label = label.into();
        self
    }
    pub fn open(mut self, open: bool) -> Self {
        self.open = open;
        self
    }
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    /// The popup row under the keyboard cursor, counted among the rows the search shows; `None`
    /// while the keys are in the search.
    pub fn highlighted(mut self, index: Option<usize>) -> Self {
        self.highlighted = index;
        self
    }
    /// Width; `searchableDropdownWidth` by default.
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    /// The search field's text; options whose label or value contain it (ignoring case) show.
    pub fn search(mut self, edit: &TextEdit) -> Self {
        self.search = Some(edit.clone());
        self
    }
    pub fn on_search<S: 'static>(mut self, f: impl for<'a> Fn(&'a mut S) -> &'a mut TextEdit + 'static) -> Self {
        self.search_access = Some(Box::new(move |e: El| e.on_edit(f)));
        self
    }
    /// The search placeholder, the trigger's text with nothing chosen, and the popup's text
    /// with nothing to show (by default the catalog's, in the display language, as Qt's).
    pub fn texts(mut self, placeholder: impl Into<String>, none_selected: impl Into<String>, no_options: impl Into<String>) -> Self {
        self.texts = Some((placeholder.into(), none_selected.into(), no_options.into()));
        self
    }
    pub fn on_toggle<S: 'static>(mut self, f: impl Fn(&mut S) + 'static) -> Self {
        self.toggle = Some(Box::new(move |e: El| e.on_press(f)));
        self
    }
    /// A row pressed (or toggled from the keys): `f` gets its value, to add or remove from the
    /// chosen ones.
    pub fn on_choose<S: 'static>(mut self, f: impl Fn(&mut S, &str) + Clone + 'static) -> Self {
        let g = f.clone();
        self.chooser = Some(Rc::new(move |s: &mut dyn Any, v: &str| {
            if let Some(s) = s.downcast_mut::<S>() {
                g(s, v);
            }
        }));
        self.choose = Some(Box::new(move |e: El, v: String| {
            let f = f.clone();
            e.on_press(move |s: &mut S| f(s, &v))
        }));
        self
    }
    /// Where the app keeps `open`, the search and the highlighted row, so the trigger and the
    /// keys open, filter, walk and close it (in place of `on_toggle` and `on_search`). Pass the
    /// same values to `open`, `search` and `highlighted`.
    pub fn on_state<S: 'static>(
        mut self,
        f: impl for<'a> Fn(&'a mut S) -> (&'a mut bool, &'a mut TextEdit, &'a mut Option<usize>) + 'static,
    ) -> Self {
        self.wire = Some(Box::new(Wiring::<S> { access: Rc::new(f) }));
        self
    }

    /// The values of the options the search shows, in order.
    fn shown_values(&self) -> Vec<String> {
        let query = self.search.as_ref().map(|e| e.text.to_lowercase()).unwrap_or_default();
        self.options
            .iter()
            .filter(|(v, l)| query.is_empty() || l.to_lowercase().contains(&query) || v.to_lowercase().contains(&query))
            .map(|(v, _)| v.clone())
            .collect()
    }

    fn selection_label(&self, t: &Theme) -> String {
        if self.values.is_empty() {
            return String::new();
        }
        if self.values.len() > 3 {
            return t.tokens().string("multiSelect.selectedCount", "%1 selected").replace("%1", &self.values.len().to_string());
        }
        let label = |v: &String| self.options.iter().find(|(o, _)| o == v).map_or_else(|| v.clone(), |(_, l)| l.clone());
        self.values.iter().map(label).collect::<Vec<_>>().join(", ")
    }

    pub fn build(mut self, t: &Theme, layer: &mut Vec<El>) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let c = &tk.controls;
        let pop = &tk.surfaces.popups;
        let ink = color(pop.text);
        let (rest, hot) = input_looks(t);
        let width = self.width.unwrap_or(sp.searchable_dropdown_width as f32);
        let bw = c.normal.border.max_width() as f32;

        let chosen = self.selection_label(t);
        let (placeholder, none_text, empty_text) = self.texts.take().unwrap_or_else(|| {
            (
                tk.string("common.search", "Search..."),
                tk.string("common.noneSelected", "None selected"),
                tk.string("common.noOptions", "No options"),
            )
        });
        let (words, words_ink) =
            if chosen.is_empty() { (none_text, t.dim()) } else { (chosen, ink) };
        let words = text(words).font(t.text_style(tk.font.body)).color(words_ink).single_line().truncate().grow(1.0).min_w(0.0);
        let chevron = glyph(t, "\u{f0140}", tk.font.body, t.dim());
        let base = row()
            .key(self.key.clone())
            .w(width)
            .h(sp.control_height as f32)
            .items_center()
            .gap(sp.md as f32)
            .pl(sp.control_padding_x as f32 + bw)
            .pr(sp.control_gap as f32 + bw)
            .role("combobox")
            .label(self.label.clone())
            .expanded(self.open)
            .focusable(true);
        let mut trigger = super::chrome(t, base, vec![words, chevron], &rest, &hot, self.cursor, t.radius());
        let shown = Rc::new(self.shown_values());
        if let Some(wire) = &self.wire {
            trigger = wire.trigger(trigger, shown.clone(), self.chooser.clone());
        } else if let Some(attach) = self.toggle.take() {
            trigger = attach(trigger);
        }

        if self.open {
            let access = self.search_access.take();
            layer.push(self.popup(t, width, access, placeholder, empty_text).anchored(self.key.clone(), Side::Bottom, Align::Start, sp.xxs as f32));
        }

        let mut out = col().gap(sp.label_gap as f32);
        if !self.label.is_empty() {
            out = out.child(text(self.label.clone()).font(t.text_style_bold(tk.font.caption)).color(t.dim()));
        }
        out.child(trigger)
    }

    fn popup(&self, t: &Theme, width: f32, access: Option<Box<dyn FnOnce(El) -> El>>, placeholder: String, empty_text: String) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let c = &tk.controls;
        let pop = &tk.surfaces.popups;
        let ink = color(pop.text);
        let row_h = sp.popup_row_height as f32;
        let gap = sp.label_gap as f32;

        let query = self.search.as_ref().map(|e| e.text.to_lowercase()).unwrap_or_default();
        let shown: Vec<(usize, &(String, String))> = self
            .options
            .iter()
            .enumerate()
            .filter(|(_, (v, l))| query.is_empty() || l.to_lowercase().contains(&query) || v.to_lowercase().contains(&query))
            .collect();

        let pop_border = pop.border.with_width(c.normal.border.max_width().max(1.0));
        let pad = pop_border.max_width() as f32 + sp.hairline as f32;
        let header_h = row_h + sp.control_padding_x as f32;
        let chrome_h = header_h + 1.0 + pad * 2.0;
        let n = shown.len() as f32;
        let content = n * row_h + (n - 1.0).max(0.0) * gap;
        let max_rows = row_h * 6.0 + 5.0 * gap;
        let height = content.min(max_rows) + chrome_h;

        // The search field: TextField's chrome at the header's height.
        let (rest, hot) = input_looks(t);
        let fbw = c.normal.border.max_width().max(c.hover.border.max_width()) as f32;
        let edit = self.search.clone().unwrap_or_else(|| TextEdit::new(""));
        let mut input = bare_input(t, &edit, placeholder);
        if let Some(wire) = &self.wire {
            input = wire.input(input, Rc::new(self.shown_values()), self.chooser.clone());
        } else if let Some(access) = access {
            input = access(input);
        }
        let field_base = row()
            .items_center()
            .w_full()
            .h(header_h - 2.0 * sp.md as f32)
            .px(sp.control_padding_x as f32 + fbw)
            .role("textbox")
            .hover_group_focus()
            .radius(t.radius());
        // The search field holds focus while the popup is open, so it wears the focus look.
        let field = field_base.children(state_layers(&rest, &hot, true, t.radius())).child(input);
        let header = col().w_full().h(header_h).padding(sp.md as f32).child(field);
        let rule = el().w_full().h(1.0).fill(color(pop.text.with_alpha(0.10)));

        let list_h = height - 2.0 * pad - header_h - 1.0;
        let mut list = col().w_full().h(list_h.max(0.0)).gap(gap).clip_rounded(0.0);
        if shown.is_empty() {
            list = list.center().child(text(empty_text).font(t.text_style(tk.font.body)).color(t.faint()));
        }
        let box_size = t.space(16.0);
        let box_radius = t.radius_small();
        for (row_index, (_, (value, label))) in shown.into_iter().enumerate() {
            let selected = self.values.iter().any(|v| v == value);
            let look = if selected {
                Look::new(c.selected.fill, c.selected.border.clone())
            } else {
                Look::new(Rgba::TRANSPARENT, c.normal.border.clone())
            };
            let mut check = el().size(box_size, box_size).center().child(layer(&look, box_radius));
            if selected {
                check = check.child(bold_mark(t, "\u{2713}", (box_size * 0.85).round() as i64, color(c.selected.text)));
            }
            let lit = self.highlighted == Some(row_index);
            let r = row()
                .w_full()
                .h(row_h)
                .items_center()
                .gap(sp.row_gap as f32)
                .px(sp.control_padding_x as f32)
                .radius(t.radius_small())
                .fill(if lit { color(c.hover.fill) } else { Color::TRANSPARENT })
                .key(format!("{}-{value}", self.key))
                .role("menuitemcheckbox")
                .label(label.clone())
                .checked(selected)
                .child(check)
                .child(text(label.clone()).font(t.text_style(tk.font.body)).color(ink).single_line().truncate().grow(1.0).min_w(0.0));
            list = list.child(match &self.choose {
                Some(attach) => attach(r, value.clone()),
                None => r,
            });
        }

        let p = col().w(width).h(height).padding(pad).fill(color(pop.background)).radius(t.radius()).role("listbox");
        border(p, &pop_border).child(header).child(rule).child(list)
    }
}

// ----------------------------------------------------------------------------------- ColorWell

/// A button showing a color, for opening the app's picker: the swatch inset in a bordered well.
/// The cursor is a ring outside the well, as ToggleSwitch draws it.
pub struct ColorWell {
    swatch: Color,
    cursor: bool,
    press: Option<Attach>,
}

impl ColorWell {
    pub fn new(swatch: Color) -> ColorWell {
        ColorWell { swatch, cursor: false, press: None }
    }
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    pub fn on_press<S: 'static>(mut self, f: impl Fn(&mut S) + 'static) -> Self {
        self.press = Some(Box::new(move |e: El| e.on_press(f)));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let c = &tk.controls;
        let sp = &tk.spacing;
        let (w, h) = (t.space(44.0), t.space(26.0));
        let pad = sp.sm as f32;
        let r = t.radius_small();
        let inset = c.normal.border.widths[0] as f32 + sp.xxs as f32;
        let inner = el()
            .pin(Some(inset), Some(inset), Some(inset), Some(inset))
            .radius((r - inset).max(0.0))
            .fill(self.swatch);
        let well = border(el().size(w, h).radius(r).fill(color(c.normal.fill)), &c.normal.border).child(inner);
        let ring_r = if r > 0.0 { t.radius().min(r + pad) } else { 0.0 };
        let ring = layer(&Look::new(Rgba::TRANSPARENT, c.hover.border.clone()), ring_r);
        let ring = if self.cursor { ring } else { ring.group_opacity(0.0, 1.0) };
        let e = el()
            .size(w + pad * 2.0, h + pad * 2.0)
            .center()
            .hover_group_focus()
            .focusable(true)
            .role("button")
            .label("Color")
            .child(ring)
            .child(well);
        match self.press {
            Some(attach) => attach(e),
            None => e,
        }
    }
}

// --------------------------------------------------------------------------------- SwatchStrip

/// A row of preset colors, one chosen. The chosen swatch wears an accent ring; the keyboard
/// cursor (and the pointer) the hover-cursor border in the same place.
pub struct SwatchStrip {
    colors: Vec<Color>,
    current: Option<usize>,
    cursor: Option<usize>,
    pick: Option<Box<dyn Fn(El, usize) -> El>>,
}

impl SwatchStrip {
    pub fn new(colors: impl IntoIterator<Item = Color>, current: Option<usize>) -> SwatchStrip {
        SwatchStrip { colors: colors.into_iter().collect(), current, cursor: None, pick: None }
    }
    pub fn cursor(mut self, index: Option<usize>) -> Self {
        self.cursor = index;
        self
    }
    pub fn on_pick<S: 'static>(mut self, f: impl Fn(&mut S, usize) + Clone + 'static) -> Self {
        self.pick = Some(Box::new(move |e: El, i: usize| {
            let f = f.clone();
            e.on_press(move |s: &mut S| f(s, i))
        }));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let sp = &tk.spacing;
        let size = t.space(22.0);
        let ring_w = t.space(2.0).max(1.0);
        let gap = sp.xxs as f32;
        let cell = size + (gap + ring_w) * 2.0;
        let r = t.radius_small();
        let ring_r = if r > 0.0 { r + gap + ring_w } else { 0.0 };
        let mut strip = row().gap(sp.xxs as f32).role("radiogroup").focus_group();
        for (i, swatch) in self.colors.into_iter().enumerate() {
            let chosen = self.current == Some(i);
            let ring = el().pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0)).radius(ring_r);
            let ring = if chosen {
                ring.border(ring_w, t.accent())
            } else if self.cursor == Some(i) {
                ring.border(ring_w, color(tk.controls.hover.border.color))
            } else {
                ring.border(ring_w, color(tk.controls.hover.border.color)).group_opacity(0.0, 1.0)
            };
            // A track-colored rim around the color (over the page, as Qt draws a translucent
            // border), so a swatch the color of the background still shows.
            let fill = el().pin(Some(1.0), Some(1.0), Some(1.0), Some(1.0)).radius((r - 1.0).max(0.0)).fill(swatch);
            let chip = el().size(size, size).radius(r).border(1.0, t.track()).child(fill);
            let e = el()
                .size(cell, cell)
                .center()
                .hover_group_focus()
                .focusable(true)
                .role("radio")
                .checked(chosen)
                .child(ring)
                .child(chip);
            strip = strip.child(match &self.pick {
                Some(attach) => attach(e, i),
                None => e,
            });
        }
        strip
    }
}
