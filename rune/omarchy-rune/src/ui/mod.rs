//! Omarchy's primitives for Rune, drawn the way the Omarchy shell's QML kit draws them (the Qt
//! binding in this repo is that kit). Same names, states and metrics as `spec/components.md`.
//!
//! Every builder ends in `.build(&theme)` and returns an [`El`]. Handlers attach through the
//! builder (`on_press`, `on_toggle`, `on_change`). `.cursor(true)` forces the hover-cursor look,
//! as the shell's `hasCursor` does for keyboard-driven panels.

mod button;
pub mod feedback;
mod data;
mod editing;
mod field;
mod lists;
mod menu;
mod navigation;
mod dialog;
mod forms;
mod pickers;
mod search;
mod slider;
mod surface;
mod toggle;
mod turn;

pub use button::{Button, ButtonGroup, IconButton};
pub use feedback::{Banner, EmptyState, Meter, ProgressBar, Spinner, Toast};
pub use data::{Calendar, Column, Date, Sidebar, SidebarItem, SidebarSection, SplitPane, SplitView, Table, Tree, TreeNode};
pub use editing::{Clip, ColorPicker, CropFrame, DragHandle, DropIndicator, Edge, Hsva, Scrubber, Timeline, TimelineView, TrimHandle};
pub use field::{Dropdown, NumberField, TextField};
pub use lists::{
    Avatar, AvatarSize, Badge, CursorKey, CursorModel, CursorSection, Hero, KeyValue, ListRow, ScrollArea, StatusDot, Tile, TileGrid,
    Tone, printable,
};
pub use menu::{ContextMenu, Menu, MenuItem};
pub use navigation::{IconRail, KeyCombo, Keycap, ShortcutSheet, Tabs, Toolbar, key_combo_width, key_label, keycap_width, split_combo, toolbar_separator};
pub use dialog::Dialog;
pub use forms::{Checkbox, PasswordField, Radio, RadioGroup, SettingRow, TextArea};
pub use pickers::{ColorWell, MultiSelect, SwatchStrip};
pub use search::{SEARCH_SECTION, SearchItem, SearchList};
pub use slider::Slider;
pub use surface::{Card, ConfirmDialog, MenuRow, Tooltip, section_header, separator};
pub use surface::{Heading, HeadingLevel, Pane};
pub use toggle::{Toggle, ToggleSwitch};

use omarchy_theme::{BorderSpec, Rgba};
use rune::{El, el, text};

use crate::{Theme, border, color};

/// A handler waiting for the app's state type: builders keep these rather than being generic
/// themselves, so `Button::new("Save")` needs no type annotation.
pub(crate) type Attach = Box<dyn FnOnce(El) -> El>;

/// One visual state of a control: fill and border.
#[derive(Clone)]
pub(crate) struct Look {
    pub fill: Rgba,
    pub border: BorderSpec,
}

impl Look {
    pub fn new(fill: Rgba, border: BorderSpec) -> Look {
        Look { fill, border }
    }
    pub fn clear() -> Look {
        Look { fill: Rgba::TRANSPARENT, border: BorderSpec::none() }
    }
}

/// The widest border any of `looks` paints, per side, so changing state never relayouts (the
/// shell reserves the same way).
pub(crate) fn reserve(looks: &[&BorderSpec]) -> [f32; 4] {
    let mut out = [0.0f32; 4];
    for spec in looks {
        for (i, w) in spec.widths.iter().enumerate() {
            out[i] = out[i].max(*w as f32);
        }
    }
    out
}

/// Control chrome: the `rest` look, swapped for the `hot` look under the pointer or keyboard
/// focus (Omarchy's focus mirrors hover), or always when `forced`. Both looks are layers under
/// `children`, cross-faded by the hover group, so the hot look replaces the rest look the way
/// the shell's state ladder does rather than stacking on it. `base` is the laid-out container
/// without children.
pub(crate) fn chrome(t: &Theme, base: El, children: Vec<El>, rest: &Look, hot: &Look, forced: bool, radius: f32) -> El {
    chrome_corners(t, base, children, rest, hot, forced, [radius; 4])
}

/// Like [`chrome`], with per-corner radii `[top-left, top-right, bottom-right, bottom-left]`.
pub(crate) fn chrome_corners(t: &Theme, base: El, children: Vec<El>, rest: &Look, hot: &Look, forced: bool, corners: [f32; 4]) -> El {
    let layer = |look: &Look| {
        let e = el().pin(Some(0.0), Some(0.0), Some(0.0), Some(0.0)).corners(corners).fill(color(look.fill));
        border(e, &look.border)
    };
    let pressed = color(t.tokens().controls.pressed.fill);
    let base = base.hover_group_focus().corners(corners).press(pressed);
    let base = if forced {
        base.child(layer(hot))
    } else {
        base.child(layer(rest).group_opacity(1.0, 0.0)).child(layer(hot).group_opacity(0.0, 1.0))
    };
    base.children(children)
}

pub(crate) use navigation::optical;

/// The width Qt's `Text` gives a glyph: its advance widened by ink overhanging either side, in
/// whole pixels (as [`glyph`] boxes it).
pub(crate) fn glyph_width(t: &Theme, s: &str, size: i64) -> f32 {
    let style = t.text_style(size);
    let advance = rune::text::advance(s, style);
    let Some([x, _, w, _]) = rune::text::ink(s, style).map(|i| i.bounds) else { return advance };
    let left = (-x).max(0.0).ceil();
    let right = if x + w > advance { ((x + w).ceil() - advance).ceil() } else { 0.0 };
    advance + left + right
}

/// A glyph boxed the way Qt's `Text` boxes one: the advance, widened by any ink that overhangs
/// it, in whole pixels as Qt's hinted bearings are. Nerd Font icons in the non-Mono Nerd Fonts
/// overhang their cell to the right, so centering the bare advance (as Rune lays text out) leaves
/// the icon right of center.
pub(crate) fn glyph(t: &Theme, s: &str, size: i64, ink: rune::Color) -> El {
    let style = t.text_style(size);
    let e = text(s.to_string()).font(style).color(ink).single_line();
    let Some([x, _, w, _]) = rune::text::ink(s, style).map(|i| i.bounds) else { return e };
    let advance = rune::text::advance(s, style);
    let left = (-x).max(0.0).ceil();
    let right = if x + w > advance { ((x + w).ceil() - advance).ceil() } else { 0.0 };
    if left == 0.0 && right == 0.0 {
        return e;
    }
    let (_, h) = rune::text::measure(s, &[], style, None);
    el().w(advance + left + right).h(h).child(e.offset(left, 0.0))
}
