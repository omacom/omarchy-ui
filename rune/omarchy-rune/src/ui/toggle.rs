//! ToggleSwitch and Toggle: the shell's `ToggleSwitch.qml` and `Toggle.qml`.

use omarchy_theme::Rgba;
use rune::{El, col, el, row, text};

use super::{Attach, Look, chrome};
use crate::{Theme, border, color};

/// A bare switch. Pill-shaped when Hyprland rounds corners, square when it doesn't. The cursor is
/// a ring around the track, not a change to it, so a bordered track never gets fainter.
pub struct ToggleSwitch {
    key: Option<String>,
    checked: bool,
    cursor: bool,
    interactive: bool,
    toggle: Option<Attach>,
}

impl ToggleSwitch {
    pub fn new(checked: bool) -> ToggleSwitch {
        ToggleSwitch { key: None, checked, cursor: false, interactive: true, toggle: None }
    }
    pub fn key(mut self, key: impl Into<String>) -> Self {
        self.key = Some(key.into());
        self
    }
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    /// Off when a surrounding row owns the click (and the cursor ring), as in [`Toggle`].
    pub fn interactive(mut self, on: bool) -> Self {
        self.interactive = on;
        self
    }
    pub fn on_toggle<S: 'static>(mut self, f: impl Fn(&mut S) + 'static) -> Self {
        self.toggle = Some(Box::new(move |e: El| e.on_press(f)));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let c = &tk.controls;
        let rounded = tk.radius > 0;
        let track_h = 22f32.max((tk.spacing.control_height as f32 * 0.55).round());
        let track_w = (track_h * 1.9).round();
        let knob = 6f32.max((track_h * 0.72).round());
        let inset = 1f32.max(((track_h - knob) / 2.0).round());
        let pad = if self.interactive { t.space(6.0) } else { 0.0 };

        let state = if self.checked { &c.selected } else { &c.normal };
        let knob_color = if self.checked { c.selected.text } else { tk.roles.text_dim };
        let knob_el = el()
            .key("knob")
            .size(knob, knob)
            .radius(if rounded { knob / 2.0 } else { 0.0 })
            .fill(color(knob_color))
            .absolute(if self.checked { track_w - knob - inset } else { inset }, (track_h - knob) / 2.0);
        let track = border(
            el().size(track_w, track_h).radius(if rounded { track_h / 2.0 } else { 0.0 }).fill(color(state.fill)),
            &state.border,
        )
        .child(knob_el);

        let base = row().center().padding(pad).role("switch").checked(self.checked);
        if !self.interactive {
            return base.child(track);
        }
        let ring = Look::new(Rgba::TRANSPARENT, c.hover.border.clone());
        let mut e = chrome(t, base.focusable(true), vec![track], &Look::clear(), &ring, self.cursor, t.radius());
        if let Some(k) = self.key {
            e = e.key(k);
        }
        match self.toggle {
            Some(attach) => attach(e),
            None => e,
        }
    }
}

/// A labeled switch row: title, optional description, and the switch at the end. The whole row
/// is the control.
pub struct Toggle {
    label: String,
    description: String,
    checked: bool,
    cursor: bool,
    width: Option<f32>,
    toggle: Option<Attach>,
}

impl Toggle {
    pub fn new(label: impl Into<String>) -> Toggle {
        Toggle { label: label.into(), description: String::new(), checked: false, cursor: false, width: None, toggle: None }
    }
    pub fn description(mut self, d: impl Into<String>) -> Self {
        self.description = d.into();
        self
    }
    pub fn checked(mut self, on: bool) -> Self {
        self.checked = on;
        self
    }
    pub fn cursor(mut self, on: bool) -> Self {
        self.cursor = on;
        self
    }
    /// A fixed width; by default the row fills its parent.
    pub fn width(mut self, w: f32) -> Self {
        self.width = Some(w);
        self
    }
    pub fn on_toggle<S: 'static>(mut self, f: impl Fn(&mut S) + 'static) -> Self {
        self.toggle = Some(Box::new(move |e: El| e.on_press(f)));
        self
    }

    pub fn build(self, t: &Theme) -> El {
        let tk = t.tokens();
        let c = &tk.controls;
        let sp = &tk.spacing;
        let rest = Look::new(c.normal.fill, c.normal.border.clone());
        let hot = Look::new(c.hover.fill, c.hover.border.clone());
        let bw = c.normal.border.max_width() as f32;

        let mut words = col().grow(1.0).min_w(0.0).gap(sp.xs as f32).child(
            text(self.label.clone()).font(t.text_style_bold(tk.font.subtitle)).color(t.fg()).single_line().truncate(),
        );
        if !self.description.is_empty() {
            words = words.child(text(self.description).font(t.text_style(tk.font.caption)).color(t.dim()).wrap());
        }
        let switch = ToggleSwitch::new(self.checked).interactive(false).build(t);

        let mut base = row()
            .items_center()
            .gap(sp.row_padding_x as f32)
            .min_h(54f32.max(0.0))
            .py(sp.huge as f32 / 2.0)
            .px(sp.row_padding_x as f32 + bw)
            .role("switch")
            .label(self.label)
            .checked(self.checked)
            .focusable(true);
        base = match self.width {
            Some(w) => base.w(w),
            None => base.w_full(),
        };
        let e = chrome(t, base, vec![words, switch], &rest, &hot, self.cursor, t.radius());
        match self.toggle {
            Some(attach) => attach(e),
            None => e,
        }
    }
}
