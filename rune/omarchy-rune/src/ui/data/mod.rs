//! Data and layout primitives: Table, Tree, SplitView, Sidebar and Calendar. Same anatomy and
//! metrics as the Qt binding's `Table.qml`, `Tree.qml`, `SplitView.qml`, `Sidebar.qml` and
//! `Calendar.qml` (spec/components/data.md).

mod calendar;
mod sidebar;
mod split;
mod table;
mod tree;

pub use calendar::{Calendar, Date};
pub use sidebar::{Sidebar, SidebarItem, SidebarSection};
pub use split::{SplitPane, SplitView};
pub use table::{Column, Table};
pub use tree::{Tree, TreeNode};

use omarchy_theme::{BorderSpec, Rgba};
use rune::{El, el};

use super::{Look, chrome};
use crate::Theme;

/// A list row's chrome: transparent or the selected fill at rest, the hover fill under the
/// pointer, keyboard focus or `cursor` (the hover look replaces the selected one, as the state
/// ladder says).
pub(crate) fn row_chrome(t: &Theme, base: El, children: Vec<El>, selected: bool, cursor: bool) -> El {
    let c = &t.tokens().controls;
    let rest = Look::new(if selected { c.selected.fill } else { Rgba::TRANSPARENT }, BorderSpec::none());
    let hot = Look::new(c.hover.fill, BorderSpec::none());
    chrome(t, base, children, &rest, &hot, cursor, t.radius())
}

/// A hairline in the track color along the bottom of its parent.
pub(crate) fn bottom_rule(t: &Theme) -> El {
    let hair = t.tokens().spacing.hairline as f32;
    el().pin(Some(0.0), None, Some(0.0), Some(0.0)).h(hair).fill(t.track())
}
