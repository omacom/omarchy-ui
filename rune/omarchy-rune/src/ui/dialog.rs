//! Dialog: a modal card on the `scrim` with a title, a body and a button row, drawn as the Qt
//! binding's `Dialog.qml`. [`super::ConfirmDialog`] is its preset with no title and a
//! destructive Confirm.

use omarchy_theme::{BorderSpec, Rgba};
use rune::{El, col, row, text};

use super::Attach;
use crate::{Theme, border, color};

/// A modal card: title, message or any body, then a secondary and a primary button (urgent
/// when `destructive`), one of them selected. `build` returns the card; with [`Dialog::scrim`]
/// it returns the scrim filling its parent with the card centered on it, for an overlay.
/// Return presses the selected button (it is the window's default button while shown); with
/// [`Dialog::modal`], Escape cancels.
pub struct Dialog {
    title: String,
    message: String,
    body: Vec<El>,
    /// `None`: the catalog's `common.cancel` and `common.confirm`, in the display language.
    secondary: Option<String>,
    primary: Option<String>,
    destructive: bool,
    selected: usize,
    width: Option<f32>,
    scrim: bool,
    modal: bool,
    on_cancel: Option<Attach>,
    on_accept: Option<Attach>,
    dismiss: Option<Attach>,
}

impl Dialog {
    pub fn new(title: impl Into<String>) -> Dialog {
        Dialog {
            title: title.into(),
            message: String::new(),
            body: Vec::new(),
            secondary: None,
            primary: None,
            destructive: false,
            selected: 1,
            width: None,
            scrim: false,
            modal: false,
            on_cancel: None,
            on_accept: None,
            dismiss: None,
        }
    }
    /// Body text under the title, in `body` size.
    pub fn message(mut self, m: impl Into<String>) -> Self {
        self.message = m.into();
        self
    }
    /// Anything under the title and message.
    pub fn body(mut self, e: El) -> Self {
        self.body.push(e);
        self
    }
    /// The secondary and primary button labels (`common.cancel` and `common.confirm` by default,
    /// Cancel and Confirm in English, as Qt's Dialog).
    pub fn labels(mut self, secondary: impl Into<String>, primary: impl Into<String>) -> Self {
        self.secondary = Some(secondary.into());
        self.primary = Some(primary.into());
        self
    }
    /// Draw the primary button in urgent, for actions that destroy something.
    pub fn destructive(mut self, on: bool) -> Self {
        self.destructive = on;
        self
    }
    /// 0 for the secondary button, 1 for the primary (the default).
    pub fn selected(mut self, index: usize) -> Self {
        self.selected = index;
        self
    }
    /// The card's width; `space(370)` by default.
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    /// Fill the parent with the scrim and center the card on it.
    pub fn scrim(mut self, on: bool) -> Self {
        self.scrim = on;
        self
    }
    /// Trap focus inside and make everything behind inert while shown (Escape still cancels).
    /// For a real modal in the app's overlay; needs [`Dialog::on_cancel`].
    pub fn modal(mut self, on: bool) -> Self {
        self.modal = on;
        self
    }
    /// The secondary button, and with [`Dialog::modal`] Escape and a press outside the card.
    pub fn on_cancel<S: 'static>(mut self, f: impl Fn(&mut S) + Clone + 'static) -> Self {
        let dismiss = f.clone();
        self.on_cancel = Some(Box::new(move |e: El| e.on_press(f)));
        self.dismiss = Some(Box::new(move |e: El| e.modal(true, dismiss)));
        self
    }
    pub fn on_accept<S: 'static>(mut self, f: impl Fn(&mut S) + 'static) -> Self {
        self.on_accept = Some(Box::new(move |e: El| e.on_press(f)));
        self
    }
    /// Handlers already waiting for the state type (ConfirmDialog's).
    pub(crate) fn attach(mut self, cancel: Option<Attach>, accept: Option<Attach>) -> Self {
        self.on_cancel = cancel;
        self.on_accept = accept;
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let secondary = self.secondary.unwrap_or_else(|| tk.string("common.cancel", "Cancel"));
        let primary = self.primary.unwrap_or_else(|| tk.string("common.confirm", "Confirm"));
        let p = &tk.palette;
        let line = tk.controls.normal.border.max_width().max(1.0);
        let accent = tk.surfaces.menu.selected_text;
        let selected_index = self.selected;
        let destructive = self.destructive;
        let choice = |label: String, index: usize, attach: Option<Attach>| {
            let selected = selected_index == index;
            let urgent = destructive && index == 1;
            let (fill, edge, ink) = if urgent {
                (
                    if selected { p.urgent.with_alpha(0.22) } else { Rgba::TRANSPARENT },
                    if selected { p.urgent } else { p.urgent.with_alpha(0.56) },
                    if selected { p.urgent } else { p.foreground },
                )
            } else {
                (
                    if selected { tk.surfaces.menu.selected_background } else { Rgba::TRANSPARENT },
                    if selected { accent } else { p.foreground.with_alpha(0.38) },
                    if selected { accent } else { p.foreground },
                )
            };
            let mut b = row()
                .size(t.space(88.0), t.space(34.0))
                .center()
                .radius(t.radius())
                .fill(color(fill))
                .border(line as f32, color(edge))
                .role("button")
                .label(label.clone())
                .child(text(label).font(t.text_style(tk.font.caption)).color(color(ink)));
            if selected && attach.is_some() {
                b = b.default_button();
            }
            match attach {
                Some(a) => a(b),
                None => b,
            }
        };

        let mut content = col().w_full().gap(t.space(10.0));
        if !self.title.is_empty() {
            content = content.child(text(self.title.clone()).font(t.text_style_bold(tk.font.title)).color(t.fg()).wrap().role("heading"));
        }
        if !self.message.is_empty() {
            content = content.child(text(self.message).font(t.text_style(tk.font.body)).color(t.fg()).wrap());
        }
        content = content.children(self.body);

        let pad = t.space(18.0);
        let card_border = BorderSpec { color: accent, widths: [line; 4], gradient: None };
        let mut card = col()
            .gap(t.space(20.0))
            .padding(pad + line as f32)
            .fill(color(p.background))
            .radius(t.radius())
            .role(if self.destructive { "alertdialog" } else { "dialog" })
            .label(self.title);
        card = border(card, &card_border);
        card = card.w(self.width.unwrap_or_else(|| t.space(370.0)));
        card = card.child(content).child(
            row()
                .w_full()
                .justify_end()
                .gap(t.space(10.0))
                .child(choice(secondary, 0, self.on_cancel))
                .child(choice(primary, 1, self.on_accept)),
        );
        if self.modal
            && let Some(dismiss) = self.dismiss
        {
            card = dismiss(card);
        }
        if !self.scrim {
            return card;
        }
        rune::el().pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0)).fill(t.scrim()).center().child(card)
    }
}
